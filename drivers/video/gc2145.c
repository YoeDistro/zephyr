/*
 * Copyright (c) 2024 Felipe Neves
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT galaxycore_gc2145
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dt-bindings/video/video-interfaces.h>
#include <zephyr/logging/log.h>

#include "video_ctrls.h"
#include "video_device.h"

LOG_MODULE_REGISTER(video_gc2145, CONFIG_VIDEO_LOG_LEVEL);

#define GC2145_REG_AMODE1               0x17
#define GC2145_AMODE1_WINDOW_MASK       0xFC
#define GC2145_REG_AMODE1_DEF           0x14
#define GC2145_REG_OUTPUT_FMT           0x84
#define GC2145_REG_OUTPUT_FMT_MASK      0x1F
#define GC2145_REG_OUTPUT_FMT_RGB565    0x06
#define GC2145_REG_OUTPUT_FMT_YCBYCR    0x02
#define GC2145_REG_SYNC_MODE            0x86
#define GC2145_REG_SYNC_MODE_DEF        0x23
#define GC2145_REG_SYNC_MODE_COL_SWITCH 0x10
#define GC2145_REG_SYNC_MODE_ROW_SWITCH 0x20
#define GC2145_REG_RESET                0xFE
#define GC2145_REG_SW_RESET             0x80
#define GC2145_SET_P0_REGS              0x00
#define GC2145_REG_CROP_ENABLE          0x90
#define GC2145_CROP_SET_ENABLE          0x01
#define GC2145_REG_BLANK_WINDOW_BASE    0x09
#define GC2145_REG_WINDOW_BASE          0x91
#define GC2145_REG_SUBSAMPLE            0x99
#define GC2145_REG_SUBSAMPLE_MODE       0x9A
#define GC2145_SUBSAMPLE_MODE_SMOOTH    0x0E

/* MIPI-CSI registers - on page 3 */
#define GC2145_REG_DPHY_MODE1		0x01
#define GC2145_DPHY_MODE1_CLK_EN	BIT(0)
#define GC2145_DPHY_MODE1_LANE0_EN	BIT(1)
#define GC2145_DPHY_MODE1_LANE1_EN	BIT(2)
#define GC2145_DPHY_MODE1_CLK_LANE_P2S_SEL	BIT(7)

#define GC2145_REG_DPHY_MODE2	0x02
#define GC2145_DPHY_MODE2_CLK_DIFF(a)		((a) & 0x07)
#define GC2145_DPHY_MODE2_LANE0_DIFF(a)	(((a) & 0x07) << 4)

#define GC2145_REG_DPHY_MODE3	0x03
#define GC2145_DPHY_MODE3_LANE1_DIFF(a)	((a) & 0x07)
#define GC2145_DPHY_MODE3_CLK_DELAY		BIT(4)
#define GC2145_DPHY_MODE3_LANE0_DELAY		BIT(5)
#define GC2145_DPHY_MODE3_LANE1_DELAY		BIT(6)

#define GC2145_REG_FIFO_FULL_LVL_LOW	0x04
#define GC2145_REG_FIFO_FULL_LVL_HIGH	0x05
#define GC2145_REG_FIFO_MODE		0x06
#define GC2145_FIFO_MODE_READ_GATE	BIT(3)
#define GC2145_FIFO_MODE_MIPI_CLK_MODULE	BIT(7)

#define GC2145_REG_BUF_CSI2_MODE	0x10
#define GC2145_CSI2_MODE_DOUBLE		BIT(0)
#define GC2145_CSI2_MODE_RAW8		BIT(2)
#define GC2145_CSI2_MODE_MIPI_EN	BIT(4)
#define GC2145_CSI2_MODE_EN		BIT(7)

#define GC2145_REG_MIPI_DT		0x11
#define GC2145_REG_LWC_LOW		0x12
#define GC2145_REG_LWC_HIGH		0x13
#define GC2145_REG_DPHY_MODE		0x15
#define GC2145_DPHY_MODE_TRIGGER_PROG	BIT(4)

#define GC2145_REG_FIFO_GATE_MODE	0x17
#define GC2145_REG_T_LPX		0x21
#define GC2145_REG_T_CLK_HS_PREPARE	0x22
#define GC2145_REG_T_CLK_ZERO		0x23
#define GC2145_REG_T_CLK_PRE		0x24
#define GC2145_REG_T_CLK_POST		0x25
#define GC2145_REG_T_CLK_TRAIL		0x26
#define GC2145_REG_T_HS_EXIT		0x27
#define GC2145_REG_T_WAKEUP		0x28
#define GC2145_REG_T_HS_PREPARE		0x29
#define GC2145_REG_T_HS_ZERO		0x2a
#define GC2145_REG_T_HS_TRAIL		0x2b

#define UXGA_HSIZE 1600
#define UXGA_VSIZE 1200

struct gc2145_reg {
	uint8_t addr;
	uint8_t value;
};

