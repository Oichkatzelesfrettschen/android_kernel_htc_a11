/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_WAKE_PWRKEY_H
#define _LINUX_WAKE_PWRKEY_H

/*
 * One KEY_POWER input device shared by the wake gestures. The device exists
 * only between the first wake_pwrkey_get() and the last wake_pwrkey_put(),
 * so a kernel with every gesture switch at zero registers no input device.
 */
int wake_pwrkey_get(void);
void wake_pwrkey_put(void);
void wake_pwrkey_press(void);

struct notifier_block;

/*
 * A gesture switch that changes whether the touch controller must scan
 * while the screen is off calls wake_gesture_changed() from process
 * context; the touch driver registers a notifier to rearm the controller.
 */
int wake_gesture_register_notifier(struct notifier_block *nb);
int wake_gesture_unregister_notifier(struct notifier_block *nb);
void wake_gesture_changed(void);

#endif /* _LINUX_WAKE_PWRKEY_H */
