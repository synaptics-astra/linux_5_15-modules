/*
 * NAND Flash Controller Device Driver
 * Copyright © 2015-2016, Cadence and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "nand_randomizer.h"

#define __FPGA__			1
#define RANDOM_DATA_LENGTH	4096
/***************************************************/
/*  Register definition */
/***************************************************/

/* Command register 0.
 * Writing data to this register will initiate a new transaction
 * of the NF controller. */
#define HPNFC_CMD_REG0				0x0000
/* command type field shift */
#define  HPNFC_CMD_REG0_CT_SHIFT		30
/* command type field mask */
#define  HPNFC_CMD_REG0_CT_MASK			(3uL << 30)
/* command type CDMA */
#define  HPNFC_CMD_REG0_CT_CDMA			(0uL)
/* command type PIO */
#define  HPNFC_CMD_REG0_CT_PIO			(1uL)
/* command type reset */
#define  HPNFC_CMD_REG0_CT_RST			(2uL)
/* command type generic */
#define  HPNFC_CMD_REG0_CT_GEN			(3uL)
/* command thread number field shift */
#define  HPNFC_CMD_REG0_TN_SHIFT		24
/* command thread number field mask */
#define  HPNFC_CMD_REG0_TN_MASK			(3uL << 24)
/* command code field shift */
#define  HPNFC_CMD_REG0_PIO_CC_SHIFT		0
/* command code field mask */
#define  HPNFC_CMD_REG0_PIO_CC_MASK		(0xFFFFuL << 0)
/* command code - read page */
#define  HPNFC_CMD_REG0_PIO_CC_RD		(0x2200uL)
/* command code - write page */
#define  HPNFC_CMD_REG0_PIO_CC_WR		(0x2100uL)
/* command code - copy back */
#define  HPNFC_CMD_REG0_PIO_CC_CPB		(0x1200uL)
/* command code - reset */
#define  HPNFC_CMD_REG0_PIO_CC_RST		(0x1100uL)
/* command code - set feature */
#define  HPNFC_CMD_REG0_PIO_CC_SF		(0x0100uL)
/* command interrupt shift */
#define  HPNFC_CMD_REG0_INT_SHIFT		20
/* command interrupt mask */
#define  HPNFC_CMD_REG0_INT_MASK		(1uL << 20)

/* PIO command - volume ID - shift */
#define  HPNFC_CMD_REG0_VOL_ID_SHIFT		16
/* PIO command - volume ID - mask */
#define  HPNFC_CMD_REG0_VOL_ID_MASK		(0xFuL << 16)

/* Command register 1. */
#define HPNFC_CMD_REG1				0x0004
/* PIO command - bank number - shift */
#define  HPNFC_CMD_REG1_BANK_SHIFT		24
/* PIO command - bank number - mask */
#define  HPNFC_CMD_REG1_BANK_MASK		(0x3uL << 24)
/* PIO command - set feature - feature address - shift */
#define  HPNFC_CMD_REG1_FADDR_SHIFT		0
/* PIO command - set feature - feature address - mask */
#define  HPNFC_CMD_REG1_FADDR_MASK		(0xFFuL << 0)

/* Command register 2 */
#define HPNFC_CMD_REG2				0x0008
/* Command register 3 */
#define HPNFC_CMD_REG3				0x000C
/* Pointer register to select which thread status will be selected. */
#define HPNFC_CMD_STATUS_PTR			0x0010
/* Command status register for selected thread */
#define HPNFC_CMD_STATUS			0x0014

/* interrupt status register */
#define HPNFC_INTR_STATUS			0x0110
#define  HPNFC_INTR_STATUS_SDMA_ERR_MASK	(1uL << 22)
#define  HPNFC_INTR_STATUS_SDMA_TRIGG_MASK	(1uL << 21)
#define  HPNFC_INTR_STATUS_UNSUPP_CMD_MASK	(1uL << 19)
#define  HPNFC_INTR_STATUS_DDMA_TERR_MASK	(1uL << 18)
#define  HPNFC_INTR_STATUS_CDMA_TERR_MASK	(1uL << 17)
#define  HPNFC_INTR_STATUS_CDMA_IDL_MASK	(1uL << 16)

/* interrupt enable register */
#define HPNFC_INTR_ENABLE			0x0114
#define  HPNFC_INTR_ENABLE_INTR_EN_MASK		(1uL << 31)
#define  HPNFC_INTR_ENABLE_SDMA_ERR_EN_MASK	(1uL << 22)
#define  HPNFC_INTR_ENABLE_SDMA_TRIGG_EN_MASK	(1uL << 21)
#define  HPNFC_INTR_ENABLE_UNSUPP_CMD_EN_MASK	(1uL << 19)
#define  HPNFC_INTR_ENABLE_DDMA_TERR_EN_MASK	(1uL << 18)
#define  HPNFC_INTR_ENABLE_CDMA_TERR_EN_MASK	(1uL << 17)
#define  HPNFC_INTR_ENABLE_CDMA_IDLE_EN_MASK	(1uL << 16)

/* Controller internal state */
#define HPNFC_CTRL_STATUS			0x0118
#define  HPNFC_CTRL_STATUS_INIT_COMP_MASK	(1uL << 9)
#define  HPNFC_CTRL_STATUS_CTRL_BUSY_MASK	(1uL << 8)

/* Command Engine threads state */
#define HPNFC_TRD_STATUS			0x0120

/*  Command Engine interrupt thread error status */
#define HPNFC_TRD_ERR_INT_STATUS		0x0128
/*  Command Engine interrupt thread error enable */
#define HPNFC_TRD_ERR_INT_STATUS_EN		0x0130
/*  Command Engine interrupt thread complete status*/
#define HPNFC_TRD_COMP_INT_STATUS		0x0138

/* Transfer config 0 register.
 * Configures data transfer parameters. */
#define HPNFC_TRAN_CFG_0			0x0400
/* Offset value from the beginning of the page - shift */
#define  HPNFC_TRAN_CFG_0_OFFSET_SHIFT		16
/* Offset value from the beginning of the page - mask */
#define  HPNFC_TRAN_CFG_0_OFFSET_MASK		(0xFFFFuL << 16)
/* Numbers of sectors to transfer within single NF device's page */
#define  HPNFC_TRAN_CFG_0_SEC_CNT_SHIFT		0
#define  HPNFC_TRAN_CFG_0_SEC_CNT_MASK		(0xFFuL << 0)

/* Transfer config 1 register.
 * Configures data transfer parameters. */
#define HPNFC_TRAN_CFG_1			0x0404
/* Size of last data sector. - shift */
#define  HPNFC_TRAN_CFG_1_LAST_SEC_SIZE_SHIFT	16
/* Size of last data sector. - mask */
#define  HPNFC_TRAN_CFG_1_LAST_SEC_SIZE_MASK	(0xFFFFuL << 16)
/*  Size of not-last data sector. - shift*/
#define  HPNFC_TRAN_CFG_1_SECTOR_SIZE_SHIFT	0
/*  Size of not-last data sector. - last*/
#define  HPNFC_TRAN_CFG_1_SECTOR_SIZE_MASK	(0xFFFFuL << 0)

/* NF device layout. */
#define HPNFC_NF_DEV_LAYOUT			0x0424
/* Bit in ROW address used for selecting of the LUN - shift */
#define  HPNFC_NF_DEV_LAYOUT_ROWAC_SHIFT	24
/* Bit in ROW address used for selecting of the LUN - mask */
#define  HPNFC_NF_DEV_LAYOUT_ROWAC_MASK		(0xFuL << 24)
/* The number of LUN presents in the device. - shift */
#define  HPNFC_NF_DEV_LAYOUT_LN_SHIFT		20
/* The number of LUN presents in the device. - mask */
#define  HPNFC_NF_DEV_LAYOUT_LN_MASK		(0xF << 20)
/* Enables Multi LUN operations - mask */
#define  HPNFC_NF_DEV_LAYOUT_LUN_EN_MASK	(1uL << 16)
/* Pages Per Block - number of pages in a block - shift */
#define  HPNFC_NF_DEV_LAYOUT_PPB_SHIFT		0
/* Pages Per Block - number of pages in a block - mask */
#define  HPNFC_NF_DEV_LAYOUT_PPB_MASK		(0xFFFFuL << 0)

/* ECC engine configuration register 0. */
#define HPNFC_ECC_CONFIG_0			0x0428
/* Correction strength - shift */
#define  HPNFC_ECC_CONFIG_0_CORR_STR_SHIFT	8
/* Correction strength - mask */
#define  HPNFC_ECC_CONFIG_0_CORR_STR_MASK	(3uL << 8)
/* Enables scrambler logic in the controller - mask */
#define  HPNFC_ECC_CONFIG_0_SCRAMBLER_EN_MASK	(1uL << 2)
/*  Enable erased pages detection mechanism - mask */
#define  HPNFC_ECC_CONFIG_0_ERASE_DET_EN_MASK	(1uL << 1)
/*  Enable controller ECC check bits generation and correction - mask */
#define  HPNFC_ECC_CONFIG_0_ECC_EN_MASK		(1uL << 0)
/* ECC engine configuration register 1. */
#define HPNFC_ECC_CONFIG_1			0x042C

/* Multiplane settings register */
#define HPNFC_MULTIPLANE_CFG			0x0434
/* Cache operation settings. */
#define HPNFC_CACHE_CFG				0x0438

/* DMA settings register */
#define HPNFC_DMA_SETTINGS			0x043C
/* Enable SDMA error report on access unprepared slave DMA interface */
#define  HPNFC_DMA_SETTINGS_SDMA_ERR_RSP_MASK	(1uL << 17)
/* Outstanding transaction enable - mask */
#define  HPNFC_DMA_SETTINGS_OTE_MASK		(1uL << 16)
/* DMA burst selection - shift */
#define  HPNFC_DMA_SETTINGS_BURST_SEL_SHIFT	0
/* DMA burst selection - mask */
#define  HPNFC_DMA_SETTINGS_BURST_SEL_MASK	(0xFFuL << 0)

/* Transferred data block size for the slave DMA module */
#define HPNFC_SDMA_SIZE				0x0440

/* Thread number associated with transferred data block
 * for the slave DMA module */
#define HPNFC_SDMA_TRD_NUM			0x0444
/* Thread number mask */
#define  HPNFC_SDMA_TRD_NUM_SDMA_TRD_MASK	(0x3 << 0)
/* Thread number shift */
#define  HPNFC_SDMA_TRD_NUM_SDMA_TRD_SHIFT	(0)

/* available hardware features of the controller */
#define HPNFC_CTRL_FEATURES			0x804
/* Support for NV-DDR2/3 work mode - mask */
#define  HPNFC_CTRL_FEATURES_NVDDR_2_3_MASK	(1 << 28)
/* Support for NV-DDR2/3 work mode - shift */
#define  HPNFC_CTRL_FEATURES_NVDDR_2_3_SHIFT	28
/* Support for NV-DDR work mode - mask */
#define  HPNFC_CTRL_FEATURES_NVDDR_MASK		(1 << 27)
/* Support for NV-DDR work mode - shift */
#define  HPNFC_CTRL_FEATURES_NVDDR_SHIFT	27
/* Support for asynchronous work mode - mask */
#define  HPNFC_CTRL_FEATURES_ASYNC_MASK		(1 << 26)
/* Support for asynchronous work mode - shift */
#define  HPNFC_CTRL_FEATURES_ASYNC_SHIFT	26
/* Support for number of banks supported by HW - mask */
#define  HPNFC_CTRL_FEATURES_N_BANKS_MASK	(3 << 24)
/* Support for number of banks supported by HW - shift */
#define  HPNFC_CTRL_FEATURES_N_BANKS_SHIFT	24
/* Slave and Master DMA data width - mask */
#define  HPNFC_CTRL_FEATURES_DMA_DWITH_64_MASK	(1 << 21)
/* Slave and Master DMA data width - shift */
#define  HPNFC_CTRL_FEATURES_DMA_DWITH_64_SHIFT	21
/* number of threads available in the controller - mask */
#define  HPNFC_CTRL_FEATURES_N_THREADS_MASK	(0x7 << 0)
/* number of threads available in the controller - shift */
#define  HPNFC_CTRL_FEATURES_N_THREADS_SHIFT	0x0

/* NAND Flash memory device ID information */
#define HPNFC_MANUFACTURER_ID			0x0808
/* Device ID - mask */
#define  HPNFC_MANUFACTURER_ID_DID_MASK		(0xFFuL << 16)
/* Device ID - shift */
#define  HPNFC_MANUFACTURER_ID_DID_SHIFT	16
/* Manufacturer ID - mask */
#define  HPNFC_MANUFACTURER_ID_MID_MASK		(0xFFuL << 0)
/* Manufacturer ID - shift */
#define  HPNFC_MANUFACTURER_ID_MID_SHIFT	0

/* Device areas settings. */
#define HPNFC_NF_DEV_AREAS			0x080c
/* Spare area size in bytes for the NF device page - shift */
#define  HPNFC_NF_DEV_AREAS_SPARE_SIZE_SHIFT	16
/* Spare area size in bytes for the NF device page - mask */
#define  HPNFC_NF_DEV_AREAS_SPARE_SIZE_MASK	(0xFFFFuL << 16)
/* Main area size in bytes for the NF device page - shift*/
#define  HPNFC_NF_DEV_AREAS_MAIN_SIZE_SHIFT	0
/* Main area size in bytes for the NF device page - mask*/
#define  HPNFC_NF_DEV_AREAS_MAIN_SIZE_MASK	(0xFFFFuL << 0)

/* device parameters 1 register contains device signature */
#define HPNFC_DEV_PARAMS_1			0x0814
#define  HPNFC_DEV_PARAMS_1_READID_6_SHIFT	24
#define  HPNFC_DEV_PARAMS_1_READID_6_MASK	(0xFFuL << 24)
#define  HPNFC_DEV_PARAMS_1_READID_5_SHIFT	16
#define  HPNFC_DEV_PARAMS_1_READID_5_MASK	(0xFFuL << 16)
#define  HPNFC_DEV_PARAMS_1_READID_4_SHIFT	8
#define  HPNFC_DEV_PARAMS_1_READID_4_MASK	(0xFFuL << 8)
#define  HPNFC_DEV_PARAMS_1_READID_3_SHIFT	0
#define  HPNFC_DEV_PARAMS_1_READID_3_MASK	(0xFFuL << 0)

/* device parameters 0 register */
#define HPNFC_DEV_PARAMS_0			0x0810
/* device type shift */
#define  HPNFC_DEV_PARAMS_0_DEV_TYPE_SHIFT	30
/* device type mask */
#define  HPNFC_DEV_PARAMS_0_DEV_TYPE_MASK	(3uL << 30)
/* device type - ONFI */
#define  HPNFC_DEV_PARAMS_0_DEV_TYPE_ONFI	1
/* device type - JEDEC */
#define  HPNFC_DEV_PARAMS_0_DEV_TYPE_JEDEC	2
/* device type - unknown */
#define  HPNFC_DEV_PARAMS_0_DEV_TYPE_UNKNOWN	3
/* Number of bits used to addressing planes  - shift*/
#define  HPNFC_DEV_PARAMS_0_PLANE_ADDR_SHIFT	8
/* Number of bits used to addressing planes  - mask*/
#define  HPNFC_DEV_PARAMS_0_PLANE_ADDR_MASK	(0xFFuL << 8)
/* Indicates the number of LUNS present - mask*/
#define  HPNFC_DEV_PARAMS_0_NO_OF_LUNS_SHIFT	0
/* Indicates the number of LUNS present - mask*/
#define  HPNFC_DEV_PARAMS_0_NO_OF_LUNS_MASK	(0xFFuL << 0)

/* Features and optional commands supported
 * by the connected device */
#define HPNFC_DEV_FEATURES			0x0818

/* Number of blocks per LUN present in the NF device. */
#define HPNFC_DEV_BLOCKS_PER_LUN		0x081c

/* Device revision version */
#define HPNFC_DEV_REVISION			0x0820

