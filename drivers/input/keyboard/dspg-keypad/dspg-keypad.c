/*
 *  Keyboard driver for DSPG keypad controller
 *
 *  Copyright (c) 2018 DSPG Technologies GmbH
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
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "dspg-keypad.h"

#define DSPG_KBD_CFG 0x00

#define DSPG_KEYPAD_ACTIVATE_DELAY  20 /* us */
#define DSPG_KEYPAD_DEBOUNCE_DELAY  50 /* ms */

#define STATE_IGNORE	-1
#define STATE_RELEASED	0
#define STATE_PRESSED	1

static inline void
dspg_keypad_activate_col(struct dspg_keypad *dspg_keypad, int col)
{
	/* STROBE col -> High, not col -> HiZ */
	if ((1 << col) & dspg_keypad->config->cols)
		writel(1 << col, dspg_keypad->regs + dspg_keypad->reg_col);
}

static inline void
dspg_keypad_activate_all(struct dspg_keypad *dspg_keypad)
{
	/* STROBE ALL -> High, against the column bitmask */
	writel(dspg_keypad->config->cols,
	       dspg_keypad->regs + dspg_keypad->reg_col);
}

static int
dspg_keypad_key_pressed(struct dspg_keypad *dspg_keypad)
{
	return (readl(dspg_keypad->regs + dspg_keypad->reg_row) &
		dspg_keypad->config->rows);
}

static void
dspg_keypad_setup_sense_lines_and_strobe_high(struct dspg_keypad *dspg_keypad)
{
	unsigned long val;

	val = dspg_keypad->config->cols | (dspg_keypad->config->rows << 8);
	writel(val, dspg_keypad->regs + DSPG_KBD_CFG);
}

/*
 * The dw keyboard generates interrupts as long as a key is pressed.
 * When a key is pressed, we disable the interrupt and enable a timer to
 * periodically can the keyboard to detect when the key is released.
 */

/* Scan the hardware keyboard and push any changes up through the input layer */
static void
dspg_keypad_scankeyboard(struct timer_list *t)
{
	struct dspg_keypad *dspg_keypad = from_timer(dspg_keypad, t, timer);
	unsigned int max_rows, max_cols, col, row, row_mask, scancode;
	unsigned int pressed;
	struct list_head *entry;
	int ret;
	unsigned long flags;

	max_cols = fls(dspg_keypad->config->cols);
	max_rows = fls(dspg_keypad->config->rows);

	for (col = 0; col < max_cols; col++) {
		if (!((1 << col) & dspg_keypad->config->cols))
			continue;

		/*
		 * Discharge the output driver capacitance
		 * in the keyboard matrix. (Yes it is significant..)
		 */
		dspg_keypad_activate_col(dspg_keypad, col);
		udelay(dspg_keypad->config->discharge_us);

		row_mask = dspg_keypad_key_pressed(dspg_keypad);

		for (row = 0; row < max_rows; row++) {
			if (!((1 << row) & dspg_keypad->config->rows))
				continue;

			scancode = MATRIX_SCAN_CODE(row, col, dspg_keypad->config->row_shift);
			pressed = !!(row_mask & (1 << row));
			ret = 0;

			spin_lock_irqsave(&dspg_keypad->handler_lock, flags);
			list_for_each(entry, &dspg_keypad->handler_list) {
				struct dspg_keypad_handler *handler;

				handler = list_entry(entry,
						     struct dspg_keypad_handler,
						     next_handler);
				ret |= handler->handler(handler, scancode,
							pressed);
			}
			spin_unlock_irqrestore(&dspg_keypad->handler_lock,
					       flags);

			if (ret)
				dspg_keypad->keystate[scancode] = STATE_IGNORE;
			else
				dspg_keypad->keystate[scancode] = pressed;
		}
	}

	if (dspg_keypad->last_scancode != -1) {
		if (dspg_keypad->keystate[dspg_keypad->last_scancode] !=
		    STATE_PRESSED)
			dspg_keypad->last_scancode = -1;
	}


	for (col = 0; col < max_cols; col++) {
		if (!((1 << col) & dspg_keypad->config->cols))
			continue;
		for (row = 0; row < max_rows; row++) {
			if (!((1 << row) & dspg_keypad->config->rows))
				continue;

			scancode = MATRIX_SCAN_CODE(row, col, dspg_keypad->config->row_shift);

			if (dspg_keypad->keystate[scancode] == STATE_IGNORE)
				continue;

			if ((dspg_keypad->last_scancode == -1) &&
			    (dspg_keypad->keystate[scancode] == STATE_PRESSED))
				dspg_keypad->last_scancode = scancode;

			input_report_key(dspg_keypad->input,
				dspg_keypad->keycodes[scancode],
				((dspg_keypad->keystate[scancode] ==
							STATE_PRESSED) &&
				(scancode == dspg_keypad->last_scancode)));
		}
	}

	input_sync(dspg_keypad->input);

	dspg_keypad_activate_all(dspg_keypad);
	udelay(dspg_keypad->config->discharge_us);
	dspg_keypad->irq_disabled = 0;
	enable_irq(dspg_keypad->irq);
}

