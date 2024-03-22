/**
 * linux/drivers/net/dsa/rtl8363nb.c
 *
 *  Copyright (C) 2014 DSP Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <linux/of_irq.h>
#include <linux/sysfs.h>
#include <linux/if_bridge.h>
#include <net/dsa.h>
#include "rtl8363nb.h"
#include "rtk_types.h"
#include "rtk_switch.h"
#include "port.h"
#include "rtl8367c_asicdrv_port.h"
#include "smi.h"
#include "vlan.h"
#include "interrupt.h"

#define MAX_NR_PORTS 7

static struct mii_bus *stmmac_mdio_bus;
static rtk_port_t cpu_port;
static rtk_port_t lan_port;
static rtk_port_t pc_port;
static int learning_mode;
int link_status;

//#define RTL8363NB_USE_MII
#define RTL8363NB_USE_RGMII_2V5


int
rtl8363nb_switch_init(void)
{
	rtk_api_ret_t rtk_ret;
	rtk_port_mac_ability_t rtk_macability;
	/* TODO: initialize realtek switch, and configure it to selected mode  */
	rtk_ret = rtk_switch_init();
	if (rtk_ret != RT_ERR_OK) {
		pr_err("rtk_switch_init() returned %d\n", (int)rtk_ret);
		return -1;
	}

	/* On RTL8365MB there is only one external link, and it is Ext1 */
#if defined(RTL8363NB_USE_MII) || defined(RTL8363NB_USE_RMII)
	/* RMII */
	rtk_macability.forcemode	= MAC_FORCE;
	rtk_macability.speed		= SPD_100M;
	rtk_macability.duplex		= FULL_DUPLEX;
	rtk_macability.link		= 1;
	rtk_macability.nway		= 0;
	rtk_macability.txpause		= 0;
	rtk_macability.rxpause		= 0;

#ifdef RTL8363NB_USE_RMII
	/* Force PHONE_PORT_ID at RMII MAC, 100M/Full */
	rtk_ret = rtk_port_macForceLinkExt_set(EXT_PORT0, MODE_EXT_RMII_MAC, &rtk_macability);
#else
	rtk_ret = rtk_port_macForceLinkExt_set(EXT_PORT0, MODE_EXT_MII_MAC, &rtk_macability);
#endif
	if (rtk_ret != RT_ERR_OK) {
		pr_err("rtk_port_macForceLinkExt_set() returned %d\n", (int)rtk_ret);
		return -1;
	}

#ifdef RTL8363NB_USE_MII
	/* Enable PHY (ignore EN_PHY strip pin) */
	rtk_port_phyEnableAll_set(ENABLED);
#endif

#else
	/* Standard RGMII */
	rtk_macability.forcemode	= MAC_FORCE;
	rtk_macability.speed		= SPD_1000M; /* or 10 or 100 */
	rtk_macability.duplex		= FULL_DUPLEX;
	rtk_macability.link		= PORT_LINKUP;
	rtk_macability.nway		= DISABLED;
	rtk_macability.txpause		= ENABLED;
	rtk_macability.rxpause		= ENABLED;

	rtk_ret = rtk_port_macForceLinkExt_set(EXT_PORT0, MODE_EXT_RGMII,
					       &rtk_macability);
	if (rtk_ret != RT_ERR_OK) {
		pr_err("rtk_port_macForceLinkExt_set() returned %d\n",
		       (int)rtk_ret);
		return -1;
	}

	rtk_port_phyEnableAll_set(ENABLED);
#endif

#ifdef RTL8363NB_USE_RGMII_2V5
	/* Set RGMII Interface 0 TX delay to 2ns and RX to step 0 */
	rtk_port_rgmiiDelayExt_set(EXT_PORT0, 1, 2);
#else
	/* Set RGMII Interface 0 TX delay to 2ns and RX to step 0 */
	rtk_port_rgmiiDelayExt_set(EXT_PORT0, 1, 0);
#endif
	mdelay(1000);

	return 0;
}
EXPORT_SYMBOL(rtl8363nb_switch_init);

static int
rtl8363nb_dsa_setup(struct dsa_switch *ds)
{
	struct rtl8363nb_priv *priv = (struct rtl8363nb_priv *)ds->priv;
	stmmac_mdio_bus = priv->bus;

	return rtl8363nb_switch_init();
}