/*  Device Timing modes 0*/
#define HPNFC_ONFI_TIME_MOD_0			0x0824
/* SDR timing modes support - shift */
#define  HPNFC_ONFI_TIME_MOD_0_SDR_SHIFT	0
/* SDR timing modes support - mask */
#define  HPNFC_ONFI_TIME_MOD_0_SDR_MASK		(0xFFFFuL << 0)
/* DDR timing modes support - shift */
#define  HPNFC_ONFI_TIME_MOD_0_DDR_SHIFT	16
/* DDR timing modes support - mask */
#define  HPNFC_ONFI_TIME_MOD_0_DDR_MASK		(0xFFFFuL << 16)

/*  Device Timing modes 1*/
#define HPNFC_ONFI_TIME_MOD_1			0x0828
/* DDR2 timing modes support - shift */
#define  HPNFC_ONFI_TIME_MOD_1_DDR2_SHIFT	0
/* DDR2 timing modes support - mask */
#define  HPNFC_ONFI_TIME_MOD_1_DDR2_MASK	(0xFFFFuL << 0)
/* DDR3 timing modes support - shift */
#define  HPNFC_ONFI_TIME_MOD_1_DDR3_SHIFT	16
/* DDR3 timing modes support - mask */
#define  HPNFC_ONFI_TIME_MOD_1_DDR3_MASK	(0xFFFFuL << 16)

/* BCH Engine identification register 0 - correction strengths. */
#define HPNFC_BCH_CFG_0				0x838
#define  HPNFC_BCH_CFG_0_CORR_CAP_0_SHIFT	0
#define  HPNFC_BCH_CFG_0_CORR_CAP_0_MASK	(0XFFul << 0)
#define  HPNFC_BCH_CFG_0_CORR_CAP_1_SHIFT	8
#define  HPNFC_BCH_CFG_0_CORR_CAP_1_MASK	(0XFFul << 8)
#define  HPNFC_BCH_CFG_0_CORR_CAP_2_SHIFT	16
#define  HPNFC_BCH_CFG_0_CORR_CAP_2_MASK	(0XFFul << 16)
#define  HPNFC_BCH_CFG_0_CORR_CAP_3_SHIFT	24
#define  HPNFC_BCH_CFG_0_CORR_CAP_3_MASK	(0XFFul << 24)


/* BCH Engine identification register 1 - correction strengths. */
#define HPNFC_BCH_CFG_1				0x83C
#define  HPNFC_BCH_CFG_1_CORR_CAP_4_SHIFT	0
#define  HPNFC_BCH_CFG_1_CORR_CAP_4_MASK	(0XFFul << 0)
#define  HPNFC_BCH_CFG_1_CORR_CAP_5_SHIFT	8
#define  HPNFC_BCH_CFG_1_CORR_CAP_5_MASK	(0XFFul << 8)
#define  HPNFC_BCH_CFG_1_CORR_CAP_6_SHIFT	16
#define  HPNFC_BCH_CFG_1_CORR_CAP_6_MASK	(0XFFul << 16)
#define  HPNFC_BCH_CFG_1_CORR_CAP_7_SHIFT	24
#define  HPNFC_BCH_CFG_1_CORR_CAP_7_MASK	(0XFFul << 24)

/* BCH Engine identification register 2 - sector sizes. */
#define HPNFC_BCH_CFG_2				0x840
#define  HPNFC_BCH_CFG_2_SECT_0_SHIFT		0
#define  HPNFC_BCH_CFG_2_SECT_0_MASK		(0xFFFFuL << 0)
#define  HPNFC_BCH_CFG_2_SECT_1_SHIFT		16
#define  HPNFC_BCH_CFG_2_SECT_1_MASK		(0xFFFFuL << 16)

/* BCH Engine identification register 3  */
#define HPNFC_BCH_CFG_3				0x844

/* Ready/Busy# line status */
#define HPNFC_RBN_SETTINGS			0x1004

/*  Common settings */
#define HPNFC_COMMON_SETT			0x1008
/* write warm-up cycles. shift */
#define  HPNFC_COMMON_SETT_WR_WARMUP_SHIFT	20
/* read warm-up cycles. shift */
#define  HPNFC_COMMON_SETT_RD_WARMUP_SHIFT	16
/* 16 bit device connected to the NAND Flash interface - shift */
#define  HPNFC_COMMON_SETT_DEVICE_16BIT_SHIFT	8
/* Operation work mode - mask */
#define  HPNFC_COMMON_SETT_OPR_MODE_MASK	0x3
/* Operation work mode - shift */
#define  HPNFC_COMMON_SETT_OPR_MODE_SHIFT	0
/* Operation work mode - SDR mode */
#define  HPNFC_COMMON_SETT_OPR_MODE_SDR		0
/* Operation work mode - NV_DDR mode */
#define  HPNFC_COMMON_SETT_OPR_MODE_NV_DDR	1
/* Operation work mode - ToggleMode/NV-DDR2/NV-DDR3 mode */
#define  HPNFC_COMMON_SETT_OPR_MODE_TOGGLE	2

/* ToggleMode/NV-DDR2/NV-DDR3 and SDR timings configuration. */
#define HPNFC_ASYNC_TOGGLE_TIMINGS		0x101c
/*  The number of clock cycles (nf_clk) the Minicontroller
 *  needs to de-assert RE# to meet the tREH  - shift*/
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TRH_SHIFT	24
/*  The number of clock cycles (nf_clk) the Minicontroller
 *  needs to de-assert RE# to meet the tREH  - mask*/
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TRH_MASK	(0x1FuL << 24)
/* The number of clock cycles (nf_clk) the Minicontroller
 * needs to assert RE# to meet the tRP - shift*/
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TRP_SHIFT	16
/* The number of clock cycles (nf_clk) the Minicontroller
 * needs to assert RE# to meet the tRP - mask*/
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TRP_MASK	(0x1FuL << 16)
/* The number of clock cycles (nf_clk) the Minicontroller
 * needs to assert WE# to meet the tWH - shift*/
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TWH_SHIFT	8
/* The number of clock cycles (nf_clk) the Minicontroller
 * needs to assert WE# to meet the tWH - mask*/
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TWH_MASK	(0x1FuL << 8)
/* The number of clock cycles (nf_clk) the Minicontroller
 * needs to assert WE# to meet the tWP - shift */
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TWP_SHIFT	0
/* The number of clock cycles (nf_clk) the Minicontroller
 * needs to assert WE# to meet the tWP - mask */
#define  HPNFC_ASYNC_TOGGLE_TIMINGS_TWP_MASK	(0x1FuL << 0)

#define HPNFC_TIMINGS0				0x1024
#define HPNFC_TIMINGS0_tADL_SHIFT		24
#define HPNFC_TIMINGS0_tADL_MASK		(0xFFuL << 24)
#define HPNFC_TIMINGS0_tCCS_SHIFT		16
#define HPNFC_TIMINGS0_tCCS_MASK		(0xFFuL << 16)
#define HPNFC_TIMINGS0_tWHR_SHIFT		8
#define HPNFC_TIMINGS0_tWHR_MASK		(0xFFuL << 8)
#define HPNFC_TIMINGS0_tRHW_SHIFT		0
#define HPNFC_TIMINGS0_tRHW_MASK		(0xFFuL << 0)

#define HPNFC_TIMINGS1				0x1028
#define HPNFC_TIMINGS1_tRHZ_SHIFT		24
#define HPNFC_TIMINGS1_tRHZ_MASK		(0xFFuL << 24)
#define HPNFC_TIMINGS1_tWB_SHIFT		16
#define HPNFC_TIMINGS1_tWB_MASK			(0xFFuL << 16)
#define HPNFC_TIMINGS1_tCWAW_SHIFT		8
#define HPNFC_TIMINGS1_tCWAW_MASK		(0xFFuL << 8)
#define HPNFC_TIMINGS1_tVDLY_SHIFT		0
#define HPNFC_TIMINGS1_tVDLY_MASK		(0xFFuL << 0)

#define HPNFC_TIMINGS2				0x102c
#define HPNFC_TIMINGS2_tFEAT_SHIFT		16
#define HPNFC_TIMINGS2_tFEAT_MASK		(0x3FFuL << 16)
#define HPNFC_TIMINGS2_CS_hold_time_SHIFT	8
#define HPNFC_TIMINGS2_CS_hold_time_MASK	(0x3FuL << 8)
#define HPNFC_TIMINGS2_CS_setup_time_SHIFT	0
#define HPNFC_TIMINGS2_CS_setup_time_MASK	(0x3FuL << 0)

/* Configuration of the resynchronization of slave DLL of PHY */
#define HPNFC_DLL_PHY_CTRL			0x1034
/* Acknowledge signal to resynchronize the DLLs and read and
 * write FIFO pointers - mask */
#define  HPNFC_DLL_PHY_CTRL_DFI_CTRLUPD_ACK_MASK	(1uL << 26)
/* Signal to resynchronize the DLLs and read and write FIFO pointers */
#define  HPNFC_DLL_PHY_CTRL_DFI_CTRLUPD_REQ_MASK	(1uL << 25)
/*  Signal to reset the DLLs of the PHY and start
 *  searching for lock again - mask */
#define  HPNFC_DLL_PHY_CTRL_DLL_RST_N_MASK	(1uL << 24)
/* Information to controller and PHY that the WE# is in extended mode */
#define  HPNFC_DLL_PHY_CTRL_EXTENDED_WR_MODE_MASK	(1uL << 17)
/* Information to controller and PHY that the RE# is in extended mode */
#define  HPNFC_DLL_PHY_CTRL_EXTENDED_RD_MODE_MASK	(1uL << 16)
/*  This field defines the number of Minicontroller clock cycles
 *  (nf_clk) for which the DLL update request (dfi_ctrlupd_req)
 *  has to be asserted - mask */
#define  HPNFC_DLL_PHY_CTRL_RS_HIGH_WAIT_CNT_MASK	(0xFuL << 8)
/* This field defines the wait time(in terms of Minicontroller
 * clock cycles (nf_clk)) between the deassertion of the DLL
 * update request (dfi_ctrlupd_req) and resuming traffic to the PHY */
#define  HPNFC_DLL_PHY_CTRL_RS_IDLE_CNT_MASK	(0xFFuL << 0)

/* register controlling DQ related timing  */
#define HPNFC_PHY_DQ_TIMING_REG			0x2000
/* register controlling DSQ related timing  */
#define HPNFC_PHY_DQS_TIMING_REG		0x2004
/* register controlling the gate and loopback control related timing. */
#define HPNFC_PHY_GATE_LPBK_CTRL_REG		0x2008
/* register holds the control for the master DLL logic */
#define HPNFC_PHY_DLL_MASTER_CTRL_REG		0x200C
/* register holds the control for the slave DLL logic */
#define HPNFC_PHY_DLL_SLAVE_CTRL_REG		0x2010

/*   This register handles the global control settings for the PHY */
#define HPNFC_PHY_CTRL_REG			0x2080
/*The value that should be driven on the DQS pin while SDR
 * operations are in progress. - shift*/
#define  HPNFC_PHY_CTRL_REG_SDR_DQS_SHIFT	14
/*The value that should be driven on the DQS pin while SDR
 * operations are in progress. - mask*/
#define  HPNFC_PHY_CTRL_REG_SDR_DQS_MASK	(1uL << 14)
/* The timing of assertion of phony DQS to the data slices - shift */
#define  HPNFC_PHY_CTRL_REG_PHONY_DQS_SHIFT	4
/* The timing of assertion of phony DQS to the data slices - mask */
#define  HPNFC_PHY_CTRL_REG_PHONY_DQS_MASK	(0x1FuL << 4)
/* This register handles the global control settings
 * for the termination selects for reads */
#define HPNFC_PHY_TSEL_REG			0x2084
/***************************************************/

#ifdef	CONFIG_MTD_NAND_CADENCE_AS390REMAP
#undef  HPNFC_RBN_SETTINGS
#define HPNFC_RBN_SETTINGS			0x0904
#undef  HPNFC_COMMON_SETT
#define HPNFC_COMMON_SETT			0x0908
#undef  HPNFC_ASYNC_TOGGLE_TIMINGS
#define HPNFC_ASYNC_TOGGLE_TIMINGS		0x091c
#undef  HPNFC_DLL_PHY_CTRL
#define HPNFC_DLL_PHY_CTRL			0x0934
#undef  HPNFC_PHY_DQ_TIMING_REG
#define HPNFC_PHY_DQ_TIMING_REG			0x0a00
#undef  HPNFC_PHY_DQS_TIMING_REG
#define HPNFC_PHY_DQS_TIMING_REG		0x0a04
#undef  HPNFC_PHY_GATE_LPBK_CTRL_REG
#define HPNFC_PHY_GATE_LPBK_CTRL_REG		0x0a08
#undef  HPNFC_PHY_DLL_MASTER_CTRL_REG
#define HPNFC_PHY_DLL_MASTER_CTRL_REG		0x0a0C
#undef  HPNFC_PHY_DLL_SLAVE_CTRL_REG
#define HPNFC_PHY_DLL_SLAVE_CTRL_REG		0x0a10
#undef  HPNFC_PHY_CTRL_REG
#define HPNFC_PHY_CTRL_REG			0x0a80
#undef  HPNFC_PHY_TSEL_REG
#define HPNFC_PHY_TSEL_REG			0x0a84
#endif