static const struct gc2145_reg default_regs[] = {
	{0xfe, 0xf0},
	{0xfe, 0xf0},
	{0xfe, 0xf0},
	{0xfc, 0x06},
	{0xf6, 0x00},
	{0xf7, 0x1d},
	{0xf8, 0x85},
	{0xfa, 0x00},
	{0xf9, 0xfe},
	{0xf2, 0x00},

	/* ISP settings */
	{0xfe, 0x00},
	{0x03, 0x04},
	{0x04, 0xe2},

	{0x09, 0x00},
	{0x0a, 0x00},

	{0x0b, 0x00},
	{0x0c, 0x00},

	{0x0d, 0x04}, /* Window height */
	{0x0e, 0xc0},

	{0x0f, 0x06}, /* Window width */
	{0x10, 0x52},

	{0x99, 0x11}, /* Subsample */
	{0x9a, 0x0E}, /* Subsample mode */

	{0x12, 0x2e},
	{0x17, 0x14}, /* Analog Mode 1 (vflip/mirror[1:0]) */
	{0x18, 0x22}, /* Analog Mode 2 */
	{0x19, 0x0e},
	{0x1a, 0x01},
	{0x1b, 0x4b},
	{0x1c, 0x07},
	{0x1d, 0x10},
	{0x1e, 0x88},
	{0x1f, 0x78},
	{0x20, 0x03},
	{0x21, 0x40},
	{0x22, 0xa0},
	{0x24, 0x16},
	{0x25, 0x01},
	{0x26, 0x10},
	{0x2d, 0x60},
	{0x30, 0x01},
	{0x31, 0x90},
	{0x33, 0x06},
	{0x34, 0x01},
	{0x80, 0x7f},
	{0x81, 0x26},
	{0x82, 0xfa},
	{0x83, 0x00},
	{GC2145_REG_OUTPUT_FMT, 0x06},
	{GC2145_REG_SYNC_MODE, 0x23},
	{0x88, 0x03},
	{0x89, 0x03},
	{0x85, 0x08},
	{0x8a, 0x00},
	{0x8b, 0x00},
	{0xb0, 0x55},
	{0xc3, 0x00},
	{0xc4, 0x80},
	{0xc5, 0x90},
	{0xc6, 0x3b},
	{0xc7, 0x46},
	{0xec, 0x06},
	{0xed, 0x04},
	{0xee, 0x60},
	{0xef, 0x90},
	{0xb6, 0x01},

	{0x90, 0x01},
	{0x91, 0x00},
	{0x92, 0x00},
	{0x93, 0x00},
	{0x94, 0x00},
	{0x95, 0x02},
	{0x96, 0x58},
	{0x97, 0x03},
	{0x98, 0x20},
	{0x99, 0x22},
	{0x9a, 0x0E},

	{0x9b, 0x00},
	{0x9c, 0x00},
	{0x9d, 0x00},
	{0x9e, 0x00},
	{0x9f, 0x00},
	{0xa0, 0x00},
	{0xa1, 0x00},
	{0xa2, 0x00},

	/* BLK Settings */
	{0xfe, 0x00},
	{0x40, 0x42},
	{0x41, 0x00},
	{0x43, 0x5b},
	{0x5e, 0x00},
	{0x5f, 0x00},
	{0x60, 0x00},
	{0x61, 0x00},
	{0x62, 0x00},
	{0x63, 0x00},
	{0x64, 0x00},
	{0x65, 0x00},
	{0x66, 0x20},
	{0x67, 0x20},
	{0x68, 0x20},
	{0x69, 0x20},
	{0x76, 0x00},
	{0x6a, 0x08},
	{0x6b, 0x08},
	{0x6c, 0x08},
	{0x6d, 0x08},
	{0x6e, 0x08},
	{0x6f, 0x08},
	{0x70, 0x08},
	{0x71, 0x08},
	{0x76, 0x00},
	{0x72, 0xf0},
	{0x7e, 0x3c},
	{0x7f, 0x00},
	{0xfe, 0x02},
	{0x48, 0x15},
	{0x49, 0x00},
	{0x4b, 0x0b},
	{0xfe, 0x00},

	/* AEC Settings */
	{0xfe, 0x01},
	{0x01, 0x04},
	{0x02, 0xc0},
	{0x03, 0x04},
	{0x04, 0x90},
	{0x05, 0x30},
	{0x06, 0x90},
	{0x07, 0x30},
	{0x08, 0x80},
	{0x09, 0x00},
	{0x0a, 0x82},
	{0x0b, 0x11},
	{0x0c, 0x10},
	{0x11, 0x10},
	{0x13, 0x68},
	{GC2145_REG_OUTPUT_FMT, 0x00},
	{0x1c, 0x11},
	{0x1e, 0x61},
	{0x1f, 0x35},
	{0x20, 0x40},
	{0x22, 0x40},
	{0x23, 0x20},
	{0xfe, 0x02},
	{0x0f, 0x04},
	{0xfe, 0x01},
	{0x12, 0x30},
	{0x15, 0xb0},
	{0x10, 0x31},
	{0x3e, 0x28},
	{0x3f, 0xb0},
	{0x40, 0x90},
	{0x41, 0x0f},
	{0xfe, 0x02},
	{0x90, 0x6c},
	{0x91, 0x03},
	{0x92, 0xcb},
	{0x94, 0x33},
	{0x95, 0x84},
	{0x97, 0x65},
	{0xa2, 0x11},
	{0xfe, 0x00},
	{0xfe, 0x02},
	{0x80, 0xc1},
	{0x81, 0x08},
	{0x82, 0x05},
	{0x83, 0x08},
	{GC2145_REG_OUTPUT_FMT, 0x0a},
	{GC2145_REG_SYNC_MODE, 0xf0},
	{0x87, 0x50},
	{0x88, 0x15},
	{0x89, 0xb0},
	{0x8a, 0x30},
	{0x8b, 0x10},
	{0xfe, 0x01},
	{0x21, 0x04},
	{0xfe, 0x02},
	{0xa3, 0x50},
	{0xa4, 0x20},
	{0xa5, 0x40},
	{0xa6, 0x80},
	{0xab, 0x40},
	{0xae, 0x0c},
	{0xb3, 0x46},
	{0xb4, 0x64},
	{0xb6, 0x38},
	{0xb7, 0x01},
	{0xb9, 0x2b},
	{0x3c, 0x04},
	{0x3d, 0x15},
	{0x4b, 0x06},
	{0x4c, 0x20},
	{0xfe, 0x00},

	/* Gamma Control */
	{0xfe, 0x02},
	{0x10, 0x09},
	{0x11, 0x0d},
	{0x12, 0x13},
	{0x13, 0x19},
	{0x14, 0x27},
	{0x15, 0x37},
	{0x16, 0x45},
	{GC2145_REG_OUTPUT_FMT, 0x53},
	{0x18, 0x69},
	{0x19, 0x7d},
	{0x1a, 0x8f},
	{0x1b, 0x9d},
	{0x1c, 0xa9},
	{0x1d, 0xbd},
	{0x1e, 0xcd},
	{0x1f, 0xd9},
	{0x20, 0xe3},
	{0x21, 0xea},
	{0x22, 0xef},
	{0x23, 0xf5},
	{0x24, 0xf9},
	{0x25, 0xff},
	{0xfe, 0x00},
	{0xc6, 0x20},
	{0xc7, 0x2b},
	{0xfe, 0x02},
	{0x26, 0x0f},
	{0x27, 0x14},
	{0x28, 0x19},
	{0x29, 0x1e},
	{0x2a, 0x27},
	{0x2b, 0x33},
	{0x2c, 0x3b},
	{0x2d, 0x45},
	{0x2e, 0x59},
	{0x2f, 0x69},
	{0x30, 0x7c},
	{0x31, 0x89},
	{0x32, 0x98},
	{0x33, 0xae},
	{0x34, 0xc0},
	{0x35, 0xcf},
	{0x36, 0xda},
	{0x37, 0xe2},
	{0x38, 0xe9},
	{0x39, 0xf3},
	{0x3a, 0xf9},
	{0x3b, 0xff},
	{0xfe, 0x02},
	{0xd1, 0x32},
	{0xd2, 0x32},
	{0xd3, 0x40},
	{0xd6, 0xf0},
	{0xd7, 0x10},
	{0xd8, 0xda},
	{0xdd, 0x14},
	{0xde, 0x86},
	{0xed, 0x80},
	{0xee, 0x00},
	{0xef, 0x3f},
	{0xd8, 0xd8},
	{0xfe, 0x01},
	{0x9f, 0x40},
	{0xfe, 0x01},
	{0xc2, 0x14},
	{0xc3, 0x0d},
	{0xc4, 0x0c},
	{0xc8, 0x15},
	{0xc9, 0x0d},
	{0xca, 0x0a},
	{0xbc, 0x24},
	{0xbd, 0x10},
	{0xbe, 0x0b},
	{0xb6, 0x25},
	{0xb7, 0x16},
	{0xb8, 0x15},
	{0xc5, 0x00},
	{0xc6, 0x00},
	{0xc7, 0x00},
	{0xcb, 0x00},
	{0xcc, 0x00},
	{0xcd, 0x00},
	{0xbf, 0x07},
	{0xc0, 0x00},
	{0xc1, 0x00},
	{0xb9, 0x00},
	{0xba, 0x00},
	{0xbb, 0x00},
	{0xaa, 0x01},
	{0xab, 0x01},
	{0xac, 0x00},
	{0xad, 0x05},
	{0xae, 0x06},
	{0xaf, 0x0e},
	{0xb0, 0x0b},
	{0xb1, 0x07},
	{0xb2, 0x06},
	{0xb3, 0x17},
	{0xb4, 0x0e},
	{0xb5, 0x0e},
	{0xd0, 0x09},
	{0xd1, 0x00},
	{0xd2, 0x00},
	{0xd6, 0x08},
	{0xd7, 0x00},
	{0xd8, 0x00},
	{0xd9, 0x00},
	{0xda, 0x00},
	{0xdb, 0x00},
	{0xd3, 0x0a},
	{0xd4, 0x00},
	{0xd5, 0x00},
	{0xa4, 0x00},
	{0xa5, 0x00},
	{0xa6, 0x77},
	{0xa7, 0x77},
	{0xa8, 0x77},
	{0xa9, 0x77},
	{0xa1, 0x80},
	{0xa2, 0x80},

	{0xfe, 0x01},
	{0xdf, 0x0d},
	{0xdc, 0x25},
	{0xdd, 0x30},
	{0xe0, 0x77},
	{0xe1, 0x80},
	{0xe2, 0x77},
	{0xe3, 0x90},
	{0xe6, 0x90},
	{0xe7, 0xa0},
	{0xe8, 0x90},
	{0xe9, 0xa0},
	{0xfe, 0x00},
	{0xfe, 0x01},
	{0x4f, 0x00},
	{0x4f, 0x00},
	{0x4b, 0x01},
	{0x4f, 0x00},

	{0x4c, 0x01},
	{0x4d, 0x71},
	{0x4e, 0x01},
	{0x4c, 0x01},
	{0x4d, 0x91},
	{0x4e, 0x01},
	{0x4c, 0x01},
	{0x4d, 0x70},
	{0x4e, 0x01},
	{0x4c, 0x01},
	{0x4d, 0x90},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xb0},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x8f},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x6f},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xaf},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xd0},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xf0},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xcf},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0xef},
	{0x4e, 0x02},
	{0x4c, 0x01},
	{0x4d, 0x6e},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8e},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xae},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xce},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x4d},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x6d},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8d},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xad},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xcd},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x4c},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x6c},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8c},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xac},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xcc},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xcb},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x4b},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x6b},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8b},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0xab},
	{0x4e, 0x03},
	{0x4c, 0x01},
	{0x4d, 0x8a},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xaa},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xca},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xca},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xc9},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0x8a},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0x89},
	{0x4e, 0x04},
	{0x4c, 0x01},
	{0x4d, 0xa9},
	{0x4e, 0x04},
	{0x4c, 0x02},
	{0x4d, 0x0b},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x0a},
	{0x4e, 0x05},
	{0x4c, 0x01},
	{0x4d, 0xeb},
	{0x4e, 0x05},
	{0x4c, 0x01},
	{0x4d, 0xea},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x09},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x29},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x2a},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x4a},
	{0x4e, 0x05},
	{0x4c, 0x02},
	{0x4d, 0x8a},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x49},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x69},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x89},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0xa9},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x48},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x68},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0x69},
	{0x4e, 0x06},
	{0x4c, 0x02},
	{0x4d, 0xca},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xc9},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xe9},
	{0x4e, 0x07},
	{0x4c, 0x03},
	{0x4d, 0x09},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xc8},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xe8},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xa7},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xc7},
	{0x4e, 0x07},
	{0x4c, 0x02},
	{0x4d, 0xe7},
	{0x4e, 0x07},
	{0x4c, 0x03},
	{0x4d, 0x07},
	{0x4e, 0x07},

	{0x4f, 0x01},
	{0x50, 0x80},
	{0x51, 0xa8},
	{0x52, 0x47},
	{0x53, 0x38},
	{0x54, 0xc7},
	{0x56, 0x0e},
	{0x58, 0x08},
	{0x5b, 0x00},
	{0x5c, 0x74},
	{0x5d, 0x8b},
	{0x61, 0xdb},
	{0x62, 0xb8},
	{0x63, 0x86},
	{0x64, 0xc0},
	{0x65, 0x04},
	{0x67, 0xa8},
	{0x68, 0xb0},
	{0x69, 0x00},
	{0x6a, 0xa8},
	{0x6b, 0xb0},
	{0x6c, 0xaf},
	{0x6d, 0x8b},
	{0x6e, 0x50},
	{0x6f, 0x18},
	{0x73, 0xf0},
	{0x70, 0x0d},
	{0x71, 0x60},
	{0x72, 0x80},
	{0x74, 0x01},
	{0x75, 0x01},
	{0x7f, 0x0c},
	{0x76, 0x70},
	{0x77, 0x58},
	{0x78, 0xa0},
	{0x79, 0x5e},
	{0x7a, 0x54},
	{0x7b, 0x58},
	{0xfe, 0x00},
	{0xfe, 0x02},
	{0xc0, 0x01},
	{0xc1, 0x44},
	{0xc2, 0xfd},
	{0xc3, 0x04},
	{0xc4, 0xF0},
	{0xc5, 0x48},
	{0xc6, 0xfd},
	{0xc7, 0x46},
	{0xc8, 0xfd},
	{0xc9, 0x02},
	{0xca, 0xe0},
	{0xcb, 0x45},
	{0xcc, 0xec},
	{0xcd, 0x48},
	{0xce, 0xf0},
	{0xcf, 0xf0},
	{0xe3, 0x0c},
	{0xe4, 0x4b},
	{0xe5, 0xe0},
	{0xfe, 0x01},
	{0x9f, 0x40},
	{0xfe, 0x00},

	/* Output Control  */
	{0xfe, 0x02},
	{0x40, 0xbf},
	{0x46, 0xcf},
	{0xfe, 0x00},

	{0xfe, 0x00},
	{0x05, 0x01},
	{0x06, 0x1C},
	{0x07, 0x00},
	{0x08, 0x32},
	{0x11, 0x00},
	{0x12, 0x1D},
	{0x13, 0x00},
	{0x14, 0x00},

	{0xfe, 0x01},
	{0x3c, 0x00},
	{0x3d, 0x04},
	{0xfe, 0x00},
	{0x00, 0x00},
};

