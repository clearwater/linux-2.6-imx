/*
 * Copyright 2005-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/regulator/regulator.h>

#include "mxc_v4l2_capture.h"

#ifdef CAMERA_DBG
	#define CAMERA_TRACE(x) (printk)x
#else
	#define CAMERA_TRACE(x)
#endif

#define OV3640_VOLTAGE_ANALOG               2800000
/*#define OV3640_VOLTAGE_DIGITAL_CORE         1500000*/
#define OV3640_VOLTAGE_DIGITAL_IO           1800000

enum ov3640_mode {
	ov3640_mode_MIN = 0,
	ov3640_mode_QXGA_2048_1536 = 0,
	ov3640_mode_XGA_1024_768   = 1,
	ov3640_mode_VGA_640_480    = 2,
	ov3640_mode_QVGA_320_240   = 3,
	ov3640_mode_MAX = 3
};

struct reg_value {
	u16 u16RegAddr;
	u8  u8Val;
	u32 u32Delay_ms;
};

struct ov3640_mode_info {
	enum ov3640_mode mode;
	u32		width;
	u32		height;
	struct reg_value *init_data_ptr;
	u32		init_data_size;
	u32		fmt;
};

static struct reg_value ov3640_setting_QXGA_2048_1536[] = {
	{0x3012, 0x80, 0}, {0x304d, 0x45, 0}, {0x30a7, 0x5e, 0},
	{0x3087, 0x16, 0}, {0x309c, 0x1a, 0}, {0x30a2, 0xe4, 0},
	{0x30aa, 0x42, 0}, {0x30b0, 0xff, 0}, {0x30b1, 0xff, 0},
	{0x30b2, 0x10, 0}, {0x300e, 0x32, 0}, {0x300f, 0x21, 0},
	{0x3010, 0x20, 0}, {0x3011, 0x00, 0}, {0x304c, 0x81, 0},
	{0x30d7, 0x10, 0}, {0x30d9, 0x0d, 0}, {0x30db, 0x08, 0},
	{0x3016, 0x82, 0}, {0x3018, 0x38, 0}, {0x3019, 0x30, 0},
	{0x301a, 0x61, 0}, {0x307d, 0x00, 0}, {0x3087, 0x02, 0},
	{0x3082, 0x20, 0}, {0x3015, 0x12, 0}, {0x3014, 0x04, 0},
	{0x3013, 0xf7, 0}, {0x303c, 0x08, 0}, {0x303d, 0x18, 0},
	{0x303e, 0x06, 0}, {0x303f, 0x0c, 0}, {0x3030, 0x62, 0},
	{0x3031, 0x26, 0}, {0x3032, 0xe6, 0}, {0x3033, 0x6e, 0},
	{0x3034, 0xea, 0}, {0x3035, 0xae, 0}, {0x3036, 0xa6, 0},
	{0x3037, 0x6a, 0}, {0x3104, 0x02, 0}, {0x3105, 0xfd, 0},
	{0x3106, 0x00, 0}, {0x3107, 0xff, 0}, {0x3300, 0x12, 0},
	{0x3301, 0xde, 0}, {0x3302, 0xcf, 0}, {0x3312, 0x26, 0},
	{0x3314, 0x42, 0}, {0x3313, 0x2b, 0}, {0x3315, 0x42, 0},
	{0x3310, 0xd0, 0}, {0x3311, 0xbd, 0}, {0x330c, 0x18, 0},
	{0x330d, 0x18, 0}, {0x330e, 0x56, 0}, {0x330f, 0x5c, 0},
	{0x330b, 0x1c, 0}, {0x3306, 0x5c, 0}, {0x3307, 0x11, 0},
	{0x336a, 0x52, 0}, {0x3370, 0x46, 0}, {0x3376, 0x38, 0},
	{0x30b8, 0x20, 0}, {0x30b9, 0x17, 0}, {0x30ba, 0x04, 0},
	{0x30bb, 0x08, 0}, {0x3507, 0x06, 0}, {0x350a, 0x4f, 0},
	{0x3100, 0x02, 0}, {0x3301, 0xde, 0}, {0x3304, 0x00, 0},
	{0x3400, 0x01, 0}, {0x3404, 0x1d, 0}, {0x3600, 0xc4, 0},
	{0x3302, 0xef, 0}, {0x3020, 0x01, 0}, {0x3021, 0x1d, 0},
	{0x3022, 0x00, 0}, {0x3023, 0x0a, 0}, {0x3024, 0x08, 0},
	{0x3025, 0x00, 0}, {0x3026, 0x06, 0}, {0x3027, 0x00, 0},
	{0x335f, 0x68, 0}, {0x3360, 0x00, 0}, {0x3361, 0x00, 0},
	{0x3362, 0x68, 0}, {0x3363, 0x00, 0}, {0x3364, 0x00, 0},
	{0x3403, 0x00, 0}, {0x3088, 0x08, 0}, {0x3089, 0x00, 0},
	{0x308a, 0x06, 0}, {0x308b, 0x00, 0}, {0x307c, 0x10, 0},
	{0x3090, 0xc0, 0}, {0x304c, 0x84, 0}, {0x308d, 0x04, 0},
	{0x3086, 0x03, 0}, {0x3086, 0x00, 0},