#if 0
static int
rtl8363nb_smi_read(struct mii_bus *bus, int phy_id, int regnum)
{
	int err = 0;

	err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 31, 0x000e);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 23, regnum);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 21, 0x0001);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_read(bus, 0, 25);

	return err;
}

static int
rtl8363nb_smi_write(struct mii_bus *bus, int phy_id,
				int regnum, u16 val)
{
	int err = 0;

	err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 31, 0x000e);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 23, regnum);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 24, val);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 29, 0xffff);
	if (!err)
		err = stmmac_mdio_write(bus, 0, 21, 0x0003);

	return err;
}
#endif
int
rtl83xx_smi_read(int phy_id, int regnum)
{
	return mdiobus_read(stmmac_mdio_bus, phy_id, regnum);
}
EXPORT_SYMBOL(rtl83xx_smi_read);

int
rtl83xx_smi_write(int phy_id, int regnum, unsigned short val)
{
	return mdiobus_write(stmmac_mdio_bus, phy_id, regnum, val);
}
EXPORT_SYMBOL(rtl83xx_smi_write);

static int
rtl8363nb_dsa_read(struct dsa_switch *ds, int port, int regnum)
{
	int rtk_ret = 0,regValue;

	if (port > MAX_NR_PORTS)
		return 0xffff;
	if(port == 1)
		port = UTP_PORT1;
	if(port == 2)
		port = UTP_PORT3;
	if(port == 6)
		port = EXT_PORT0;

	rtk_ret=rtk_port_phyReg_get(port,regnum,&regValue);
	return regValue;
}

static int
rtl8363nb_dsa_write(struct dsa_switch *ds, int port, int regnum, u16 val)
{
	int rtk_ret = 0;

	if (port > MAX_NR_PORTS)
		return 0xffff;

	if(port == 1)
		port = UTP_PORT1;
	if(port == 2)
		port = UTP_PORT3;
	if(port == 6)
		port = EXT_PORT0;
	
	rtk_ret=rtk_port_phyReg_set(port,regnum,val);
	return rtk_ret;
}

static ssize_t
rtl8363nb_enable_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t size)
{
	u32 enable;
	rtk_api_ret_t rtk_ret;

	if (kstrtoul(buf, 0, (unsigned long *)&enable))
		return -EINVAL;

	if (cpu_port < 0 || lan_port < 0 || pc_port < 0)
		return -EACCES;

	if (!enable) {
		rtk_ret = rtk_vlan_egrFilterEnable_set(DISABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_egrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_portIgrFilterEnable_set(pc_port, DISABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_portIgrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_portIgrFilterEnable_set(lan_port, DISABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_portIgrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_portIgrFilterEnable_set(cpu_port, DISABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_portIgrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_tagMode_set(pc_port, VLAN_TAG_MODE_REAL_KEEP_FORMAT);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_tagMode_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_tagMode_set(lan_port, VLAN_TAG_MODE_REAL_KEEP_FORMAT);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_tagMode_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_tagMode_set(cpu_port, VLAN_TAG_MODE_REAL_KEEP_FORMAT);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_tagMode_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}
	} else {
		rtk_ret = rtk_vlan_egrFilterEnable_set(ENABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_egrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_portIgrFilterEnable_set(pc_port, ENABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_portIgrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_portIgrFilterEnable_set(lan_port, ENABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_portIgrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_portIgrFilterEnable_set(cpu_port, ENABLED);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_portIgrFilterEnable_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_tagMode_set(pc_port, VLAN_TAG_MODE_ORIGINAL);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_tagMode_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_tagMode_set(lan_port, VLAN_TAG_MODE_ORIGINAL);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_tagMode_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}

		rtk_ret = rtk_vlan_tagMode_set(cpu_port, VLAN_TAG_MODE_ORIGINAL);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_tagMode_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}
	}

	return size;
}

static ssize_t
rtl8363nb_enable_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	rtk_api_ret_t rtk_ret = 0;
	rtk_enable_t VlanStatus = 0;

	rtk_ret = rtk_vlan_egrFilterEnable_get(&VlanStatus);
	if (rtk_ret != RT_ERR_OK) {
		dev_err(dev,
			"Error: rtk_vlan_egrFilterEnable_get() returned 0x%08x\n",
			(int)rtk_ret);
		return -EIO;
	}

	return sprintf(buf, "%d\n", VlanStatus == ENABLED ? 1 : 0);
}

static ssize_t
rtl8363nb_port_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t size, rtk_port_t *_port)
{
	u32 port;

	if (kstrtoul(buf, 0, (unsigned long *)&port))
		return -EINVAL;
	if (rtk_switch_logicalPortCheck(port) != RT_ERR_OK)
		return -EINVAL;

	*_port = port;

	return size;
}

static ssize_t
rtl8363nb_cpu_port_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t size)
{
	return rtl8363nb_port_store(dev, attr, buf, size, &cpu_port);
}

static ssize_t
rtl8363nb_lan_port_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t size)
{
	return rtl8363nb_port_store(dev, attr, buf, size, &lan_port);
}

