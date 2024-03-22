// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Andreas Weissel <andreas.weisel@synaptics.com>
 * based on leds-tlc5917.c
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>

#define TLC5917_MAX_LEDS	8
#define TLC5917_MAX_BRIGHTNESS	1

#define ldev_to_led(c)		container_of(c, struct tlc5917_led, ldev)

struct tlc5917_led {
	bool active;
	unsigned int led_no;
	char name[LED_MAX_NAME_SIZE];
	struct led_classdev ldev;
	struct tlc5917_priv *priv;
};

struct tlc5917_priv {
	struct tlc5917_led leds[TLC5917_MAX_LEDS];
	struct spi_device *spi;
	struct mutex mutex;
	unsigned int cs_gpio;
	unsigned int oe_gpio;
	u8 led_state;
};

static int
tlc5917_brightness_set_blocking(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct tlc5917_led *led = ldev_to_led(led_cdev);
	struct tlc5917_priv *priv = led->priv;
	u8 mask = priv->led_state;
	int ret;

	if (brightness)
		mask |= 1 << led->led_no;
	else
		mask &= ~(1 << led->led_no);

	mutex_lock(&priv->mutex);
	ret = spi_write(priv->spi, &mask, sizeof(mask));
	gpio_set_value_cansleep(priv->cs_gpio, 1);
	udelay(1);
	gpio_set_value_cansleep(priv->cs_gpio, 0);
	mutex_unlock(&priv->mutex);

	return ret;
}

static const struct of_device_id of_tlc5917_leds_match[] = {
	{ .compatible = "ti,tlc5917" },
	{},
};
MODULE_DEVICE_TABLE(of, of_tlc5917_leds_match);

static int
tlc5917_probe(struct spi_device *spi)
{
	struct device_node *np, *child;
	struct device *dev = &spi->dev;
	struct tlc5917_priv *priv;
	const char *name = "leds-tlc5917::";
	int err, count, reg;

	np = dev_of_node(dev);
	if (!np)
		return -ENODEV;

	count = of_get_available_child_count(np);
	if (!count || count > TLC5917_MAX_LEDS)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;

	priv->cs_gpio = of_get_named_gpio(np, "cs-gpio", 0);
	if (!gpio_is_valid(priv->cs_gpio)) {
		dev_err(dev, "cannot request 'cs' gpio\n");
		return -EINVAL;
	}

	err = gpio_request(priv->cs_gpio, "tlc5917 cs gpio");
	if (err < 0) {
		dev_err(dev, "cannot request 'cs' gpio\n");
		return -EINVAL;
	}

	gpio_direction_output(priv->cs_gpio, 0);

	priv->oe_gpio = of_get_named_gpio(np, "oe-gpio", 0);
	if (!gpio_is_valid(priv->oe_gpio)) {
		dev_err(dev, "cannot request 'cs' gpio\n");
		return -EINVAL;
	}

	err = gpio_request(priv->oe_gpio, "tlc5917 oe gpio");
	if (err < 0) {
		dev_err(dev, "cannot request 'oe' gpio\n");
		return -EINVAL;
	}

	gpio_direction_output(priv->oe_gpio, 0);

	for_each_available_child_of_node(np, child) {
		struct tlc5917_led *led;
		struct led_init_data init_data = {};

		init_data.fwnode = of_fwnode_handle(child);

		err = of_property_read_u32(child, "reg", &reg);
		if (err) {
			of_node_put(child);

			return err;
		}

		if (reg < 0 || reg >= TLC5917_MAX_LEDS ||
		    priv->leds[reg].active) {
			of_node_put(child);

			return -EINVAL;
		}

		led = &priv->leds[reg];

		of_property_read_string(child, "label", &name);
		strlcpy(led->name, name, sizeof(led->name));

		led->active = true;
		led->priv = priv;
		led->led_no = reg;
		led->ldev.name = led->name;
		led->ldev.brightness_set_blocking =
					tlc5917_brightness_set_blocking;
		led->ldev.max_brightness = TLC5917_MAX_BRIGHTNESS;
		err = devm_led_classdev_register_ext(dev, &led->ldev,
						     &init_data);
		if (err < 0) {
			of_node_put(child);

			return dev_err_probe(dev, err,
					     "couldn't register LED %s\n",
					     led->ldev.name);
		}
	}

	mutex_init(&priv->mutex);
	spi_set_drvdata(spi, priv);

	return 0;
}

static int tlc5917_remove(struct spi_device *spi)
{
	struct tlc5917_priv *priv = spi_get_drvdata(spi);

	mutex_destroy(&priv->mutex);

	return 0;
}

static struct spi_driver tlc5917_driver = {
	.driver = {
		.name = "tlc5917",
		.of_match_table = of_match_ptr(of_tlc5917_leds_match),
	},
	.probe = tlc5917_probe,
	.remove = tlc5917_remove,
};

module_spi_driver(tlc5917_driver);

MODULE_AUTHOR("Andreas Weissel <andreas.weissel@synaptics.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TLC5917 LED driver");