static irqreturn_t
dspg_keypad_interrupt(int irq, void *dev_id)
{
	struct dspg_keypad *dspg_keypad = (struct dspg_keypad *)dev_id;

	disable_irq_nosync(dspg_keypad->irq);
	dspg_keypad->irq_disabled = 1;
	mod_timer(&dspg_keypad->timer, jiffies +
		msecs_to_jiffies(dspg_keypad->config->debounce_ms));

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int
dspg_keypad_suspend(struct platform_device *dev, pm_message_t state)
{
	struct dspg_keypad *dspg_keypad = platform_get_drvdata(dev);

	if (device_may_wakeup(&dev->dev))
		enable_irq_wake(dspg_keypad->irq);

	/*
	 * Prevent a race with the interrupt / scanning function.
	 */
	disable_irq(dspg_keypad->irq);
	del_timer_sync(&dspg_keypad->timer);
	if (dspg_keypad->irq_disabled) {
		enable_irq(dspg_keypad->irq);
		dspg_keypad->irq_disabled = 0;
	}
	enable_irq(dspg_keypad->irq);

	return 0;
}
static int
dspg_keypad_resume(struct platform_device *dev)
{
	struct dspg_keypad *dspg_keypad = platform_get_drvdata(dev);

	if (device_may_wakeup(&dev->dev))
		disable_irq_wake(dspg_keypad->irq);

	return 0;
}
#else
#define dspg_keypad_suspend NULL
#define dspg_keypad_resume  NULL
#endif

static struct dspg_keypad_config * __init
dspg_keypad_parse_dt(struct platform_device *pdev)
{
	struct dspg_keypad_config *pdata;
	struct device_node *np = pdev->dev.of_node;
	u32 prop;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_property_read_u32(np, "dspg,rows", &prop)) {
		dev_err(&pdev->dev, "missing dspg,rows\n");
		return NULL;
	}
	pdata->rows = prop;

	if (of_property_read_u32(np, "dspg,cols", &prop)) {
		dev_err(&pdev->dev, "missing dspg,cols\n");
		return NULL;
	}
	pdata->cols = prop;

	if (of_property_read_u32(np, "dspg,discharge-us", &prop)) {
		dev_info(&pdev->dev,
			 "no dspg,discharge-us in dt, falling back to %dus\n",
			 DSPG_KEYPAD_ACTIVATE_DELAY);
		prop = DSPG_KEYPAD_ACTIVATE_DELAY;
	}
	pdata->discharge_us = prop;

	if (of_property_read_u32(np, "dspg,debounce-ms", &prop)) {
		dev_info(&pdev->dev,
			 "no dspg,debounce-ms in dt, falling back to %dms\n",
			 DSPG_KEYPAD_DEBOUNCE_DELAY);
		prop = DSPG_KEYPAD_DEBOUNCE_DELAY;
	}
	pdata->debounce_ms = prop;

	pdata->row_shift = get_count_order(fls(pdata->cols));

	return pdata;
}

