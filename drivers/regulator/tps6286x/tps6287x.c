// SPDX-License-Identifier: GPL-2.0-only
/* based on tps6286x.c
 */

#include <linux/err.h>
#include <linux/i2c.h>

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>


#define TPS6287X_VSET		0x00
#define TPS6287X_CTRL1		0x01
#define TPS6287X_CTRL1_VRAMP	GENMASK(1, 0)
#define TPS6287X_CTRL1_FPWMEN	BIT(4)
#define TPS6287X_CTRL1_SWEN	BIT(5)
#define TPS6287X_CTRL2		0x02
#define TPS6287X_CTRL2_VRANGE	GENMASK(3, 2)
#define TPS6287X_CTRL3		0x03
#define TPS6287X_STATUS		0x04
#define TPS6287X_DISCHARGE_MASK	BIT(3)
#define TPS6287X_DISCHARGE_ENABLE	BIT(3)
#define TPS6287X_DISCHARGE_DISABLE	0
#define TPS6287X_MAX_REGS	(TPS6287X_STATUS + 1)

#define TPS6287X_MIN_MV		400
#define TPS6287X_MAX_MV		1675
#define TPS6287X_STEP_MV	5

static bool tps6287x_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS6287X_VSET ... TPS6287X_CTRL3:
		return true;
	default:
		return false;
	}
}

static bool tps6287x_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS6287X_VSET ... TPS6287X_STATUS:
		return true;
	default:
		return false;
	}
}

static bool tps6287x_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == TPS6287X_STATUS)
		return true;
	return false;
}

static const struct regmap_config tps6287x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.num_reg_defaults_raw = TPS6287X_MAX_REGS,
	.cache_type = REGCACHE_FLAT,
	.writeable_reg = tps6287x_writeable_reg,
	.readable_reg = tps6287x_readable_reg,
	.volatile_reg = tps6287x_volatile_reg,
};

static int tps6287x_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_FAST:
		val = TPS6287X_CTRL1_FPWMEN;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, TPS6287X_CTRL1,
				  TPS6287X_CTRL1_FPWMEN, val);
}

static unsigned int tps6287x_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, TPS6287X_CTRL1, &val);
	if (ret < 0)
		return 0;

	return (val & TPS6287X_CTRL1_FPWMEN) ? REGULATOR_MODE_FAST :
	    REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops tps6287x_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.set_mode = tps6287x_set_mode,
	.get_mode = tps6287x_get_mode,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
};
static unsigned int tps6287x_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_FAST:
		return mode;
	default:
		return REGULATOR_MODE_INVALID;
	}
}



static struct regulator_desc tps6287x_reg = {
	.name = "tps6287x",
	.owner = THIS_MODULE,
	.ops = &tps6287x_regulator_ops,
	.of_map_mode = tps6287x_of_map_mode,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = ((TPS6287X_MAX_MV - TPS6287X_MIN_MV) / TPS6287X_STEP_MV) + 1,
	.min_uV = TPS6287X_MIN_MV * 1000,
	.uV_step = TPS6287X_STEP_MV * 1000,
	.vsel_reg = TPS6287X_VSET,
	.vsel_mask = 0xFF,
	.enable_reg = TPS6287X_CTRL1,
	.enable_mask = TPS6287X_CTRL1_SWEN,
	.active_discharge_reg = TPS6287X_CTRL1,
	.active_discharge_mask = TPS6287X_DISCHARGE_MASK,
	.active_discharge_on = TPS6287X_DISCHARGE_ENABLE,
	.active_discharge_off = TPS6287X_DISCHARGE_DISABLE,
	.vsel_range_reg = TPS6287X_CTRL2,
	.vsel_range_mask = TPS6287X_CTRL2_VRANGE,
	.ramp_reg = TPS6287X_CTRL1,
	.ramp_mask = TPS6287X_CTRL1_VRAMP,
};

static const struct of_device_id tps6287x_dt_ids[] = {
	{ .compatible = "ti,tps62870", },
	{ .compatible = "ti,tps62871", },
	{ .compatible = "ti,tps62872", },
	{ .compatible = "ti,tps62873", },
	{ }
};
MODULE_DEVICE_TABLE(of, tps6287x_dt_ids);

static int tps6287x_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regulator_config config = {};
	struct regulator_dev *rdev;

	config.regmap = devm_regmap_init_i2c(i2c, &tps6287x_regmap_config);
	if (IS_ERR(config.regmap))
		return PTR_ERR(config.regmap);

	config.dev = dev;
	config.of_node = dev->of_node;
	config.init_data = of_get_regulator_init_data(dev, dev->of_node,
						      &tps6287x_reg);

	rdev = devm_regulator_register(dev, &tps6287x_reg, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "Failed to register regulator\n");
		return PTR_ERR(rdev);
	}

	return 0;
}

static const struct i2c_device_id tps6287x_i2c_id[] = {
	{ "tps62870", 0 },
	{ "tps62871", 0 },
	{ "tps62872", 0 },
	{ "tps62873", 0 },
	{},
};

MODULE_DEVICE_TABLE(i2c, tps6287x_i2c_id);

static struct i2c_driver tps6287x_regulator_driver = {
	.driver = {
		.name = "tps6287x",
		.of_match_table = tps6287x_dt_ids,
	},
	.probe_new = tps6287x_i2c_probe,
	.id_table = tps6287x_i2c_id,
};

module_i2c_driver(tps6287x_regulator_driver);
MODULE_LICENSE("GPL v2");
