// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Synaptics Incorporated
 *
 * based on as370.c
 *
 * Author: Benson Gui <begu@synaptics.com>
 *
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#include "clk.h"

static const struct gateclk_desc dolphin_gates[] = {
	{ "usb0coreclk",	"perifsysclk",	0 },
	{ "sdiosysclk",		"perifsysclk",	1 },
	{ "pcie0sysclk",	"perifsysclk",	2 },
	{ "emmcsysclk",		"perifsysclk",	3 },
	{ "pbridgecoreclk",	"perifsysclk",	4 },
	{ "npuaxiclk",		"gfx3dsysclk",	5 },
	{ "gethrgmiisysclk",	"perifsysclk",	6 },
};

static int dolphin_gateclk_setup(struct platform_device *pdev)
{
	return berlin_gateclk_setup(pdev, dolphin_gates, ARRAY_SIZE(dolphin_gates));
}

static const struct clk_desc dolphin_descs[] = {
	{ "cpufastrefclk",		0x0, CLK_IS_CRITICAL },
	{ "memfastrefclk",		0x4 },
	{ "cfgclk",			0x20, CLK_IS_CRITICAL },
	{ "perifsysclk",		0x24, CLK_IS_CRITICAL },
	{ "atbclk",			0x28 },
	{ "decoderclk",			0x2c },
	{ "encoderclk",			0x34 },
	{ "ovpcoreclk",			0x38 },
	{ "gfx3dcoreclk",		0x40 },
	{ "gfx3dsysclk",		0x44, CLK_IS_CRITICAL },
	{ "tspclk",			0x70 },
	{ "tsprefclk",			0x74 },
	{ "apbcoreclk",			0x80, CLK_IS_CRITICAL, CLK_RATE_NO_CHANGE },
	{ "emmcclk",			0x90 },
	{ "sd0clk",			0x94 },
	{ "gethrgmiiclk",		0xa0 },
	{ "periftest125mclk",		0xc0 },
	{ "usb2testclk",		0xd0 },
	{ "periftest250mclk",		0xd4 },
	{ "usb3coreclk",		0xd8 },
	{ "vxsysclk",			0xf4, CLK_IS_CRITICAL },
	{ "npuclk",			0xf8 },
	{ "sisssysclk",			0xfc, CLK_IS_CRITICAL },
	{ "ifcpclk",			0x100 },
	{ "isssysclk",			0x104, CLK_IS_CRITICAL },
	{ "ispclk",			0x108 },
	{ "ispbeclk",			0x10c },
	{ "ispdscclk",			0x110 },
	{ "ispcsi0clk",			0x114 },
	{ "ispcsi1clk",			0x118 },
	{ "mipirxscanclk",		0x120 },
	{ "usb2test480mg0clk",		0x124 },
	{ "usb2test480mg1clk",		0x128 },
	{ "usb2test480mg2clk",		0x12c },
	{ "usb2test100mg0clk",		0x130 },
	{ "usb2test100mg1clk",		0x134 },
	{ "usb2test100mg2clk",		0x138 },
	{ "usb2test100mg3clk",		0x13c },
	{ "usb2test100mg4clk",		0x140 },
	{ "periftest200mg0clk",		0x144 },
	{ "periftest200mg1clk",		0x148 },
	{ "periftest500mg0clk",		0x14c },
	{ "txescclk",			0x150 },
	{ "aiosysclk",			0x154 },
};

static int dolphin_clk_setup(struct platform_device *pdev)
{
	return berlin_clk_setup(pdev, dolphin_descs, ARRAY_SIZE(dolphin_descs));
}

static const struct of_device_id dolphin_clks_match_table[] = {
	{ .compatible = "syna,dolphin-clk",
	  .data = dolphin_clk_setup },
	{ .compatible = "syna,dolphin-gateclk",
	  .data = dolphin_gateclk_setup },
	{ }
};
MODULE_DEVICE_TABLE(of, dolphin_clks_match_table);

static int dolphin_clks_probe(struct platform_device *pdev)
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

static struct platform_driver dolphin_clks_driver = {
	.probe		= dolphin_clks_probe,
	.driver		= {
		.name	= "syna-dolphin-clks",
		.of_match_table = dolphin_clks_match_table,
	},
};
module_platform_driver(dolphin_clks_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synaptics dolphin clks Driver");
