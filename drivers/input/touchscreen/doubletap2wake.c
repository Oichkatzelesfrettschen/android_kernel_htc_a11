/*
 * drivers/input/touchscreen/doubletap2wake.c
 *
 *
 * Copyright (c) 2013, Dennis Rassmann <showp1984@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/input/doubletap2wake.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/input/wake_pwrkey.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/wakelock.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>


/* Version, author, desc, etc */
#define DRIVER_AUTHOR "Dennis Rassmann <showp1984@gmail.com>"
#define DRIVER_DESCRIPTION "Doubletap2wake for almost any device"
#define DRIVER_VERSION "1.0"
#define LOGTAG "[doubletap2wake]: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");

/* Tuneables */
#define DT2W_DEBUG		0
#define DT2W_DEFAULT		0

#define DT2W_FEATHER		200
#define DT2W_TIME		700

/* Resources */
int dt2w_switch = DT2W_DEFAULT;
bool dt2w_scr_suspended = false;
static cputime64_t tap_time_pre = 0;
static int touch_x = 0, touch_y = 0, touch_nr = 0, x_pre = 0, y_pre = 0;
static bool touch_x_called = false, touch_y_called = false, touch_cnt = true;
static bool exec_count = true;
//static struct notifier_block dt2w_lcd_notif;
static struct workqueue_struct *dt2w_input_wq;
static struct work_struct dt2w_input_work;
static struct wake_lock dt2w_wake_lock;

/* Read cmdline for dt2w */
static int __init read_dt2w_cmdline(char *dt2w)
{
	if (strcmp(dt2w, "1") == 0) {
		pr_info("[cmdline_dt2w]: DoubleTap2Wake enabled. | dt2w='%s'\n", dt2w);
		dt2w_switch = 1;
	} else if (strcmp(dt2w, "0") == 0) {
		pr_info("[cmdline_dt2w]: DoubleTap2Wake disabled. | dt2w='%s'\n", dt2w);
		dt2w_switch = 0;
	} else {
		pr_info("[cmdline_dt2w]: No valid input found. Going with default: | dt2w='%u'\n", dt2w_switch);
	}
	return 1;
}
__setup("dt2w=", read_dt2w_cmdline);

/* reset on finger release */
static void doubletap2wake_reset(void) {
	exec_count = true;
	touch_nr = 0;
	tap_time_pre = 0;
	x_pre = 0;
	y_pre = 0;
}

/* PowerKey work func */
static void doubletap2wake_presspwr(struct work_struct *work) {
	wake_pwrkey_press();
}
static DECLARE_WORK(doubletap2wake_presspwr_work, doubletap2wake_presspwr);

/* PowerKey trigger */
static void doubletap2wake_pwrtrigger(void) {
	schedule_work(&doubletap2wake_presspwr_work);
        return;
}

/* unsigned */
static unsigned int calc_feather(int coord, int prev_coord) {
	int calc_coord = 0;
	calc_coord = coord-prev_coord;
	if (calc_coord < 0)
		calc_coord = calc_coord * (-1);
	return calc_coord;
}

/* init a new touch */
static void new_touch(int x, int y) {
	tap_time_pre = ktime_to_ms(ktime_get());
	x_pre = x;
	y_pre = y;
	touch_nr++;
}

/* Doubletap2wake main function */
static void detect_doubletap2wake(int x, int y, bool st)
{
        bool single_touch = st;
#if DT2W_DEBUG
        pr_info(LOGTAG"x,y(%4d,%4d) single:%s\n",
                x, y, (single_touch) ? "true" : "false");
#endif
	if ((single_touch) && (dt2w_switch > 0) && (exec_count) && (touch_cnt)) {
		touch_cnt = false;
		if (touch_nr == 0) {
			new_touch(x, y);
		} else if (touch_nr == 1) {
			if ((calc_feather(x, x_pre) < DT2W_FEATHER) &&
			    (calc_feather(y, y_pre) < DT2W_FEATHER) &&
			    ((ktime_to_ms(ktime_get())-tap_time_pre) < DT2W_TIME))
				touch_nr++;
			else {
				doubletap2wake_reset();
				new_touch(x, y);
			}
		} else {
			doubletap2wake_reset();
			new_touch(x, y);
		}
		if ((touch_nr > 1)) {
			pr_info(LOGTAG"ON\n");
			exec_count = false;
			doubletap2wake_pwrtrigger();
			doubletap2wake_reset();
		}
	}
}

static void dt2w_input_callback(struct work_struct *unused) {

	detect_doubletap2wake(touch_x, touch_y, true);

	return;
}

static void dt2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
#if DT2W_DEBUG
	pr_info("doubletap2wake: code: %s|%u, val: %i\n",
		((code==ABS_MT_POSITION_X) ? "X" :
		(code==ABS_MT_POSITION_Y) ? "Y" :
		(code==ABS_MT_TRACKING_ID) ? "ID" :
		"undef"), code, value);
#endif
	if (dt2w_switch <= 0 || !dt2w_scr_suspended)
		return;

	if (code == ABS_MT_SLOT) {
		doubletap2wake_reset();
		return;
	}

	if (code == ABS_MT_TRACKING_ID && value == -1) {
		touch_cnt = true;
		return;
	}

	if (code == ABS_MT_POSITION_X) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called || touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		wake_lock_timeout(&dt2w_wake_lock, HZ);
		queue_work_on(0, dt2w_input_wq, &dt2w_input_work);
	}
}