	{0x3012, 0x0, 0}, {0x3020, 0x1, 0}, {0x3021, 0x1d, 0},
	{0x3022, 0x0, 0}, {0x3023, 0xa, 0}, {0x3024, 0x8, 0},
	{0x3025, 0x18, 0}, {0x3026, 0x6, 0}, {0x3027, 0xc, 0},
	{0x302a, 0x6, 0}, {0x302b, 0x20, 0}, {0x3075, 0x44, 0},
	{0x300d, 0x0, 0}, {0x30d7, 0x0, 0}, {0x3069, 0x40, 0},
	{0x303e, 0x1, 0}, {0x303f, 0x80, 0}, {0x3302, 0x20, 0},
	{0x335f, 0x68, 0}, {0x3360, 0x18, 0}, {0x3361, 0xc, 0},
	{0x3362, 0x68, 0}, {0x3363, 0x8, 0}, {0x3364, 0x4, 0},
	{0x3403, 0x42, 0}, {0x3088, 0x8, 0}, {0x3089, 0x0, 0},
	{0x308a, 0x6, 0}, {0x308b, 0x0, 0},
};

static struct reg_value ov3640_setting_XGA_1024_768[] = {
	{0x3012, 0x80, 0}, {0x304d, 0x45, 0}, {0x30a7, 0x5e, 0},
	{0x3087, 0x16, 0}, {0x309c, 0x1a, 0}, {0x30a2, 0xe4, 0},
	{0x30aa, 0x42, 0}, {0x30b0, 0xff, 0}, {0x30b1, 0xff, 0},
	{0x30b2, 0x10, 0}, {0x300e, 0x32, 0}, {0x300f, 0x21, 0},
	{0x3010, 0x20, 0}, {0x3011, 0x00, 0}, {0x304c, 0x81, 0},
	{0x3016, 0x82, 0}, {0x3018, 0x38, 0}, {0x3019, 0x30, 0},
	{0x301a, 0x61, 0}, {0x307d, 0x00, 0}, {0x3087, 0x02, 0},
	{0x3082, 0x20, 0}, {0x3015, 0x12, 0}, {0x3014, 0x04, 0},
	{0x3013, 0xf7, 0}, {0x303c, 0x08, 0}, {0x303d, 0x18, 0},
	{0x303e, 0x06, 0}, {0x303f, 0x0c, 0}, {0x3030, 0x62, 0},
	{0x3031, 0x26, 0}, {0x3032, 0xe6, 0}, {0x3033, 0x6e, 0},
	{0x3034, 0xea, 0}, {0x3035, 0xae, 0}, {0x3036, 0xa6, 0},
	{0x3037, 0x6a, 0}, {0x3104, 0x02, 0}, {0x3105, 0xfd, 0},
	{0x3106, 0x00, 0}, {0x3107, 0xff, 0}, {0x3300, 0x12, 0},
	{0x3301, 0xde, 0}, {0x3302, 0xcf, 0}, {0x3312, 0x26, 0},
	{0x3314, 0x42, 0}, {0x3313, 0x2b, 0}, {0x3315, 0x42, 0},
	{0x3310, 0xd0, 0}, {0x3311, 0xbd, 0}, {0x330c, 0x18, 0},
	{0x330d, 0x18, 0}, {0x330e, 0x56, 0}, {0x330f, 0x5c, 0},
	{0x330b, 0x1c, 0}, {0x3306, 0x5c, 0}, {0x3307, 0x11, 0},
	{0x336a, 0x52, 0}, {0x3370, 0x46, 0}, {0x3376, 0x38, 0},
	{0x30b8, 0x20, 0}, {0x30b9, 0x17, 0}, {0x30ba, 0x04, 0},
	{0x30bb, 0x08, 0}, {0x3507, 0x06, 0}, {0x350a, 0x4f, 0},
	{0x3100, 0x02, 0}, {0x3301, 0xde, 0}, {0x3304, 0x00, 0},
	{0x3400, 0x01, 0}, {0x3404, 0x1d, 0}, {0x3600, 0xc4, 0},
	{0x3302, 0xef, 0}, {0x3020, 0x01, 0}, {0x3021, 0x1d, 0},
	{0x3022, 0x00, 0}, {0x3023, 0x0a, 0}, {0x3024, 0x08, 0},
	{0x3025, 0x00, 0}, {0x3026, 0x06, 0}, {0x3027, 0x00, 0},
	{0x335f, 0x68, 0}, {0x3360, 0x00, 0}, {0x3361, 0x00, 0},
	{0x3362, 0x34, 0}, {0x3363, 0x00, 0}, {0x3364, 0x00, 0},
	{0x3403, 0x00, 0}, {0x3088, 0x04, 0}, {0x3089, 0x00, 0},
	{0x308a, 0x03, 0}, {0x308b, 0x00, 0}, {0x307c, 0x10, 0},
	{0x3090, 0xc0, 0}, {0x304c, 0x84, 0}, {0x308d, 0x04, 0},
	{0x3086, 0x03, 0}, {0x3086, 0x00, 0}, {0x3011, 0x01, 0},
};