static const struct gc2145_reg default_mipi_csi_regs[] = {
	/* Switch to page 3 */
	{0xfe, 0x03},
	{GC2145_REG_DPHY_MODE1, GC2145_DPHY_MODE1_CLK_EN |
				GC2145_DPHY_MODE1_LANE0_EN | GC2145_DPHY_MODE1_LANE1_EN |
				GC2145_DPHY_MODE1_CLK_LANE_P2S_SEL},
	{GC2145_REG_DPHY_MODE2, GC2145_DPHY_MODE2_CLK_DIFF(2) |
				GC2145_DPHY_MODE2_LANE0_DIFF(2)},
	{GC2145_REG_DPHY_MODE3, GC2145_DPHY_MODE3_LANE1_DIFF(0) |
				GC2145_DPHY_MODE3_CLK_DELAY},
	{GC2145_REG_FIFO_MODE, GC2145_FIFO_MODE_READ_GATE |
			       GC2145_FIFO_MODE_MIPI_CLK_MODULE},
	{GC2145_REG_DPHY_MODE, GC2145_DPHY_MODE_TRIGGER_PROG},

	/* Clock & Data lanes timing */
	{GC2145_REG_T_LPX, 0x10},
	{GC2145_REG_T_CLK_HS_PREPARE, 0x04},
	{GC2145_REG_T_CLK_ZERO, 0x10},
	{GC2145_REG_T_CLK_PRE, 0x10},
	{GC2145_REG_T_CLK_POST, 0x10},
	{GC2145_REG_T_CLK_TRAIL, 0x05},
	{GC2145_REG_T_HS_PREPARE, 0x03},
	{GC2145_REG_T_HS_ZERO, 0x0a},
	{GC2145_REG_T_HS_TRAIL, 0x06},
};