/***************************************************/
/*  Register field accessing macros */
/***************************************************/
/* write field of 32 bit register */
#define WRITE_FIELD(reg, field_name, field_val)	\
	reg = (reg & ~field_name ## _MASK) \
	      | ((uint32_t)field_val) << field_name ## _SHIFT

/* write field of 64 bit register */
#define WRITE_FIELD64(reg, field_name, field_val) \
	reg = (reg & ~field_name ## _MASK) \
	      | ((uint64_t)field_val) << field_name ## _SHIFT

/* read field of 32 bit register */
#define READ_FIELD(reg, field_name) \
	((reg & field_name ## _MASK) >> field_name ## _SHIFT)
/***************************************************/

#define SETFIELD(field_name, val) \
	(((val)  << field_name##_SHIFT) & field_name##_MASK)


/***************************************************/
/*  Register write/read macros */
/***************************************************/
#define IOWR_32(reg, val)   iowrite32((val), (reg));
#define IORD_32(reg)        ioread32(reg)
/***************************************************/


/***************************************************/
/*  Generic command definition */
/***************************************************/
typedef struct generic_data_t {
	/* use interrupts   */
	uint8_t use_intr : 1;
	/* transfer direction */
	uint8_t direction : 1;
	/* enable ECC  */
	uint8_t ecc_en : 1;
	/* enable scrambler (if ECC is enabled) */
	uint8_t scr_en : 1;
	/* enable erase page detection */
	uint8_t erpg_en : 1;
	/* number of bytes to transfer of the n-1 sectors
	 * except the last onesector */
	uint16_t sec_size;
	/* defines the number of sectors to transfer within a single
	 * sequence */
	uint8_t sec_cnt;
	/* number of bytes to transfer in the last sector */
	uint16_t last_sec_size;
	/* ECC correction capability (if ECC is enabled) */
	uint8_t corr_cap;
} generic_data_t;


/* generic command layout*/
/* chip select - shift */
#define HPNFC_GCMD_LAY_CS_SHIFT         8
/* chip select - mask */
#define HPNFC_GCMD_LAY_CS_MASK          (0xF << 8)
/* commands complaint with Jedec spec*/
#define HPNFC_GCMD_LAY_JEDEC_MASK       (1 << 7)
/* This bit informs the minicotroller if it has to wait for tWB
 * after sending the last CMD/ADDR/DATA in the sequence. */
#define HPNFC_GCMD_LAY_TWB_MASK         (1 << 6)
/*  type of instruction - shift */
#define HPNFC_GCMD_LAY_INSTR_SHIFT      0
/*  type of instruction - mask */
#define HPNFC_GCMD_LAY_INSTR_MASK       (0x3F << 0)
/*  type of instruction - data transfer */
#define  HPNFC_GCMD_LAY_INSTR_DATA   2
/*  type of instruction - read parameter page (0xEF) */
#define  HPNFC_GCMD_LAY_INSTR_RDPP   28
/* type of instruction - read memory ID (0x90) */
#define  HPNFC_GCMD_LAY_INSTR_RDID   27
/* type of instruction - reset command (0xFF) */
#define  HPNFC_GCMD_LAY_INSTR_RDST   7
/* type of instruction - change read column command */
#define  HPNFC_GCMD_LAY_INSTR_CHRC   12

/* input part of generic command type of input is address 0 - shift */
#define HPNFC_GCMD_LAY_INPUT_ADDR0_SHIFT    16
/* input part of generic command type of input is address 0 - mask */
#define HPNFC_GCMD_LAY_INPUT_ADDR0_MASK     (0xFFFFFFFFFFuLL << 16)

/* generic command data sequence - transfer direction - shift  */
#define HPNFC_GCMD_DIR_SHIFT        11
/* generic command data sequence - transfer direction - mask  */
#define HPNFC_GCMD_DIR_MASK         (1uL << 11)
/* generic command data sequence - transfer direction - read  */
#define  HPNFC_GCMD_DIR_READ     0
/* generic command data sequence - transfer direction - write  */
#define  HPNFC_GCMD_DIR_WRITE    1

/* generic command data sequence - ecc enabled - mask  */
#define HPNFC_GCMD_ECC_EN_MASK      (1uLL << 12)
/* generic command data sequence - scrambler enabled - mask  */
#define HPNFC_GCMD_SCR_EN_MASK      (1uLL << 13)
/* generic command data sequence - erase page detection enabled - mask  */
#define HPNFC_GCMD_ERPG_EN_MASK     (1uLL << 14)
/* generic command data sequence - sctor size - shift  */
#define HPNFC_GCMD_SECT_SIZE_SHIFT  16
/* generic command data sequence - sector size - mask  */
#define HPNFC_GCMD_SECT_SIZE_MASK   (0xFFFFuL << 16)
/* generic command data sequence - sector count - shift  */
#define HPNFC_GCMD_SECT_CNT_SHIFT   32
/* generic command data sequence - sector count - mask  */
#define HPNFC_GCMD_SECT_CNT_MASK    (0xFFuLL << 32)
/* generic command data sequence - last sector size - shift  */
#define HPNFC_GCMD_LAST_SIZE_SHIFT  40
/* generic command data sequence - last sector size - mask  */
#define HPNFC_GCMD_LAST_SIZE_MASK   (0xFFFFuLL << 40)
/* generic command data sequence - correction capability - shift  */
#define HPNFC_GCMD_CORR_CAP_SHIFT   56
/* generic command data sequence - correction capability - mask  */
#define HPNFC_GCMD_CORR_CAP_MASK    (3uLL << 56)



/***************************************************/
/*  CDMA descriptor fields */
/***************************************************/

/** command DMA descriptor type - erase command */
#define HPNFC_CDMA_CT_ERASE         0x1000
/** command DMA descriptor type - reset command */
#define HPNFC_CDMA_CT_RST           0x1100
/** command DMA descriptor type - copy back command */
#define HPNFC_CDMA_CT_CPYB          0x1200
/** command DMA descriptor type - write page command */
#define HPNFC_CDMA_CT_WR            0x2100
/** command DMA descriptor type - read page command */
#define HPNFC_CDMA_CT_RD            0x2200
/** command DMA descriptor type - nop command */
#define HPNFC_CDMA_CT_NOP           0xFFFF

/** flash pointer memory - shift */
#define HPNFC_CDMA_CFPTR_MEM_SHIFT  24
/** flash pointer memory - mask */
#define HPNFC_CDMA_CFPTR_MEM_MASK   (7uL << 24)

/** command DMA descriptor flags - issue interrupt after
 * the completion of descriptor processing */
#define HPNFC_CDMA_CF_INT           (1uL << 8)
/** command DMA descriptor flags - the next descriptor
 * address field is valid and descriptor processing should continue */
#define HPNFC_CDMA_CF_CONT          (1uL << 9)
/* command DMA descriptor flags - selects DMA slave */
#define HPNFC_CDMA_CF_DMA_SLAVE     (0uL << 10)
/* command DMA descriptor flags - selects DMA master */
#define HPNFC_CDMA_CF_DMA_MASTER    (1uL << 10)


/* command descriptor status  - the operation number
 * where the first error was detected - shift*/
#define HPNFC_CDMA_CS_ERR_IDX_SHIFT 24
/* command descriptor status  - the operation number
 * where the first error was detected - mask*/
#define HPNFC_CDMA_CS_ERR_IDX_MASK  (0xFFuL << 24)
/* command descriptor status  - operation complete - mask*/
#define HPNFC_CDMA_CS_COMP_MASK     (1uL << 15)
/* command descriptor status  - operation fail - mask*/
#define HPNFC_CDMA_CS_FAIL_MASK     (1uL << 14)
/* command descriptor status  - page erased - mask*/
#define HPNFC_CDMA_CS_ERP_MASK      (1uL << 11)
/* command descriptor status  - timeout occurred - mask*/
#define HPNFC_CDMA_CS_TOUT_MASK     (1uL << 10)
/* command descriptor status - maximum amount of correction
 * applied to one ECC sector - shift */
#define HPNFC_CDMA_CS_MAXERR_SHIFT  2
/* command descriptor status - maximum amount of correction
 * applied to one ECC sector - mask */
#define HPNFC_CDMA_CS_MAXERR_MASK   (0xFFuL << 2)
/* command descriptor status - uncorrectable ECC error - mask*/
#define HPNFC_CDMA_CS_UNCE_MASK     (1uL << 1)
/* command descriptor status - descriptor error - mask*/
#define HPNFC_CDMA_CS_ERR_MASK      (1uL << 0)

/***************************************************/


/***************************************************/
/*  internal used status*/
/***************************************************/
/* status of operation - OK */
#define HPNFC_STAT_OK               0
/* status of operation - FAIL */
#define HPNFC_STAT_FAIL             2
/* status of operation - uncorrectable ECC error */
#define HPNFC_STAT_ECC_UNCORR       3
/* status of operation - page erased */
#define HPNFC_STAT_ERASED           5
/* status of operation - correctable ECC error */
#define HPNFC_STAT_ECC_CORR         6
/* status of operation - operation is not completed yet */
#define HPNFC_STAT_BUSY             0xFF
/***************************************************/

/***************************************************/
/* work modes  */
/***************************************************/
#define HPNFC_WORK_MODE_ASYNC       0x00
#define HPNFC_WORK_MODE_NV_DDR      0x10
#define HPNFC_WORK_MODE_NV_DDR2     0x20
#define HPNFC_WORK_MODE_NV_DDR3     0x30
#define HPNFC_WORK_MODE_TOGG        0x40
/***************************************************/

/***************************************************/
/* ECC correction capabilities  */
/***************************************************/
#define HPNFC_ECC_CORR_CAP_2    2
#define HPNFC_ECC_CORR_CAP_4    4
#define HPNFC_ECC_CORR_CAP_8    8
#define HPNFC_ECC_CORR_CAP_12   12
#define HPNFC_ECC_CORR_CAP_16   16
#define HPNFC_ECC_CORR_CAP_24   24
#define HPNFC_ECC_CORR_CAP_40   40
/***************************************************/

struct nand_buf {
	uint8_t *buf;
	int tail;
	int head;
	dma_addr_t dma_buf;
};

/* Command DMA descriptor */
typedef struct hpnfc_cdma_desc_t {
	/* next descriptor address */
	uint64_t next_pointer;
	/* glash address is a 32-bit address comprising of BANK and ROW ADDR. */
	uint32_t flash_pointer;
	uint32_t rsvd0;
	/* operation the controller needs to perform */
	uint16_t command_type;
	uint16_t rsvd1;
	/* flags for operation of this command */
	uint16_t command_flags;
	uint16_t rsvd2;
	/* system/host memory address required for data DMA commands. */
	uint64_t memory_pointer;
	/* status of operation */
	uint32_t status;
	uint32_t rsvd3;
	/* address pointer to sync buffer location */
	uint64_t sync_flag_pointer;
	/* Controls the buffer sync mechanism. */
	uint32_t sync_arguments;
	uint32_t rsvd4;
} hpnfc_cdma_desc_t;

/* interrupt status */
typedef struct hpnfc_irq_status_t {
	/*  Thread operation complete status */
	uint32_t trd_status;
	/*  Thread operation error */
	uint32_t trd_error;
	/* Controller status  */
	uint32_t status;
} hpnfc_irq_status_t;

/** BCH HW configuration info */
typedef struct hpnfc_bch_config_info_t {
	/** Supported BCH correction capabilities. */
	uint8_t corr_caps[8];
	/** Supported BCH sector sizes */
	uint32_t sector_sizes[2];
}  hpnfc_bch_config_info_t;

typedef struct hpnfc_state_t {
	hpnfc_cdma_desc_t *cdma_desc;
	dma_addr_t dma_cdma_desc;
	struct nand_buf buf;
	/* actual selected chip */
	uint8_t chip_nr;
	int offset;
	uint8_t *random_data;
	struct nand_randomizer randomizer;

	struct nand_chip nand;
	void __iomem *reg;
	void __iomem *slave_dma;
#if __FPGA__ 	//for fpga only
	void __iomem *reg_emmc;
	void __iomem *reg_peri;
#endif
	struct device *dev;

	int irq;
	hpnfc_irq_status_t irq_status;
	hpnfc_irq_status_t irq_mask;
	struct completion complete;
	spinlock_t irq_lock;

	/* part of spare area of NANF flash memory page.
	 * This part is available for user to read or write. Rest of spare area
	 * is used by controller for ECC correction */
	uint32_t usnused_spare_size;
	/* spare area size of NANF flash memory page */
	uint32_t spare_size;
	/* main area size of NANF flash memory page */
	uint32_t main_size;
	/* sector size few sectors are located on main area of NF memory page */
	uint32_t sector_size;
	/* last sector size (containing spare area) */
	uint32_t last_sector_size;
	uint32_t sector_count;
	uint32_t curr_trans_type;
	uint8_t corr_cap;
	uint8_t lun_count;
	uint32_t blocks_per_lun;

	uint32_t devnum;
	uint32_t bbtskipbytes;
	uint8_t max_banks;
	uint8_t dev_type;
	uint8_t ecc_enabled;
	hpnfc_bch_config_info_t bch_cfg;
	uint8_t bytesPerSdmaAccess;
	int	zos_page_end;
} hpnfc_state_t;

#define CADENCE_NAND_NAME    "cdns-hpnfc"
#define mtd_to_hpnfc(m) container_of(mtd_to_nand(m), struct hpnfc_state_t, nand)
#define nand_to_hpnfc(m) container_of(m, struct hpnfc_state_t, nand)

#define HPNFC_MINIMUM_SPARE_SIZE            4
#define HPNFC_MAX_SPARE_SIZE_PER_SECTOR     32
#define HPNFC_BCH_MAX_NUM_CORR_CAPS         8
#define HPNFC_BCH_MAX_NUM_SECTOR_SIZES      2

static int maxchips = 0;
static int disable_ddr = 0;

module_param(maxchips, int, S_IRUGO);
module_param(disable_ddr, int, S_IRUGO);

/* PHY configurations for nf_clk = 100MHz
 * phy ctrl, phy tsel, DQ timing, DQS timing, LPBK, dll master, dll slave*/
static uint32_t phy_timings_ddr[] = {
	0x0000, 0x00, 0x02, 0x00000004,	 0x00200002, 0x01140004, 0x1f1f
};
static uint32_t phy_timings_ddr2[] = {
	0x4000, 0x00, 0x02, 0x00000005,	 0x00380000, 0x01140004, 0x1f1f
};
static uint32_t phy_timings_toggle[] = {
	0x4000, 0x00, 0x02, 0x00000004,	 0x00280001, 0x01140004, 0x1f1f
};
static uint32_t phy_timings_async[] = {
	0x4040, 0x00, 0x02, 0x00100004, 0x1b << 19, 0x00800000, 0x0000
};

static int wait_for_thread(hpnfc_state_t *hpnfc, int8_t thread);
static int hpnfc_wait_for_idle(hpnfc_state_t *hpnfc);

/* send set features command to nand flash memory device */
static int nf_mem_set_features(hpnfc_state_t *hpnfc, uint8_t feat_addr,
			       uint8_t feat_val, uint8_t mem, uint8_t thread,
			       uint8_t vol_id)
{
	uint32_t reg;
	int status;

	/* wait for thread ready */
	status = wait_for_thread(hpnfc, thread);
	if (status)
		return status;

	reg = 0;
	WRITE_FIELD(reg, HPNFC_CMD_REG1_FADDR, feat_addr);
	WRITE_FIELD(reg, HPNFC_CMD_REG1_BANK, mem);
	/* set feature address and bank number */
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG1, reg);
	/* set feature - value*/
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG2, feat_val);

	reg = 0;
	/* select PIO mode */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_CT, HPNFC_CMD_REG0_CT_PIO);
	/* thread number */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_TN, thread);
	/* volume ID */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_VOL_ID, vol_id);
	/* disabled interrupt */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_INT, 0);
	/* select set feature command */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_PIO_CC, HPNFC_CMD_REG0_PIO_CC_SF);
	/* execute command */
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG0, reg);

	return 0;
}

/* send reset command to nand flash memory device */
static int nf_mem_reset(hpnfc_state_t *hpnfc, uint8_t mem, uint8_t thread,
			uint8_t vol_id)
{
	uint32_t reg = 0;
	int status;

	/* wait for thread ready */
	status = wait_for_thread(hpnfc, thread);
	if (status)
		return status;

	WRITE_FIELD(reg, HPNFC_CMD_REG1_BANK, mem);
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG1, reg);

	reg = 0;

	/* select PIO mode */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_CT, HPNFC_CMD_REG0_CT_PIO);
	/* thread number */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_TN, thread);
	/* volume ID */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_VOL_ID, vol_id);
	/* disabled interrupt */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_INT, 0);
	/* select reset command */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_PIO_CC, HPNFC_CMD_REG0_PIO_CC_RST);
	/* execute command */
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG0, reg);

	return 0;
}

/* function returns thread status */
static uint32_t hpnfc_get_thrd_status(hpnfc_state_t *hpnfc, uint8_t thread)
{
	uint32_t reg;

	IOWR_32(hpnfc->reg + HPNFC_CMD_STATUS_PTR, thread);
	reg = IORD_32(hpnfc->reg + HPNFC_CMD_STATUS);
	return reg;
}

/* Wait until operation is finished  */
static int hpnfc_pio_check_finished(hpnfc_state_t *hpnfc, uint8_t thread)
{
	uint32_t thrd_status;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/* wait for fail or complete status */
	do {
		thrd_status = hpnfc_get_thrd_status(hpnfc, thread);
		thrd_status &= (HPNFC_CDMA_CS_COMP_MASK | HPNFC_CDMA_CS_FAIL_MASK);
	} while ((thrd_status == 0) && time_before(jiffies, timeout));

	if (time_after_eq(jiffies, timeout)) {
		dev_err(hpnfc->dev, "Timeout while waiting for PIO command finished\n");
		return -ETIMEDOUT;
	}

	if (thrd_status & HPNFC_CDMA_CS_FAIL_MASK)
		return -EIO;
	if (thrd_status & HPNFC_CDMA_CS_COMP_MASK)
		return 0;

	return -EIO;
}

/* checks what is the best work mode */
static void hpnfc_check_the_best_mode(hpnfc_state_t *hpnfc,
				      uint8_t *work_mode,
				      uint8_t *nf_timing_mode)
{
	uint32_t reg;

	*work_mode = HPNFC_WORK_MODE_ASYNC;
	*nf_timing_mode = 0;

	if (hpnfc->dev_type != HPNFC_DEV_PARAMS_0_DEV_TYPE_ONFI)
		return;

	/* check if DDR is supported */
	reg = IORD_32(hpnfc->reg + HPNFC_ONFI_TIME_MOD_0);
	reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_0_DDR);
	if (reg)
		*work_mode = HPNFC_WORK_MODE_NV_DDR;

	/* check if DDR2 is supported */
	reg = IORD_32(hpnfc->reg + HPNFC_ONFI_TIME_MOD_1);
	reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_1_DDR2);
	if (reg)
		*work_mode = HPNFC_WORK_MODE_NV_DDR2;

	/* check if DDR is supported */
	reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_1_DDR3);
	if (reg)
		*work_mode = HPNFC_WORK_MODE_NV_DDR3;

	switch (*work_mode) {
	case HPNFC_WORK_MODE_NV_DDR:
		reg = IORD_32(hpnfc->reg + HPNFC_ONFI_TIME_MOD_0);
		reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_0_DDR);
		break;
	case HPNFC_WORK_MODE_NV_DDR2:
	case HPNFC_WORK_MODE_TOGG:
		reg = IORD_32(hpnfc->reg + HPNFC_ONFI_TIME_MOD_1);
		reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_1_DDR2);
		break;
	case HPNFC_WORK_MODE_NV_DDR3:
		reg = IORD_32(hpnfc->reg + HPNFC_ONFI_TIME_MOD_1);
		reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_1_DDR3);
		break;
	case HPNFC_WORK_MODE_ASYNC:
	default:
		reg = IORD_32(hpnfc->reg + HPNFC_ONFI_TIME_MOD_0);
		reg = READ_FIELD(reg, HPNFC_ONFI_TIME_MOD_0_SDR);
	}

	/* calculate from timing mode 1 */
	reg >>= 1;
	while (reg != 0) {
		reg >>= 1;
		*nf_timing_mode  += 1;
	}
}

