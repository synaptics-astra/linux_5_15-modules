// SPDX-License-Identifier: GPL-2.0
//
// RT5739 regulator driver, take hl7593.c as reference
//
// Copyright (C) 2020 Synaptics Incorporated
//
// Author: Terry Zhou <terry.zhou@synaptics.com>

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define RT5739_SEL0		0
#define RT5739_SEL1		1
#define RT5739_CTRL		2
#define   RT5739_MODE0		(1 << 0)
#define   RT5739_MODE1		(1 << 1)
#define RT5739_ID1		3
#define RT5739_ID2		4
#define RT5739_MONITOR		5
#define RT5739_CTRL2		6
#define   RT5739_BUCK_EN0	(1 << 0)
#define   RT5739_BUCK_EN1	(1 << 1)
#define RT5739_MAX		(RT5739_CTRL2 + 1)

#define RT5739_DISCHARGE_MASK	(1<<7)
#define RT5739_DISCHARGE_ENABLE	(1<<7)
#define RT5739_DISCHARGE_DISABLE	0

#define RT5739_NVOLTAGES	200
#define RT5739_VSELMIN		300000
#define RT5739_VSELSTEP		5000
#define RT5739_VSEL_MASK	0xff

struct rt5739_device_info {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_init_data *regulator;
	unsigned int vsel_reg;
	unsigned int vsel_step;
};

static int rt5739_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct rt5739_device_info *di = rdev_get_drvdata(rdev);
	u32 val =  di->vsel_reg == RT5739_SEL0 ? RT5739_MODE0 : RT5739_MODE1;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		regmap_update_bits(rdev->regmap, RT5739_CTRL, val, val);
		break;
	case REGULATOR_MODE_NORMAL:
		regmap_update_bits(rdev->regmap, RT5739_CTRL, val, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int rt5739_get_mode(struct regulator_dev *rdev)
{
	struct rt5739_device_info *di = rdev_get_drvdata(rdev);
	u32 mode =  di->vsel_reg == RT5739_SEL0 ? RT5739_MODE0 : RT5739_MODE1;
	u32 val;
	int ret = 0;

	ret = regmap_read(rdev->regmap, RT5739_CTRL, &val);
	if (ret < 0)
		return ret;
	if (val & mode)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops rt5739_regulator_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = rt5739_set_mode,
	.get_mode = rt5739_get_mode,
	.set_active_discharge = regulator_set_active_discharge_regmap,
};

static int rt5739_regulator_register(struct rt5739_device_info *di,
			struct regulator_config *config)
{
	struct regulator_desc *rdesc = &di->desc;
	struct regulator_dev *rdev;

	rdesc->name = "rt5739-reg";
	rdesc->supply_name = "vin";
	rdesc->ops = &rt5739_regulator_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->n_voltages = RT5739_NVOLTAGES;
	rdesc->enable_reg = RT5739_CTRL2;;
	rdesc->enable_mask = di->vsel_reg == RT5739_SEL0 ? RT5739_BUCK_EN0 : RT5739_BUCK_EN1;
	rdesc->min_uV = RT5739_VSELMIN;
	rdesc->uV_step = RT5739_VSELSTEP;
	rdesc->vsel_reg = di->vsel_reg;
	rdesc->vsel_mask = RT5739_VSEL_MASK;
	rdesc->vsel_step = di->vsel_step;
	rdesc->active_discharge_reg = RT5739_CTRL;
	rdesc->active_discharge_mask = RT5739_DISCHARGE_MASK;
	rdesc->active_discharge_on = RT5739_DISCHARGE_ENABLE;
	rdesc->active_discharge_off = RT5739_DISCHARGE_DISABLE;
	rdesc->owner = THIS_MODULE;

	rdev = devm_regulator_register(di->dev, &di->desc, config);
	return PTR_ERR_OR_ZERO(rdev);
}

static bool rt5739_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5739_MONITOR:
		return true;
	}
	return false;
}

static const struct regmap_config rt5739_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = rt5739_volatile_reg,
	.num_reg_defaults_raw = RT5739_MAX,
	.cache_type = REGCACHE_FLAT,
};

static int rt5739_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct rt5739_device_info *di;
	struct regulator_config config = { };
	struct regmap *regmap;
	int ret;

	di = devm_kzalloc(dev, sizeof(struct rt5739_device_info), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->regulator = of_get_regulator_init_data(dev, np, &di->desc);
	if (!di->regulator) {
		dev_err(dev, "Platform data not found!\n");
		return -EINVAL;
	}

	if (of_property_read_bool(np, "richtek,vsel-state-high"))
		di->vsel_reg = RT5739_SEL1;
	else
		di->vsel_reg = RT5739_SEL0;

	di->dev = dev;

	regmap = devm_regmap_init_i2c(client, &rt5739_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to allocate regmap!\n");
		return PTR_ERR(regmap);
	}
	i2c_set_clientdata(client, di);

	config.dev = di->dev;
	config.init_data = di->regulator;
	config.regmap = regmap;
	config.driver_data = di;
	config.of_node = np;

	of_property_read_u32(np, "richtek,vsel-step", &di->vsel_step);

	ret = rt5739_regulator_register(di, &config);
	if (ret < 0)
		dev_err(dev, "Failed to register regulator!\n");
	return ret;
}

static const struct of_device_id rt5739_dt_ids[] = {
	{
		.compatible = "richtek,rt5739",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, rt5739_dt_ids);

static const struct i2c_device_id rt5739_id[] = {
	{ "rt5739", },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rt5739_id);

static struct i2c_driver rt5739_regulator_driver = {
	.driver = {
		.name = "rt5739-regulator",
		.of_match_table = of_match_ptr(rt5739_dt_ids),
	},
	.probe_new = rt5739_i2c_probe,
	.id_table = rt5739_id,
};
module_i2c_driver(rt5739_regulator_driver);

MODULE_AUTHOR("Terry Zhou <terry.zhou@synaptics.com>");
MODULE_DESCRIPTION("RT5739 regulator driver");
MODULE_LICENSE("GPL v2");