struct gc2145_config {
	struct i2c_dt_spec i2c;
#if DT_INST_NODE_HAS_PROP(0, pwdn_gpios)
	struct gpio_dt_spec pwdn_gpio;
#endif
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	struct gpio_dt_spec reset_gpio;
#endif
	int bus_type;
};

struct gc2145_ctrls {
	struct video_ctrl hflip;
	struct video_ctrl vflip;
	struct video_ctrl linkfreq;
};

struct gc2145_data {
	struct gc2145_ctrls ctrls;
	struct video_format fmt;
};

#define GC2145_VIDEO_FORMAT_CAP(width, height, format)                                             \
	{                                                                                          \
		.pixelformat = format, .width_min = width, .width_max = width,                     \
		.height_min = height, .height_max = height, .width_step = 0, .height_step = 0,     \
	}

#define RESOLUTION_QVGA_W	320
#define RESOLUTION_QVGA_H	240

#define RESOLUTION_VGA_W	640
#define RESOLUTION_VGA_H	480

#define RESOLUTION_UXGA_W	1600
#define RESOLUTION_UXGA_H	1200

static const struct video_format_cap fmts[] = {
	GC2145_VIDEO_FORMAT_CAP(RESOLUTION_QVGA_W, RESOLUTION_QVGA_H, VIDEO_PIX_FMT_RGB565),
	GC2145_VIDEO_FORMAT_CAP(RESOLUTION_VGA_W, RESOLUTION_VGA_H, VIDEO_PIX_FMT_RGB565),
	GC2145_VIDEO_FORMAT_CAP(RESOLUTION_UXGA_W, RESOLUTION_UXGA_H, VIDEO_PIX_FMT_RGB565),
	GC2145_VIDEO_FORMAT_CAP(RESOLUTION_QVGA_W, RESOLUTION_QVGA_H, VIDEO_PIX_FMT_YUYV),
	GC2145_VIDEO_FORMAT_CAP(RESOLUTION_VGA_W, RESOLUTION_VGA_H, VIDEO_PIX_FMT_YUYV),
	GC2145_VIDEO_FORMAT_CAP(RESOLUTION_UXGA_W, RESOLUTION_UXGA_H, VIDEO_PIX_FMT_YUYV),
	{0},
};