static struct reg_value ov3640_setting_VGA_640_480[] = {
	{0x3012, 0x80, 0}, {0x304d, 0x45, 0}, {0x30a7, 0x5e, 0},
	{0x3087, 0x16, 0}, {0x309c, 0x1a, 0}, {0x30a2, 0xe4, 0},
	{0x30aa, 0x42, 0}, {0x30b0, 0xff, 0}, {0x30b1, 0xff, 0},
	{0x30b2, 0x10, 0}, {0x300e, 0x32, 0}, {0x300f, 0x21, 0},
	{0x3010, 0x20, 0}, {0x3011, 0x00, 0}, {0x304c, 0x81, 0},
	{0x30d7, 0x10, 0}, {0x30d9, 0x0d, 0}, {0x30db, 0x08, 0},
	{0x3016, 0x82, 0}, {0x3018, 0x38, 0}, {0x3019, 0x30, 0},
	{0x301a, 0x61, 0}, {0x307d, 0x00, 0}, {0x3087, 0x02, 0},
	{0x3082, 0x20, 0}, {0x3015, 0x12, 0}, {0x3014, 0x04, 0},
	{0x3013, 0xf7, 0}, {0x303c, 0x08, 0}, {0x303d, 0x18, 0},
	{0x303e, 0x06, 0}, {0x303f, 0x0c, 0}, {0x3030, 0x62, 0},
	{0x3031, 0x26, 0}, {0x3032, 0xe6, 0}, {0x3033, 0x6e, 0},
	{0x3034, 0xea, 0}, {0x3035, 0xae, 0}, {0x3036, 0xa6, 0},
	{0x3037, 0x6a, 0}, {0x3104, 0x02, 0}, {0x3105, 0xfd, 0},
	{0x3106, 0x00, 0}, {0x3107, 0xff, 0}, {0x3300, 0x12, 0},
	{0x3301, 0xde, 0}, {0x3302, 0xcf, 0}, {0x3312, 0x26, 0},
	{0x3314, 0x42, 0}, {0x3313, 0x2b, 0}, {0x3315, 0x42, 0},
	{0x3310, 0xd0, 0}, {0x3311, 0xbd, 0}, {0x330c, 0x18, 0},
	{0x330d, 0x18, 0}, {0x330e, 0x56, 0}, {0x330f, 0x5c, 0},
	{0x330b, 0x1c, 0}, {0x3306, 0x5c, 0}, {0x3307, 0x11, 0},
	{0x336a, 0x52, 0}, {0x3370, 0x46, 0}, {0x3376, 0x38, 0},
	{0x30b8, 0x20, 0}, {0x30b9, 0x17, 0}, {0x30ba, 0x04, 0},
	{0x30bb, 0x08, 0}, {0x3507, 0x06, 0}, {0x350a, 0x4f, 0},
	{0x3100, 0x02, 0}, {0x3301, 0xde, 0}, {0x3304, 0x00, 0},
	{0x3400, 0x00, 0}, {0x3404, 0x42, 0}, {0x3600, 0xc4, 0},
	{0x3302, 0xef, 0}, {0x3020, 0x01, 0}, {0x3021, 0x1d, 0},
	{0x3022, 0x00, 0}, {0x3023, 0x0a, 0}, {0x3024, 0x08, 0},
	{0x3025, 0x00, 0}, {0x3026, 0x06, 0}, {0x3027, 0x00, 0},
	{0x335f, 0x68, 0}, {0x3360, 0x00, 0}, {0x3361, 0x00, 0},
	{0x3362, 0x12, 0}, {0x3363, 0x80, 0}, {0x3364, 0xe0, 0},
	{0x3403, 0x00, 0}, {0x3088, 0x02, 0}, {0x3089, 0x80, 0},
	{0x308a, 0x01, 0}, {0x308b, 0xe0, 0}, {0x307c, 0x10, 0},
	{0x3090, 0xc0, 0}, {0x304c, 0x84, 0}, {0x308d, 0x04, 0},
	{0x3086, 0x03, 0}, {0x3086, 0x00, 0}, {0x3011, 0x01, 0},
};

