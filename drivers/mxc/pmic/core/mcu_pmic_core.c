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
 * @file mc9sdz60/mcu_pmic_core.c
 * @brief This is the main file of mc9sdz60 Power Control driver.
 *
 * @ingroup PMIC_POWER
 */

/*
 * Includes
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/ioctl.h>
#include <asm/uaccess.h>
#include <asm/arch/gpio.h>

#include <../arch/arm/mach-mx3/iomux.h>

#include <asm/arch/pmic_status.h>
#include <asm/arch/pmic_external.h>
#include "pmic.h"
#include "mc9sdz60.h"
#include "max8660.h"

/* bitfield macros for mcu pmic*/
#define SET_BIT_IN_BYTE(byte, pos) (byte |= (0x01 << pos))
#define CLEAR_BIT_IN_BYTE(byte, pos) (byte &= ~(0x01 << pos))

/*
 * Global variables
 */
static pmic_version_t mxc_pmic_version;
unsigned int active_events[11];

/* default event-enable-value after reset*/
static u8 events_enabled1 = 0x40;
static u8 events_enabled2;

/*
 * Platform device structure for PMIC client drivers
 */
static struct platform_device power_ldm = {
	.name = "pmic_power",
	.id = 1,
};
static struct platform_device rtc_ldm = {
	.name = "pmic_rtc",
	.id = 1,
};

/* map reg names (enum pmic_reg in pmic_external.h) to real addr*/
const static u8 mcu_pmic_reg_addr_table[] = {
	MCU_VERSION,
	MCU_SECS,
	MCU_MINS,
	MCU_HRS,
	MCU_DAY,
	MCU_DATE,
	MCU_MONTH,
	MCU_YEAR,
	MCU_ALARM_SECS,
	MCU_ALARM_MINS,
	MCU_ALARM_HRS,
	MCU_TS_CONTROL,
	MCU_X_LOW,
	MCU_Y_LOW,
	MCU_XY_HIGH,
	MCU_X_LEFT_LOW,
	MCU_X_LEFT_HIGH,
	MCU_X_RIGHT,
	MCU_Y_TOP_LOW,
	MCU_Y_TOP_HIGH,
	MCU_Y_BOTTOM,
	MCU_RESET_1,
	MCU_RESET_2,
	MCU_POWER_CTL,
	MCU_DELAY_CONFIG,
	MCU_GPIO_1,
	MCU_GPIO_2,
	MCU_KPD_1,
	MCU_KPD_2,
	MCU_KPD_CONTROL,
	MCU_INT_ENABLE_1,
	MCU_INT_ENABLE_2,
	MCU_INT_FLAG_1,
	MCU_INT_FLAG_2,
	MCU_DES_FLAG,
	MAX8660_OUTPUT_ENABLE_1,
	MAX8660_OUTPUT_ENABLE_2,
	MAX8660_VOLT_CHANGE_CONTROL,
	MAX8660_V3_TARGET_VOLT_1,
	MAX8660_V3_TARGET_VOLT_2,
	MAX8660_V4_TARGET_VOLT_1,
	MAX8660_V4_TARGET_VOLT_2,
	MAX8660_V5_TARGET_VOLT_1,
	MAX8660_V5_TARGET_VOLT_2,
	MAX8660_V6V7_TARGET_VOLT,
	MAX8660_FORCE_PWM
};

#define DPRINTK(format, args...) printk(KERN_ERR "pmic-core "format"\n", ##args)

int pmic_read(int reg_num, unsigned int *reg_val)
{
	int ret;
	u8 value = 0;
	/* mcu ops */
	if (reg_num >= REG_MCU_VERSION && reg_num <= REG_MCU_DES_FLAG) {

		ret =
		    mc9sdz60_read_reg(mcu_pmic_reg_addr_table[reg_num], &value);
		if (ret < 0)
			goto error1;
		*reg_val = value;
	} else if (reg_num >= REG_MAX8660_OUTPUT_ENABLE_1
		   && reg_num <= REG_MAX8660_FORCE_PWM) {
		ret = max8660_get_buffered_reg_val(reg_num, &value);
		if (ret < 0)
			goto error1;
		*reg_val = value;
	} else {
		DPRINTK("reg_num=%d out of range", reg_num);
		goto error1;
	}

	return 0;

error1:
	return -1;
}

