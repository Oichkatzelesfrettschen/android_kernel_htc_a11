/*
 * drivers/input/touchscreen/sweep2wake.c
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
 *
 * 11/18/2013 - port my N4's vertical sweep to N5
 * 		Paul Reioux <reioux@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/input/sweep2wake.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/wake_pwrkey.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/wakelock.h>
#include <linux/hrtimer.h>


/* Version, author, desc, etc */
#define DRIVER_AUTHOR "Dennis Rassmann <showp1984@gmail.com>"
#define DRIVER_DESCRIPTION "Sweep2wake for almost any device"
#define DRIVER_VERSION "1.5"
#define LOGTAG "[sweep2wake]: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");

/* Tuneables */
#define S2W_DEBUG		0
#define S2W_DEFAULT		0
#define S2W_S2SONLY_DEFAULT	0

#define DEFAULT_S2W_Y_MAX               1280
#define DEFAULT_S2W_X_MAX               720
#define DEFAULT_S2W_Y_LIMIT             DEFAULT_S2W_Y_MAX-100
#define DEFAULT_S2W_X_B1                130
#define DEFAULT_S2W_X_B2                360
#define DEFAULT_S2W_X_FINAL             160

/*
 * Slot bound for the per-slot coordinate cache. The Himax report carries the
 * contact count in a 4-bit field, so slot numbers stay below 16.
 */
#define S2W_MAX_SLOTS		16

/* Resources */
int s2w_switch = S2W_DEFAULT, s2w_s2sonly = S2W_S2SONLY_DEFAULT;
bool s2w_scr_suspended = false;
static bool exec_count = true;
static bool scr_on_touch = false, barrier[2] = {false, false};
static struct wake_lock s2w_wake_lock;

/*
 * Input core view of the touch device. input_handle_abs_event() drops an
 * ABS_MT value equal to the one already stored for its slot and passes
 * ABS_MT_SLOT only when a surviving MT value belongs to another slot than
 * the last one passed. The cache therefore mirrors the core's per-slot
 * values: a coordinate absent from a frame equals the cached one.
 */
static int s2w_slot;
static int s2w_x[S2W_MAX_SLOTS], s2w_y[S2W_MAX_SLOTS];
/* Slot of the contact that started the sweep, or -1 with no contact. */
static int s2w_sweep_slot = -1;
/* BTN_TOUCH state: at least one contact is on the panel. */
static bool s2w_touching;

static int s2w_start_posn = DEFAULT_S2W_X_B1;
static int s2w_mid_posn = DEFAULT_S2W_X_B2;
static int s2w_end_posn = (DEFAULT_S2W_X_MAX - DEFAULT_S2W_X_FINAL);
static int s2w_threshold = DEFAULT_S2W_X_FINAL;
//static int s2w_max_posn = DEFAULT_S2W_X_MAX;

static int s2w_swap_coord = 0;

/* Read cmdline for s2w */
static int __init read_s2w_cmdline(char *s2w)
{
	if (strcmp(s2w, "1") == 0) {
		pr_info("[cmdline_s2w]: Sweep2Wake enabled. | s2w='%s'\n", s2w);
		s2w_switch = 1;
	} else if (strcmp(s2w, "0") == 0) {
		pr_info("[cmdline_s2w]: Sweep2Wake disabled. | s2w='%s'\n", s2w);
		s2w_switch = 0;
	} else {
		pr_info("[cmdline_s2w]: No valid input found. Going with default: | s2w='%u'\n", s2w_switch);
	}
	return 1;
}
__setup("s2w=", read_s2w_cmdline);

/* PowerKey work func */
static void sweep2wake_presspwr(struct work_struct *work) {
	wake_pwrkey_press();
}
static DECLARE_WORK(sweep2wake_presspwr_work, sweep2wake_presspwr);

/* PowerKey trigger */
static void sweep2wake_pwrtrigger(void) {
	schedule_work(&sweep2wake_presspwr_work);
        return;
}

/* Rearms the barriers when the sweeping contact leaves the panel. */
static void sweep2wake_reset(void) {
	exec_count = true;
	barrier[0] = false;
	barrier[1] = false;
	scr_on_touch = false;
}