static struct reg_value ov3640_setting_QVGA_320_240[] = {
	{0x3012, 0x80, 0}, {0x304d, 0x45, 0}, {0x30a7, 0x5e, 0},
	{0x3087, 0x16, 0}, {0x309c, 0x1a, 0}, {0x30a2, 0xe4, 0},
	{0x30aa, 0x42, 0}, {0x30b0, 0xff, 0}, {0x30b1, 0xff, 0},
	{0x30b2, 0x10, 0}, {0x300e, 0x32, 0}, {0x300f, 0x21, 0},
	{0x3010, 0x20, 0}, {0x3011, 0x00, 0}, {0x304c, 0x81, 0},
	{0x30d7, 0x10, 0}, {0x30d9, 0x0d, 0}, {0x30db, 0x08, 0},
	{0x3016, 0x82, 0}, {0x3018, 0x38, 0}, {0x3019, 0x30, 0},
	{0x301a, 0x61, 0}, {0x307d, 0x00, 0}, {0x3087, 0x02, 0},
	{0x3082, 0x20, 0}, {0x3015, 0x12, 0}, {0x3014, 0x04, 0},
	{0x3013, 0xf7, 0}, {0x303c, 0x08, 0}, {0x303d, 0x18, 0},
	{0x303e, 0x06, 0}, {0x303f, 0x0c, 0}, {0x3030, 0x62, 0},
	{0x3031, 0x26, 0}, {0x3032, 0xe6, 0}, {0x3033, 0x6e, 0},
	{0x3034, 0xea, 0}, {0x3035, 0xae, 0}, {0x3036, 0xa6, 0},
	{0x3037, 0x6a, 0}, {0x3104, 0x02, 0}, {0x3105, 0xfd, 0},
	{0x3106, 0x00, 0}, {0x3107, 0xff, 0}, {0x3300, 0x12, 0},
	{0x3301, 0xde, 0}, {0x3302, 0xcf, 0}, {0x3312, 0x26, 0},
	{0x3314, 0x42, 0}, {0x3313, 0x2b, 0}, {0x3315, 0x42, 0},
	{0x3310, 0xd0, 0}, {0x3311, 0xbd, 0}, {0x330c, 0x18, 0},
	{0x330d, 0x18, 0}, {0x330e, 0x56, 0}, {0x330f, 0x5c, 0},
	{0x330b, 0x1c, 0}, {0x3306, 0x5c, 0}, {0x3307, 0x11, 0},
	{0x336a, 0x52, 0}, {0x3370, 0x46, 0}, {0x3376, 0x38, 0},
	{0x30b8, 0x20, 0}, {0x30b9, 0x17, 0}, {0x30ba, 0x04, 0},
	{0x30bb, 0x08, 0}, {0x3507, 0x06, 0}, {0x350a, 0x4f, 0},
	{0x3100, 0x02, 0}, {0x3301, 0xde, 0}, {0x3304, 0x00, 0},
	{0x3400, 0x00, 0}, {0x3404, 0x42, 0}, {0x3600, 0xc4, 0},
	{0x3302, 0xef, 0}, {0x3020, 0x01, 0}, {0x3021, 0x1d, 0},
	{0x3022, 0x00, 0}, {0x3023, 0x0a, 0}, {0x3024, 0x08, 0},
	{0x3025, 0x00, 0}, {0x3026, 0x06, 0}, {0x3027, 0x00, 0},
	{0x335f, 0x68, 0}, {0x3360, 0x00, 0}, {0x3361, 0x00, 0},
	{0x3362, 0x01, 0}, {0x3363, 0x40, 0}, {0x3364, 0xf0, 0},
	{0x3403, 0x00, 0}, {0x3088, 0x01, 0}, {0x3089, 0x40, 0},
	{0x308a, 0x00, 0}, {0x308b, 0xf0, 0}, {0x307c, 0x10, 0},
	{0x3090, 0xc0, 0}, {0x304c, 0x84, 0}, {0x308d, 0x04, 0},
	{0x3086, 0x03, 0}, {0x3086, 0x00, 0}, {0x3011, 0x01, 0},
};

