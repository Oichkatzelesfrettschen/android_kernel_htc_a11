/*
 * kernel/power/wakeup_reason.c
 *
 * Logs the reasons which caused the kernel to resume from
 * the suspend mode.
 *
 * Copyright (C) 2014 Google, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/wakeup_reason.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/string.h>


#define MAX_WAKEUP_REASON_IRQS 32
#define WAKEUP_REASON_NAME_LEN 32
static int irq_list[MAX_WAKEUP_REASON_IRQS];
static char irq_names[MAX_WAKEUP_REASON_IRQS][WAKEUP_REASON_NAME_LEN];
static int irqcount;
static struct kobject *wakeup_reason;
static spinlock_t resume_reason_lock;

/*
 * The action name is copied under desc->lock at log time, the point where
 * free_irq() cannot release the irqaction; the show path reads only the
 * snapshot, under resume_reason_lock, and never touches the irq_desc.
 */
static ssize_t last_resume_reason_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	int irq_no, buf_offset = 0;
	unsigned long flags;

	spin_lock_irqsave(&resume_reason_lock, flags);
	for (irq_no = 0; irq_no < irqcount && buf_offset < PAGE_SIZE - 1;
			irq_no++) {
		if (irq_names[irq_no][0])
			buf_offset += scnprintf(buf + buf_offset,
					PAGE_SIZE - buf_offset, "%d %s\n",
					irq_list[irq_no], irq_names[irq_no]);
		else
			buf_offset += scnprintf(buf + buf_offset,
					PAGE_SIZE - buf_offset, "%d\n",
					irq_list[irq_no]);
	}
	spin_unlock_irqrestore(&resume_reason_lock, flags);
	return buf_offset;
}

static struct kobj_attribute resume_reason = __ATTR_RO(last_resume_reason);

static struct attribute *attrs[] = {
	&resume_reason.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

/*
 * logs all the wake up reasons to the kernel
 * stores the irqs to expose them to the userspace via sysfs
 */
void log_wakeup_reason(int irq)
{
	struct irq_desc *desc;
	unsigned long flags, dflags;
	char name[WAKEUP_REASON_NAME_LEN] = "";

	desc = irq_to_desc(irq);
	if (desc) {
		raw_spin_lock_irqsave(&desc->lock, dflags);
		if (desc->action && desc->action->name)
			strlcpy(name, desc->action->name, sizeof(name));
		raw_spin_unlock_irqrestore(&desc->lock, dflags);
	}

	if (name[0])
		printk(KERN_INFO "Resume caused by IRQ %d, %s\n", irq, name);
	else
		printk(KERN_INFO "Resume caused by IRQ %d\n", irq);

	spin_lock_irqsave(&resume_reason_lock, flags);
	if (irqcount == MAX_WAKEUP_REASON_IRQS) {
		spin_unlock_irqrestore(&resume_reason_lock, flags);
		printk(KERN_WARNING "Resume caused by more than %d IRQs\n",
				MAX_WAKEUP_REASON_IRQS);
		return;
	}

	irq_list[irqcount] = irq;
	strlcpy(irq_names[irqcount], name, WAKEUP_REASON_NAME_LEN);
	irqcount++;
	spin_unlock_irqrestore(&resume_reason_lock, flags);
}

/* Detects a suspend and clears all the previous wake up reasons*/
static int wakeup_reason_pm_event(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	unsigned long flags;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		spin_lock_irqsave(&resume_reason_lock, flags);
		irqcount = 0;
		spin_unlock_irqrestore(&resume_reason_lock, flags);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wakeup_reason_pm_notifier_block = {
	.notifier_call = wakeup_reason_pm_event,
};

/* Initializes the sysfs parameter
 * registers the pm_event notifier
 */
int __init wakeup_reason_init(void)
{
	int retval;
	spin_lock_init(&resume_reason_lock);
	retval = register_pm_notifier(&wakeup_reason_pm_notifier_block);
	if (retval)
		printk(KERN_WARNING "[%s] failed to register PM notifier %d\n",
				__func__, retval);

	wakeup_reason = kobject_create_and_add("wakeup_reasons", kernel_kobj);
	if (!wakeup_reason) {
		printk(KERN_WARNING "[%s] failed to create a sysfs kobject\n",
				__func__);
		return 1;
	}
	retval = sysfs_create_group(wakeup_reason, &attr_group);
	if (retval) {
		kobject_put(wakeup_reason);
		printk(KERN_WARNING "[%s] failed to create a sysfs group %d\n",
				__func__, retval);
	}
	return 0;
}

late_initcall(wakeup_reason_init);