/* Sweep2wake main function */
static void detect_sweep2wake(int sweep_coord, int sweep_height, bool st)
{
	int swap_temp1, swap_temp2;
	int prev_coord = 0, next_coord = 0;
	bool single_touch = st;
#if S2W_DEBUG
        pr_info(LOGTAG"x,y(%4d,%4d) single:%s\n",
                sweep_coord, sweep_height, (single_touch) ? "true" : "false");
#endif
	if (s2w_swap_coord == 1) {
		//swap the coordinate system
		swap_temp1 = sweep_coord;
		swap_temp2 = sweep_height;

		sweep_height = swap_temp1;
		sweep_coord = swap_temp2;
	}

	/* s2w_s2sonly leaves only the screen-on sweep to sleep. */
	if ((single_touch) && (s2w_scr_suspended == true) && (s2w_switch > 0) &&
	    (s2w_s2sonly == 0)) {
		prev_coord = 0;
		next_coord = s2w_start_posn;
		if ((barrier[0] == true) ||
		   ((sweep_coord > prev_coord) &&
		    (sweep_coord < next_coord))) {
			prev_coord = next_coord;
			next_coord = s2w_mid_posn;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((sweep_coord > prev_coord) &&
			    (sweep_coord < next_coord))) {
				prev_coord = next_coord;
				barrier[1] = true;
				if ((sweep_coord > prev_coord)) {
					if (sweep_coord > s2w_end_posn) {
						if (exec_count) {
							pr_info(LOGTAG"ON\n");
							sweep2wake_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	//power off
	} else if ((single_touch) && (s2w_scr_suspended == false) && (s2w_switch > 0)) {
		if (s2w_swap_coord == 1) {
			//swap back for off scenario ONLY
			swap_temp1 = sweep_coord;
			swap_temp2 = sweep_height;

			sweep_height = swap_temp1;
			sweep_coord = swap_temp2;
		}

		/*
		 * The sleep sweep runs right to left along the bottom edge: it
		 * starts left of DEFAULT_S2W_X_MAX - s2w_threshold and ends left
		 * of s2w_threshold.
		 */
		scr_on_touch=true;
		prev_coord = (DEFAULT_S2W_X_MAX - s2w_threshold);
		next_coord = DEFAULT_S2W_X_B2;
		if ((barrier[0] == true) ||
		   ((sweep_coord < prev_coord) &&
		    (sweep_coord > next_coord) &&
		    (sweep_height > DEFAULT_S2W_Y_LIMIT))) {
			prev_coord = next_coord;
			next_coord = DEFAULT_S2W_X_B1;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((sweep_coord < prev_coord) &&
			    (sweep_coord > next_coord) &&
			    (sweep_height > DEFAULT_S2W_Y_LIMIT))) {
				prev_coord = next_coord;
				barrier[1] = true;
				if ((sweep_coord < prev_coord) &&
				    (sweep_height > DEFAULT_S2W_Y_LIMIT)) {
					if (sweep_coord < s2w_threshold) {
						if (exec_count) {
							pr_info(LOGTAG"OFF\n");
							sweep2wake_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	}
}

/****************** SYSFS INTERFACE (START) ********************/
static ssize_t s2w_start_posn_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_start_posn);
}

static ssize_t s2w_start_posn_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_start_posn = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_mid_posn_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_mid_posn);
}

static ssize_t s2w_mid_posn_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_mid_posn = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_end_posn_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_end_posn);
}

static ssize_t s2w_end_posn_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_end_posn = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_threshold_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_threshold);
}

/* The threshold is an X distance inside the 0..DEFAULT_S2W_X_MAX window. */
static ssize_t s2w_threshold_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int data;
	int rc = kstrtoint(buf, 0, &data);

	if (rc)
		return rc;
	if (data < 0 || data > DEFAULT_S2W_X_MAX)
		return -EINVAL;
	s2w_threshold = data;
	return count;
}

static ssize_t s2w_swap_coord_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_swap_coord);
}

static ssize_t s2w_swap_coord_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_swap_coord = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static struct kobj_attribute s2w_start_posn_attribute =
	__ATTR(s2w_start_posn,
		0666,
		s2w_start_posn_show,
		s2w_start_posn_store);

static struct kobj_attribute s2w_mid_posn_attribute =
	__ATTR(s2w_mid_posn,
		0666,
		s2w_mid_posn_show,
		s2w_mid_posn_store);

static struct kobj_attribute s2w_end_posn_attribute =
	__ATTR(s2w_end_posn,
		0666,
		s2w_end_posn_show,
		s2w_end_posn_store);

static struct kobj_attribute s2w_threshold_attribute =
	__ATTR(s2w_threshold,
		0666,
		s2w_threshold_show,
		s2w_threshold_store);

static struct kobj_attribute s2w_swap_coord_attribute =
	__ATTR(s2w_swap_coord,
		0666,
		s2w_swap_coord_show,
		s2w_swap_coord_store);

static struct attribute *s2w_parameters_attrs[] =
	{
		&s2w_start_posn_attribute.attr,
		&s2w_mid_posn_attribute.attr,
		&s2w_end_posn_attribute.attr,
		&s2w_threshold_attribute.attr,
		&s2w_swap_coord_attribute.attr,
		NULL,
	};

static struct attribute_group s2w_parameters_attr_group =
	{
		.attrs = s2w_parameters_attrs,
	};

static struct kobject *s2w_parameters_kobj;
/****************** SYSFS INTERFACE (END) ********************/