static int gc2145_write_reg(const struct i2c_dt_spec *spec, uint8_t reg_addr, uint8_t value)
{
	int ret;
	uint8_t tries = 3;

	/*
	 * It rarely happens that the camera does not respond with ACK signal.
	 * In that case it usually responds on 2nd try but there is a 3rd one
	 * just to be sure that the connection error is not caused by driver
	 * itself.
	 */
	do {
		ret = i2c_reg_write_byte_dt(spec, reg_addr, value);
		if (!ret) {
			return 0;
		}
		/* If writing failed wait 5ms before next attempt */
		k_msleep(5);
	} while (tries-- > 0);

	LOG_ERR("failed to write 0x%x to 0x%x,", value, reg_addr);
	return ret;
}

static int gc2145_read_reg(const struct i2c_dt_spec *spec, uint8_t reg_addr, uint8_t *value)
{
	int ret;
	uint8_t tries = 3;

	/*
	 * It rarely happens that the camera does not respond with ACK signal.
	 * In that case it usually responds on 2nd try but there is a 3rd one
	 * just to be sure that the connection error is not caused by driver
	 * itself.
	 */
	do {
		ret = i2c_reg_read_byte_dt(spec, reg_addr, value);
		if (!ret) {
			return 0;
		}
		/* If writing failed wait 5ms before next attempt */
		k_msleep(5);
	} while (tries-- > 0);

	LOG_ERR("failed to read 0x%x register", reg_addr);
	return ret;
}