int pmic_write(int reg_num, const unsigned int reg_val)
{
	int ret;
	u8 value = reg_val;
	/* mcu ops */
	if (reg_num >= REG_MCU_VERSION && reg_num <= REG_MCU_DES_FLAG) {

		ret =
		    mc9sdz60_write_reg(mcu_pmic_reg_addr_table[reg_num], value);
		if (ret < 0)
			goto error1;
	} else if (reg_num >= REG_MAX8660_OUTPUT_ENABLE_1
		   && reg_num <= REG_MAX8660_FORCE_PWM) {
		ret =
		    max8660_write_reg(mcu_pmic_reg_addr_table[reg_num], value);

		if (ret < 0)
			goto error1;

		ret = max8660_save_buffered_reg_val(reg_num, value);
	} else {
		DPRINTK("reg_num=%d out of range", reg_num);
		goto error1;
	}

	return 0;

error1:
	DPRINTK(" reg_num = %d, write failed", reg_num);
	return -1;

}

/* for debug*/
void dump_pmic_reg(void)
{
#define REG_DUMP(reg) do {\
		pmic_read_reg(reg, &reg_val, 0xff);\
		DPRINTK("%s = %x", #reg, reg_val); } while (0)
	unsigned int reg_val;

	REG_DUMP(REG_MCU_VERSION);
	REG_DUMP(REG_MCU_SECS);
	REG_DUMP(REG_MCU_MINS);
	REG_DUMP(REG_MCU_HRS);
	REG_DUMP(REG_MCU_DAY);
	REG_DUMP(REG_MCU_DATE);
	REG_DUMP(REG_MCU_MONTH);
	REG_DUMP(REG_MCU_YEAR);
	REG_DUMP(REG_MCU_ALARM_SECS);
	REG_DUMP(REG_MCU_ALARM_MINS);
	REG_DUMP(REG_MCU_ALARM_HRS);
	REG_DUMP(REG_MCU_TS_CONTROL);
	REG_DUMP(REG_MCU_X_LOW);
	REG_DUMP(REG_MCU_Y_LOW);
	REG_DUMP(REG_MCU_XY_HIGH);
	REG_DUMP(REG_MCU_X_LEFT_LOW);
	REG_DUMP(REG_MCU_X_LEFT_HIGH);
	REG_DUMP(REG_MCU_X_RIGHT);
	REG_DUMP(REG_MCU_Y_TOP_LOW);
	REG_DUMP(REG_MCU_Y_TOP_HIGH);
	REG_DUMP(REG_MCU_Y_BOTTOM);
	REG_DUMP(REG_MCU_RESET_1);
	REG_DUMP(REG_MCU_RESET_2);
	REG_DUMP(REG_MCU_POWER_CTL);
	REG_DUMP(REG_MCU_DELAY_CONFIG);
	REG_DUMP(REG_MCU_GPIO_1);
	REG_DUMP(REG_MCU_GPIO_2);
	REG_DUMP(REG_MCU_KPD_1);
	REG_DUMP(REG_MCU_KPD_2);
	REG_DUMP(REG_MCU_KPD_CONTROL);
	REG_DUMP(REG_MCU_INT_ENABLE_1);
	REG_DUMP(REG_MCU_INT_ENABLE_2);
	REG_DUMP(REG_MCU_INT_FLAG_1);
	REG_DUMP(REG_MCU_INT_FLAG_2);
}

/*!
 * This function unsets a bit in mask register of pmic to unmask an event IT.
 *
 * @param	event 	the event to be unmasked
 *
 * @return    This function returns PMIC_SUCCESS on SUCCESS, error on FAILURE.
 */
int pmic_event_unmask(type_event event)
{
	int reg_name;
	u8 reg_mask = 0;
	unsigned int event_bit = 0;
	int ret = -1;

	switch (event) {
	case EVENT_HEADPHONE_DET:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 0;
		break;
	case EVENT_SD1_DET:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 2;
		break;
	case EVENT_SD1_WP:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 3;
		break;
	case EVENT_SD2_DET:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 4;
		break;
	case EVENT_SD2_WP:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 5;
		break;
	case EVENT_GPS_INT:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 1;
		break;
	case EVENT_POWER_KEY:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 6;
		break;
	case EVENT_KEYPAD:
		reg_name = REG_MCU_INT_ENABLE_2;
		event_bit = 2;
		break;
	case EVENT_RTC:
		reg_name = REG_MCU_INT_ENABLE_2;
		event_bit = 0;
		break;
	case EVENT_TS_ADC:
		reg_name = REG_MCU_INT_ENABLE_2;
		event_bit = 1;
		break;
	default:
		return PMIC_ERROR;
	}
	SET_BIT_IN_BYTE(reg_mask, event_bit);
	ret = pmic_write_reg(reg_name, reg_mask, reg_mask);
	if (PMIC_SUCCESS == ret) {

		if (REG_MCU_INT_ENABLE_2 == reg_name)
			events_enabled2 |= reg_mask;
		else if (REG_MCU_INT_ENABLE_1 == reg_name)
			events_enabled1 |= reg_mask;
		pr_debug("Enable Event : %d\n", event);
	}

	return ret;
}

/*!
 * This function sets a bit in mask register of pmic to disable an event IT.
 *
 * @param	event 	the event to be masked
 *
 * @return     This function returns PMIC_SUCCESS on SUCCESS, error on FAILURE.
 */
int pmic_event_mask(type_event event)
{
	int reg_name;
	u8 reg_mask = 0;
	unsigned int event_bit = 0;
	int ret = -1;

	switch (event) {
	case EVENT_HEADPHONE_DET:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 0;
		break;
	case EVENT_SD1_DET:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 2;
		break;
	case EVENT_SD1_WP:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 3;
		break;
	case EVENT_SD2_DET:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 4;
		break;
	case EVENT_SD2_WP:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 5;
		break;
	case EVENT_GPS_INT:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 1;
		break;
	case EVENT_POWER_KEY:
		reg_name = REG_MCU_INT_ENABLE_1;
		event_bit = 6;
		break;
	case EVENT_KEYPAD:
		reg_name = REG_MCU_INT_ENABLE_2;
		event_bit = 2;
		break;
	case EVENT_RTC:
		reg_name = REG_MCU_INT_ENABLE_2;
		event_bit = 0;
		break;
	case EVENT_TS_ADC:
		reg_name = REG_MCU_INT_ENABLE_2;
		event_bit = 1;
		break;
	default:
		return PMIC_ERROR;
	}
	SET_BIT_IN_BYTE(reg_mask, event_bit);

	ret = pmic_write_reg(reg_name, 0, reg_mask);
	if (PMIC_SUCCESS == ret) {

		if (REG_MCU_INT_ENABLE_2 == reg_name)
			events_enabled2 &= ~reg_mask;
		else if (REG_MCU_INT_ENABLE_1 == reg_name)
			events_enabled1 &= ~reg_mask;
		pr_debug("Disable Event : %d\n", event);
	}

	return ret;
}

/*!
 * This function reads the interrupt status registers of PMIC
 * and determine the current active events.
 *
 * @param 	active_events array pointer to be used to return active
 *		event numbers.
 *
 * @return       This function returns PMIC version.
 */
unsigned int pmic_get_active_events(unsigned int *active_events)
{
	unsigned int count = 0;
	unsigned int flag1, flag2;
	int bit_set;

	/* read int flags and ack int */
	pmic_read(REG_MCU_INT_FLAG_1, &flag1);
	pmic_read(REG_MCU_INT_FLAG_2, &flag2);
	pmic_write(REG_MCU_INT_FLAG_1, 0);
	pmic_write(REG_MCU_INT_FLAG_2, 0);
	DPRINTK("pmic interrupt flag1=%x, flag2=%x", flag1, flag2);

	flag1 &= events_enabled1;
	flag2 &= events_enabled2;

	while (flag1) {
		bit_set = ffs(flag1) - 1;
		*(active_events + count) = bit_set;
		count++;
		flag1 ^= (1 << bit_set);
	}
	while (flag2) {
		bit_set = ffs(flag2) - 1;
		*(active_events + count) = bit_set + 7;
		count++;
		flag2 ^= (1 << bit_set);
	}

	return count;
}

void pmic_bh_handler(struct work_struct *work);

/*
 * External functions
 */
extern void pmic_event_list_init(void);
extern void pmic_event_callback(type_event event);
extern void gpio_pmic_active(void);

/*!
 * Bottom half handler of PMIC event handling.
 */
DECLARE_WORK(pmic_ws, pmic_bh_handler);

/*!
 * This function registers platform device structures for
 * PMIC client drivers.
 */
static void pmic_pdev_register(void)
{
	platform_device_register(&rtc_ldm);
	platform_device_register(&power_ldm);
	reg_max8660_probe();
}

/*!
 * This function unregisters platform device structures for
 * PMIC client drivers.
 */
static void pmic_pdev_unregister(void)
{
	platform_device_unregister(&rtc_ldm);
}

/*!
 * This function is called when pmic interrupt occurs on the processor.
 * It is the interrupt handler for the pmic module.
 *
 * @param        irq        the irq number
 * @param        dev_id     the pointer on the device
 *
 * @return       The function returns IRQ_HANDLED when handled.
 */
static irqreturn_t pmic_irq_handler(int irq, void *dev_id)
{
	/* prepare a task */
	schedule_work(&pmic_ws);

	return IRQ_HANDLED;
}

/*!
 * This function is the bottom half handler of the PMIC interrupt.
 * It checks for active events and launches callback for the
 * active events.
 */
void pmic_bh_handler(struct work_struct *work)
{
	unsigned int loop;
	unsigned int count = 0;

	count = pmic_get_active_events(active_events);

	for (loop = 0; loop < count; loop++)
		pmic_event_callback(active_events[loop]);
}

/*!
 * This function is used to determine the PMIC type and its revision.
 *
 * @return      Returns the PMIC type and its revision.
 */

pmic_version_t pmic_get_version(void)
{
	return mxc_pmic_version;
}
EXPORT_SYMBOL(pmic_get_version);

static int __init mcu_pmic_init(void)
{
	int err;

	/* init chips */
	err = max8660_init();
	if (err)
		goto fail1;

	err = mc9sdz60_init();
	if (err)
		goto fail1;

	/* Initialize the PMIC event handling */
	pmic_event_list_init();

	/* Set and install PMIC IRQ handler */
	mxc_request_iomux(MX35_PIN_GPIO1_0, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);
	mxc_iomux_set_pad(MX35_PIN_GPIO1_0, PAD_CTL_PKE_NONE);
	mxc_set_gpio_direction(MX35_PIN_GPIO1_0, 1);	/* input */

	err = set_irq_type(IOMUX_TO_IRQ(MX35_PIN_GPIO1_0), IRQT_RISING);
	err = request_irq(IOMUX_TO_IRQ(MX35_PIN_GPIO1_0), pmic_irq_handler,
			  0, "PMIC_IRQ", 0);
	if (err) {
		DPRINTK("mcu pmic request irq failed");
		goto fail1;
	}

	pmic_pdev_register();

	return 0;

fail1:
	return err;
}

static void __exit mcu_pmic_exit(void)
{
	free_irq(IOMUX_TO_IRQ(MX35_PIN_GPIO1_1), 0);
	pmic_pdev_unregister();
}

subsys_initcall_sync(mcu_pmic_init);
module_exit(mcu_pmic_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("mcu pmic driver");
MODULE_LICENSE("GPL");
