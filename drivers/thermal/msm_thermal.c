/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/msm_tsens.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/msm_thermal.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/syscalls.h>

#define MAX_LEVELS		4
#define DEFAULT_POLL_MS		1000

/*
 * CPU mitigation ladder, coolest level first. A level trips when its CPU
 * sensor reads trip_degc or more and clears when it reads clear_degc or
 * less; the CPU runs under the cap of the highest tripped level and under
 * none once every level has cleared. clear_degc sits below trip_degc, so a
 * level holds until the sensor has fallen through the whole gap and a
 * reading that hovers at a trip point changes the cap once.
 */
static int trip_degc[MAX_LEVELS] = { 72, 75, 90, 108 };
static int clear_degc[MAX_LEVELS] = { 68, 71, 87, 104 };
static unsigned int cap_khz[MAX_LEVELS] = { 1094400, 787200, 600000, 300000 };
static unsigned int nr_trip = MAX_LEVELS;
static unsigned int nr_clear = MAX_LEVELS;
static unsigned int nr_cap = MAX_LEVELS;
module_param_array(trip_degc, int, &nr_trip, 0444);
module_param_array(clear_degc, int, &nr_clear, 0444);
module_param_array(cap_khz, uint, &nr_cap, 0444);

/*
 * trip_offset lowers every trip and clear point by that many degrees C, so a
 * load test reaches each level at a lower die temperature. enabled set to 0
 * clears every level and releases the caps, the handoff a userspace thermal
 * daemon performs by writing N.
 */
static int trip_offset;
module_param(trip_offset, int, 0644);
static bool enabled = true;
module_param(enabled, bool, 0644);

/*
 * A CPU sensor at critical_degc syncs the filesystems and powers the phone
 * off through kernel_power_off. orderly_poweroff is unusable here: it starts
 * poweroff_cmd with UMH_NO_WAIT, Android ships no /sbin/poweroff, and the
 * helper's failure arrives after the call has already returned success. The
 * check ignores trip_offset and enabled, so a load test or a handoff never
 * moves or removes it.
 */
static int critical_degc = 115;
module_param(critical_degc, int, 0444);
static bool critical_fired;

static unsigned int nr_levels;
static unsigned int poll_ms = DEFAULT_POLL_MS;
static struct delayed_work check_temp_work;

/*
 * Per-CPU TSENS sensor from qcom,cpu-sensors. msm_cpufreq gives all four
 * cores one policy because they share one clock, so the CPU cap is that of
 * the highest level across every CPU sensor.
 */
static uint32_t cpu_sensor[NR_CPUS];
static unsigned int thermal_cap = UINT_MAX;

/* Current level of each TSENS sensor; -1 is no mitigation. */
static int sensor_level[TSENS_MAX_SENSORS];

static int msm_thermal_cpufreq_callback(struct notifier_block *nfb,
		unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;

	if (event == CPUFREQ_ADJUST)
		cpufreq_verify_within_limits(policy, 0, thermal_cap);

	return NOTIFY_OK;
}

static struct notifier_block msm_thermal_cpufreq_notifier = {
	.notifier_call = msm_thermal_cpufreq_callback,
};

/*
 * Moves a sensor's level up through every trip point the reading has reached,
 * then down through every level whose clear point it has fallen to.
 */
static int next_level(int level, long temp)
{
	while (level + 1 < (int)nr_levels &&
	       temp >= trip_degc[level + 1] - trip_offset)
		level++;
	while (level >= 0 && temp <= clear_degc[level] - trip_offset)
		level--;
	return level;
}

static void check_temp(struct work_struct *work)
{
	struct tsens_device tsens_dev;
	unsigned int cap;
	unsigned int cpu;
	unsigned int s;
	long temp = 0;
	int highest = -1;
	int level;

	for (s = 0; s < TSENS_MAX_SENSORS; s++) {
		bool used = false;

		for_each_possible_cpu(cpu)
			used |= cpu_sensor[cpu] == s;
		if (!used)
			continue;

		tsens_dev.sensor_num = s;
		if (tsens_get_temp(&tsens_dev, &temp)) {
			level = enabled ? sensor_level[s] : -1;
		} else {
			if (temp >= critical_degc && !critical_fired) {
				critical_fired = true;
				pr_crit("msm_thermal: tsens %u %ldC reached %dC, powering off\n",
					s, temp, critical_degc);
				sys_sync();
				kernel_power_off();
			}
			level = enabled ? next_level(sensor_level[s], temp) : -1;
		}
		if (level != sensor_level[s]) {
			pr_info("msm_thermal: tsens %u %ldC level %d -> %d\n",
				s, enabled ? temp : 0L, sensor_level[s], level);
			sensor_level[s] = level;
		}
		if (sensor_level[s] > highest)
			highest = sensor_level[s];
	}

	cap = highest < 0 ? UINT_MAX : cap_khz[highest];
	if (cap != thermal_cap) {
		pr_info("msm_thermal: CPU cap %u kHz\n", cap);
		thermal_cap = cap;
		get_online_cpus();
		for_each_online_cpu(cpu)
			cpufreq_update_policy(cpu);
		put_online_cpus();
	}

	schedule_delayed_work(&check_temp_work, msecs_to_jiffies(poll_ms));
}