/*
 * The Himax HM852xD protocol B report emits, per contact, ABS_MT_SLOT, the
 * touch size and pressure, ABS_MT_POSITION_X/Y and then ABS_MT_TRACKING_ID
 * through input_mt_report_slot_state(); BTN_TOUCH and SYN_REPORT close the
 * frame. A lift emits ABS_MT_TRACKING_ID -1 for each released slot and
 * BTN_TOUCH 0 once every contact is up. The sweep follows the contact whose
 * tracking id began first and ends when that slot's tracking id returns to
 * -1 or BTN_TOUCH drops to 0. The handler runs under the device event_lock,
 * so the release and the coordinates reach detect_sweep2wake() in event
 * order, once per SYN_REPORT.
 */
static void s2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
	int slot;

#if S2W_DEBUG
	pr_info("sweep2wake: type: %u code: %u, val: %i\n",
		type, code, value);
#endif
	switch (type) {
	case EV_ABS:
		switch (code) {
		case ABS_MT_SLOT:
			s2w_slot = value;
			break;
		case ABS_MT_POSITION_X:
			if (s2w_slot >= 0 && s2w_slot < S2W_MAX_SLOTS)
				s2w_x[s2w_slot] = value;
			break;
		case ABS_MT_POSITION_Y:
			if (s2w_slot >= 0 && s2w_slot < S2W_MAX_SLOTS)
				s2w_y[s2w_slot] = value;
			break;
		case ABS_MT_TRACKING_ID:
			if (value >= 0) {
				if (s2w_sweep_slot < 0)
					s2w_sweep_slot = s2w_slot;
			} else if (s2w_slot == s2w_sweep_slot) {
				s2w_sweep_slot = -1;
				sweep2wake_reset();
			}
			break;
		}
		return;
	case EV_KEY:
		if (code != BTN_TOUCH)
			return;
		s2w_touching = value;
		if (!value) {
			s2w_sweep_slot = -1;
			sweep2wake_reset();
		}
		return;
	case EV_SYN:
		if (code != SYN_REPORT)
			return;
		break;
	default:
		return;
	}

	if (!s2w_touching || s2w_switch <= 0)
		return;

	/* A slot whose tracking id never returned to -1 reports no new id. */
	if (s2w_sweep_slot < 0)
		s2w_sweep_slot = s2w_slot;
	slot = s2w_sweep_slot;
	if (slot < 0 || slot >= S2W_MAX_SLOTS)
		return;

	if (s2w_scr_suspended) {
		if (s2w_s2sonly)
			return;
		wake_lock_timeout(&s2w_wake_lock, HZ);
	}
	detect_sweep2wake(s2w_x[slot], s2w_y[slot], true);
}

/* Seeds the slot cache from the values the input core already holds. */
static void s2w_sync_slots(struct input_dev *dev)
{
	int i;

	s2w_slot = input_abs_get_val(dev, ABS_MT_SLOT);
	for (i = 0; dev->mt && i < dev->mtsize && i < S2W_MAX_SLOTS; i++) {
		s2w_x[i] = input_mt_get_value(&dev->mt[i], ABS_MT_POSITION_X);
		s2w_y[i] = input_mt_get_value(&dev->mt[i], ABS_MT_POSITION_Y);
	}
}

/* The Himax HM852xD driver registers its touch input device under this name. */
static int input_dev_filter(struct input_dev *dev) {
	return strcmp(dev->name, "himax-touchscreen") ? 1 : 0;
}

static int s2w_input_connect(struct input_handler *handler,
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
	handle->name = "s2w";

	s2w_sync_slots(dev);

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

static void s2w_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id s2w_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler s2w_input_handler = {
	.event		= s2w_input_event,
	.connect	= s2w_input_connect,
	.disconnect	= s2w_input_disconnect,
	.name		= "s2w_inputreq",
	.id_table	= s2w_ids,
};

/* Tracks the primary framebuffer: any blank level other than UNBLANK is
 * screen off, the same split the Himax suspend path uses. */
static int s2w_fb_notifier_call(struct notifier_block *nb,
				unsigned long event, void *data) {
	struct fb_event *evdata = data;
	int *blank;

	if (event != FB_EVENT_BLANK || !evdata || !evdata->data ||
	    !evdata->info || evdata->info->node != 0)
		return NOTIFY_OK;

	blank = evdata->data;
	s2w_scr_suspended = (*blank != FB_BLANK_UNBLANK);
	return NOTIFY_OK;
}

static struct notifier_block s2w_fb_notif = {
	.notifier_call = s2w_fb_notifier_call,
};

/*
 * SYSFS stuff below here
 */
static ssize_t s2w_sweep2wake_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_switch);

	return count;
}

/*
 * The shared power key device is registered while the switch is above zero.
 * Returns 0 or a negative errno.
 */