static int __init
dspg_keypad_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dspg_keypad_config *pdata;
	struct dspg_keypad *dspg_keypad;
	struct input_dev *input_dev;
	struct resource *res;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "missing dt entry\n");
		ret = -EINVAL;
		goto out;
	}

	dspg_keypad = devm_kzalloc(&pdev->dev, sizeof(struct dspg_keypad),
				   GFP_KERNEL);
	if (!dspg_keypad) {
		ret = -ENOMEM;
		goto out;
	}

	if (of_property_read_bool(pdev->dev.of_node, "reg-layout-myna2")) {
		dspg_keypad->reg_col = 0x04;
		dspg_keypad->reg_row = 0x08;
	} else {
		dspg_keypad->reg_col = 0x08;
		dspg_keypad->reg_row = 0x04;
	}

	dspg_keypad->input = input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev) {
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, dspg_keypad);

	pdata = dspg_keypad_parse_dt(pdev);
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		ret = -EINVAL;
		goto out;
	}
	dspg_keypad->config = pdata;
	dspg_keypad->last_scancode = -1;

	dspg_keypad->irq = platform_get_irq(pdev, 0);
	if (dspg_keypad->irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		ret = -ENODEV;
		goto out;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto out;
	}

	dspg_keypad->mem = devm_request_mem_region(&pdev->dev, res->start,
						   resource_size(res),
						   pdev->name);
	if (!dspg_keypad->mem) {
		dev_err(&pdev->dev, "cannot get register range\n");
		ret = EBUSY;
		goto out;
	}

	dspg_keypad->regs = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (dspg_keypad->regs == NULL) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		ret = -ENODEV;
		goto out;
	}

	/* Init Keyboard rescan timer */
	timer_setup(&dspg_keypad->timer, dspg_keypad_scankeyboard, 0);

	input_dev->name = "DSPG keypad";
	input_dev->phys = pdev->name;
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &pdev->dev;
	input_set_drvdata(input_dev, dspg_keypad);

	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	input_dev->keycode = dspg_keypad->keycodes;
	input_dev->keycodesize = sizeof(dspg_keypad->keycodes[0]);
	input_dev->keycodemax =
			fls(pdata->rows) << dspg_keypad->config->row_shift;

	ret = matrix_keypad_build_keymap(pdata->keymap_data, NULL,
			fls(pdata->rows), fls(pdata->cols),
			input_dev->keycode, input_dev);

	ret = input_register_device(dspg_keypad->input);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto out;
	}

	/* Set Strobe lines as outputs - set high */
	dspg_keypad_setup_sense_lines_and_strobe_high(dspg_keypad);
	dspg_keypad_activate_all(dspg_keypad);

	/* Setup sense interrupts - RisingEdge Detect, sense lines as inputs */
	ret = devm_request_irq(&pdev->dev, dspg_keypad->irq,
			       dspg_keypad_interrupt, IRQF_TRIGGER_HIGH,
			       "dspg_keypad", dspg_keypad);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot get irq\n");
		goto out;
	}

	spin_lock_init(&dspg_keypad->handler_lock);
	INIT_LIST_HEAD(&dspg_keypad->handler_list);

	ret = of_platform_populate(np, NULL, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error populating subdevices\n");
		goto out;
	}

	/* Keypad is wakeup capable by default */
	device_init_wakeup(&pdev->dev, 1);

	return 0;

out:
	return ret;
}

static int
dspg_keypad_remove(struct platform_device *pdev)
{
	struct dspg_keypad *dspg_keypad = platform_get_drvdata(pdev);

	del_timer_sync(&dspg_keypad->timer);
	return 0;
}

static const struct of_device_id dspg_keypad_of_match[] = {
	{ .compatible = "dspg,keypad", },
	{ },
};
MODULE_DEVICE_TABLE(of, dspg_keypad_of_match);

static struct platform_driver dspg_keypad_driver = {
	.remove		= dspg_keypad_remove,
	.suspend	= dspg_keypad_suspend,
	.resume		= dspg_keypad_resume,
	.driver		= {
		.name	= "dspg-keypad",
		.owner  = THIS_MODULE,
		.of_match_table = dspg_keypad_of_match,
	},
};
module_platform_driver_probe(dspg_keypad_driver, dspg_keypad_probe);

MODULE_AUTHOR("Andreas Weissel <andreas.weissel@dspg.com>");
MODULE_DESCRIPTION("DSPG Keypad Driver");
MODULE_LICENSE("GPL");
