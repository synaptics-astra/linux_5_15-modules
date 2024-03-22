/*
 *  arch/arm/mach-dmw/include/mach/keypad.h
 *
 *  Copyright (C) 2011 DSPG Technologies GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MACH_KEYPAD_H
#define __MACH_KEYPAD_H

#include <linux/input/matrix_keypad.h>

struct dspg_keypad_config {
	int rows;
	int cols;
	unsigned int row_shift;
	unsigned int discharge_us;
	unsigned int debounce_ms;
	const struct matrix_keymap_data *keymap_data;
};

#define MAX_ROW		10
#define MAX_COL		8
#define MAX_KEY		(MAX_ROW * MAX_COL)

struct dspg_keypad {
	struct input_dev *input;
	struct resource *mem;

	struct timer_list timer;
	int irq;
	int irq_disabled;
	void __iomem *regs;

	unsigned short keycodes[MAX_KEY];
	char keystate[MAX_KEY];
	unsigned int last_scancode;
	struct dspg_keypad_config *config;

	spinlock_t handler_lock;
	struct list_head handler_list;

	unsigned int reg_col;
	unsigned int reg_row;
};

struct dspg_keypad_handler {
	struct list_head next_handler;
	int (*handler)(struct dspg_keypad_handler *handler, int scancode,
		       int pressed);
};

#endif