/* set NAND flash memory device work mode */
static int nf_mem_set_work_mode(hpnfc_state_t *hpnfc, uint8_t work_mode,
				uint8_t timing_mode)
{
	uint8_t flash_work_mode = timing_mode;
	int i, status;

	switch (work_mode) {
	case HPNFC_WORK_MODE_NV_DDR:
		flash_work_mode |= (1 << 4);
		break;
	case HPNFC_WORK_MODE_NV_DDR2:
	case HPNFC_WORK_MODE_TOGG:
		flash_work_mode |= (2 << 4);
		break;
	case HPNFC_WORK_MODE_NV_DDR3:
		flash_work_mode |= (3 << 4);
		break;
	case HPNFC_WORK_MODE_ASYNC:
	default:
		break;
	}

	/* send SET FEATURES command */
	for (i = 0; i < hpnfc->devnum; i++)
		nf_mem_set_features(hpnfc, 0x01, flash_work_mode, i, i, 0);
	for (i = 0; i < hpnfc->devnum; i++) {
		status = hpnfc_pio_check_finished(hpnfc, i);
		if (status)
			return status;
	}

	/* wait for controller IDLE */
	status = hpnfc_wait_for_idle(hpnfc);
	if (status)
		return status;

	return 0;
}

static void hpnfc_apply_phy_settings(hpnfc_state_t *hpnfc, uint32_t settings[])
{
	IOWR_32(hpnfc->reg + HPNFC_PHY_CTRL_REG, settings[0]);
	IOWR_32(hpnfc->reg + HPNFC_PHY_TSEL_REG, settings[1]);
	IOWR_32(hpnfc->reg + HPNFC_PHY_DQ_TIMING_REG, settings[2]);
	IOWR_32(hpnfc->reg + HPNFC_PHY_DQS_TIMING_REG, settings[3]);
	IOWR_32(hpnfc->reg + HPNFC_PHY_GATE_LPBK_CTRL_REG, settings[4]);
	IOWR_32(hpnfc->reg + HPNFC_PHY_DLL_MASTER_CTRL_REG, settings[5]);
	IOWR_32(hpnfc->reg + HPNFC_PHY_DLL_SLAVE_CTRL_REG, settings[6]);
}

struct phy_timing {
	u8 id[NAND_MAX_ID_LEN];
	u32 async_toggle;
	u32 s0;
	u32 s1;
	u32 s2;
	u32 phy_ctrl;
};

static const struct phy_timing supported_chips[] = {
	{
		/* TC58NVG2S0H */
		.id = {0x98, 0xdc, 0x90, 0x26, 0x76, 0x16, 0x08, 0x00},
		.async_toggle = SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TRH, 2)|
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TRP, 2) |
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TWH, 2) |
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TWP, 2),
		.s0 = SETFIELD(HPNFC_TIMINGS0_tADL, 1) |
			SETFIELD(HPNFC_TIMINGS0_tCCS, 99) |
			SETFIELD(HPNFC_TIMINGS0_tWHR, 11) |
			SETFIELD(HPNFC_TIMINGS0_tRHW, 5),
		.s1 = SETFIELD(HPNFC_TIMINGS1_tRHZ, 11) |
			SETFIELD(HPNFC_TIMINGS1_tWB, 21) |
			SETFIELD(HPNFC_TIMINGS1_tCWAW, 255) |
			SETFIELD(HPNFC_TIMINGS1_tVDLY, 255),
		.s2 = SETFIELD(HPNFC_TIMINGS2_tFEAT, 199) |
			SETFIELD(HPNFC_TIMINGS2_CS_hold_time, 0) |
			SETFIELD(HPNFC_TIMINGS2_CS_setup_time, 3),
		.phy_ctrl = SETFIELD(HPNFC_PHY_CTRL_REG_PHONY_DQS, 4),
	},
		{
		/* TC58NVG1S3HTA00 */
		.id = {0x98, 0xdc, 0x90, 0x26, 0x76, 0x16, 0x08, 0x00},
		.async_toggle = SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TRH, 2)|
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TRP, 2) |
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TWH, 2) |
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TWP, 2),
		.s0 = SETFIELD(HPNFC_TIMINGS0_tADL, 60) |
			SETFIELD(HPNFC_TIMINGS0_tCCS, 20) |
			SETFIELD(HPNFC_TIMINGS0_tWHR, 12) |
			SETFIELD(HPNFC_TIMINGS0_tRHW, 20),
		.s1 = SETFIELD(HPNFC_TIMINGS1_tRHZ, 20) |
			SETFIELD(HPNFC_TIMINGS1_tWB, 20) |
			SETFIELD(HPNFC_TIMINGS1_tCWAW, 255) |
			SETFIELD(HPNFC_TIMINGS1_tVDLY, 255),
		.s2 = SETFIELD(HPNFC_TIMINGS2_tFEAT, 20) |
			SETFIELD(HPNFC_TIMINGS2_CS_hold_time, 7) |
			SETFIELD(HPNFC_TIMINGS2_CS_setup_time, 7),
		.phy_ctrl = SETFIELD(HPNFC_PHY_CTRL_REG_PHONY_DQS, 2),
	},
	{
		/* MT29F4G08ABAFAH4 */
		.id = {0x2c, 0xdc, 0x80, 0xa6, 0x62, 0x00, 0x00, 0x00},
		.async_toggle = SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TRH, 2)|
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TRP, 2) |
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TWH, 2) |
			SETFIELD(HPNFC_ASYNC_TOGGLE_TIMINGS_TWP, 2),
		.s0 = SETFIELD(HPNFC_TIMINGS0_tADL, 13) |
			SETFIELD(HPNFC_TIMINGS0_tCCS, 99) |
			SETFIELD(HPNFC_TIMINGS0_tWHR, 11) |
			SETFIELD(HPNFC_TIMINGS0_tRHW, 19),
		.s1 = SETFIELD(HPNFC_TIMINGS1_tRHZ, 19) |
			SETFIELD(HPNFC_TIMINGS1_tWB, 21) |
			SETFIELD(HPNFC_TIMINGS1_tCWAW, 255) |
			SETFIELD(HPNFC_TIMINGS1_tVDLY, 255),
		.s2 = SETFIELD(HPNFC_TIMINGS2_tFEAT, 199) |
			SETFIELD(HPNFC_TIMINGS2_CS_hold_time, 0) |
			SETFIELD(HPNFC_TIMINGS2_CS_setup_time, 3),
		.phy_ctrl = SETFIELD(HPNFC_PHY_CTRL_REG_PHONY_DQS, 4),
	},
};

static int hpnfc_config_phy_timing(hpnfc_state_t *hpnfc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_chips); i++) {
		if (!memcmp(supported_chips[i].id, hpnfc->nand.id.data, NAND_MAX_ID_LEN))
			break;
	}

	if (i >= ARRAY_SIZE(supported_chips))
		return 0;

	IOWR_32(hpnfc->reg + HPNFC_ASYNC_TOGGLE_TIMINGS, supported_chips[i].async_toggle);
	IOWR_32(hpnfc->reg + HPNFC_TIMINGS0, supported_chips[i].s0);
	IOWR_32(hpnfc->reg + HPNFC_TIMINGS1, supported_chips[i].s1);
	IOWR_32(hpnfc->reg + HPNFC_TIMINGS2, supported_chips[i].s2);
	IOWR_32(hpnfc->reg + HPNFC_PHY_CTRL_REG, supported_chips[i].phy_ctrl);

	return hpnfc_wait_for_idle(hpnfc);
}

/*
 * Sets desired work mode to HPNFC controller and all NAND flash devices
 * Each memory is processed in separate thread, starting from thread 0
 */
static int hpnfc_set_work_mode(hpnfc_state_t *hpnfc, uint8_t work_mode,
			       uint8_t timing_mode)
{
	uint32_t reg;
	uint32_t dll_phy_ctrl;
	int i, status;

	reg = (IORD_32(hpnfc->reg + HPNFC_DEV_PARAMS_1)) & 0xFF;
	reg = READ_FIELD(reg, HPNFC_DEV_PARAMS_1_READID_5);
	if (reg == 0x01)
		/* exit if memory works in NV-DDR3 mode */
		return -EINVAL;

	/* set NF memory in known work mode by sending the reset command */
	reg = 0;
	/* select SDR mode in the controller */
	WRITE_FIELD(reg, HPNFC_COMMON_SETT_OPR_MODE,
		    HPNFC_COMMON_SETT_OPR_MODE_SDR);
	IOWR_32(hpnfc->reg + HPNFC_COMMON_SETT, reg);

	/* select default timings */
	hpnfc_apply_phy_settings(hpnfc, phy_timings_async);

	/* send reset command to all nand flash memory devices*/
	for (i = 0; i < hpnfc->devnum; i++) {
		status = nf_mem_reset(hpnfc, i, i, 0);
		if (status)
			return status;
	}
	/* wait until reset commands is finished*/
	for (i = 0; i < hpnfc->devnum; i++) {
		status = hpnfc_pio_check_finished(hpnfc, i);
		if (status)
			return status;
	}

	/* set NAND flash memory work mode */
	status = nf_mem_set_work_mode(hpnfc, work_mode, timing_mode);
	if (status)
		return status;

	/* set dll_rst_n in dll_phy_ctrl to 0 */
	dll_phy_ctrl = IORD_32(hpnfc->reg + HPNFC_DLL_PHY_CTRL);
	dll_phy_ctrl &= ~HPNFC_DLL_PHY_CTRL_DLL_RST_N_MASK;
	IOWR_32(hpnfc->reg + HPNFC_DLL_PHY_CTRL, dll_phy_ctrl );

	/*
	 * set value of other PHY registers according to PHY user guide
	 * currently all values for nf_clk = 100 MHz
	 */
	switch (work_mode) {
	case HPNFC_WORK_MODE_NV_DDR:
		dev_info(hpnfc->dev, "Switch to NV_DDR mode %d \n", timing_mode);
		hpnfc_apply_phy_settings(hpnfc, phy_timings_ddr);
		break;

	case HPNFC_WORK_MODE_NV_DDR2:
		dev_info(hpnfc->dev, "Switch to NV_DDR2 mode %d \n", timing_mode);
		hpnfc_apply_phy_settings(hpnfc, phy_timings_ddr2);
		dll_phy_ctrl &= ~HPNFC_DLL_PHY_CTRL_EXTENDED_RD_MODE_MASK;
		break;

	case HPNFC_WORK_MODE_TOGG:
		dev_info(hpnfc->dev, "Switch to toggle DDR mode\n");
		hpnfc_apply_phy_settings(hpnfc, phy_timings_toggle);
		break;

	case HPNFC_WORK_MODE_ASYNC:
	default:
		dev_info(hpnfc->dev, "Switch to SDR mode %d \n", timing_mode);
		hpnfc_apply_phy_settings(hpnfc, phy_timings_async);

		reg = 0;
		WRITE_FIELD(reg, HPNFC_ASYNC_TOGGLE_TIMINGS_TRH, 3);
		WRITE_FIELD(reg, HPNFC_ASYNC_TOGGLE_TIMINGS_TRP, 4);
		WRITE_FIELD(reg, HPNFC_ASYNC_TOGGLE_TIMINGS_TWH, 3);
		WRITE_FIELD(reg, HPNFC_ASYNC_TOGGLE_TIMINGS_TWP, 4);
		IOWR_32(hpnfc->reg + HPNFC_ASYNC_TOGGLE_TIMINGS, reg);

		dll_phy_ctrl |= HPNFC_DLL_PHY_CTRL_EXTENDED_RD_MODE_MASK;
		dll_phy_ctrl |= HPNFC_DLL_PHY_CTRL_EXTENDED_WR_MODE_MASK;
	}

	/* set HPNFC controller work mode */
	reg = IORD_32(hpnfc->reg + HPNFC_COMMON_SETT);
	switch (work_mode) {
	case HPNFC_WORK_MODE_NV_DDR:
		WRITE_FIELD(reg, HPNFC_COMMON_SETT_OPR_MODE,
			    HPNFC_COMMON_SETT_OPR_MODE_NV_DDR);
		break;
	case HPNFC_WORK_MODE_TOGG:
	case HPNFC_WORK_MODE_NV_DDR2:
	case HPNFC_WORK_MODE_NV_DDR3:
		WRITE_FIELD(reg, HPNFC_COMMON_SETT_OPR_MODE,
			    HPNFC_COMMON_SETT_OPR_MODE_TOGGLE);
		break;

	case HPNFC_WORK_MODE_ASYNC:
	default:
		WRITE_FIELD(reg, HPNFC_COMMON_SETT_OPR_MODE,
			    HPNFC_COMMON_SETT_OPR_MODE_SDR);
	}
	IOWR_32(hpnfc->reg + HPNFC_COMMON_SETT, reg);

	/* set dll_rst_n in dll_phy_ctrl to 1 */
	dll_phy_ctrl |= HPNFC_DLL_PHY_CTRL_DLL_RST_N_MASK;
	IOWR_32(hpnfc->reg + HPNFC_DLL_PHY_CTRL, dll_phy_ctrl );

	/* wait for controller IDLE */
	return hpnfc_wait_for_idle(hpnfc);
}

static void hpnfc_ecc_config(hpnfc_state_t *hpnfc, bool ecc, bool edet)
{
	uint32_t reg = 0;
	uint32_t corr_str = hpnfc->corr_cap / 8 - 1;

	if (ecc) {
		WRITE_FIELD(reg, HPNFC_ECC_CONFIG_0_CORR_STR, corr_str);
		reg |= HPNFC_ECC_CONFIG_0_ECC_EN_MASK;
	}

	if (edet)
		reg |= HPNFC_ECC_CONFIG_0_ERASE_DET_EN_MASK;

	IOWR_32(hpnfc->reg + HPNFC_ECC_CONFIG_0, reg);
}

static void hpnfc_ecc_check_config(hpnfc_state_t *hpnfc, bool ecc, int page)
{
	if (unlikely(page < hpnfc->zos_page_end))
		hpnfc_ecc_config(hpnfc, ecc, false);
	else
		hpnfc_ecc_config(hpnfc, ecc, true);
}

static void hpnfc_clear_interrupt(hpnfc_state_t *hpnfc,
				  hpnfc_irq_status_t *irq_status)
{
	IOWR_32(hpnfc->reg + HPNFC_INTR_STATUS, irq_status->status);
	IOWR_32(hpnfc->reg + HPNFC_TRD_COMP_INT_STATUS, irq_status->trd_status);
	IOWR_32(hpnfc->reg + HPNFC_TRD_ERR_INT_STATUS, irq_status->trd_error);
}

static void hpnfc_read_int_status(hpnfc_state_t *hpnfc,
				  hpnfc_irq_status_t *irq_status)
{
	irq_status->status = IORD_32(hpnfc->reg + HPNFC_INTR_STATUS);
	irq_status->trd_status = IORD_32(hpnfc->reg + HPNFC_TRD_COMP_INT_STATUS);
	irq_status->trd_error = IORD_32(hpnfc->reg + HPNFC_TRD_ERR_INT_STATUS);
}

