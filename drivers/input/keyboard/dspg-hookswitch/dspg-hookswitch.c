/*
 * Hookswitch driver for DSPG models
 *
 *  Copyright (c) 2013 DSP Group Hong Kong Limited, Simon Lam
 *
 *  Based on corgikbd.c, which is based on xtkbd.c/locomkbd.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>
#include <linux/io.h>

#define DSPG_HOOKSWITCH_UP		1	/* off hook  */
#define DSPG_HOOKSWITCH_DOWN		0	/* on hook   */
#define DSPG_HOOKSWITCH_UNKNOWN		-1	/* undefined */

struct dspg_hookswitch_config {
	unsigned int gpio;	/* hookswitch GPIO */
	unsigned short offhook;	/* scan code for offhook */
	unsigned short onhook;	/* scan code for onhook  */
	unsigned int debounce;	/* hookswitch debounce delay in ms */
};

struct dspg_hookswitch {
	struct input_dev *input;
	struct timer_list timer;
	int irq;
	int irq_disabled;
	int status;
	struct dspg_hookswitch_config *config;
};

/* Scan the hardware key and push any changes up through the input layer */
static void
dspg_hookswitch_scankey(struct timer_list *t)
{
	struct dspg_hookswitch *hookswitch = from_timer(hookswitch, t, timer);
	unsigned int pressed = 0;

	pressed = gpio_get_value(hookswitch->config->gpio);

	if (hookswitch->status != pressed) {
		hookswitch->status = pressed;

		if (hookswitch->status == DSPG_HOOKSWITCH_UP) {
			input_report_key(hookswitch->input,
					 hookswitch->config->offhook, 1);
			if (hookswitch->config->onhook) {
				input_sync(hookswitch->input);
				input_report_key(hookswitch->input,
						 hookswitch->config->offhook,
						 0);
			}
		} else {
			if (hookswitch->config->onhook) {
				input_report_key(hookswitch->input,
						 hookswitch->config->onhook,
						 1);
				input_sync(hookswitch->input);
				input_report_key(hookswitch->input,
						 hookswitch->config->onhook,
						 0);
			} else {
				input_report_key(hookswitch->input,
						 hookswitch->config->offhook,
						 0);
			}
		}
		input_sync(hookswitch->input);
	}
	enable_irq(hookswitch->irq);
	hookswitch->irq_disabled = 0;
}

static irqreturn_t
dspg_hookswitch_interrupt(int irq, void *dev)
{
	struct dspg_hookswitch *hookswitch = (struct dspg_hookswitch *)dev;

	disable_irq_nosync(hookswitch->irq);
	hookswitch->irq_disabled = 1;

	mod_timer(&hookswitch->timer,
		  jiffies + msecs_to_jiffies(hookswitch->config->debounce));

	return IRQ_HANDLED;
}

static ssize_t
state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dspg_hookswitch *hookswitch = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hookswitch->status);
}

static struct device_attribute attr_state = __ATTR_RO(state);

#ifdef CONFIG_PM
static int
dspg_hookswitch_suspend(struct platform_device *dev, pm_message_t state)
{
	struct dspg_hookswitch *hookswitch = platform_get_drvdata(dev);

	del_timer_sync(&hookswitch->timer);

	if (device_may_wakeup(&dev->dev)) {
		enable_irq_wake(hookswitch->irq);

		if (hookswitch->irq_disabled) {
			enable_irq(hookswitch->irq);
			hookswitch->irq_disabled = 0;
		}
	} else {
		if (!hookswitch->irq_disabled) {
			disable_irq(hookswitch->irq);
			hookswitch->irq_disabled = 1;
		}
	}

	return 0;
}

static int dspg_hookswitch_resume(struct platform_device *dev)
{
	struct dspg_hookswitch *hookswitch = platform_get_drvdata(dev);

	if (device_may_wakeup(&dev->dev))
		disable_irq_wake(hookswitch->irq);

	if (hookswitch->irq_disabled) {
		enable_irq(hookswitch->irq);
		hookswitch->irq_disabled = 0;
	}

	return 0;
}
#else
#define dspg_hookswitch_suspend NULL
#define dspg_hookswitch_resume  NULL
#endif

