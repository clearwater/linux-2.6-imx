/*
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

    /*!
     * @file mc9sdz60.c
     * @brief Driver for MC9sdz60
     *
     * @ingroup pmic
     */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/i2c.h>

#include <asm/arch/clock.h>
#include <asm/uaccess.h>
#include "mc9sdz60.h"

/* I2C bus id and device address of mcu */
#define I2C1_BUS	0
#define MC9SDZ60_I2C_ADDR	0xD2	/* 7bits I2C address */
static struct i2c_client *mc9sdz60_i2c_client;

#define DEBUG_MC9SDZ60 1
#if DEBUG_MC9SDZ60
#define DPRINTK(format, args...) printk(KERN_ERR "mc9sdz60: "format"\n", ##args)
#else
#define DPRINTK(format, args...)
#endif

int mc9sdz60_read_reg(u8 reg, u8 *value)
{
	*value = (u8) i2c_smbus_read_byte_data(mc9sdz60_i2c_client, reg);
	return 0;
}

int mc9sdz60_write_reg(u8 reg, u8 value)
{
	if (i2c_smbus_write_byte_data(mc9sdz60_i2c_client, reg, value) < 0) {
		printk(KERN_ERR "%s:write reg errorr:reg=%x,val=%x\n",
		       __func__, reg, value);
		return -1;
	}
	return 0;
}

/*!
 * mc9sdz60 I2C attach function
 *
 * @param adapter            struct i2c_adapter *
 * @return  0
 */
static int mc9sdz60_probe(struct i2c_client *client)
{
	mc9sdz60_i2c_client = client;
	DPRINTK("mc9sdz60_i2c_client = %p", mc9sdz60_i2c_client);
	return 0;
}

/*!
 * mc9sdz60 I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  0
 */
static int mc9sdz60_remove(struct i2c_client *client)
{
	return 0;
}
static struct i2c_driver mc9sdz60_i2c_driver = {
	.driver = {.owner = THIS_MODULE,
		   .name = "mc9sdz60",
		   },
	.probe = mc9sdz60_probe,
	.remove = mc9sdz60_remove,
};

#define SET_BIT_IN_BYTE(byte, pos) (byte |= (0x01 << pos))
#define CLEAR_BIT_IN_BYTE(byte, pos) (byte &= ~(0x01 << pos))

int mc9sdz60_init(void)
{
	int err;
	DPRINTK("Freescale mc9sdz60 driver,\
	     (c) 2008 Freescale Semiconductor, Inc.\n");
	err = i2c_add_driver(&mc9sdz60_i2c_driver);
	if (err) {
		printk(KERN_ERR "mc9sdz60: driver registration failed\n");
		return err;
	}
	DPRINTK("mc9sdz60 inited\n");
	return 0;
}
void mc9sdz60_exit(void)
{
	i2c_del_driver(&mc9sdz60_i2c_driver);
}