/* The Himax HM852xD driver registers its touch input device under this name. */
static int input_dev_filter(struct input_dev *dev) {
	return strcmp(dev->name, "himax-touchscreen") ? 1 : 0;
}

static int dt2w_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "dt2w";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void dt2w_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id dt2w_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler dt2w_input_handler = {
	.event		= dt2w_input_event,
	.connect	= dt2w_input_connect,
	.disconnect	= dt2w_input_disconnect,
	.name		= "dt2w_inputreq",
	.id_table	= dt2w_ids,
};

/* Tracks the primary framebuffer: any blank level other than UNBLANK is
 * screen off, the same split the Himax suspend path uses. */
static int dt2w_fb_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data) {
	struct fb_event *evdata = data;
	int *blank;

	if (event != FB_EVENT_BLANK || !evdata || !evdata->data ||
	    !evdata->info || evdata->info->node != 0)
		return NOTIFY_OK;

	blank = evdata->data;
	dt2w_scr_suspended = (*blank != FB_BLANK_UNBLANK);
	return NOTIFY_OK;
}

static struct notifier_block dt2w_fb_notif = {
	.notifier_call = dt2w_fb_notifier_call,
};

/*
 * SYSFS stuff below here
 */
static ssize_t dt2w_doubletap2wake_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_switch);

	return count;
}

/*
 * The shared power key device is registered while the switch is above zero.
 * Returns 0 or a negative errno.
 */
static int dt2w_set_switch(int val)
{
	static DEFINE_MUTEX(dt2w_switch_lock);
	int rc = 0;

	mutex_lock(&dt2w_switch_lock);
	if (val > 0 && dt2w_switch <= 0)
		rc = wake_pwrkey_get();
	else if (val <= 0 && dt2w_switch > 0)
		wake_pwrkey_put();
	if (!rc)
		dt2w_switch = val;
	mutex_unlock(&dt2w_switch_lock);
	return rc;
}

static ssize_t dt2w_doubletap2wake_dump(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	if (buf[0] >= '0' && buf[0] <= '2' && buf[1] == '\n')
		return dt2w_set_switch(buf[0] - '0') ? : count;

	return count;
}

static struct kobj_attribute dev_attr_doubletap2wake =
	__ATTR(doubletap2wake, (S_IWUSR|S_IRUGO), dt2w_doubletap2wake_show, dt2w_doubletap2wake_dump);

static ssize_t dt2w_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%s\n", DRIVER_VERSION);

	return count;
}

static ssize_t dt2w_version_dump(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static struct kobj_attribute dev_attr_doubletap2wake_version =
	__ATTR(doubletap2wake_version, (S_IWUSR|S_IRUGO), dt2w_version_show, dt2w_version_dump);


/*
 * Called by the Himax driver once its android_touch kobject exists, so the
 * gesture switch appears beside the other touchscreen controls.
 */
int doubletap2wake_sysfs_init(struct kobject *kobj)
{
	int rc;

	rc = sysfs_create_file(kobj, &dev_attr_doubletap2wake.attr);
	if (rc)
		goto err;
	rc = sysfs_create_file(kobj, &dev_attr_doubletap2wake_version.attr);
	if (rc)
		goto err_version;
	return 0;

err_version:
	sysfs_remove_file(kobj, &dev_attr_doubletap2wake.attr);
err:
	pr_warn(LOGTAG"%s: sysfs_create_file failed (%d)\n", __func__, rc);
	return rc;
}

void doubletap2wake_sysfs_exit(struct kobject *kobj)
{
	sysfs_remove_file(kobj, &dev_attr_doubletap2wake_version.attr);
	sysfs_remove_file(kobj, &dev_attr_doubletap2wake.attr);
}

static int __init doubletap2wake_init(void)
{
	int rc;

	dt2w_input_wq = create_workqueue("dt2wiwq");
	if (!dt2w_input_wq) {
		pr_err("%s: Failed to create dt2wiwq workqueue\n", __func__);
		rc = -ENOMEM;
		goto err_wq;
	}
	INIT_WORK(&dt2w_input_work, dt2w_input_callback);
	wake_lock_init(&dt2w_wake_lock, WAKE_LOCK_SUSPEND, "doubletap2wake");

	rc = input_register_handler(&dt2w_input_handler);
	if (rc) {
		pr_err("%s: Failed to register dt2w_input_handler\n", __func__);
		goto err_handler;
	}

	rc = fb_register_client(&dt2w_fb_notif);
	if (rc) {
		pr_err("%s: Failed to register the fb notifier\n", __func__);
		goto err_fb;
	}

	/* A boot parameter can enable the gesture before this point. */
	if (dt2w_switch > 0 && wake_pwrkey_get()) {
		pr_err("%s: no power key device, doubletap2wake disabled\n", __func__);
		dt2w_switch = 0;
	}

	pr_info(LOGTAG"%s done\n", __func__);
	return 0;

err_fb:
	input_unregister_handler(&dt2w_input_handler);
err_handler:
	wake_lock_destroy(&dt2w_wake_lock);
	destroy_workqueue(dt2w_input_wq);
err_wq:
	return rc;
}

module_init(doubletap2wake_init);