static int gc2145_write_all(const struct device *dev, const struct gc2145_reg *regs,
			    uint16_t reg_num)
{
	const struct gc2145_config *cfg = dev->config;

	for (uint16_t i = 0; i < reg_num; i++) {
		int ret;

		ret = gc2145_write_reg(&cfg->i2c, regs[i].addr, regs[i].value);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int gc2145_soft_reset(const struct device *dev)
{
	int ret;
	const struct gc2145_config *cfg = dev->config;

	/* Initiate system reset */
	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_RESET, GC2145_REG_SW_RESET);

	k_msleep(300);

	return ret;
}

static int gc2145_set_ctrl_vflip(const struct device *dev, bool enable)
{
	int ret;
	const struct gc2145_config *cfg = dev->config;
	uint8_t old_value;

	ret = gc2145_read_reg(&cfg->i2c, GC2145_REG_AMODE1, &old_value);
	if (ret < 0) {
		return ret;
	}

	/* Set the vertical flip state */
	return gc2145_write_reg(&cfg->i2c, GC2145_REG_AMODE1,
				(old_value & GC2145_AMODE1_WINDOW_MASK) | (enable << 1));
}

static int gc2145_set_ctrl_hmirror(const struct device *dev, bool enable)
{
	int ret;
	const struct gc2145_config *cfg = dev->config;
	uint8_t old_value;

	ret = gc2145_read_reg(&cfg->i2c, GC2145_REG_AMODE1, &old_value);
	if (ret < 0) {
		return ret;
	}

	/* Set the horizontal mirror state */
	return gc2145_write_reg(&cfg->i2c, GC2145_REG_AMODE1,
				(old_value & GC2145_AMODE1_WINDOW_MASK) | enable);
}

static int gc2145_set_window(const struct device *dev, uint16_t reg, uint16_t x, uint16_t y,
			     uint16_t w, uint16_t h)
{
	int ret;
	const struct gc2145_config *cfg = dev->config;

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_RESET, GC2145_SET_P0_REGS);
	if (ret < 0) {
		return ret;
	}

	/* Y/row offset */
	ret = gc2145_write_reg(&cfg->i2c, reg++, y >> 8);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, reg++, y & 0xff);
	if (ret < 0) {
		return ret;
	}

	/* X/col offset */
	ret = gc2145_write_reg(&cfg->i2c, reg++, x >> 8);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, reg++, x & 0xff);
	if (ret < 0) {
		return ret;
	}

	/* Window height */
	ret = gc2145_write_reg(&cfg->i2c, reg++, h >> 8);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, reg++, h & 0xff);
	if (ret < 0) {
		return ret;
	}

	/* Window width */
	ret = gc2145_write_reg(&cfg->i2c, reg++, w >> 8);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, reg++, w & 0xff);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int gc2145_set_output_format(const struct device *dev, int output_format)
{
	int ret;
	uint8_t old_value;
	const struct gc2145_config *cfg = dev->config;

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_RESET, GC2145_SET_P0_REGS);
	if (ret < 0) {
		return ret;
	}

	/* Map format to sensor format */
	if (output_format == VIDEO_PIX_FMT_RGB565) {
		output_format = GC2145_REG_OUTPUT_FMT_RGB565;
	} else if (output_format == VIDEO_PIX_FMT_YUYV) {
		output_format = GC2145_REG_OUTPUT_FMT_YCBYCR;
	} else {
		LOG_ERR("Image format not supported");
		return -ENOTSUP;
	}

	ret = gc2145_read_reg(&cfg->i2c, GC2145_REG_OUTPUT_FMT, &old_value);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_OUTPUT_FMT,
			(old_value & ~GC2145_REG_OUTPUT_FMT_MASK) | output_format);
	if (ret < 0) {
		return ret;
	}

	k_sleep(K_MSEC(30));

	return 0;
}

static int gc2145_set_resolution(const struct device *dev, uint32_t w, uint32_t h)
{
	int ret;
	const struct gc2145_config *cfg = dev->config;

	uint16_t win_w;
	uint16_t win_h;
	uint16_t c_ratio;
	uint16_t r_ratio;
	uint16_t x;
	uint16_t y;
	uint16_t win_x;
	uint16_t win_y;

	/* Add the subsampling factor depending on resolution */
	switch (w) {
	case RESOLUTION_QVGA_W:
		c_ratio = 3;
		r_ratio = 3;
		break;
	case RESOLUTION_VGA_W:
		c_ratio = 2;
		r_ratio = 2;
		break;
	case RESOLUTION_UXGA_W:
		c_ratio = 1;
		r_ratio = 1;
		break;
	default:
		LOG_ERR("Unsupported resolution %d %d", w, h);
		return -EIO;
	};

	/* Calculates the window boundaries to obtain the desired resolution */
	win_w = w * c_ratio;
	win_h = h * r_ratio;
	x = (((win_w / c_ratio) - w) / 2);
	y = (((win_h / r_ratio) - h) / 2);
	win_x = ((UXGA_HSIZE - win_w) / 2);
	win_y = ((UXGA_VSIZE - win_h) / 2);

	/* Set readout window first. */
	ret = gc2145_set_window(dev, GC2145_REG_BLANK_WINDOW_BASE, win_x, win_y, win_w + 16,
				win_h + 8);
	if (ret < 0) {
		return ret;
	}

	/* Set cropping window next. */
	ret = gc2145_set_window(dev, GC2145_REG_WINDOW_BASE, x, y, w, h);
	if (ret < 0) {
		return ret;
	}

	/* Enable crop */
	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_CROP_ENABLE, GC2145_CROP_SET_ENABLE);
	if (ret < 0) {
		return ret;
	}

	/* Set Sub-sampling ratio and mode */
	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_SUBSAMPLE, ((r_ratio << 4) | c_ratio));
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_SUBSAMPLE_MODE, GC2145_SUBSAMPLE_MODE_SMOOTH);
	if (ret < 0) {
		return ret;
	}
	/*
	 * Galaxy Core datasheet does not document the reason behind of this
	 * and other short delay requirements, but the reason exposed by them
	 * is to give enough time for the sensor DSP to handle the I2C transaction
	 * give some time time to apply the changes before the next instruction.
	 */
	k_sleep(K_MSEC(30));

	return 0;
}

static uint8_t gc2145_check_connection(const struct device *dev)
{
	int ret;
	const struct gc2145_config *cfg = dev->config;
	uint8_t reg_chip_id[2];
	uint16_t chip_id;

	ret = gc2145_read_reg(&cfg->i2c, 0xf0, &reg_chip_id[0]);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_read_reg(&cfg->i2c, 0xf1, &reg_chip_id[1]);
	if (ret < 0) {
		return ret;
	}

	chip_id = reg_chip_id[0] << 8 | reg_chip_id[1];

	if (chip_id != 0x2145 && chip_id != 0x2155) {
		LOG_WRN("Unexpected GC2145 chip ID: 0x%04x", chip_id);
	}

	return 0;
}