static int ladder_valid(void)
{
	unsigned int i;

	if (nr_trip != nr_clear || nr_trip != nr_cap || !nr_trip)
		return 0;
	for (i = 0; i < nr_trip; i++) {
		if (clear_degc[i] >= trip_degc[i])
			return 0;
		if (i && (trip_degc[i] <= trip_degc[i - 1] ||
			  clear_degc[i] < clear_degc[i - 1] ||
			  cap_khz[i] > cap_khz[i - 1]))
			return 0;
	}
	return 1;
}

int __devinit msm_thermal_init(struct msm_thermal_data *pdata)
{
	unsigned int cpu;
	unsigned int s;
	int ret;

	BUG_ON(!pdata);
	BUG_ON(pdata->sensor_id >= TSENS_MAX_SENSORS);

	if (!ladder_valid()) {
		pr_err("msm_thermal: trip, clear, and cap ladder is inconsistent\n");
		return -EINVAL;
	}
	nr_levels = nr_trip;
	if (pdata->poll_ms)
		poll_ms = pdata->poll_ms;

	for (s = 0; s < TSENS_MAX_SENSORS; s++)
		sensor_level[s] = -1;
	for_each_possible_cpu(cpu) {
		if (cpu_sensor[cpu] >= TSENS_MAX_SENSORS)
			cpu_sensor[cpu] = pdata->sensor_id;
		pr_info("msm_thermal: cpu%u follows tsens %u\n", cpu,
			cpu_sensor[cpu]);
	}

	ret = cpufreq_register_notifier(&msm_thermal_cpufreq_notifier,
			CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&check_temp_work, check_temp);
	schedule_delayed_work(&check_temp_work, HZ*20);

	return 0;
}

/*
 * qcom,cpu-sensors names one thermal zone per CPU as "tsens_tz_sensorN"; a
 * CPU with no usable entry follows qcom,sensor-id.
 */
static void __devinit read_cpu_sensors(struct device_node *node)
{
	const char *name;
	unsigned int cpu;
	unsigned long id;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		cpu_sensor[cpu] = TSENS_MAX_SENSORS;
		if (of_property_read_string_index(node, "qcom,cpu-sensors",
						  cpu, &name))
			continue;
		if (strncmp(name, "tsens_tz_sensor", 15) ||
		    kstrtoul(name + 15, 10, &id) || id >= TSENS_MAX_SENSORS)
			continue;
		cpu_sensor[cpu] = id;
	}
}

static int __devinit msm_thermal_dev_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct msm_thermal_data data;
	int ret;

	memset(&data, 0, sizeof(struct msm_thermal_data));
	ret = of_property_read_u32(node, "qcom,sensor-id", &data.sensor_id);
	if (ret) {
		pr_err("%s: Failed reading node=%s, key=qcom,sensor-id\n",
		       __func__, node->full_name);
		return ret;
	}
	if (data.sensor_id >= TSENS_MAX_SENSORS)
		return -EINVAL;
	of_property_read_u32(node, "qcom,poll-ms", &data.poll_ms);
	read_cpu_sensors(node);

	return msm_thermal_init(&data);
}

static struct of_device_id msm_thermal_match_table[] = {
	{.compatible = "qcom,msm-thermal"},
	{},
};

static struct platform_driver msm_thermal_device_driver = {
	.probe = msm_thermal_dev_probe,
	.driver = {
		.name = "msm-thermal",
		.owner = THIS_MODULE,
		.of_match_table = msm_thermal_match_table,
	},
};

int __init msm_thermal_device_init(void)
{
	return platform_driver_register(&msm_thermal_device_driver);
}