static inline uint32_t irq_detected(hpnfc_state_t *hpnfc,
				    hpnfc_irq_status_t *irq_status)
{
	hpnfc_read_int_status(hpnfc, irq_status);

	return irq_status->status || irq_status->trd_status
	       || irq_status->trd_error;
}

static irqreturn_t hpnfc_isr(int irq, void *dev_id)
{
	struct hpnfc_state_t *hpnfc = dev_id;
	hpnfc_irq_status_t irq_status;
	irqreturn_t result = IRQ_NONE;

	spin_lock(&hpnfc->irq_lock);

	if (irq_detected(hpnfc, &irq_status)) {
		/* handle interrupt */
		/* first acknowledge it */
		hpnfc_clear_interrupt(hpnfc, &irq_status);
		/* store the status in the device context for someone to read */
		hpnfc->irq_status.status |= irq_status.status;
		hpnfc->irq_status.trd_status |= irq_status.trd_status;
		hpnfc->irq_status.trd_error |= irq_status.trd_error;
		/* notify anyone who cares that it happened */
		complete(&hpnfc->complete);
		/* tell the OS that we've handled this */
		result = IRQ_HANDLED;
	}
	spin_unlock(&hpnfc->irq_lock);
	return result;
}

static void wait_for_irq(struct hpnfc_state_t *hpnfc,
			 hpnfc_irq_status_t *irq_mask,
			 hpnfc_irq_status_t *irq_status)
{
	unsigned long comp_res;
	unsigned long timeout = msecs_to_jiffies(10000);

	do {
		comp_res =
			wait_for_completion_timeout(&hpnfc->complete, timeout);
		spin_lock_irq(&hpnfc->irq_lock);
		*irq_status = hpnfc->irq_status;

		if ((irq_status->status & irq_mask->status)
		    || (irq_status->trd_status & irq_mask->trd_status)
		    || (irq_status->trd_error & irq_mask->trd_error)
		    ) {
			hpnfc->irq_status.status &= ~irq_mask->status;
			hpnfc->irq_status.trd_status &= ~irq_mask->trd_status;
			hpnfc->irq_status.trd_error &= ~irq_mask->trd_error;
			spin_unlock_irq(&hpnfc->irq_lock);
			/* our interrupt was detected */
			break;
		}

		/*
		 * these are not the interrupts you are looking for; need to
		 * wait again
		 */
		spin_unlock_irq(&hpnfc->irq_lock);
	} while (comp_res != 0);

	if (comp_res == 0) {
		/* timeout */
		dev_err(hpnfc->dev, "timeout occurred:"
			"\t status = 0x%x, mask = 0x%x\n"
			"\t trd_status = 0x%x, trd_status mask= 0x%x\n"
			"\t trd_error = 0x%x, trd_error mask = 0x%x\n",
			irq_status->status, irq_mask->status,
			irq_status->trd_status, irq_mask->trd_status,
			irq_status->trd_error, irq_mask->trd_error
			);

		memset(irq_status, 0, sizeof(hpnfc_irq_status_t));
	}
}

static void hpnfc_irq_cleanup(int irqnum, struct hpnfc_state_t *hpnfc)
{
	/* disable interrupts */
	IOWR_32(hpnfc->reg + HPNFC_INTR_ENABLE, HPNFC_INTR_ENABLE_INTR_EN_MASK);
}

/*
 * We need to buffer some data for some of the NAND core routines.
 * The operations manage buffering that data.
 */
static void reset_buf(struct hpnfc_state_t *hpnfc)
{
	hpnfc->buf.head = hpnfc->buf.tail = 0;
	memset(&hpnfc->buf.buf[0], 0, 20);
}

static void write_byte_to_buf(struct hpnfc_state_t *hpnfc, uint8_t byte)
{
	hpnfc->buf.buf[hpnfc->buf.tail++] = byte;
}

static void write_dword_to_buf(struct hpnfc_state_t *hpnfc, uint32_t dword)
{
	memcpy(&hpnfc->buf.buf[hpnfc->buf.tail], &dword, 4);
	hpnfc->buf.tail += 4;
}

static uint8_t* get_buf_ptr(struct hpnfc_state_t *hpnfc)
{
	return &hpnfc->buf.buf[hpnfc->buf.tail];
}

static void increase_buff_ptr(struct hpnfc_state_t *hpnfc, uint32_t size)
{
	hpnfc->buf.tail += size;
}

/* wait until NAND flash device is ready */
static int wait_for_rb_ready(hpnfc_state_t *hpnfc)
{
	uint32_t reg;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	do {
		reg = IORD_32(hpnfc->reg + HPNFC_RBN_SETTINGS);
		reg = (reg >> hpnfc->chip_nr) & 0x01;
	} while ((reg == 0) && time_before(jiffies, timeout));

	if (time_after_eq(jiffies, timeout)) {
		dev_err(hpnfc->dev,
			"Timeout while waiting for flash device %d ready\n",
			hpnfc->chip_nr);
		return -ETIMEDOUT;
	}
	return 0;
}

static int wait_for_thread(hpnfc_state_t *hpnfc, int8_t thread)
{
	uint32_t reg;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	do {
		/* get busy status of all threads */
		reg = IORD_32(hpnfc->reg + HPNFC_TRD_STATUS);
		/* mask all threads but selected */
		reg &= (1 << thread);
	} while (reg && time_before(jiffies, timeout));

	if (time_after_eq(jiffies, timeout)) {
		dev_err(hpnfc->dev, "Timeout while waiting for thread  %d\n", thread);
		return -ETIMEDOUT;
	}

	return 0;
}

static int hpnfc_wait_for_idle(hpnfc_state_t *hpnfc)
{
	uint32_t reg;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	do {
		reg = IORD_32(hpnfc->reg + HPNFC_CTRL_STATUS);
	} while ((reg & HPNFC_CTRL_STATUS_CTRL_BUSY_MASK)
		 && time_before(jiffies, timeout));

	if (time_after_eq(jiffies, timeout)) {
		dev_err(hpnfc->dev, "Timeout while waiting for controller idle\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*  This function waits for device initialization */
static int wait_for_init_complete(hpnfc_state_t *hpnfc)
{
	uint32_t reg;
	unsigned long timeout = jiffies + msecs_to_jiffies(10000);

	do {    /* get ctrl status register */
		reg = IORD_32(hpnfc->reg + HPNFC_CTRL_STATUS);
	} while (((reg & HPNFC_CTRL_STATUS_INIT_COMP_MASK) == 0)
		 && time_before(jiffies, timeout));

	if (time_after_eq(jiffies, timeout)) {
		dev_err(hpnfc->dev,
			"Timeout while waiting for controller init complete\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/* execute generic command on HPNFC controller */
static int hpnfc_generic_cmd_send(hpnfc_state_t *hpnfc, uint8_t thread_nr,
				  uint64_t mini_ctrl_cmd, uint8_t use_intr)
{
	uint32_t reg = 0;
	uint8_t status;

	uint32_t mini_ctrl_cmd_l = mini_ctrl_cmd & 0xFFFFFFFF;
	uint32_t mini_ctrl_cmd_h = mini_ctrl_cmd >> 32;

	status = wait_for_thread(hpnfc, thread_nr);
	if (status)
		return status;

	IOWR_32(hpnfc->reg + HPNFC_CMD_REG2, mini_ctrl_cmd_l);
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG3, mini_ctrl_cmd_h);

	/* select generic command */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_CT, HPNFC_CMD_REG0_CT_GEN);
	/* thread number */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_TN, thread_nr);
	if (use_intr)
		reg |= HPNFC_CMD_REG0_INT_MASK;

	/* issue command */
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG0, reg);

	return 0;
}

/* prepare generic command on  HPNFC controller  */
static int hpnfc_generic_cmd_command(hpnfc_state_t *hpnfc, uint32_t command,
				     uint64_t addr, uint8_t use_intr)
{
	uint64_t mini_ctrl_cmd = 0;
	uint8_t thread_nr = hpnfc->chip_nr;
	int status;

	switch (command) {
	case HPNFC_GCMD_LAY_INSTR_RDPP:
		mini_ctrl_cmd |= HPNFC_GCMD_LAY_TWB_MASK;
		break;
	case HPNFC_GCMD_LAY_INSTR_RDID:
		mini_ctrl_cmd |= HPNFC_GCMD_LAY_TWB_MASK;
		break;
	default:
		break;
	}

	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_LAY_INSTR, command);

	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_LAY_CS, hpnfc->chip_nr);
	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_LAY_INPUT_ADDR0, addr);

	/* send command */
	status = hpnfc_generic_cmd_send(hpnfc, thread_nr, mini_ctrl_cmd, use_intr);
	if (status)
		return status;

	/* wait for thread ready*/
	status = wait_for_thread(hpnfc, thread_nr);
	if (status)
		return status;
	return 0;
}

/* prepare generic command used to data transfer on  HPNFC controller  */
static int hpnfc_generic_cmd_data(hpnfc_state_t *hpnfc,
				  generic_data_t *generic_data)
{
	uint64_t mini_ctrl_cmd = 0;
	uint8_t thread_nr = hpnfc->chip_nr;

	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_LAY_CS, hpnfc->chip_nr);
	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_LAY_INSTR,
		      HPNFC_GCMD_LAY_INSTR_DATA);
	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_DIR, generic_data->direction);

	if (generic_data->ecc_en)
		mini_ctrl_cmd |= HPNFC_GCMD_ECC_EN_MASK;

	if (generic_data->scr_en)
		mini_ctrl_cmd |= HPNFC_GCMD_SCR_EN_MASK;

	if (generic_data->erpg_en)
		mini_ctrl_cmd |= HPNFC_GCMD_ERPG_EN_MASK;

	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_SECT_SIZE,
		      (uint64_t)generic_data->sec_size);
	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_SECT_CNT,
		      (uint64_t)generic_data->sec_cnt);
	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_LAST_SIZE,
		      (uint64_t)generic_data->last_sec_size);
	WRITE_FIELD64(mini_ctrl_cmd, HPNFC_GCMD_CORR_CAP,
		      (uint64_t)generic_data->corr_cap);

	return hpnfc_generic_cmd_send(hpnfc, thread_nr, mini_ctrl_cmd,
				      generic_data->use_intr);
}

/* wait for data on slave dma interface */
static int hpnfc_wait_on_sdma_trigg(hpnfc_state_t *hpnfc,
				    uint8_t* out_sdma_trd,
				    uint32_t* out_sdma_size)
{
	hpnfc_irq_status_t irq_mask, irq_status;

	irq_mask.trd_status = 0;
	irq_mask.trd_error = 0;
	irq_mask.status = HPNFC_INTR_STATUS_SDMA_TRIGG_MASK
			  | HPNFC_INTR_STATUS_SDMA_ERR_MASK
			  | HPNFC_INTR_STATUS_UNSUPP_CMD_MASK;

	wait_for_irq(hpnfc, &irq_mask, &irq_status);
	if (irq_status.status == 0) {
		dev_err(hpnfc->dev, "Timeout while waiting for SDMA\n");
		return -ETIMEDOUT;
	}

	if (irq_status.status & HPNFC_INTR_STATUS_SDMA_TRIGG_MASK) {
		*out_sdma_size = IORD_32(hpnfc->reg + HPNFC_SDMA_SIZE);
		*out_sdma_trd  = IORD_32(hpnfc->reg + HPNFC_SDMA_TRD_NUM);
		*out_sdma_trd = READ_FIELD(*out_sdma_trd, HPNFC_SDMA_TRD_NUM_SDMA_TRD);
	} else {
		dev_err(hpnfc->dev, "SDMA error - irq_status %x\n", irq_status.status);
		return -EIO;
	}

	return 0;
}

/* read data from slave DMA interface */
static int dma_read_data(hpnfc_state_t *hpnfc, void *buf, uint32_t size)
{
	int i;
	uint32_t *buf32 = buf;

	if (size & 3) {
		return -1;
	}
	for (i = 0; i < size / hpnfc->bytesPerSdmaAccess; i++) {
		*buf32 = IORD_32(hpnfc->slave_dma);
		buf32++;
	}

	return 0;
}

static void hpnfc_read_buf32(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int status;
	uint8_t sdmatrd_num;
	uint32_t sdma_size;
	generic_data_t generic_data;
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);

	memset(&generic_data, 0, sizeof(generic_data_t));
	generic_data.sec_cnt = 1;
	generic_data.last_sec_size = len;
	generic_data.direction = HPNFC_GCMD_DIR_READ;

	/* wait for finishing operation */
	status = wait_for_rb_ready(hpnfc);
	if (status)
		return;

	/* prepare controller to read data in generic mode */
	status = hpnfc_generic_cmd_data(hpnfc, &generic_data);
	if (status)
		return;

	/* wait until data is read on slave DMA */
	status = hpnfc_wait_on_sdma_trigg(hpnfc, &sdmatrd_num, &sdma_size);
	if (status)
		return;

	/* read data  */
	status = dma_read_data(hpnfc, buf, sdma_size);
	if (status)
		return;

	hpnfc->offset += len;
}

static void hpnfc_read_buf64(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int status;
	uint8_t sdmatrd_num;
	uint32_t sdma_size;
	generic_data_t generic_data;
	uint32_t tmp[2];
	uint8_t sub_size = 4;
	int i = 0;
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);

	memset(&generic_data, 0, sizeof(generic_data_t));
	generic_data.sec_cnt = 1;
	generic_data.last_sec_size = sub_size;
	generic_data.direction = HPNFC_GCMD_DIR_READ;

	/* read is made by 4 bytes because of platform limitation */
	while (len) {
		/* send change column command */
		status = hpnfc_generic_cmd_command(hpnfc,
						   HPNFC_GCMD_LAY_INSTR_CHRC,
						   hpnfc->offset,
						   0);
		if (status)
			return;

		/* wait for finishing operation */
		status = wait_for_rb_ready(hpnfc);
		if (status)
			return;

		/* prepare controller to read data in generic mode */
		status = hpnfc_generic_cmd_data(hpnfc, &generic_data);
		if (status)
			return;

		/* wait until data is read on slave DMA */
		status = hpnfc_wait_on_sdma_trigg(hpnfc, &sdmatrd_num, &sdma_size);
		if (status)
			return;

		/* read data  */
		status = dma_read_data(hpnfc, tmp, sdma_size);
		if (status)
			return;

		if (len < sub_size)
			sub_size = len;

		memcpy(buf + i, &tmp[0], sub_size);

		len -= sub_size;
		hpnfc->offset += sub_size;
		i += sub_size;
	}
}

static void hpnfc_read_buf(struct nand_chip *nand, uint8_t *buf, int len)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(nand);

	if (hpnfc->bytesPerSdmaAccess == 8)
		hpnfc_read_buf64(&nand->base.mtd, buf, len);
	else
		hpnfc_read_buf32(&nand->base.mtd, buf, len);
}

static int read_parameter_page64(hpnfc_state_t *hpnfc, uint32_t size)
{
	int status;
	uint8_t sdmatrd_num;
	uint32_t sdma_size;
	generic_data_t generic_data;
	uint32_t tmp[2];
	const uint8_t sub_size = 4;
	uint32_t offset = 0;

	memset(&generic_data, 0, sizeof(generic_data_t));
	generic_data.sec_cnt = 1;
	generic_data.last_sec_size = sub_size;
	generic_data.direction = HPNFC_GCMD_DIR_READ;

	/* execute read parameter page instruction */
	status = hpnfc_generic_cmd_command(hpnfc, HPNFC_GCMD_LAY_INSTR_RDPP, 0, 0);
	if (status)
		return status;

	/* wait for finishing operation */
	status = wait_for_rb_ready(hpnfc);
	if (status)
		return status;

	/* read is made by 4 bytes because of platform limitation */
	while (size) {
		/* prepare controller to read data in generic mode */
		status = hpnfc_generic_cmd_data(hpnfc, &generic_data);
		if (status)
			return status;

		/* wait until data is read on slave DMA */
		status = hpnfc_wait_on_sdma_trigg(hpnfc, &sdmatrd_num, &sdma_size);
		if (status)
			return status;

		/* read data (part of parameter page) */
		status = dma_read_data(hpnfc, tmp, sdma_size);
		if (status)
			return status;

		write_dword_to_buf(hpnfc, tmp[0]);

		size -= sub_size;
		offset += sub_size;

		/* send change column command */
		status = hpnfc_generic_cmd_command(hpnfc,
						   HPNFC_GCMD_LAY_INSTR_CHRC, offset,
						   0);
		if (status)
			return status;

		/* wait for finishing operation */
		status = wait_for_rb_ready(hpnfc);
		if (status)
			return status;

	}

	return 0;
}

