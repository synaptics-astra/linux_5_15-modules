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
#include <linux/platform_device.h>
#include "clk.h"

#define CLKEN		(1 << 0)
#define CLKPLLSEL_MASK	7
#define CLKPLLSEL_SHIFT	1
#define CLKPLLSWITCH	(1 << 4)
#define CLKSWITCH	(1 << 5)
#define CLKD3SWITCH	(1 << 6)
#define CLKSEL_MASK	7
#define CLKSEL_SHIFT	7

#define CLK_SOURCE_MAX	6
#define BERLIN_DIV_MIN	1
#define BERLIN_DIV_MAX	12

struct berlin_clk {
	struct clk_hw hw;
	void __iomem *base;
};

struct berlin_clk_priv {
	unsigned int num;
	struct berlin_clk bclk[];
};

#define to_berlin_clk(hw)	container_of(hw, struct berlin_clk, hw)

static u8 clk_div[] = {1, 2, 4, 6, 8, 12, 1, 1};

static unsigned long berlin_clk_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	u32 val, divider;
	struct berlin_clk *clk = to_berlin_clk(hw);

	val = readl_relaxed(clk->base);
	if (val & CLKD3SWITCH)
		divider = 3;
	else {
		if (val & CLKSWITCH) {
			val >>= CLKSEL_SHIFT;
			val &= CLKSEL_MASK;
			divider = clk_div[val];
		} else
			divider = 1;
	}

	return parent_rate / divider;
}

static u8 berlin_clk_get_parent(struct clk_hw *hw)
{
	u32 val;
	struct berlin_clk *clk = to_berlin_clk(hw);

	val = readl_relaxed(clk->base);
	if (val & CLKPLLSWITCH) {
		val >>= CLKPLLSEL_SHIFT;
		val &= CLKPLLSEL_MASK;
		return val + 1;
	}

	return 0;
}

static int berlin_clk_set_parent(struct clk_hw *hw, u8 index)
{
	u32 val;
	struct berlin_clk *clk = to_berlin_clk(hw);

	if (index >= CLK_SOURCE_MAX)
		return -EPERM;

	val = readl_relaxed(clk->base);
	if (index == 0)
		val &= ~CLKPLLSWITCH;
	else {
		val |= CLKPLLSWITCH;
		val &= ~(CLKPLLSEL_MASK << CLKPLLSEL_SHIFT);
		val |= (index - 1) << CLKPLLSEL_SHIFT;
	}
	writel_relaxed(val, clk->base);

	return 0;
}

static long berlin_clk_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	long div;
	unsigned long parent = *parent_rate;

	div = (parent + rate/2) / rate;
	if (div < BERLIN_DIV_MIN) {
		div = BERLIN_DIV_MIN;
	} else if (div > BERLIN_DIV_MAX)
		div = BERLIN_DIV_MAX;

	switch(div) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 6:
	case 8:
	case 12:
		break;
	case 5:
		div = 6;
		break;
	case 7:
	case 9:
		div = 8;
		break;
	case 10:
	case 11:
		div = 12;
		break;
	default:
		return berlin_clk_recalc_rate(hw, parent);
	}

	return parent / div;
}

static int berlin_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	long div;
	u32  val, i;
	struct berlin_clk *clk = to_berlin_clk(hw);

	div = (parent_rate + rate/2) / rate;
	if (div < BERLIN_DIV_MIN) {
		div = BERLIN_DIV_MIN;
	} else if (div > BERLIN_DIV_MAX)
		div = BERLIN_DIV_MAX;

	val = readl_relaxed(clk->base);
	val &= ~CLKD3SWITCH;

	switch(div) {
	case 1:
		val &= ~CLKSWITCH;
		break;
	case 3:
		val |= CLKD3SWITCH;
		break;
	default:
		for (i = 1; i < ARRAY_SIZE(clk_div); i++) {
			if (div == clk_div[i]) {
				val &= ~(CLKSEL_MASK << CLKSEL_SHIFT);
				val |= (i << CLKSEL_MASK) | CLKSWITCH;
				break;
			}
		}
		if (i >= ARRAY_SIZE(clk_div))
			return -EINVAL;
	}
	writel_relaxed(val, clk->base);
	return 0;
}

