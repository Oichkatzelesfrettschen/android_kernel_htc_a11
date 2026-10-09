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
#include <linux/input/mt.h>
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

/*
 * Slot bound for the per-slot coordinate cache. The Himax report carries the
 * contact count in a 4-bit field, so slot numbers stay below 16.
 */
#define DT2W_MAX_SLOTS		16

/* Resources */
int dt2w_switch = DT2W_DEFAULT;
bool dt2w_scr_suspended = false;
static s64 tap_time_pre = 0;
static int touch_nr = 0, x_pre = 0, y_pre = 0;
static struct wake_lock dt2w_wake_lock;

/*
 * Input core view of the touch device. input_handle_abs_event() drops an
 * ABS_MT value equal to the one already stored for its slot and passes
 * ABS_MT_SLOT only when a surviving MT value belongs to another slot than
 * the last one passed. The cache therefore mirrors the core's per-slot
 * values: a coordinate absent from a frame equals the cached one.
 */
static int dt2w_slot;
static int dt2w_x[DT2W_MAX_SLOTS], dt2w_y[DT2W_MAX_SLOTS];
/* Slot whose ABS_MT_TRACKING_ID went from -1 to a new id in this frame. */
static int dt2w_down_slot = -1;
/* BTN_TOUCH went from 0 to 1 in this frame: the first contact landed. */
static bool dt2w_touch_began;

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

static void doubletap2wake_reset(void) {
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
static void new_touch(int x, int y, s64 now) {
	tap_time_pre = now;
	x_pre = x;
	y_pre = y;
	touch_nr++;
}

/*
 * One call per tap: the frame in which the first contact lands. A second
 * tap within DT2W_TIME of the first and within DT2W_FEATHER of its position
 * presses the power key; any other tap starts a new pair.
 */
static void detect_doubletap2wake(int x, int y)
{
	s64 now = ktime_to_ms(ktime_get());

#if DT2W_DEBUG
        pr_info(LOGTAG"tap x,y(%4d,%4d)\n", x, y);
#endif
	if (touch_nr == 1 &&
	    calc_feather(x, x_pre) < DT2W_FEATHER &&
	    calc_feather(y, y_pre) < DT2W_FEATHER &&
	    now - tap_time_pre < DT2W_TIME) {
		pr_info(LOGTAG"ON\n");
		doubletap2wake_pwrtrigger();
		doubletap2wake_reset();
		return;
	}

	doubletap2wake_reset();
	new_touch(x, y, now);
}

/*
 * The Himax HM852xD protocol B report emits, per contact, ABS_MT_SLOT, the
 * touch size and pressure, ABS_MT_POSITION_X/Y and then ABS_MT_TRACKING_ID
 * through input_mt_report_slot_state(); BTN_TOUCH and SYN_REPORT close the
 * frame. A lift emits ABS_MT_TRACKING_ID -1 for each released slot and
 * BTN_TOUCH 0 once every contact is up. BTN_TOUCH reaches handlers only when
 * it changes, so its 0 to 1 edge marks the start of a tap. The handler runs
 * under the device event_lock and serializes with every other event of the
 * frame; the tap is evaluated at SYN_REPORT with the coordinates of the slot
 * whose tracking id began in that frame.
 */
static void dt2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
	int slot;

#if DT2W_DEBUG
	pr_info("doubletap2wake: type: %u code: %u, val: %i\n",
		type, code, value);
#endif
	switch (type) {
	case EV_ABS:
		switch (code) {
		case ABS_MT_SLOT:
			dt2w_slot = value;
			break;
		case ABS_MT_POSITION_X:
			if (dt2w_slot >= 0 && dt2w_slot < DT2W_MAX_SLOTS)
				dt2w_x[dt2w_slot] = value;
			break;
		case ABS_MT_POSITION_Y:
			if (dt2w_slot >= 0 && dt2w_slot < DT2W_MAX_SLOTS)
				dt2w_y[dt2w_slot] = value;
			break;
		case ABS_MT_TRACKING_ID:
			if (value >= 0 && dt2w_down_slot < 0)
				dt2w_down_slot = dt2w_slot;
			break;
		}
		return;
	case EV_KEY:
		if (code == BTN_TOUCH && value)
			dt2w_touch_began = true;
		return;
	case EV_SYN:
		if (code != SYN_REPORT)
			return;
		break;
	default:
		return;
	}

	slot = dt2w_down_slot >= 0 ? dt2w_down_slot : dt2w_slot;
	if (dt2w_touch_began && dt2w_switch > 0 && dt2w_scr_suspended &&
	    slot >= 0 && slot < DT2W_MAX_SLOTS) {
		wake_lock_timeout(&dt2w_wake_lock, HZ);
		detect_doubletap2wake(dt2w_x[slot], dt2w_y[slot]);
	}
	dt2w_touch_began = false;
	dt2w_down_slot = -1;
}

/* The Himax HM852xD driver registers its touch input device under this name. */
static int input_dev_filter(struct input_dev *dev) {
	return !dev->name || strcmp(dev->name, "himax-touchscreen") ? 1 : 0;
}

/* Seeds the slot cache from the values the input core already holds. */
static void dt2w_sync_slots(struct input_dev *dev)
{
	int i;

	dt2w_slot = input_abs_get_val(dev, ABS_MT_SLOT);
	for (i = 0; dev->mt && i < dev->mtsize && i < DT2W_MAX_SLOTS; i++) {
		dt2w_x[i] = input_mt_get_value(&dev->mt[i], ABS_MT_POSITION_X);
		dt2w_y[i] = input_mt_get_value(&dev->mt[i], ABS_MT_POSITION_Y);
	}
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

	dt2w_sync_slots(dev);

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
	if (!rc)
		wake_gesture_changed();
	return rc;
}

static ssize_t dt2w_doubletap2wake_dump(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;

	if (kstrtouint(buf, 10, &val) || val > 2)
		return -EINVAL;

	return dt2w_set_switch(val) ? : count;
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
	return rc;
}

module_init(doubletap2wake_init);