#define GC2145_640_480_LINK_FREQ	120000000
#define GC2145_640_480_LINK_FREQ_ID	0
#define GC2145_1600_1200_LINK_FREQ	240000000
#define GC2145_1600_1200_LINK_FREQ_ID	1
const int64_t gc2145_link_frequency[] = {
	GC2145_640_480_LINK_FREQ, GC2145_1600_1200_LINK_FREQ,
};
static int gc2145_config_csi(const struct device *dev, uint32_t pixelformat,
			     uint32_t width, uint32_t height)
{
	const struct gc2145_config *cfg = dev->config;
	struct gc2145_data *drv_data = dev->data;
	struct gc2145_ctrls *ctrls = &drv_data->ctrls;
	uint16_t fifo_full_level = width == 1600 ? 0x0001 : 0x0190;
	uint16_t lwc = width * video_bits_per_pixel(pixelformat) / BITS_PER_BYTE;
	uint8_t csi_dt;
	int ret;

	switch (pixelformat) {
	case VIDEO_PIX_FMT_RGB565:
		csi_dt = VIDEO_MIPI_CSI2_DT_RGB565;
		break;
	case VIDEO_PIX_FMT_YUYV:
		csi_dt = VIDEO_MIPI_CSI2_DT_YUV422_8;
		break;
	default:
		LOG_ERR("Unsupported pixelformat for CSI");
		return -EINVAL;
	}

	/* Only VGA & UXGA work (currently) in CSI */
	if (width == RESOLUTION_VGA_W && height == RESOLUTION_VGA_H) {
		ctrls->linkfreq.val = GC2145_640_480_LINK_FREQ_ID;
	} else if (width == RESOLUTION_UXGA_W && height == RESOLUTION_UXGA_H) {
		ctrls->linkfreq.val = GC2145_1600_1200_LINK_FREQ_ID;
	} else {
		LOG_ERR("Unsupported resolution 320x240 for CSI");
		return -EINVAL;
	}

	/* Apply fixed settings for MIPI-CSI. After that active page is 3 */
	ret = gc2145_write_all(dev, default_mipi_csi_regs, ARRAY_SIZE(default_mipi_csi_regs));
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_LWC_LOW, lwc & 0xff);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_LWC_HIGH, lwc >> 8);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_FIFO_FULL_LVL_LOW, fifo_full_level & 0xff);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_FIFO_FULL_LVL_HIGH, fifo_full_level >> 8);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_FIFO_GATE_MODE, 0xf0);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_MIPI_DT, csi_dt);
	if (ret < 0) {
		return ret;
	}

	return gc2145_write_reg(&cfg->i2c, 0xfe, 0x0);
}

static int gc2145_set_fmt(const struct device *dev, struct video_format *fmt)
{
	struct gc2145_data *drv_data = dev->data;
	const struct gc2145_config *cfg = dev->config;
	size_t res = ARRAY_SIZE(fmts);
	int ret;

	if (memcmp(&drv_data->fmt, fmt, sizeof(drv_data->fmt)) == 0) {
		/* nothing to do */
		return 0;
	}

	/* Check if camera is capable of handling given format */
	for (int i = 0; i < ARRAY_SIZE(fmts) - 1; i++) {
		if (fmts[i].width_min == fmt->width && fmts[i].height_min == fmt->height &&
		    fmts[i].pixelformat == fmt->pixelformat) {
			res = i;
			break;
		}
	}
	if (res == ARRAY_SIZE(fmts)) {
		LOG_ERR("Image format not supported");
		return -ENOTSUP;
	}

	/* Set output format */
	ret = gc2145_set_output_format(dev, fmt->pixelformat);
	if (ret < 0) {
		LOG_ERR("Failed to set the output format");
		return ret;
	}

	/* Set window size */
	ret = gc2145_set_resolution(dev, fmt->width, fmt->height);

	if (ret < 0) {
		LOG_ERR("Failed to set the resolution");
		return ret;
	}

	if (cfg->bus_type == VIDEO_BUS_TYPE_CSI2_DPHY) {
		ret = gc2145_config_csi(dev, fmt->pixelformat, fmt->width, fmt->height);
		if (ret < 0) {
			LOG_ERR("Failed to configure MIPI-CSI");
			return ret;
		}
	}

	drv_data->fmt = *fmt;

	return 0;
}

static int gc2145_get_fmt(const struct device *dev, struct video_format *fmt)
{
	struct gc2145_data *drv_data = dev->data;

	*fmt = drv_data->fmt;

	return 0;
}

static int gc2145_set_stream_dvp(const struct device *dev, bool enable)
{
	const struct gc2145_config *cfg = dev->config;

	return enable ? gc2145_write_reg(&cfg->i2c, 0xf2, 0x0f)
		      : gc2145_write_reg(&cfg->i2c, 0xf2, 0x00);
}

static int gc2145_set_stream_csi(const struct device *dev, bool enable)
{
	const struct gc2145_config *cfg = dev->config;
	int ret;

	ret = gc2145_write_reg(&cfg->i2c, 0xfe, 0x03);
	if (ret < 0) {
		return ret;
	}

	ret = gc2145_write_reg(&cfg->i2c, GC2145_REG_BUF_CSI2_MODE,
			       enable ? GC2145_CSI2_MODE_RAW8 | GC2145_CSI2_MODE_DOUBLE |
					GC2145_CSI2_MODE_EN | GC2145_CSI2_MODE_MIPI_EN
				      : 0);
	if (ret < 0) {
		return ret;
	}

	return gc2145_write_reg(&cfg->i2c, 0xfe, 0x0);
}

