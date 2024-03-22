#!/bin/bash

# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 Synaptics Incorporated

# directory where this script is located
mod_dir=$(readlink -f "$(dirname "${BASH_SOURCE}")")

# kernel module name
mod_name=pinctrl-myna2

# include the common file to build a kernel kernel_module
source build/kernel_module.rc
