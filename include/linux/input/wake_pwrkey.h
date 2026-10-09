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

#endif /* _LINUX_WAKE_PWRKEY_H */