static ssize_t
rtl8363nb_pc_port_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t size)
{
	return rtl8363nb_port_store(dev, attr, buf, size, &pc_port);
}

static ssize_t
rtl8363nb_cpu_port_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", cpu_port);
}

static ssize_t
rtl8363nb_lan_port_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", lan_port);
}

static ssize_t
rtl8363nb_pc_port_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return sprintf(buf, "%d\n", pc_port);
}

static ssize_t
rtl8363nb_learning_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	u32 mode;

	if (kstrtoul(buf, 0, (unsigned long *)&mode))
		return -EINVAL;

	learning_mode = !!mode;

	return size;
}

static ssize_t
rtl8363nb_learning_mode_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", learning_mode);
}

static ssize_t
rtl8363nb_port_prio_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t size, rtk_port_t port)
{
	rtk_api_ret_t rtk_ret = 0;
	rtk_vlan_t Vid = 0;
	rtk_pri_t Priority = 0;
	u32 priority;

	if (kstrtoul(buf, 0, (unsigned long *)&priority))
		return -EINVAL;
	if (priority > 7)
		return -EINVAL;

	rtk_ret = rtk_vlan_portPvid_get(port, &Vid, &Priority);
	if (rtk_ret != RT_ERR_OK) {
		dev_err(dev,
			"error:rtk_vlan_portPvid_get() for lan port returned 0x%08x\n",
			(int)rtk_ret);
		return -EIO;
	}

	rtk_ret = rtk_vlan_portPvid_set(port, Vid, priority);
	if (rtk_ret != RT_ERR_OK) {
		dev_err(dev,
			"error:rtk_vlan_portPvid_set() for lan port returned 0x%08x\n",
			(int)rtk_ret);
		return -EIO;
	}

	return size;
}

static ssize_t
rtl8363nb_cpu_port_prio_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	return rtl8363nb_port_prio_store(dev, attr, buf, size, cpu_port);
}

static ssize_t
rtl8363nb_lan_port_prio_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	return rtl8363nb_port_prio_store(dev, attr, buf, size, lan_port);
}

static ssize_t
rtl8363nb_pc_port_prio_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	return rtl8363nb_port_prio_store(dev, attr, buf, size, pc_port);
}

static ssize_t
rtl8363nb_port_pvid_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t size, rtk_port_t port)
{
	rtk_api_ret_t rtk_ret = 0;
	rtk_vlan_t TempVid = 0;
	rtk_pri_t Priority = 0;
	u32 vlanId;

	if (kstrtoul(buf, 0, (unsigned long *)&vlanId))
		return -EINVAL;
	if (vlanId > 4095)
		return -EINVAL;

	rtk_ret = rtk_vlan_portPvid_get(port, &TempVid, &Priority);
	if (rtk_ret != RT_ERR_OK) {
		dev_err(dev,
			"error:rtk_vlan_portPvid_get() for lan port returned 0x%08x\n",
			(int)rtk_ret);
		return -EIO;
	}

	rtk_ret = rtk_vlan_portPvid_set(port, (u16)vlanId, Priority);
	if (rtk_ret != RT_ERR_OK) {
		dev_err(dev,
			"error:rtk_vlan_portPvid_set() for lan port returned 0x%08x\n",
			(int)rtk_ret);
		return -EIO;
	}

	return size;
}

static ssize_t
rtl8363nb_cpu_port_pvid_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	return rtl8363nb_port_pvid_store(dev, attr, buf, size, cpu_port);
}

static ssize_t
rtl8363nb_lan_port_pvid_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	return rtl8363nb_port_pvid_store(dev, attr, buf, size, lan_port);
}

static ssize_t
rtl8363nb_pc_port_pvid_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t size)
{
	return rtl8363nb_port_pvid_store(dev, attr, buf, size, pc_port);
}

