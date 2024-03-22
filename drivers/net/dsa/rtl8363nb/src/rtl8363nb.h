/*
 * drivers/net/phy/rtl8363nb.h - Realtek 8363NB switch support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RTL8363NB_H
#define __RTL8363NB_H
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/ethtool.h>

#define RTL8363NB_NUM_PORTS					5

struct rtl8363nb_port_status {
	struct ethtool_eee eee;
	struct net_device *bridge_dev;
	int enabled;
};

struct rtl8363nb_priv {
	struct regmap *regmap;
	struct mii_bus *bus;
	struct rtl8363nb_port_status port_sts[RTL8363NB_NUM_PORTS];
	struct dsa_switch *ds;
	struct mutex reg_mutex;
	struct device *dev;
	int irq, gpio;
};

int rtl83xx_smi_read(int phy_id, int regnum);
int rtl83xx_smi_write(int phy_id, int regnum, unsigned short val);

#endif
