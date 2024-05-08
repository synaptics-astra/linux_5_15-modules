// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * Author: Kaicheng Xie <Kaicheng.Xie@Synaptics.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#include "clk.h"

static const struct gateclk_desc myna2_gates[] = {
	{ "usb0coreclk",	"perifsysclk",	0 },
	{ "sdiosysclk",		"perifsysclk",	1 },
	{ "emmcsysclk",		"perifsysclk",	2 },
	{ "pbridgecoreclk",	"perifsysclk",	3 },
	{ "gpuaxiclk",		"perifsysclk",	4 },
	{ "gethrgmiisysclk",	"perifsysclk",	5 },
	{ "nfcsysclk",		"perifsysclk",	6 },
};

static int myna2_gateclk_setup(struct platform_device *pdev)
{
	return berlin_gateclk_setup(pdev, myna2_gates, ARRAY_SIZE(myna2_gates));
}

static const struct clk_desc myna2_descs[] = {
	{ "cpufastrefclk",		0x0, CLK_IS_CRITICAL, CLK_RATE_NO_CHANGE},
	{ "memfastrefclk",		0x4, CLK_IS_CRITICAL, CLK_RATE_NO_CHANGE },
	{ "cfgclk",			0x8, CLK_IS_CRITICAL, CLK_RATE_NO_CHANGE },
	{ "atbclk",			0xc, CLK_IS_CRITICAL, CLK_RATE_NO_CHANGE },
	{ "apbcoreclk",			0x10, CLK_IS_CRITICAL, CLK_RATE_NO_CHANGE},
	{ "usb2test480mg0clk",		0x14 },
	{ "usb2test480mg1clk",		0x18 },
	{ "usb2test480mg2clk",		0x1c },
	{ "usb2test100mg0clk",		0x20 },
	{ "usb2test100mg1clk",		0x24 },
	{ "usb2test100mg2clk",		0x28 },
	{ "usb2test100mg3clk",		0x2c },
	{ "usb2test100mg4clk",		0x30 },
	{ "periftest125mg0clk",		0x34 },
	{ "periftest250mg0clk",		0x38 },
	{ "periftest500mg0clk",		0x3c },
	{ "periftest200mg0clk",		0x40 },
	{ "periftest200mg1clk",		0x44 },
	{ "emmcclk",			0x48 },
	{ "sd0clk",			0x4c },
	{ "gethrgmiiclk",		0x50 },
	{ "usb2testclk",		0x54 },
	{ "usb3coreclk",		0x58 },
	{ "nfceccclk",			0x5c },
	{ "nfccoreclk",			0x60 },
	{ "gpuclk",			0x68 },
	{ "sysclk",			0x6c, CLK_IS_CRITICAL },
	{ "aiosysclk",			0x70, CLK_IS_CRITICAL },
	{ "perifsysclk",		0x74, CLK_IS_CRITICAL },
	{ "avioclk",			0x78, CLK_IS_CRITICAL },
	{ "avsysclk",			0x7c, CLK_IS_CRITICAL },
	{ "lcdc1scanclk",		0x80, CLK_IS_CRITICAL },
	{ "lcdc2scanclk",		0x84, CLK_IS_CRITICAL },
};

static int myna2_clk_setup(struct platform_device *pdev)
{
	return berlin_clk_setup(pdev, myna2_descs, ARRAY_SIZE(myna2_descs));
}

static const struct of_device_id myna2_clks_match_table[] = {
	{ .compatible = "syna,myna2-clk",
	  .data = myna2_clk_setup },
	{ .compatible = "syna,myna2-gateclk",
	  .data = myna2_gateclk_setup },
	{ }
};
MODULE_DEVICE_TABLE(of, myna2_clks_match_table);

static int myna2_clks_probe(struct platform_device *pdev)
{
	int (*clk_setup)(struct platform_device *pdev);
	int ret;

	clk_setup = of_device_get_match_data(&pdev->dev);
	if (!clk_setup)
		return -ENODEV;

	ret = clk_setup(pdev);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver myna2_clks_driver = {
	.probe		= myna2_clks_probe,
	.driver		= {
		.name	= "syna-myna2-clks",
		.of_match_table = myna2_clks_match_table,
	},
};
module_platform_driver(myna2_clks_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synaptics myna2 clks Driver");