static ssize_t
rtl8363nb_port_prio_show(struct device *dev, struct device_attribute *attr,
			 char *buf, rtk_port_t port)
{
	rtk_api_ret_t rtk_ret;
	rtk_pri_t pPriority;
	rtk_vlan_t vlanIdx;

	if (port < 0)
		return -EACCES;

	rtk_ret = rtk_vlan_portPvid_get(port, &vlanIdx, &pPriority);

	if (rtk_ret == RT_ERR_OK)
		return sprintf(buf, "%u\n", pPriority);

	return -EIO;
}

static ssize_t
rtl8363nb_cpu_port_prio_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return rtl8363nb_port_prio_show(dev, attr, buf, cpu_port);
}

static ssize_t
rtl8363nb_lan_port_prio_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return rtl8363nb_port_prio_show(dev, attr, buf, lan_port);
}

static ssize_t
rtl8363nb_pc_port_prio_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return rtl8363nb_port_prio_show(dev, attr, buf, pc_port);
}

static ssize_t
rtl8363nb_port_pvid_show(struct device *dev, struct device_attribute *attr,
			 char *buf, rtk_port_t port)
{
	rtk_api_ret_t rtk_ret;
	rtk_pri_t pPriority;
	rtk_vlan_t vlanIdx;

	if (port < 0)
		return -EACCES;

	rtk_ret = rtk_vlan_portPvid_get(port, &vlanIdx, &pPriority);

	if (rtk_ret == RT_ERR_OK)
		return sprintf(buf, "%u\n", vlanIdx);

	return -EIO;
}

static ssize_t
rtl8363nb_cpu_port_pvid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return rtl8363nb_port_pvid_show(dev, attr, buf, cpu_port);
}

static ssize_t
rtl8363nb_lan_port_pvid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return rtl8363nb_port_pvid_show(dev, attr, buf, lan_port);
}

static ssize_t
rtl8363nb_pc_port_pvid_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return rtl8363nb_port_pvid_show(dev, attr, buf, pc_port);
}

static ssize_t
rtl8363nb_create_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t size)
{
	u32 vlanId;
	rtk_api_ret_t rtk_ret;
	rtk_vlan_cfg_t vlan_cfg;

	if (kstrtoul(buf, 0, (unsigned long *)&vlanId))
		return -EINVAL;
	if (vlanId > 4095)
		return -EINVAL;

	memset(&vlan_cfg, 0x00, sizeof(rtk_vlan_cfg_t));
	if (learning_mode)
		vlan_cfg.ivl_en = 1;

	rtk_ret = rtk_vlan_set((u16)vlanId, &vlan_cfg);

	return size;
}

static ssize_t
rtl8363nb_delete_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t size)
{
	u32 vlanId;
	rtk_api_ret_t rtk_ret = 0;
	rtk_vlan_cfg_t vlan_cfg;

	if (kstrtoul(buf, 0, (unsigned long *)&vlanId))
		return -EINVAL;
	if (vlanId > 4095 && vlanId != 0xffff)
		return -EINVAL;

	if (vlanId == 0xffff) {
		rtk_ret = rtk_vlan_reset();
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_reset() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}
	} else {
		rtk_ret = rtk_vlan_get(vlanId, &vlan_cfg);
		if (rtk_ret != RT_ERR_OK)
			return rtk_ret;

		RTK_PORTMASK_CLEAR(vlan_cfg.mbr);
		RTK_PORTMASK_CLEAR(vlan_cfg.untag);

		rtk_ret = rtk_vlan_set((u16)vlanId, &vlan_cfg);
		if (rtk_ret != RT_ERR_OK) {
			dev_err(dev,
				"Error: rtk_vlan_set() returned 0x%08x\n",
				(int)rtk_ret);
			return -EIO;
		}
	}

	return size;
}

static DEVICE_ATTR(vlan_enable, S_IRUGO | S_IWUSR,
		rtl8363nb_enable_show, rtl8363nb_enable_store);
static DEVICE_ATTR(cpu_port, S_IRUGO | S_IWUSR,
		rtl8363nb_cpu_port_show, rtl8363nb_cpu_port_store);
static DEVICE_ATTR(lan_port, S_IRUGO | S_IWUSR,
		rtl8363nb_lan_port_show, rtl8363nb_lan_port_store);
static DEVICE_ATTR(pc_port, S_IRUGO | S_IWUSR,
		rtl8363nb_pc_port_show, rtl8363nb_pc_port_store);