#ifdef CONFIG_OF
static struct dspg_hookswitch_config * __init
dspg_hookswitch_parse_dt(struct platform_device *pdev)
{
	struct dspg_hookswitch_config *pdata;
	struct device_node *np = pdev->dev.of_node;
	u32 prop;
	int ret;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->gpio = of_get_named_gpio(np, "gpio", 0);

	if (of_property_read_u32(np, "dspg,offhook", &prop)) {
		dev_err(&pdev->dev, "missing or invalid dspg,offhook\n");
		return NULL;
	}
	pdata->offhook = (unsigned short)prop;

	prop = 0;
	ret = of_property_read_u32(np, "dspg,onhook", &prop);
	if (ret && ret != -EINVAL) {
		dev_err(&pdev->dev, "invalid dspg,onhook\n");
		return NULL;
	}
	pdata->onhook = (unsigned short)prop;

	if (of_property_read_u32(np, "dspg,debounce", &prop)) {
		dev_err(&pdev->dev, "missing or invalid dspg,debounce\n");
		return NULL;
	}
	pdata->debounce = prop;

	return pdata;
}
#else
static struct dspg_hookswitch_config * __init
dspg_hookswitch_parse_dt(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int __init
dspg_hookswitch_probe(struct platform_device *pdev)
{
	struct dspg_hookswitch_config *pdata;
	struct dspg_hookswitch *hookswitch;
	struct input_dev *input_dev;
	int ret = 0;

	hookswitch = kzalloc(sizeof(struct dspg_hookswitch), GFP_KERNEL);
	if (!hookswitch)
		return -ENOMEM;

	hookswitch->input = input_dev = input_allocate_device();
	if (!input_dev) {
		ret = -ENOMEM;
		goto out_input_alloc_fail;
	}

	platform_set_drvdata(pdev, hookswitch);
	if (pdev->dev.of_node)
		pdata = dspg_hookswitch_parse_dt(pdev);
	else
		pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		ret = -EINVAL;
		goto out_no_pdata;
	}
	hookswitch->config = pdata;

	if (!gpio_is_valid(pdata->gpio)) {
		dev_err(&pdev->dev, "gpio invalid\n");
		goto out_gpio_not_valid;
	}

	gpio_direction_input(pdata->gpio);

	hookswitch->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot mapped gpio to irq\n");
		goto out_irq_map_failed;
	}

	/* Init Keyboard rescan timer */
	timer_setup(&hookswitch->timer, dspg_hookswitch_scankey, 0);

	input_dev->name = "DSPG hookswitch";
	input_dev->phys = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0002;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &pdev->dev;
	input_set_drvdata(input_dev, hookswitch);
	input_dev->evbit[0] = BIT(EV_KEY);
	input_dev->keycodesize = sizeof(unsigned short);

	hookswitch->status = gpio_get_value(hookswitch->config->gpio);
	set_bit(hookswitch->config->offhook, input_dev->keybit);
	set_bit(hookswitch->config->onhook, input_dev->keybit);

	/* uncomment to enable QT device discovery */
	/* set_bit(KEY_Q, input_dev->keybit); */

	clear_bit(0, input_dev->keybit);

	ret = input_register_device(hookswitch->input);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto out_register_failed;
	}

	ret = device_create_file(&pdev->dev, &attr_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs group\n");
		goto out_sysfs_failed;
	}

	/* hookswitch is wakeup capable by default */
	device_init_wakeup(&pdev->dev, 1);

	/* Setup sense interrupts - RisingEdge Detect, sense lines as inputs */
	ret = request_irq(hookswitch->irq, dspg_hookswitch_interrupt,
			  IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,
			  "dspg_hookswitch", hookswitch);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot get irq\n");
		goto out_irq_request_failed;
	}

	return 0;

out_irq_request_failed:
	device_remove_file(&pdev->dev, &attr_state);

out_sysfs_failed:
	input_unregister_device(hookswitch->input);
	hookswitch->input = NULL;

out_register_failed:
out_irq_map_failed:
	if (gpio_is_valid(pdata->gpio))
		gpio_free(pdata->gpio);

out_gpio_not_valid:
out_no_pdata:
	if (hookswitch->input)
		input_free_device(hookswitch->input);

out_input_alloc_fail:
	kfree(hookswitch);

	return ret;
}

static int
dspg_hookswitch_remove(struct platform_device *pdev)
{
	struct dspg_hookswitch *hookswitch = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &attr_state);
	free_irq(hookswitch->irq, hookswitch);
	del_timer_sync(&hookswitch->timer);
	input_unregister_device(hookswitch->input);
	kfree(hookswitch);

	return 0;
}

static const struct of_device_id dspg_hookswitch_of_match[] = {
	{ .compatible = "dspg,hookswitch", },
	{ },
};
MODULE_DEVICE_TABLE(of, dspg_hookswitch_of_match);

static struct platform_driver dspg_hookswitch_driver = {
	.remove		= dspg_hookswitch_remove,
	.suspend	= dspg_hookswitch_suspend,
	.resume		= dspg_hookswitch_resume,
	.driver		= {
		.name	= "dspg-hookswitch",
		.owner	= THIS_MODULE,
		.of_match_table = dspg_hookswitch_of_match,
	},
};

static int __init dspg_hookswitch_init(void)
{
	return platform_driver_probe(&dspg_hookswitch_driver,
				     dspg_hookswitch_probe);
}

static void __exit dspg_hookswitch_exit(void)
{
	platform_driver_unregister(&dspg_hookswitch_driver);
}

module_init(dspg_hookswitch_init);
module_exit(dspg_hookswitch_exit);

MODULE_AUTHOR("Simon Lam <simon.lam@dspg.com>");
MODULE_DESCRIPTION("DSPG hookswitch driver");
MODULE_LICENSE("GPL");