static int read_parameter_page32(hpnfc_state_t *hpnfc, uint32_t size)
{
	int status;
	uint8_t sdmatrd_num;
	uint32_t sdma_size;
	generic_data_t generic_data;
	uint8_t *buffer;

	memset(&generic_data, 0, sizeof(generic_data_t));
	generic_data.sec_cnt = 1;
	generic_data.last_sec_size = size;
	generic_data.direction = HPNFC_GCMD_DIR_READ;

	/* execute read parameter page instruction */
	status = hpnfc_generic_cmd_command(hpnfc, HPNFC_GCMD_LAY_INSTR_RDPP, 0, 0);
	if (status)
		return status;

	/* wait for finishing operation */
	status = wait_for_rb_ready(hpnfc);
	if (status)
		return status;

	/* prepare controller to read data in generic mode */
	status = hpnfc_generic_cmd_data(hpnfc, &generic_data);
	if (status)
		return status;

	/* wait until data is read on slave DMA */
	status = hpnfc_wait_on_sdma_trigg(hpnfc, &sdmatrd_num, &sdma_size);
	if (status)
		return status;

	buffer = get_buf_ptr(hpnfc);
	/* read data (part of parameter page) */
	status = dma_read_data(hpnfc, buffer, sdma_size);
	if (status)
		return status;

	increase_buff_ptr(hpnfc, sdma_size);

	return 0;
}

static int read_parameter_page(hpnfc_state_t *hpnfc, uint32_t size)
{
	if (hpnfc->bytesPerSdmaAccess == 8)
		return read_parameter_page64(hpnfc, size);
	else
		return read_parameter_page32(hpnfc, size);
}

static int nf_mem_read_id(hpnfc_state_t *hpnfc, uint8_t address, uint32_t size)
{
	int status;
	uint8_t sdmatrd_num;
	uint32_t sdma_size;
	generic_data_t generic_data;
	uint32_t tmp[4];

	memset(&generic_data, 0, sizeof(generic_data_t));
	generic_data.sec_cnt = 1;
	generic_data.last_sec_size = size;
	generic_data.direction = HPNFC_GCMD_DIR_READ;

	/* execute read ID instruction */
	status = hpnfc_generic_cmd_command(hpnfc, HPNFC_GCMD_LAY_INSTR_RDID,
					   address, 0);
	if (status)
		return status;

	/* wait for finishing operation */
	status = wait_for_rb_ready(hpnfc);
	if (status)
		return status;

	/* prepare controller to read data in generic mode */
	status = hpnfc_generic_cmd_data(hpnfc, &generic_data);
	if (status)
		return status;

	/* wait until data is read on slave DMA */
	status = hpnfc_wait_on_sdma_trigg(hpnfc, &sdmatrd_num, &sdma_size);
	if (status)
		return status;

	/* read data (flash id) */
	status = dma_read_data(hpnfc, tmp, sdma_size);
	if (status)
		return status;

	memcpy(&hpnfc->buf.buf[hpnfc->buf.tail], tmp, sdma_size);
	hpnfc->buf.tail += sdma_size;

	return 0;
}



static void hpnfc_get_dma_data_width(hpnfc_state_t *hpnfc)
{
	uint32_t reg;

	reg = IORD_32(hpnfc->reg + HPNFC_CTRL_FEATURES);

	if (READ_FIELD(reg,  HPNFC_CTRL_FEATURES_DMA_DWITH_64))
		hpnfc->bytesPerSdmaAccess = 8;
	else
		hpnfc->bytesPerSdmaAccess = 4;
}

static int hpnfc_dev_info(hpnfc_state_t *hpnfc)
{
	uint32_t reg;
	struct mtd_info *mtd = nand_to_mtd(&hpnfc->nand);

	reg = IORD_32(hpnfc->reg + HPNFC_DEV_PARAMS_0);
	hpnfc->dev_type = READ_FIELD(reg, HPNFC_DEV_PARAMS_0_DEV_TYPE);

	switch (hpnfc->dev_type) {
	case HPNFC_DEV_PARAMS_0_DEV_TYPE_ONFI:
		dev_info(hpnfc->dev, "Detected ONFI device:\n");
		break;
	case HPNFC_DEV_PARAMS_0_DEV_TYPE_JEDEC:
		dev_info(hpnfc->dev, "Detected JEDEC device:\n");
		break;
	default:
		dev_info(hpnfc->dev, "Device type was not detected.\n");
	}

	hpnfc->spare_size = mtd->oobsize;
	hpnfc->main_size = mtd->writesize;
	dev_info(hpnfc->dev, "-- Page main area size: %u\n", hpnfc->main_size);
	dev_info(hpnfc->dev, "-- Page spare area size: %u\n", hpnfc->spare_size);

	hpnfc->devnum = nanddev_ntargets(&hpnfc->nand.base);
	hpnfc->chip_nr = 0;

	return 0;
}

static void hpnfc_cdma_desc_prepare(hpnfc_cdma_desc_t* cdma_desc, char nf_mem,
				    uint32_t flash_ptr, char* mem_ptr,
				    uint16_t ctype)
{
	memset(cdma_desc, 0, sizeof(hpnfc_cdma_desc_t));

	/* set fields for one descriptor */
	cdma_desc->flash_pointer = (nf_mem << HPNFC_CDMA_CFPTR_MEM_SHIFT)
				   + flash_ptr;
	cdma_desc->command_flags |= HPNFC_CDMA_CF_DMA_MASTER;
	cdma_desc->command_flags  |= HPNFC_CDMA_CF_INT;

	cdma_desc->memory_pointer = (uintptr_t)mem_ptr;

	cdma_desc->command_type = ctype;

	/* ensure desc is written before poking hardware */
	wmb();
}

static uint8_t hpnfc_check_desc_error(uint32_t desc_status)
{
	if (desc_status & HPNFC_CDMA_CS_ERP_MASK)
		return HPNFC_STAT_ERASED;

	if (desc_status & HPNFC_CDMA_CS_UNCE_MASK)
		return HPNFC_STAT_ECC_UNCORR;

	if (desc_status & HPNFC_CDMA_CS_ERR_MASK) {
		pr_err(CADENCE_NAND_NAME ":CDMA descriptor error flag detected.\n");
		return HPNFC_STAT_FAIL;
	}

	if (READ_FIELD(desc_status, HPNFC_CDMA_CS_MAXERR))
		return HPNFC_STAT_ECC_CORR;

	if (desc_status & HPNFC_CDMA_CS_FAIL_MASK)
		return HPNFC_STAT_FAIL;

	return HPNFC_STAT_OK;
}

static int hpnfc_wait_cdma_finish(hpnfc_cdma_desc_t* cdma_desc)
{
	hpnfc_cdma_desc_t *desc_ptr;
	uint32_t desc_status;
	uint8_t status = HPNFC_STAT_BUSY;

	desc_ptr = cdma_desc;
	do {
		desc_status = desc_ptr->status;
		if (desc_status & HPNFC_CDMA_CS_FAIL_MASK) {
			status = hpnfc_check_desc_error(desc_status);
			pr_err(CADENCE_NAND_NAME ":CDMA error %x\n", desc_status);
			break;
		}
		if (desc_status & HPNFC_CDMA_CS_COMP_MASK) {
			/* descriptor finished with no errors */
			if (desc_ptr->command_flags & HPNFC_CDMA_CF_CONT)
				/* not last descriptor */
				desc_ptr =
					(hpnfc_cdma_desc_t*)(uintptr_t)desc_ptr->next_pointer;
			else
				/* last descriptor  */
				status = hpnfc_check_desc_error(desc_status);
		}
	} while (status == HPNFC_STAT_BUSY);

	return status;
}

static int hpnfc_cdma_send(hpnfc_state_t *hpnfc, uint8_t thread)
{
	uint32_t reg = 0;
	int status;

	status = wait_for_thread(hpnfc, thread);
	if (status)
		return status;

	IOWR_32(hpnfc->reg + HPNFC_CMD_REG2, (uint32_t )hpnfc->dma_cdma_desc);
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG3, 0 );

	/* select CDMA mode */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_CT, HPNFC_CMD_REG0_CT_CDMA);
	/* thread number */
	WRITE_FIELD(reg, HPNFC_CMD_REG0_TN, thread);
	/* issue command */
	IOWR_32(hpnfc->reg + HPNFC_CMD_REG0, reg);

	return 0;
}

static uint32_t hpnfc_cdma_send_and_wait(hpnfc_state_t *hpnfc, uint8_t thread)
{
	int status;
	hpnfc_irq_status_t irq_mask, irq_status;

	status = hpnfc_cdma_send(hpnfc, thread);
	if (status)
		return status;

	irq_mask.trd_status = 1 << thread;
	irq_mask.trd_error = 1 << thread;
	irq_mask.status = HPNFC_INTR_STATUS_CDMA_TERR_MASK;
	wait_for_irq(hpnfc, &irq_mask, &irq_status);

	if ((irq_status.status == 0) && (irq_status.trd_status == 0)
	    && (irq_status.trd_error == 0)) {
		dev_err(hpnfc->dev, "CDMA command timeout\n");
		return -ETIMEDOUT;
	}
	if (irq_status.status & irq_mask.status) {
		dev_err(hpnfc->dev, "CDMA command failed\n");
		return -EIO;
	}

	return 0;
}

static int hpnfc_hw_init(hpnfc_state_t *hpnfc)
{
	uint32_t reg, val;
	int status;

#if __FPGA__		//for fpga only
	IOWR_32(hpnfc->reg_peri + 0x0C, 0x03);
	// config emmc PHY
	val = IORD_32(hpnfc->reg_emmc + 0x300);
	IOWR_32(hpnfc->reg_emmc + 0x300, (val & ~1));	// assert PHY reset
	IOWR_32(hpnfc->reg_emmc + 0x300, 0x00880002);	// [19:16] PAD_SP, [23:20] PAD_SN,// [19:16] PAD_SP,
	IOWR_32(hpnfc->reg_emmc + 0x304, 0x06090601);	// [15:0] cmd-READY, [31:16] data-DAT
	IOWR_32(hpnfc->reg_emmc + 0x308, 0x06000600);	// [15:0] clk-WR, [31:16] strobe-CLE
	IOWR_32(hpnfc->reg_emmc + 0x30c, 0x00000600);	// [15:0] rst-RD

	val = IORD_32(hpnfc->reg_emmc + 0x300);
	IOWR_32(hpnfc->reg_emmc + 0x300, (val | 1));	// deassert PHY reset
#endif

	status = wait_for_init_complete(hpnfc);
	if (status)
		return status;

#if 0
	tmp = HPNFC_DMA_SETTINGS_OTE_MASK;
	WRITE_FIELD(tmp, HPNFC_DMA_SETTINGS_BURST_SEL, 127);
	IOWR_32(hpnfc->reg + HPNFC_DMA_SETTINGS, tmp);
#endif

	/* disable cache and multiplane */
	IOWR_32(hpnfc->reg + HPNFC_MULTIPLANE_CFG, 0);
	IOWR_32(hpnfc->reg + HPNFC_CACHE_CFG, 0);

	/* enable interrupts */
	reg = HPNFC_INTR_ENABLE_INTR_EN_MASK
	      | HPNFC_INTR_ENABLE_CDMA_TERR_EN_MASK
	      | HPNFC_INTR_ENABLE_DDMA_TERR_EN_MASK
	      | HPNFC_INTR_ENABLE_UNSUPP_CMD_EN_MASK
	      | HPNFC_INTR_ENABLE_SDMA_TRIGG_EN_MASK
	      | HPNFC_INTR_ENABLE_SDMA_ERR_EN_MASK;
	IOWR_32(hpnfc->reg + HPNFC_INTR_ENABLE, reg);
	/* clear all interrupts */
	IOWR_32(hpnfc->reg + HPNFC_INTR_STATUS, 0xFFFFFFFF);
	/* enable signaling thread error interrupts for all threads  */
	IOWR_32(hpnfc->reg + HPNFC_TRD_ERR_INT_STATUS_EN, 0xFF);

	return 0;
}

static int hpnfc_read_bch_cfg(struct hpnfc_state_t *hpnfc)
{
	uint32_t reg;

	reg = IORD_32(hpnfc->reg + HPNFC_BCH_CFG_0);
	hpnfc->bch_cfg.corr_caps[0] = READ_FIELD(reg, HPNFC_BCH_CFG_0_CORR_CAP_0);
	hpnfc->bch_cfg.corr_caps[1] = READ_FIELD(reg, HPNFC_BCH_CFG_0_CORR_CAP_1);
	hpnfc->bch_cfg.corr_caps[2] = READ_FIELD(reg, HPNFC_BCH_CFG_0_CORR_CAP_2);
	hpnfc->bch_cfg.corr_caps[3] = READ_FIELD(reg, HPNFC_BCH_CFG_0_CORR_CAP_3);

	reg = IORD_32(hpnfc->reg + HPNFC_BCH_CFG_1);
	hpnfc->bch_cfg.corr_caps[4] = READ_FIELD(reg, HPNFC_BCH_CFG_1_CORR_CAP_4);
	hpnfc->bch_cfg.corr_caps[5] = READ_FIELD(reg, HPNFC_BCH_CFG_1_CORR_CAP_5);
	hpnfc->bch_cfg.corr_caps[6] = READ_FIELD(reg, HPNFC_BCH_CFG_1_CORR_CAP_6);
	hpnfc->bch_cfg.corr_caps[7] = READ_FIELD(reg, HPNFC_BCH_CFG_1_CORR_CAP_7);

	reg = IORD_32(hpnfc->reg + HPNFC_BCH_CFG_2);
	hpnfc->bch_cfg.sector_sizes[0] = READ_FIELD(reg, HPNFC_BCH_CFG_2_SECT_0);
	hpnfc->bch_cfg.sector_sizes[1] = READ_FIELD(reg, HPNFC_BCH_CFG_2_SECT_1);

	return 0;
}

/* calculate size of check bit size per one sector */
static int bch_calculate_ecc_size(struct hpnfc_state_t *hpnfc,
				  uint32_t *check_bit_size)
{
	uint8_t mult = 14;
	uint32_t tmp, corr_cap = hpnfc->nand.ecc.strength;
	uint32_t max_sector_size, sector_size = hpnfc->nand.ecc.size;
	int i;

	*check_bit_size = 0;

	for (i = 0; i < HPNFC_BCH_MAX_NUM_SECTOR_SIZES; i++) {
		if (sector_size == hpnfc->bch_cfg.sector_sizes[i]) {
			break;
		}
	}

	if (i >= HPNFC_BCH_MAX_NUM_SECTOR_SIZES) {
		dev_err(hpnfc->dev,
			"Wrong ECC configuration, ECC sector size:%u"
			"is not supported. List of supported sector sizes\n", sector_size);
		for (i = 0; i < HPNFC_BCH_MAX_NUM_SECTOR_SIZES; i++) {
			if (hpnfc->bch_cfg.sector_sizes[i] == 0)
				break;
			dev_err(hpnfc->dev,
				"%u ", hpnfc->bch_cfg.sector_sizes[i]);
		}
		return -1;
	}

	if (hpnfc->bch_cfg.sector_sizes[1] > hpnfc->bch_cfg.sector_sizes[0])
		max_sector_size = hpnfc->bch_cfg.sector_sizes[1];
	else
		max_sector_size = hpnfc->bch_cfg.sector_sizes[0];

	switch (max_sector_size) {
	case 256:
		mult = 12;
		break;
	case 512:
		mult = 13;
		break;
	case 1024:
		mult = 14;
		break;
	case 2048:
		mult = 15;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < HPNFC_BCH_MAX_NUM_CORR_CAPS; i++) {
		if (corr_cap == hpnfc->bch_cfg.corr_caps[i])
			break;
	}

	if (i >= HPNFC_BCH_MAX_NUM_CORR_CAPS) {
		dev_err(hpnfc->dev,
			"Wrong ECC configuration, correction capability:%d"
			"is not supported. List of supported corrections: \n", corr_cap);
		for (i = 0; i < HPNFC_BCH_MAX_NUM_CORR_CAPS; i++) {
			if (hpnfc->bch_cfg.corr_caps[i] == 0)
				break;
			dev_err(hpnfc->dev,
				"%d ", hpnfc->bch_cfg.corr_caps[i]);
		}
		return -1;
	}

	hpnfc->sector_size = sector_size;
	hpnfc->corr_cap = corr_cap;

	tmp = (mult * corr_cap) / 16;
	/* round up */
	if ((tmp * 16) < (mult * corr_cap))
		tmp++;

	/* check bit size per one sector */
	*check_bit_size = 2 * tmp;

	return 0;
}