static int s2w_set_switch(int val)
{
	static DEFINE_MUTEX(s2w_switch_lock);
	int rc = 0;

	mutex_lock(&s2w_switch_lock);
	if (val > 0 && s2w_switch <= 0)
		rc = wake_pwrkey_get();
	else if (val <= 0 && s2w_switch > 0)
		wake_pwrkey_put();
	if (!rc)
		s2w_switch = val;
	mutex_unlock(&s2w_switch_lock);
	return rc;
}

static ssize_t s2w_sweep2wake_dump(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	if (buf[0] >= '0' && buf[0] <= '1' && buf[1] == '\n')
		return s2w_set_switch(buf[0] - '0') ? : count;

	return count;
}

static struct kobj_attribute dev_attr_sweep2wake =
	__ATTR(sweep2wake, (S_IWUSR|S_IRUGO), s2w_sweep2wake_show, s2w_sweep2wake_dump);

static ssize_t s2w_s2w_s2sonly_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", s2w_s2sonly);

	return count;
}

static ssize_t s2w_s2w_s2sonly_dump(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	if (buf[0] >= '0' && buf[0] <= '1' && buf[1] == '\n')
		s2w_s2sonly = buf[0] - '0';

	return count;
}

static struct kobj_attribute dev_attr_s2w_s2sonly =
	__ATTR(s2w_s2sonly, (S_IWUSR|S_IRUGO), s2w_s2w_s2sonly_show, s2w_s2w_s2sonly_dump);

static ssize_t s2w_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%s\n", DRIVER_VERSION);

	return count;
}

static ssize_t s2w_version_dump(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static struct kobj_attribute dev_attr_sweep2wake_version =
	__ATTR(sweep2wake_version, (S_IWUSR|S_IRUGO), s2w_version_show, s2w_version_dump);


/*
 * Called by the Himax driver once its android_touch kobject exists, so the
 * gesture switches appear beside the other touchscreen controls.
 */
int sweep2wake_sysfs_init(struct kobject *kobj)
{
	int rc;

	rc = sysfs_create_file(kobj, &dev_attr_sweep2wake.attr);
	if (rc)
		goto err;
	rc = sysfs_create_file(kobj, &dev_attr_s2w_s2sonly.attr);
	if (rc)
		goto err_s2sonly;
	rc = sysfs_create_file(kobj, &dev_attr_sweep2wake_version.attr);
	if (rc)
		goto err_version;
	return 0;

err_version:
	sysfs_remove_file(kobj, &dev_attr_s2w_s2sonly.attr);
err_s2sonly:
	sysfs_remove_file(kobj, &dev_attr_sweep2wake.attr);
err:
	pr_warn(LOGTAG"%s: sysfs_create_file failed (%d)\n", __func__, rc);
	return rc;
}

void sweep2wake_sysfs_exit(struct kobject *kobj)
{
	sysfs_remove_file(kobj, &dev_attr_sweep2wake_version.attr);
	sysfs_remove_file(kobj, &dev_attr_s2w_s2sonly.attr);
	sysfs_remove_file(kobj, &dev_attr_sweep2wake.attr);
}

static int __init sweep2wake_init(void)
{
	int rc;

	s2w_parameters_kobj = kobject_create_and_add("s2w_parameters", kernel_kobj);
	if (!s2w_parameters_kobj) {
		pr_err("%s kobject create failed!\n", __func__);
		return -ENOMEM;
	}
	rc = sysfs_create_group(s2w_parameters_kobj, &s2w_parameters_attr_group);
	if (rc) {
		pr_err("%s sysfs create failed!\n", __func__);
		goto err_group;
	}

	wake_lock_init(&s2w_wake_lock, WAKE_LOCK_SUSPEND, "sweep2wake");

	rc = input_register_handler(&s2w_input_handler);
	if (rc) {
		pr_err("%s: Failed to register s2w_input_handler\n", __func__);
		goto err_handler;
	}

	rc = fb_register_client(&s2w_fb_notif);
	if (rc) {
		pr_err("%s: Failed to register the fb notifier\n", __func__);
		goto err_fb;
	}

	/* A boot parameter can enable the gesture before this point. */
	if (s2w_switch > 0 && wake_pwrkey_get()) {
		pr_err("%s: no power key device, sweep2wake disabled\n", __func__);
		s2w_switch = 0;
	}

	pr_info(LOGTAG"%s done\n", __func__);
	return 0;

err_fb:
	input_unregister_handler(&s2w_input_handler);
err_handler:
	wake_lock_destroy(&s2w_wake_lock);
	sysfs_remove_group(s2w_parameters_kobj, &s2w_parameters_attr_group);
err_group:
	kobject_put(s2w_parameters_kobj);
	return rc;
}

module_init(sweep2wake_init);