static struct ov3640_mode_info ov3640_mode_info_data[] = {
	{ov3640_mode_QXGA_2048_1536, 2048, 1536, ov3640_setting_QXGA_2048_1536,
	ARRAY_SIZE(ov3640_setting_QXGA_2048_1536), IPU_PIX_FMT_UYVY},
	{ov3640_mode_XGA_1024_768,   1024, 768,  ov3640_setting_XGA_1024_768,
	ARRAY_SIZE(ov3640_setting_XGA_1024_768), IPU_PIX_FMT_UYVY},
	{ov3640_mode_VGA_640_480,    640,  480,  ov3640_setting_VGA_640_480,
	ARRAY_SIZE(ov3640_setting_VGA_640_480), IPU_PIX_FMT_UYVY},
	{ov3640_mode_QVGA_320_240,   320,  240,  ov3640_setting_QVGA_320_240,
	ARRAY_SIZE(ov3640_setting_QVGA_320_240), IPU_PIX_FMT_UYVY},
};

static s32 s32csi_index;
static struct regulator *io_regulator;
/*static struct regulator *core_regulator; */
static struct regulator *analog_regulator;
/*static struct regulator *gpo_regulator;*/

u32 mclk = 24000000; /* 6 - 54 MHz, typical 24MHz */

struct i2c_client *ov3640_i2c_client;

static sensor_interface *interface_param;
static s32 reset_frame_rate = 30;
static int ov3640_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int ov3640_remove(struct i2c_client *client);

static s32 ov3640_read_reg(u16 reg, u8 *val);
static s32 ov3640_write_reg(u16 reg, u8 val);

static const struct i2c_device_id ov3640_id[] = {
	{"ov3640", 0},
};
MODULE_DEVICE_TABLE(i2c, ov3640_id);

static struct i2c_driver ov3640_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name  = "ov3640",
	},
	.probe  = ov3640_probe,
	.remove = ov3640_remove,
	.id_table = ov3640_id,
};

extern struct camera_sensor camera_sensor_if;

/*!
 * ov3640 I2C attach function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int ov3640_probe(struct i2c_client *client,
				const struct i2c_device_id *device_id)
{
	struct mxc_camera_platform_data *plat_data = client->dev.platform_data;

	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_probe\n"));

	ov3640_i2c_client = client;
	mclk = plat_data->mclk;
	s32csi_index = camera_sensor_if.csi = plat_data->csi;

	io_regulator     = regulator_get(&client->dev,
						plat_data->io_regulator);
	/*core_regulator   = regulator_get(&client->dev,
						plat_data->core_regulator); */
	analog_regulator = regulator_get(&client->dev,
						plat_data->analog_regulator);
	/*gpo_regulator    = regulator_get(&client->dev,
						plat_data->gpo_regulator); */

	if (NULL == interface_param) {
		interface_param = kmalloc(sizeof(sensor_interface), GFP_KERNEL);
		if (!interface_param) {
			dev_dbg(&ov3640_i2c_client->dev,
				"ov3640_probe: kmalloc failed \n");
			return -1;
		}
		memset(interface_param, 0, sizeof(sensor_interface));
	} else {
		dev_dbg(&ov3640_i2c_client->dev,
				"ov3640_probe: kmalloc pointer not NULL \n");
		return -1;
	}

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_probe\n"));

	return 0;
}