static DEVICE_ATTR(learning_mode, S_IRUGO | S_IWUSR,
		rtl8363nb_learning_mode_show,
		rtl8363nb_learning_mode_store);
static DEVICE_ATTR(cpu_port_pvid, S_IRUGO | S_IWUSR,
		rtl8363nb_cpu_port_pvid_show,
		rtl8363nb_cpu_port_pvid_store);
static DEVICE_ATTR(lan_port_pvid, S_IRUGO | S_IWUSR,
		rtl8363nb_lan_port_pvid_show,
		rtl8363nb_lan_port_pvid_store);
static DEVICE_ATTR(pc_port_pvid, S_IRUGO | S_IWUSR,
		rtl8363nb_pc_port_pvid_show, rtl8363nb_pc_port_pvid_store);
static DEVICE_ATTR(cpu_port_prio, S_IRUGO | S_IWUSR,
		rtl8363nb_cpu_port_prio_show,
		rtl8363nb_cpu_port_prio_store);
static DEVICE_ATTR(lan_port_prio, S_IRUGO | S_IWUSR,
		rtl8363nb_lan_port_prio_show,
		rtl8363nb_lan_port_prio_store);
static DEVICE_ATTR(pc_port_prio, S_IRUGO | S_IWUSR,
		rtl8363nb_pc_port_prio_show, rtl8363nb_pc_port_prio_store);
static DEVICE_ATTR(create, S_IWUSR, NULL, rtl8363nb_create_store);
static DEVICE_ATTR(delete, S_IWUSR, NULL, rtl8363nb_delete_store);

static struct attribute *rtl8363nb_attributes[] = {
	&dev_attr_vlan_enable.attr,
	&dev_attr_cpu_port.attr,
	&dev_attr_lan_port.attr,
	&dev_attr_pc_port.attr,
	&dev_attr_learning_mode.attr,
	&dev_attr_cpu_port_pvid.attr,
	&dev_attr_lan_port_pvid.attr,
	&dev_attr_pc_port_pvid.attr,
	&dev_attr_cpu_port_prio.attr,
	&dev_attr_lan_port_prio.attr,
	&dev_attr_pc_port_prio.attr,
	&dev_attr_create.attr,
	&dev_attr_delete.attr,
	NULL,
};

static struct attribute_group __attribute__((unused))
rtl8363nb_group = {
	.attrs = rtl8363nb_attributes,
	.name = "rtl8363nb",
};

static enum dsa_tag_protocol
rtl8363nb_get_tag_protocol(struct dsa_switch *ds, int port, enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_NONE;
}
#if 0
static int
rtl8363nb_link_update(struct dsa_switch *ds, int port)
{
	rtk_port_linkStatus_t link;
	rtk_data_t speed;
	rtk_data_t duplex;
	rtk_int32 retval;

	retval = rtk_port_phyStatus_get(port, &link, &speed, &duplex);
	if (retval != RT_ERR_OK)
		return retval;

	if (link) {
		link_status = 1;
		netif_carrier_on(ds->dst->pd->of_netdev);
	} else {
		link_status = 0;
		netif_carrier_off(ds->dst->pd->of_netdev);
	}
	return 0;
}
#endif

static void
rtl8363nb_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	int stp_state, rtl_port;

	if (port > MAX_NR_PORTS)
		return;
	if (port == 1)
		rtl_port = UTP_PORT1;
	else if (port == 2)
		rtl_port = UTP_PORT3;
	else if (port == 6)
		rtl_port = EXT_PORT0;

	if (state == BR_STATE_DISABLED)
		stp_state = STP_STATE_DISABLED;
	else if (state == BR_STATE_BLOCKING)
		stp_state = STP_STATE_BLOCKING;
	else if (state == BR_STATE_LEARNING)
		stp_state = STP_STATE_LEARNING;
	else if (state == BR_STATE_FORWARDING)
		stp_state = STP_STATE_FORWARDING;
	else
		return;

	(void)rtk_stp_mstpState_set(0 /* msti */, rtl_port, stp_state);
};

static struct dsa_switch_ops rtl8363nb_switch_ops = {
	.get_tag_protocol	= rtl8363nb_get_tag_protocol,
	.setup		= rtl8363nb_dsa_setup,
	.port_stp_state_set = rtl8363nb_port_stp_state_set,
	.phy_read	= rtl8363nb_dsa_read,
	.phy_write	= rtl8363nb_dsa_write,
};

