// SPDX-License-Identifier: GPL-2.0
/*
 * KEY_POWER injection device shared by sweep2wake and doubletap2wake.
 * Registered on the first reference and unregistered on the last, so the
 * input device numbering of a kernel with the gestures off is unchanged.
 */
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/wake_pwrkey.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>

#define WAKE_PWRKEY_DUR_MS	60

static DEFINE_MUTEX(wake_pwrkey_lock);
static BLOCKING_NOTIFIER_HEAD(wake_gesture_chain);
static struct input_dev *wake_pwrkey_dev;
static int wake_pwrkey_refs;

int wake_pwrkey_get(void)
{
	struct input_dev *dev;
	int rc = 0;

	mutex_lock(&wake_pwrkey_lock);
	if (wake_pwrkey_refs == 0) {
		dev = input_allocate_device();
		if (!dev) {
			rc = -ENOMEM;
			goto out;
		}
		input_set_capability(dev, EV_KEY, KEY_POWER);
		dev->name = "wake_pwrkey";
		dev->phys = "wake_pwrkey/input0";
		rc = input_register_device(dev);
		if (rc) {
			input_free_device(dev);
			goto out;
		}
		wake_pwrkey_dev = dev;
	}
	wake_pwrkey_refs++;
out:
	mutex_unlock(&wake_pwrkey_lock);
	return rc;
}

void wake_pwrkey_put(void)
{
	mutex_lock(&wake_pwrkey_lock);
	if (wake_pwrkey_refs > 0 && --wake_pwrkey_refs == 0) {
		input_unregister_device(wake_pwrkey_dev);
		wake_pwrkey_dev = NULL;
	}
	mutex_unlock(&wake_pwrkey_lock);
}

int wake_gesture_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&wake_gesture_chain, nb);
}

int wake_gesture_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&wake_gesture_chain, nb);
}

void wake_gesture_changed(void)
{
	blocking_notifier_call_chain(&wake_gesture_chain, 0, NULL);
}

/* Presses and releases KEY_POWER; sleeps, so it runs from process context. */
void wake_pwrkey_press(void)
{
	mutex_lock(&wake_pwrkey_lock);
	if (wake_pwrkey_dev) {
		input_event(wake_pwrkey_dev, EV_KEY, KEY_POWER, 1);
		input_event(wake_pwrkey_dev, EV_SYN, 0, 0);
		msleep(WAKE_PWRKEY_DUR_MS);
		input_event(wake_pwrkey_dev, EV_KEY, KEY_POWER, 0);
		input_event(wake_pwrkey_dev, EV_SYN, 0, 0);
		msleep(WAKE_PWRKEY_DUR_MS);
	}
	mutex_unlock(&wake_pwrkey_lock);
}