/*!
 * ov3640 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int ov3640_remove(struct i2c_client *client)
{
	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_remove\n"));

	if (NULL != interface_param) {
		kfree(interface_param);
		interface_param = NULL;
	}

	if (!IS_ERR_VALUE((u32)io_regulator)) {
		regulator_disable(io_regulator);
		regulator_put(io_regulator, NULL);
	}

/*
	if (!IS_ERR_VALUE((u32)core_regulator)) {
		regulator_disable(core_regulator);
		regulator_put(core_regulator, NULL);
	}

	if (!IS_ERR_VALUE((u32)gpo_regulator)) {
		regulator_disable(gpo_regulator);
		regulator_put(gpo_regulator, NULL);
	}
*/

	if (!IS_ERR_VALUE((u32)analog_regulator)) {
		regulator_disable(analog_regulator);
		regulator_put(analog_regulator, NULL);
	}

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_remove\n"));

	return 0;
}

static s32 ov3640_write_reg(u16 reg, u8 val)
{
	u8 au8Buf[3] = { 0 };

	au8Buf[0] = reg >> 8;
	au8Buf[1] = reg & 0xff;
	au8Buf[2] = val;

	if (i2c_master_send(ov3640_i2c_client, au8Buf, 3) < 0) {
		dev_dbg(&ov3640_i2c_client->dev,
				"%s:write reg error:reg=%x,val=%x\n",
				__func__, reg, val);
		return -1;
	}

	return 0;
}

static s32 ov3640_read_reg(u16 reg, u8 *val)
{
	u8 au8RegBuf[2] = { 0 };
	u8 u8RdVal = 0;

	au8RegBuf[0] = reg >> 8;
	au8RegBuf[1] = reg & 0xff;

	if (2 != i2c_master_send(ov3640_i2c_client, au8RegBuf, 2)) {
		dev_dbg(&ov3640_i2c_client->dev,
				"%s:write reg error:reg=%x\n",
				__func__, reg);
		return -1;
	}

	if (1 != i2c_master_recv(ov3640_i2c_client, &u8RdVal, 1)) {
		dev_dbg(&ov3640_i2c_client->dev,
				"%s:read reg error:reg=%x,val=%x\n",
				__func__, reg, u8RdVal);
		return -1;
	}

	*val = u8RdVal;

	return u8RdVal;
}

static int ov3640_init_mode(enum ov3640_mode mode)
{
	struct reg_value *pModeSetting = NULL;
	s32 i = 0;
	s32 iModeSettingArySize = 0;

	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_init_mode\n"));

	if (mode > ov3640_mode_MAX || mode < ov3640_mode_MIN) {
		dev_dbg(&ov3640_i2c_client->dev,
				"Wrong ov3640 mode detected!\n");
		return -1;
	}

	pModeSetting = ov3640_mode_info_data[mode].init_data_ptr;
	iModeSettingArySize = ov3640_mode_info_data[mode].init_data_size;

	for (i = 0; i < iModeSettingArySize; ++i, ++pModeSetting) {
		u32 u32TmpVal = pModeSetting->u32Delay_ms;

		ov3640_write_reg(pModeSetting->u16RegAddr, pModeSetting->u8Val);
		if (u32TmpVal)
			msleep(u32TmpVal);
	}

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_init_mode\n"));

	return 0;
}

/*!
 * ov3640 sensor interface Initialization
 * @param param            sensor_interface *
 * @param width            u32
 * @param height           u32
 * @return  None
 */
static void ov3640_interface(sensor_interface *param, u32 width, u32 height)
{

	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_interface\n"));

	param->data_width = IPU_CSI_DATA_WIDTH_8;
	param->clk_mode = IPU_CSI_CLK_MODE_GATED_CLK;  /* gated */
	param->ext_vsync = 1;
	param->Vsync_pol = 0;
	param->Hsync_pol = 0;
	param->pixclk_pol = 0;
	param->data_pol = 0;
	param->pack_tight = 0;
	param->force_eof = 0;
	param->data_en_pol = 0;
	param->width = width;
	param->height = height;
	param->active_width = width;
	param->active_height = height;
	param->mclk = mclk;

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_interface\n"));
}

static void ov3640_set_color(int bright, int saturation, int red, int green,
				int blue)
{

}

static void ov3640_get_color(int *bright, int *saturation, int *red, int *green,
				int *blue)
{

}