static irqreturn_t rtl8363nb_switch_isr(int irq, void *dev_id)
{
	//struct rtl8363nb_priv *priv = dev_id;
	rtk_int_status_t statusMask;
	rtk_enable_t state;

	rtk_int_control_get(INT_TYPE_LINK_STATUS, &state);

	rtk_int_status_get(&statusMask);

	if (statusMask.value[0] & 0x1) {
		/*Link  status interrupt is triggered*/
		statusMask.value[0] = 0x1;
		rtk_int_status_set(&statusMask);
		//rtl8363nb_link_update(priv->ds, SWITCH_PORT_ID);
	}

	return IRQ_HANDLED;
}


static int rtl8363nb_probe(struct mdio_device *mdiodev)
{
	struct device_node *np = mdiodev->dev.of_node;
	unsigned reset_gpio;
	unsigned enable_phy_gpio;
	int ret = 0;
	rtk_enable_t state = 0;
	struct rtl8363nb_priv *priv;

	/* allocate the private data struct so that we can probe the switches
	 * ID register
	 */
	priv = devm_kzalloc(&mdiodev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = mdiodev->bus;
	priv->dev = &mdiodev->dev;
	stmmac_mdio_bus = priv->bus;

	priv->ds = devm_kzalloc(&mdiodev->dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->priv = priv;
	priv->ds->dev = &mdiodev->dev;
	priv->ds->num_ports = 7;
	priv->ds->ops = &rtl8363nb_switch_ops;
	mutex_init(&priv->reg_mutex);
	dev_set_drvdata(&mdiodev->dev, priv);

	reset_gpio = of_get_named_gpio(np, "reset_gpio", 0);
	if (gpio_is_valid(reset_gpio)) {
		ret = gpio_request(reset_gpio, "rtl8363nb reset gpio");
		if (ret < 0) {
			dev_err(&mdiodev->dev,
				"cannot request 'reset' gpio\n");
			goto out;
		}
		gpio_direction_output(reset_gpio, 0);
		msleep(10);
		gpio_set_value_cansleep(reset_gpio, 1);
		mdelay(100);

		gpio_free(reset_gpio);
	}

	enable_phy_gpio = of_get_named_gpio(np, "enable_phy_gpio", 0);
	if (gpio_is_valid(enable_phy_gpio)) {
		ret = gpio_request(enable_phy_gpio,
				   "rtl8363nb enable phy gpio");
		if (ret < 0) {
			dev_err(&mdiodev->dev,
				"cannot request 'enable phy' gpio\n");
			goto out;
		}
		gpio_direction_output(enable_phy_gpio, 0);
		msleep(10);
		gpio_set_value_cansleep(enable_phy_gpio, 1);
		mdelay(100);

		gpio_free(enable_phy_gpio);
	}

	priv->irq = of_irq_get(np, 0);
	if (priv->irq < 0) {
		dev_warn(&mdiodev->dev, "cannot get irq\n");
	}
#ifndef CONFIG_NET_DSA_RTL8363NB_CISCO_MFGTEST
	else {
		ret = devm_request_threaded_irq(&mdiodev->dev, priv->irq, NULL,
				rtl8363nb_switch_isr, IRQF_TRIGGER_FALLING |
				IRQF_EARLY_RESUME | IRQF_ONESHOT, "rtl8363nb",
				priv);
		if (ret < 0)
			pr_warn("failed to request irq\n");
	}
#endif
	rtk_int_control_get(INT_TYPE_LINK_STATUS, &state);

	return dsa_register_switch(priv->ds);

out:
	return ret;
}

static void rtl8363nb_remove(struct mdio_device *mdiodev)
{
	return;
}

static const struct of_device_id rtl8363nb_of_match[] = {
	{ .compatible = "dspg,rtl8363nb", },
	{ /* sentinel */ }
};

static struct mdio_driver rtl8363nb_driver = {
	.probe  = rtl8363nb_probe,
	.remove = rtl8363nb_remove,
	.mdiodrv.driver = {
		.name = "RTL8363NB",
		.of_match_table = rtl8363nb_of_match,
	},
};

mdio_module_driver(rtl8363nb_driver);

MODULE_DESCRIPTION("Realtek 8363NB DSA driver");
MODULE_AUTHOR("DSPG");
MODULE_LICENSE("GPL");