static int berlin_clk_enable(struct clk_hw *hw)
{
	u32 val;
	struct berlin_clk *clk = to_berlin_clk(hw);

	val = readl_relaxed(clk->base);
	if (!(val & CLKEN)) {
		val |= CLKEN;
		writel_relaxed(val, clk->base);
	}

	return 0;
}

static void berlin_clk_disable(struct clk_hw *hw)
{
	u32 val;
	struct berlin_clk *clk = to_berlin_clk(hw);

	val = readl_relaxed(clk->base);
	if (val & CLKEN) {
		val &= ~CLKEN;
		writel_relaxed(val, clk->base);
	}
}

static int berlin_clk_is_enabled(struct clk_hw *hw)
{
	u32 val;
	struct berlin_clk *clk = to_berlin_clk(hw);

	val = readl_relaxed(clk->base);
	val &= CLKEN;

	return val ? 1 : 0;
}

static const struct clk_ops berlin_clk_ops = {
	.recalc_rate	= berlin_clk_recalc_rate,
	.get_parent	= berlin_clk_get_parent,
	.set_parent	= berlin_clk_set_parent,
	.round_rate	= berlin_clk_round_rate,
	.set_rate	= berlin_clk_set_rate,
	.enable		= berlin_clk_enable,
	.disable	= berlin_clk_disable,
	.is_enabled	= berlin_clk_is_enabled,
};

static const struct clk_ops berlin_fixed_clk_ops = {
	.recalc_rate	= berlin_clk_recalc_rate,
	.get_parent	= berlin_clk_get_parent,
	.enable		= berlin_clk_enable,
	.disable	= berlin_clk_disable,
	.is_enabled	= berlin_clk_is_enabled,
};

static int
berlin_clk_register(struct device *dev, struct berlin_clk *bclk,
		    const char *name, int num_parents,
		    const char **parent_names, unsigned long flags,
		    unsigned long priv_flags, void __iomem *base)
{
	struct clk_init_data init;

	init.name = name;
	if (priv_flags & CLK_RATE_NO_CHANGE)
		init.ops = &berlin_fixed_clk_ops;
	else
		init.ops = &berlin_clk_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = flags;

	bclk->base = base;
	bclk->hw.init = &init;

	return devm_clk_hw_register(dev, &bclk->hw);
}

static struct clk_hw *
berlin_of_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct berlin_clk_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= priv->num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &priv->bclk[idx].hw;
}

int berlin_clk_setup(struct platform_device *pdev,
		     const struct clk_desc *descs,
		     int n)
{
	int i, ret, num_parents;
	void __iomem *base;
	struct device_node *np = pdev->dev.of_node;
	struct berlin_clk_priv *priv;
	struct resource *res;
	const char *parent_names[CLK_SOURCE_MAX];

	num_parents = of_clk_get_parent_count(np);
	if (num_parents <= 0 || num_parents > CLK_SOURCE_MAX)
		return -EINVAL;

	of_clk_parent_fill(np, parent_names, num_parents);

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, bclk, n), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (WARN_ON(!base))
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		ret = berlin_clk_register(&pdev->dev, &priv->bclk[i], descs[i].name,
				num_parents, parent_names,
				descs[i].flags,
				descs[i].priv_flags,
				base + descs[i].offset);
		if (ret)
			return ret;
	}
	priv->num = n;

	return devm_of_clk_add_hw_provider(&pdev->dev, berlin_of_clk_get, priv);
}
EXPORT_SYMBOL_GPL(berlin_clk_setup);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("base clk driver for Synaptics SoCs");