static int gc2145_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	const struct gc2145_config *cfg = dev->config;

	return cfg->bus_type == VIDEO_BUS_TYPE_PARALLEL ?
	       gc2145_set_stream_dvp(dev, enable) :
	       gc2145_set_stream_csi(dev, enable);
}

static int gc2145_get_caps(const struct device *dev, struct video_caps *caps)
{
	caps->format_caps = fmts;
	return 0;
}

static int gc2145_set_ctrl(const struct device *dev, uint32_t id)
{
	struct gc2145_data *drv_data = dev->data;

	switch (id) {
	case VIDEO_CID_HFLIP:
		return gc2145_set_ctrl_hmirror(dev, drv_data->ctrls.hflip.val);
	case VIDEO_CID_VFLIP:
		return gc2145_set_ctrl_vflip(dev, drv_data->ctrls.vflip.val);
	default:
		return -ENOTSUP;
	}
}

static DEVICE_API(video, gc2145_driver_api) = {
	.set_format = gc2145_set_fmt,
	.get_format = gc2145_get_fmt,
	.get_caps = gc2145_get_caps,
	.set_stream = gc2145_set_stream,
	.set_ctrl = gc2145_set_ctrl,
};

static int gc2145_init_controls(const struct device *dev)
{
	int ret;
	struct gc2145_data *drv_data = dev->data;
	struct gc2145_ctrls *ctrls = &drv_data->ctrls;

	ret = video_init_ctrl(&ctrls->hflip, dev, VIDEO_CID_HFLIP,
			      (struct video_ctrl_range){.min = 0, .max = 1, .step = 1, .def = 0});
	if (ret) {
		return ret;
	}

	ret = video_init_ctrl(&ctrls->vflip, dev, VIDEO_CID_VFLIP,
			      (struct video_ctrl_range){.min = 0, .max = 1, .step = 1, .def = 0});
	if (ret < 0) {
		return ret;
	}

	ret = video_init_int_menu_ctrl(&ctrls->linkfreq, dev, VIDEO_CID_LINK_FREQ,
				       GC2145_640_480_LINK_FREQ_ID, gc2145_link_frequency,
				       ARRAY_SIZE(gc2145_link_frequency));
	if (ret < 0) {
		return ret;
	}

	ctrls->linkfreq.flags |= VIDEO_CTRL_FLAG_READ_ONLY;

	return 0;
}

static int gc2145_init(const struct device *dev)
{
	struct video_format fmt;
	int ret;
	const struct gc2145_config *cfg = dev->config;
	(void) cfg;

#if DT_INST_NODE_HAS_PROP(0, pwdn_gpios)
	ret = gpio_pin_configure_dt(&cfg->pwdn_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		return ret;
	}

	k_sleep(K_MSEC(10));
#endif
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret) {
		return ret;
	}

	k_sleep(K_MSEC(1));
	gpio_pin_set_dt(&cfg->reset_gpio, 0);
	k_sleep(K_MSEC(1));
#endif

	ret = gc2145_check_connection(dev);
	if (ret) {
		return ret;
	}

	gc2145_soft_reset(dev);
	gc2145_write_all(dev, default_regs, ARRAY_SIZE(default_regs));

	/* set default/init format VGA RGB565 */
	fmt.pixelformat = VIDEO_PIX_FMT_RGB565;
	fmt.width = RESOLUTION_VGA_W;
	fmt.height = RESOLUTION_VGA_H;

	ret = gc2145_set_fmt(dev, &fmt);
	if (ret) {
		LOG_ERR("Unable to configure default format");
		return ret;
	}

	/* Initialize controls */
	return gc2145_init_controls(dev);
}

/* Unique Instance */
static const struct gc2145_config gc2145_cfg_0 = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
#if DT_INST_NODE_HAS_PROP(0, pwdn_gpios)
	.pwdn_gpio = GPIO_DT_SPEC_INST_GET(0, pwdn_gpios),
#endif
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	.reset_gpio = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
#endif
	.bus_type = DT_PROP_OR(DT_INST_ENDPOINT_BY_ID(0, 0, 0), bus_type,
			       VIDEO_BUS_TYPE_PARALLEL),
};
static struct gc2145_data gc2145_data_0;

static int gc2145_init_0(const struct device *dev)
{
	const struct gc2145_config *cfg = dev->config;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

#if DT_INST_NODE_HAS_PROP(0, pwdn_gpios)
	if (!gpio_is_ready_dt(&cfg->pwdn_gpio)) {
		LOG_ERR("%s: device %s is not ready", dev->name, cfg->pwdn_gpio.port->name);
		return -ENODEV;
	}
#endif
#if DT_INST_NODE_HAS_PROP(0, reset_gpios)
	if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
		LOG_ERR("%s: device %s is not ready", dev->name, cfg->reset_gpio.port->name);
		return -ENODEV;
	}
#endif

	return gc2145_init(dev);
}

DEVICE_DT_INST_DEFINE(0, &gc2145_init_0, NULL, &gc2145_data_0, &gc2145_cfg_0, POST_KERNEL,
		      CONFIG_VIDEO_INIT_PRIORITY, &gc2145_driver_api);

VIDEO_DEVICE_DEFINE(gc2145, DEVICE_DT_INST_GET(0), NULL);
