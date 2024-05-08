// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Synaptics Incorporated
 * Copyright (c) 2015 Marvell Technology Group Ltd.
 *
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/platform_device.h>

#include "clk.h"

struct berlin_gate_clk_priv {
	struct clk_hw_onecell_data data;
};

static DEFINE_SPINLOCK(berlin_gateclk_lock);

static void devm_clk_hw_release_gate(struct device *dev, void *res)
{
	clk_hw_unregister_gate(*(struct clk_hw **)res);
}

static struct clk_hw *__devm_clk_hw_register_gate(struct device *dev,
                struct device_node *np, const char *name,
                const char *parent_name, const struct clk_hw *parent_hw,
                const struct clk_parent_data *parent_data,
                unsigned long flags,
                void __iomem *reg, u8 bit_idx,
                u8 clk_gate_flags, spinlock_t *lock)
{
	struct clk_hw **ptr, *hw;

	ptr = devres_alloc(devm_clk_hw_release_gate, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	hw = __clk_hw_register_gate(dev, np, name, parent_name, parent_hw,
				    parent_data, flags, reg, bit_idx,
				    clk_gate_flags, lock);

	if (!IS_ERR(hw)) {
		*ptr = hw;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return hw;
}

#define devm_clk_hw_register_gate(dev, name, parent_name, flags, reg, bit_idx,	\
				  clk_gate_flags, lock)				\
	__devm_clk_hw_register_gate((dev), NULL, (name), (parent_name), NULL,	\
				NULL, (flags), (reg), (bit_idx),		\
				(clk_gate_flags), (lock))

int berlin_gateclk_setup(struct platform_device *pdev,
			const struct gateclk_desc *descs,
			int n)
{
	int i;
	void __iomem *base;
	struct resource *res;
	struct berlin_gate_clk_priv *priv;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, data.hws, n), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (WARN_ON(!base))
		return -ENOMEM;

	priv->data.num = n;

	for (i = 0; i < n; i++) {
		struct clk_hw *clk;

		clk = devm_clk_hw_register_gate(&pdev->dev, descs[i].name,
				descs[i].parent_name,
				descs[i].flags, base,
				descs[i].bit_idx, 0,
				&berlin_gateclk_lock);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		priv->data.hws[i] = clk;
	}

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_onecell_get, &priv->data);
}
EXPORT_SYMBOL_GPL(berlin_gateclk_setup);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("base gate clk driver for Synaptics SoCs");