static void ov3640_set_ae_mode(int ae_mode)
{

}

static void ov3640_get_ae_mode(int *ae_mode)
{

}

static void ov3640_set_std_mode(v4l2_std_id std)
{

}

static void ov3640_get_std_mode(v4l2_std_id * std)
{

}

extern void gpio_sensor_active(unsigned int csi_index);

static sensor_interface *ov3640_config(int *frame_rate, int high_quality)
{

	u32 u32OutWidth  = 0;
	u32 u32OutHeight = 0;

	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_config\n"));

	if (high_quality > ov3640_mode_MAX || high_quality < ov3640_mode_MIN) {
		dev_dbg(&ov3640_i2c_client->dev,
				"Wrong ov3640 mode detected!\n");
		return NULL;
	}

	CAMERA_TRACE(("mode: %d\n", high_quality));

	if (!IS_ERR_VALUE((u32)io_regulator)) {
		regulator_set_voltage(io_regulator, OV3640_VOLTAGE_DIGITAL_IO);
		if (regulator_enable(io_regulator) != 0) {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:io set voltage error\n", __func__);
			return NULL;
		} else {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:io set voltage ok\n", __func__);
		}
	}

/*
	if (!IS_ERR_VALUE((u32)core_regulator)) {
		regulator_set_voltage(core_regulator,
					OV3640_VOLTAGE_DIGITAL_CORE);

		if (regulator_enable(core_regulator) != 0) {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:core set voltage error\n", __func__);
			return NULL;
		} else {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:core set voltage ok\n", __func__);
		}
	}

	if (!IS_ERR_VALUE((u32)gpo_regulator)) {
		if (regulator_enable(gpo_regulator) != 0) {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:gpo3 enable error\n", __func__);
			return NULL;
		} else {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:gpo3 enable ok\n", __func__);
		}
	}
*/

	if (!IS_ERR_VALUE((u32)analog_regulator)) {
		regulator_set_voltage(analog_regulator, OV3640_VOLTAGE_ANALOG);
		if (regulator_enable(analog_regulator) != 0) {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:analog set voltage error\n", __func__);
			return NULL;
		} else {
			dev_dbg(&ov3640_i2c_client->dev,
				"%s:analog set voltage ok\n", __func__);
		}
	}

	gpio_sensor_active(s32csi_index);

	u32OutWidth  = ov3640_mode_info_data[high_quality].width;
	u32OutHeight = ov3640_mode_info_data[high_quality].height;

	ov3640_interface(interface_param, u32OutWidth, u32OutHeight);
	interface_param->sig.pixel_fmt = ov3640_mode_info_data[high_quality].fmt;
	set_mclk_rate(&interface_param->mclk, s32csi_index);

	ov3640_init_mode(high_quality);

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_config\n"));

	return interface_param;
}

static sensor_interface *ov3640_reset(void)
{
	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_reset\n"));

	return ov3640_config(&reset_frame_rate, ov3640_mode_VGA_640_480);
}

struct camera_sensor camera_sensor_if = {
	.set_color =   ov3640_set_color,
	.get_color =   ov3640_get_color,
	.set_ae_mode = ov3640_set_ae_mode,
	.get_ae_mode = ov3640_get_ae_mode,
	.config =      ov3640_config,
	.reset =       ov3640_reset,
	.set_std =     ov3640_set_std_mode,
	.get_std =     ov3640_get_std_mode,
};

EXPORT_SYMBOL(camera_sensor_if);

/*!
 * ov3640 init function
 *
 * @return  Error code indicating success or failure
 */
static __init int ov3640_init(void)
{
	u8 err;

	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_init\n"));

	err = i2c_add_driver(&ov3640_i2c_driver);

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_init\n"));

	return err;
}

extern void gpio_sensor_inactive(unsigned int csi);
/*!
 * OV3640 cleanup function
 *
 * @return  Error code indicating success or failure
 */
static void __exit ov3640_clean(void)
{
	CAMERA_TRACE(("CAMERA_DBG Entry: ov3640_clean\n"));

	i2c_del_driver(&ov3640_i2c_driver);

	gpio_sensor_inactive(s32csi_index);

	CAMERA_TRACE(("CAMERA_DBG Exit: ov3640_clean\n"));
}

module_init(ov3640_init);
module_exit(ov3640_clean);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("OV3640 Camera Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");