#define TT_SPARE_AREA		1
#define TT_MAIN_SPARE_AREAS	2
#define TT_RAW_SPARE_AREA	3
#define TT_MAIN_AREA		4
#define TT_RAW_ZOS_SPARE_AREA	5

static int hpnfc_prepare_data_size(hpnfc_state_t *hpnfc, int transfer_type)
{
	uint32_t reg = 0;
	uint32_t sec_size = 0, last_sec_size, offset, sec_cnt;
	uint32_t ecc_size = hpnfc->nand.ecc.bytes;

	if (hpnfc->curr_trans_type == transfer_type)
		return 0;

	switch (transfer_type) {
	case TT_SPARE_AREA:
		offset = hpnfc->main_size - hpnfc->sector_size;
		ecc_size = ecc_size * (offset / hpnfc->sector_size);
		offset = offset + ecc_size;
		sec_cnt = 1;
		last_sec_size = hpnfc->sector_size + hpnfc->usnused_spare_size;
		break;
	case TT_MAIN_SPARE_AREAS:
		offset = 0;
		sec_cnt = hpnfc->sector_count;
		last_sec_size = hpnfc->sector_size + hpnfc->usnused_spare_size;
		sec_size = hpnfc->sector_size;
		break;
	case TT_RAW_SPARE_AREA:
		offset = hpnfc->main_size - hpnfc->sector_size;
		ecc_size = ecc_size * (offset / hpnfc->sector_size);
		offset = offset + ecc_size;
		sec_cnt = 1;
		last_sec_size = hpnfc->usnused_spare_size;
		break;
	case TT_MAIN_AREA:
		offset = 0;
		sec_cnt = hpnfc->sector_count;
		last_sec_size = hpnfc->sector_size;
		sec_size = hpnfc->sector_size;
		break;
	case TT_RAW_ZOS_SPARE_AREA:
		offset = hpnfc->main_size + ecc_size * hpnfc->sector_count;
		sec_cnt = 1;
		last_sec_size = hpnfc->usnused_spare_size;
		break;
	default:
		dev_err(hpnfc->dev, "Data size preparation failed \n");
		return -EINVAL;
	}

	reg = 0;
	WRITE_FIELD(reg, HPNFC_TRAN_CFG_0_OFFSET, offset);
	WRITE_FIELD(reg, HPNFC_TRAN_CFG_0_SEC_CNT, sec_cnt);
	IOWR_32(hpnfc->reg + HPNFC_TRAN_CFG_0, reg);

	reg = 0;
	WRITE_FIELD(reg, HPNFC_TRAN_CFG_1_LAST_SEC_SIZE, last_sec_size);
	WRITE_FIELD(reg, HPNFC_TRAN_CFG_1_SECTOR_SIZE, sec_size);
	IOWR_32(hpnfc->reg + HPNFC_TRAN_CFG_1, reg);

	return 0;
}

/* write data to flash memory using CDMA command */
static int cdma_write_data(struct mtd_info *mtd, int page, bool with_ecc)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	dma_addr_t dma_buf = hpnfc->buf.dma_buf;
	hpnfc_cdma_desc_t *cdma_desc = hpnfc->cdma_desc;
	uint8_t thread_nr = hpnfc->chip_nr;
	int status = 0;

	hpnfc_ecc_check_config(hpnfc, with_ecc & hpnfc->ecc_enabled, page);

	dma_sync_single_for_device(hpnfc->dev, dma_buf,
				   hpnfc->main_size + mtd->oobsize, DMA_TO_DEVICE);

	hpnfc_cdma_desc_prepare(cdma_desc, hpnfc->chip_nr, page, (void*)dma_buf,
				HPNFC_CDMA_CT_WR);

	status = hpnfc_cdma_send_and_wait(hpnfc, thread_nr);

	dma_sync_single_for_cpu(hpnfc->dev, dma_buf,
				hpnfc->main_size + mtd->oobsize, DMA_TO_DEVICE);
	if (status)
		return status;

	status = hpnfc_wait_cdma_finish(hpnfc->cdma_desc);

	if (status == HPNFC_STAT_ECC_CORR) {
		dev_err(hpnfc->dev, "CDMA write operation failed\n");
		status = -EIO;
	}

	return status;
}

/* get corrected ECC errors of last read operation */
static uint32_t get_ecc_count(hpnfc_state_t *hpnfc)
{
	return READ_FIELD(hpnfc->cdma_desc->status, HPNFC_CDMA_CS_MAXERR);
}

/* read data from flash memory using CDMA command */
static int cdma_read_data(struct mtd_info *mtd, int page, bool with_ecc,
			  uint32_t *ecc_err_count)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	dma_addr_t dma_buf = hpnfc->buf.dma_buf;
	hpnfc_cdma_desc_t *cdma_desc = hpnfc->cdma_desc;
	uint8_t thread_nr = hpnfc->chip_nr;
	int status;

	hpnfc_ecc_check_config(hpnfc, with_ecc & hpnfc->ecc_enabled, page);

	dma_sync_single_for_device(hpnfc->dev, dma_buf,
				   hpnfc->main_size + mtd->oobsize,
				   DMA_FROM_DEVICE);

	hpnfc_cdma_desc_prepare(cdma_desc, hpnfc->chip_nr, page, (void*)dma_buf,
				HPNFC_CDMA_CT_RD);

	status = hpnfc_cdma_send_and_wait(hpnfc, thread_nr);

	dma_sync_single_for_cpu(hpnfc->dev, dma_buf,
				hpnfc->main_size + mtd->oobsize, DMA_FROM_DEVICE);

	status = hpnfc_wait_cdma_finish(hpnfc->cdma_desc);
	if (status == HPNFC_STAT_ECC_CORR && ecc_err_count)
		*ecc_err_count = get_ecc_count(hpnfc);

	return status;
}

static int write_zos_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	int status = 0;

	nand_randomize_page(&hpnfc->randomizer, NULL, buf, page);
	memcpy(hpnfc->buf.buf, buf, mtd->oobsize);
	status = hpnfc_prepare_data_size(hpnfc, TT_RAW_ZOS_SPARE_AREA);
	if (status) {
		dev_err(hpnfc->dev, "write oob failed\n");
		return status;
	}

	return cdma_write_data(mtd, page, false);
}

static int write_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	int status = 0;

	/* to protect spare data by ECC
	 * we send also one ECC sector set to 0xFF */
	memset(hpnfc->buf.buf, 0xFF, hpnfc->sector_size);
	nand_randomize_page(&hpnfc->randomizer, NULL, buf, page);
	memcpy(hpnfc->buf.buf + hpnfc->sector_size, buf, mtd->oobsize);
	status = hpnfc_prepare_data_size(hpnfc, TT_SPARE_AREA);
	if (status) {
		dev_err(hpnfc->dev, "write oob failed\n");
		return status;
	}

	return cdma_write_data(mtd, page, true);
}

static int read_zos_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	int status = 0;

	status = hpnfc_prepare_data_size(hpnfc, TT_RAW_ZOS_SPARE_AREA);
	if (status)
		return -EIO;

	status = cdma_read_data(mtd, page, 0, NULL);

	switch (status) {
	case HPNFC_STAT_ERASED:
		memset(buf, 0xff, mtd->oobsize);
		break;
	case HPNFC_STAT_ECC_UNCORR:
	case HPNFC_STAT_OK:
	case HPNFC_STAT_ECC_CORR:
		memcpy(buf, hpnfc->buf.buf, mtd->oobsize);
		nand_randomize_page(&hpnfc->randomizer, NULL, buf, page);
		break;
	default:
		dev_err(hpnfc->dev, "read oob failed\n");
		return -EIO;
	}

	return 0;
}

static int read_oob_data(struct mtd_info *mtd, uint8_t *buf, int page)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	int status = 0;

	status = hpnfc_prepare_data_size(hpnfc, TT_SPARE_AREA);
	if (status)
		return -EIO;

	status = cdma_read_data(mtd, page, 1, NULL);

	switch (status) {
	case HPNFC_STAT_ERASED:
		memset(buf, 0xff, mtd->oobsize);
		break;
	case HPNFC_STAT_ECC_UNCORR:
		status = hpnfc_prepare_data_size(hpnfc, TT_RAW_SPARE_AREA);
		if (status) {
			return -EIO;
		}
		status = cdma_read_data(mtd, page, 0, NULL);
		if (status) {
			dev_err(hpnfc->dev, "read oob failed\n");
			return -EIO;
		}
		memcpy(buf, hpnfc->buf.buf, mtd->oobsize);
		nand_randomize_page(&hpnfc->randomizer, NULL, buf, page);
		break;
	case HPNFC_STAT_OK:
	case HPNFC_STAT_ECC_CORR:
		memcpy(buf, hpnfc->buf.buf + hpnfc->sector_size, mtd->oobsize);
		nand_randomize_page(&hpnfc->randomizer, NULL, buf, page);
		break;
	default:
		dev_err(hpnfc->dev, "read oob failed\n");
		return -EIO;
	}

	return 0;
}

/*
 * writes a page. user specifies type, and this function handles the
 * configuration details.
 */
static int write_page(struct mtd_info *mtd, struct nand_chip *chip,
		      const uint8_t *buf, bool oob, bool with_ecc,
		      int page)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);
	int status = 0;

	memcpy(hpnfc->buf.buf, buf, mtd->writesize);
	nand_randomize_page(&hpnfc->randomizer, hpnfc->buf.buf,
			    oob? chip->oob_poi: NULL, page);

	if (oob)
		/* transfer the data to the spare area */
		memcpy(hpnfc->buf.buf + mtd->writesize, chip->oob_poi, mtd->oobsize);
	else
		/* just set spare data to 0xFF */
		memset(hpnfc->buf.buf + mtd->writesize, 0xFF, mtd->oobsize);

	if (unlikely(page < hpnfc->zos_page_end))
		status = hpnfc_prepare_data_size(hpnfc, TT_MAIN_AREA);
	else
		status = hpnfc_prepare_data_size(hpnfc, TT_MAIN_SPARE_AREAS);

	if (status) {
		dev_err(hpnfc->dev, "write page failed\n");
		return -EIO;
	}

	return cdma_write_data(mtd, page, with_ecc);
}

static int hpnfc_write_page(struct nand_chip *chip,
			    const uint8_t *buf, int oob_required, int page)
{
	/*
	 * for regular page writes, we let HW handle all the ECC
	 * data written to the device.
	 */
	return write_page(&chip->base.mtd, chip, buf, oob_required ? true : false,
			  true, page);
}

static int hpnfc_write_page_raw(struct nand_chip *chip,
				const uint8_t *buf, int oob_required, int page)
{
	/*
	 * for raw page writes, we want to disable ECC and simply write
	 * whatever data is in the buffer.
	 */
	return write_page(&chip->base.mtd, chip, buf, oob_required ? true : false,
			  false, page);
}

static int hpnfc_write_oob(struct nand_chip *chip, int page)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(chip);
	struct mtd_info *mtd = &chip->base.mtd;

	if (unlikely(page < hpnfc->zos_page_end))
		return write_zos_oob_data(mtd, chip->oob_poi, page);
	return write_oob_data(mtd, chip->oob_poi, page);
}

static int hpnfc_read_oob(struct nand_chip *chip, int page)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(chip);
	struct mtd_info *mtd = &chip->base.mtd;

	if (unlikely(page < hpnfc->zos_page_end))
		return read_zos_oob_data(mtd, chip->oob_poi, page);
	return read_oob_data(mtd, chip->oob_poi, page);
}

static int hpnfc_read_page(struct nand_chip *chip,
			   uint8_t *buf, int oob_required, int page)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(chip);
	struct mtd_info *mtd = &chip->base.mtd;
	int status = 0;
	uint32_t ecc_err_count = 0;

	if (unlikely(page < hpnfc->zos_page_end))
		status = hpnfc_prepare_data_size(hpnfc, TT_MAIN_AREA);
	else
		status = hpnfc_prepare_data_size(hpnfc, TT_MAIN_SPARE_AREAS);
	if (status)
		return -EIO;

	status = cdma_read_data(mtd, page, 1, &ecc_err_count);
	switch (status) {
	case HPNFC_STAT_ERASED:
		memset(buf, 0xff, mtd->writesize);
		if (oob_required)
			memset(chip->oob_poi, 0xff, mtd->oobsize);
		break;
	case HPNFC_STAT_ECC_UNCORR:
		status = cdma_read_data(mtd, page, 0, NULL);
		if (status) {
			dev_err(hpnfc->dev,
				"read page w/o ecc still failed:%d\n", status);
			return -EIO;
		}

		status = nand_check_erased_ecc_chunk(hpnfc->buf.buf,
					mtd->writesize + mtd->oobsize,
					NULL, 0,
					NULL, 0,
					chip->ecc.strength);
		if (status < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += status;
			ecc_err_count = status;
		}

		memcpy(buf, hpnfc->buf.buf, mtd->writesize);
		if (oob_required)
			memcpy(chip->oob_poi, hpnfc->buf.buf + mtd->writesize,
			       mtd->oobsize);
		break;
	case HPNFC_STAT_ECC_CORR:
		if (ecc_err_count)
			mtd->ecc_stats.corrected += ecc_err_count;
		break;
	case HPNFC_STAT_OK:
		memcpy(buf, hpnfc->buf.buf, mtd->writesize);
		if (oob_required)
			memcpy(chip->oob_poi, hpnfc->buf.buf + mtd->writesize,
			       mtd->oobsize);
		nand_randomize_page(&hpnfc->randomizer, buf,
				    oob_required ? chip->oob_poi: NULL, page);
		break;
	default:
		dev_err(hpnfc->dev, "read page failed:%d\n", status);
		return -EIO;
	}

	return ecc_err_count;
}

static int hpnfc_read_page_raw(struct nand_chip *chip,
			       uint8_t *buf, int oob_required, int page)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(chip);
	struct mtd_info *mtd = &chip->base.mtd;
	int pages_per_block, status, with_ecc;

	/* block 0 special handling */
	pages_per_block = 1 << (chip->phys_erase_shift - chip->page_shift);
	if (unlikely(page < pages_per_block))
		with_ecc = 0;
	else
		with_ecc = 1;

	if (unlikely(page < hpnfc->zos_page_end))
		status = hpnfc_prepare_data_size(hpnfc, TT_MAIN_AREA);
	else
		status = hpnfc_prepare_data_size(hpnfc, TT_MAIN_SPARE_AREAS);
	if (status)
		return -EIO;

	status = cdma_read_data(mtd, page, with_ecc, NULL);
	switch (status) {
	case HPNFC_STAT_ERASED:
		memset(hpnfc->buf.buf, 0xff, mtd->writesize + mtd->oobsize);
		if (oob_required)
			memset(chip->oob_poi, 0xff, mtd->oobsize);
		break;
	case HPNFC_STAT_ECC_UNCORR:
		status = cdma_read_data(mtd, page, 0, NULL);
		if (status) {
			dev_err(hpnfc->dev, "read page failed\n");
			return -EIO;
		}

		nand_check_erased_ecc_chunk(hpnfc->buf.buf,
				mtd->writesize + mtd->oobsize,
				NULL, 0,
				NULL, 0,
				chip->ecc.strength);

		memcpy(buf, hpnfc->buf.buf, mtd->writesize);
		if (oob_required)
			memcpy(chip->oob_poi, hpnfc->buf.buf + mtd->writesize,
				mtd->oobsize);
		break;
	case HPNFC_STAT_ECC_CORR:
	case HPNFC_STAT_OK:
		memcpy(buf, hpnfc->buf.buf, mtd->writesize);
		if (oob_required)
			memcpy(chip->oob_poi, hpnfc->buf.buf + mtd->writesize,
				mtd->oobsize);
		nand_randomize_page(&hpnfc->randomizer, buf,
				    oob_required ? chip->oob_poi: NULL, page);
		break;
	default:
		dev_err(hpnfc->dev, "read raw page failed\n");
		return -EIO;
	}

	return 0;
}


static uint8_t hpnfc_read_byte(struct nand_chip *chip)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(chip);
	uint8_t result = 0xff;

	if (hpnfc->buf.head < hpnfc->buf.tail)
		result = hpnfc->buf.buf[hpnfc->buf.head++];
	return result;
}

static void hpnfc_select_chip(struct nand_chip *nand, int chip)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(nand);

	hpnfc->chip_nr = chip;
}

static int hpnfc_waitfunc(struct nand_chip *chip)
{
	return 0;
}

static void hpnfc_cmdfunc(struct nand_chip *nand, unsigned int cmd, int col,
			  int page)
{
	struct hpnfc_state_t *hpnfc = nand_to_hpnfc(nand);
	uint32_t reg;

	hpnfc->offset = 0;
	switch (cmd) {
	case NAND_CMD_PAGEPROG:
		break;
	case NAND_CMD_STATUS:
		reset_buf(hpnfc);
		reg = IORD_32(hpnfc->reg + HPNFC_RBN_SETTINGS);
		if ((reg >> hpnfc->chip_nr) & 0x01)
			write_byte_to_buf(hpnfc, 0xE0);
		else
			write_byte_to_buf(hpnfc, 0x80);
		break;
	case NAND_CMD_READID:
		reset_buf(hpnfc);
		nf_mem_read_id(hpnfc, col, 8);
		break;
	case NAND_CMD_PARAM:
		reset_buf(hpnfc);
		read_parameter_page(hpnfc, 4096);
		break;
	case NAND_CMD_READ0:
	case NAND_CMD_SEQIN:
		break;
	case NAND_CMD_RESET:
		/* resets a specific device connected to the core */
		break;
	case NAND_CMD_READOOB:
		break;
	case NAND_CMD_RNDOUT:
		hpnfc->offset = col;
		break;
	default:
		dev_warn(hpnfc->dev, "unsupported command received 0x%x\n", cmd);
		break;
	}
}

static int hpnfc_ooblayout_ecc(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	return -ENOTSUPP;
}

static int hpnfc_ooblayout_free(struct mtd_info *mtd, int section,
			        struct mtd_oob_region *oobregion)
{
	struct hpnfc_state_t *hpnfc = mtd_to_hpnfc(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = 2;
	oobregion->length = hpnfc->usnused_spare_size - 2;

	return 0;
}

static const struct mtd_ooblayout_ops hpnfc_ooblayout_ops = {
	.ecc = hpnfc_ooblayout_ecc,
	.free = hpnfc_ooblayout_free,
};

static int cadence_hpnfc_init(struct hpnfc_state_t *hpnfc)
{
	u32 val;
	int ret, status;
	uint32_t ecc_per_sec_size;
	uint8_t timing_mode;
	uint8_t work_mode;
	struct mtd_info *mtd = nand_to_mtd(&hpnfc->nand);

	hpnfc->buf.buf = devm_kzalloc(hpnfc->dev, 16 * 1024, GFP_DMA | GFP_KERNEL);
	if (!hpnfc->buf.buf)
		return -ENOMEM;

	hpnfc->cdma_desc = dmam_alloc_coherent(hpnfc->dev,
					       sizeof(hpnfc_cdma_desc_t),
					       &hpnfc->dma_cdma_desc,
					       GFP_KERNEL | GFP_DMA);
	if (!hpnfc->cdma_desc)
		return -ENOMEM;

	if (devm_request_irq(hpnfc->dev, hpnfc->irq, hpnfc_isr, IRQF_SHARED,
			     CADENCE_NAND_NAME, hpnfc)) {
		dev_err(hpnfc->dev, "Unable to allocate IRQ\n");
		return -ENODEV;
	}

	/* now that our ISR is registered, we can enable interrupts */
	mtd->name = CADENCE_NAND_NAME;
	mtd->priv = &hpnfc->nand;
	mtd->dev.parent = hpnfc->dev;

	/* register the driver with the NAND core subsystem */
	hpnfc->nand.legacy.select_chip = hpnfc_select_chip;
	hpnfc->nand.legacy.cmdfunc = hpnfc_cmdfunc;
	hpnfc->nand.legacy.read_byte = hpnfc_read_byte;
	hpnfc->nand.legacy.waitfunc = hpnfc_waitfunc;
	hpnfc->nand.legacy.read_buf = hpnfc_read_buf;
	hpnfc->nand.legacy.set_features = nand_get_set_features_notsupp;
	hpnfc->nand.legacy.get_features = nand_get_set_features_notsupp;

	ret = hpnfc_hw_init(hpnfc);
	if (ret)
		goto failed_req_irq;

	hpnfc_get_dma_data_width(hpnfc);
	hpnfc_read_bch_cfg(hpnfc);

	spin_lock_init(&hpnfc->irq_lock);
	init_completion(&hpnfc->complete);

	hpnfc->nand.ecc.engine_type = NAND_ECC_ENGINE_TYPE_NONE;

	hpnfc_set_work_mode(hpnfc, HPNFC_WORK_MODE_ASYNC, 0);
	/*
	 * scan for NAND devices attached to the controller
	 * this is the first stage in a two step process to register
	 * with the nand subsystem
	 */
	dev_info(hpnfc->dev, "Start scanning...\n");
	/* Scan to find existence of the device */
	if (nand_scan(&hpnfc->nand, 1)) {
		dev_warn(hpnfc->dev, "nand_scan failed. Try again\n");
		/* set async timing to maximum and try once again */
		IOWR_32(hpnfc->reg + HPNFC_ASYNC_TOGGLE_TIMINGS, 0x18181818);
		ret = nand_scan(&hpnfc->nand, 1);
		if (ret) {
			dev_warn(hpnfc->dev, "nand_scan failed\n");
			goto failed_req_irq;
		}
	}
	dev_info(hpnfc->dev, "Scanning finished.\n");

	if (!of_property_read_u32(hpnfc->dev->of_node, "zos-end", &val))
		hpnfc->zos_page_end = (int)(val >> hpnfc->nand.page_shift);

	/* Get info about memory parameters  */
	if (hpnfc_dev_info(hpnfc)) {
		dev_err(hpnfc->dev, "HW controller dev info failed\n");
		ret = -ENXIO;
		goto failed_req_irq;
	}

	/* allocate the right size buffer now */
	devm_kfree(hpnfc->dev, hpnfc->buf.buf);
	hpnfc->buf.buf = devm_kzalloc(hpnfc->dev,
				      mtd->writesize + mtd->oobsize,
				      GFP_DMA | GFP_KERNEL);
	if (!hpnfc->buf.buf) {
		ret = -ENOMEM;
		goto failed_req_irq;
	}

	/* Is 32-bit DMA supported? */
	ret = dma_set_mask(hpnfc->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(hpnfc->dev, "no usable DMA configuration\n");
		goto failed_req_irq;
	}

	hpnfc->buf.dma_buf =
		dma_map_single(hpnfc->dev, hpnfc->buf.buf,
			       mtd->writesize + mtd->oobsize,
			       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hpnfc->dev, hpnfc->buf.dma_buf)) {
		dev_err(hpnfc->dev, "Failed to map DMA buffer\n");
		ret = -EIO;
		goto failed_req_irq;
	}

	/* Bad block management */
	hpnfc->nand.bbt_options |= NAND_BBT_USE_FLASH;
	hpnfc->nand.options |= NAND_NO_SUBPAGE_WRITE;

	/* Error correction */
	status = bch_calculate_ecc_size(hpnfc, &ecc_per_sec_size);
	if (status) {
		hpnfc->ecc_enabled = 0;
		hpnfc->corr_cap = 0;
		hpnfc->sector_count = 1;
		hpnfc->sector_size = hpnfc->main_size;
		hpnfc->nand.ecc.strength = 2;
	} else {
		dev_info(hpnfc->dev,
			 "ECC enabled, correction capability: %d, sector size %u\n",
			 hpnfc->corr_cap, hpnfc->sector_size);
		hpnfc->ecc_enabled = 1;
		hpnfc->sector_count = hpnfc->main_size / hpnfc->sector_size;
	}
	IOWR_32(hpnfc->reg + HPNFC_ECC_CONFIG_1, 0);

	if ((hpnfc->sector_count * ecc_per_sec_size) >=
	    (hpnfc->spare_size - HPNFC_MINIMUM_SPARE_SIZE)) {
		/* to small spare area to hanlde such big ECC */
		ret = -EIO;
		goto failed_req_irq;
	}

	hpnfc->usnused_spare_size = hpnfc->spare_size
				    - hpnfc->sector_count * ecc_per_sec_size;

	if (hpnfc->usnused_spare_size > HPNFC_MAX_SPARE_SIZE_PER_SECTOR)
		hpnfc->usnused_spare_size = HPNFC_MAX_SPARE_SIZE_PER_SECTOR;

	hpnfc->nand.ecc.bytes = ecc_per_sec_size;
	mtd_set_ooblayout(mtd, &hpnfc_ooblayout_ops);
	/* override the default read operations */
	hpnfc->nand.ecc.read_page = hpnfc_read_page;
	hpnfc->nand.ecc.read_page_raw = hpnfc_read_page_raw;
	hpnfc->nand.ecc.write_page = hpnfc_write_page;
	hpnfc->nand.ecc.write_page_raw = hpnfc_write_page_raw;
	hpnfc->nand.ecc.read_oob = hpnfc_read_oob;
	hpnfc->nand.ecc.write_oob = hpnfc_write_oob;

	dev_info(hpnfc->dev, "mtd->writesize %u, mtd->oobsize %u\n",
		 mtd->writesize, mtd->oobsize);
	dev_info(hpnfc->dev, "mtd->erasesize 0x%x, mtd->size 0x%llx\n",
		 mtd->erasesize, mtd->size);

	ret = nand_randomize_init(&hpnfc->randomizer, mtd->erasesize,
				  mtd->writesize, hpnfc->usnused_spare_size,
				  hpnfc->random_data, RANDOM_DATA_LENGTH,
				  hpnfc->zos_page_end);
	if (ret)
		goto failed_req_irq;

	if (disable_ddr == 0) {
		hpnfc_check_the_best_mode(hpnfc, &work_mode, &timing_mode);
		status = hpnfc_set_work_mode(hpnfc, work_mode, timing_mode);
		if (status) {
			ret = -EIO;
			goto failed_req_irq;
		}
	}

	status = hpnfc_config_phy_timing(hpnfc);
	if (status) {
		ret = -EIO;
		goto failed_req_irq;
	}

	/* HW handles all ECC, so hide real spare size from MTD layer */
	mtd->oobsize = hpnfc->usnused_spare_size;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(hpnfc->dev, "Failed to register MTD: %d\n",
			ret);
		goto failed_req_irq;
	}
	return 0;

failed_req_irq:
	hpnfc_irq_cleanup(hpnfc->irq, hpnfc);

	return ret;

}

static void cadence_hpnfc_remove(struct hpnfc_state_t *hpnfc)
{
	struct mtd_info *mtd = nand_to_mtd(&hpnfc->nand);

	hpnfc_irq_cleanup(hpnfc->irq, hpnfc);
	dma_unmap_single(hpnfc->dev, hpnfc->buf.dma_buf,
			 mtd->writesize + mtd->oobsize,
			 DMA_BIDIRECTIONAL);
}

struct cadence_hpnfc_dt {
	struct hpnfc_state_t hpnfc;
	struct clk *clk;
	struct clk *ecc_clk;
	struct clk *sys_clk;
	struct reset_control *rst;
	struct reset_control *reg_rst;
};

static const struct of_device_id cadence_hpnfc_dt_ids[] = {
	{ .compatible = "cdns,hpnfc-dt" },
	{}
};
MODULE_DEVICE_TABLE(of, cadence_hpnfc_dt_ids);

static int cadence_hpnfc_dt_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct cadence_hpnfc_dt *dt;
	struct hpnfc_state_t *hpnfc;
	struct device *dev = &pdev->dev;
	int ret;

	dt = devm_kzalloc(dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;
	hpnfc = &dt->hpnfc;

	if (IS_ENABLED(CONFIG_MTD_NAND_RANDOMIZER)) {
		hpnfc->random_data = devm_kzalloc(dev, RANDOM_DATA_LENGTH,
						  GFP_KERNEL);
		if (!hpnfc->random_data)
			return -ENOMEM;
	}

	hpnfc->dev = dev;
	nand_set_flash_node(&hpnfc->nand, dev->of_node);

	dt->rst = devm_reset_control_get_optional(dev, "host");
	if (IS_ERR(dt->rst))
		return PTR_ERR(dt->rst);
	dt->reg_rst = devm_reset_control_get_optional(dev, "reg");
	if (IS_ERR(dt->reg_rst))
		return PTR_ERR(dt->reg_rst);

	reset_control_reset(dt->rst);
	reset_control_reset(dt->reg_rst);

	hpnfc->irq = platform_get_irq(pdev, 0);
	if (hpnfc->irq < 0) {
		dev_err(dev, "no irq defined\n");
		return hpnfc->irq;
	}
	dev_info(dev, "IRQ: nr %d\n", hpnfc->irq );

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hpnfc->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(hpnfc->reg)) {
		dev_err(dev, "devm_ioremap_resource res 0 failed\n");
		return PTR_ERR(hpnfc->reg);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	hpnfc->slave_dma = devm_ioremap_resource(dev, res);
	if (IS_ERR(hpnfc->slave_dma)) {
		dev_err(dev, "devm_ioremap_resource res 1 failed\n");
		return PTR_ERR(hpnfc->slave_dma);
	}

#if __FPGA__ 	//for fpga only
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	hpnfc->reg_emmc = devm_ioremap_resource(dev, res);
	if (IS_ERR(hpnfc->reg_emmc)) {
		dev_err(dev, "devm_ioremap_resource res 2 failed\n");
		return PTR_ERR(hpnfc->reg_emmc);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	hpnfc->reg_peri = devm_ioremap_resource(dev, res);
	if (IS_ERR(hpnfc->reg_peri)) {
		dev_err(dev, "devm_ioremap_resource res 3 failed\n");
		return PTR_ERR(hpnfc->reg_peri);
	}
#endif

	dt->clk = devm_clk_get(dev, "core");
	if (!IS_ERR(dt->clk))
		clk_prepare_enable(dt->clk);

	dt->ecc_clk = devm_clk_get(dev, "ecc");
	if (!IS_ERR(dt->ecc_clk))
		clk_prepare_enable(dt->ecc_clk);

	dt->sys_clk = devm_clk_get(dev, "sys");
	if (!IS_ERR(dt->sys_clk))
		clk_prepare_enable(dt->sys_clk);

	ret = cadence_hpnfc_init(hpnfc);
	if (ret)
		goto fail_clk;

	platform_set_drvdata(pdev, dt);
	return 0;

fail_clk:
	clk_disable_unprepare(dt->clk);
	clk_disable_unprepare(dt->ecc_clk);
	clk_disable_unprepare(dt->sys_clk);
	return ret;
}

static int cadence_hpnfc_dt_remove(struct platform_device *pdev)
{
	struct cadence_hpnfc_dt *dt = platform_get_drvdata(pdev);

	cadence_hpnfc_remove(&dt->hpnfc);
	clk_disable_unprepare(dt->clk);
	clk_disable_unprepare(dt->ecc_clk);
	clk_disable_unprepare(dt->sys_clk);

	return 0;
}

static struct platform_driver cadence_hpnfc_dt_driver = {
	.probe			= cadence_hpnfc_dt_probe,
	.remove			= cadence_hpnfc_dt_remove,
	.driver			= {
		.name		= CADENCE_NAND_NAME,
		.of_match_table = cadence_hpnfc_dt_ids,
	},
};
module_platform_driver(cadence_hpnfc_dt_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cadence");
MODULE_DESCRIPTION("DT driver for Cadence NAND flash controller");
