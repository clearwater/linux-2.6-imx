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
 * @file dvfs_core.c
 *
 * @brief A simplied driver for the Freescale Semiconductor MXC DVFS module.
 *
 * Upon initialization, the DVFS driver initializes the DVFS hardware
 * sets up driver nodes attaches to the DVFS interrupt and initializes internal
 * data structures. When the DVFS interrupt occurs the driver checks the cause
 * of the interrupt (lower frequency, increase frequency or emergency) and
 * changes the CPU voltage according to translation table that is loaded into
 * the driver.
 *
 * @ingroup PM
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/regulator/regulator.h>
#include <linux/input.h>

#include <asm/arch/gpio.h>

#include "iomux.h"
#include "crm_regs.h"

#define DRIVER_NAME "DVFSCORE"

#define MXC_DVFSTHRS_UPTHR_MASK               0x0FC00000
#define MXC_DVFSTHRS_UPTHR_OFFSET             22
#define MXC_DVFSTHRS_DNTHR_MASK               0x003F0000
#define MXC_DVFSTHRS_DNTHR_OFFSET             16
#define MXC_DVFSTHRS_PNCTHR_MASK              0x0000003F
#define MXC_DVFSTHRS_PNCTHR_OFFSET            0

#define MXC_DVFSCOUN_DNCNT_MASK               0x00FF0000
#define MXC_DVFSCOUN_DNCNT_OFFSET             16
#define MXC_DVFSCOUN_UPCNT_MASK              0x000000FF
#define MXC_DVFSCOUN_UPCNT_OFFSET            0

#define MXC_DVFSCNTR_LBMI                    0x08000000
#define MXC_DVFSCNTR_DVFIS                   0x01000000
#define MXC_DVFSCNTR_FSVAIM                  0x00400000
#define MXC_DVFSCNTR_FSVAI_MASK              0x00300000
#define MXC_DVFSCNTR_FSVAI_OFFSET            20
#define MXC_DVFSCNTR_MAXF_MASK               0x00040000
#define MXC_DVFSCNTR_MAXF_OFFSET             18
#define MXC_DVFSCNTR_MINF_MASK               0x00020000
#define MXC_DVFSCNTR_MINF_OFFSET             17
#define MXC_DVFSCNTR_LTBRSR_MASK             0x00000018
#define MXC_DVFSCNTR_LTBRSR_OFFSET           3
#define MXC_DVFSCNTR_DIV3CK_MASK             0x00000006
#define MXC_DVFSCNTR_DIV3CK_OFFSET           1
#define MXC_DVFSCNTR_DVFEN                   0x00000001

#define MXC_GPCCNTR_GPCIRQ                   0x00100000
#define MXC_GPCCNTR_DVFS0CR                  0x00010000
#define MXC_GPCCNTR_ADU                      0x00008000
#define MXC_GPCCNTR_STRT                     0x00004000
#define MXC_GPCCNTR_FUPD                     0x00002000
#define MXC_GPCCNTR_HTRI_MASK                0x0000000F
#define MXC_GPCCNTR_HTRI_OFFSET              0

#define MXC_GPCVCR_VINC_MASK                 0x00020000
#define MXC_GPCVCR_VINC_OFFSET               17

/*
 * Voltage can be set for core.
 */
#define GP_VOLTAGE_MAX_uV		1000000
#define VOLTAGE_CHANGED_STEP_uV		25000

/*
 * Maximum and minimum values of ARM_PODF.
 */
#define ARM_PODF_MIN		0
#define ARM_PODF_MAX		7

static struct regulator *vddgp_reg;
static struct delayed_work dvfs_core_work;
static int dvfs_is_active;
static int upthr;
static int dnthr;
static int pncthr;
static int dncnt;
static int upcnt;
static int maxf;		/* 1=maximum freq reached */
static int minf;		/* 1=minimum freq reached */
static int arm_podf;
static int fupd;		/* freq update needed if = 1 */
static int vinc;		/* 1=freq up; 0=freq down */
static int dvfs_int_no;
static int div3ck;
static int ltbrsr;
static int ARM_FREQ_SHIFT_DIVIDER;
static int htri;
static int vddgp;
static int irq_times;

/* Used for tracking the number of interrupts */
static u32 dvfs_nr_up[4];
static u32 dvfs_nr_dn[4];

/*
 * Clock structures
 */
static struct clk *cpu_clk;
static struct clk *ahb_clk;

enum {
	FSVAI_FREQ_NOCHANGE = 0x0,
	FSVAI_FREQ_INCREASE,
	FSVAI_FREQ_DECREASE,
	FSVAI_FREQ_EMERG,
};

DEFINE_SPINLOCK(mxc_dvfs_core_lock);

static int start_dvfs(void)
{
	u32 reg, flags;

	if (dvfs_is_active)
		return 0;

	irq_times = 0;
	dvfs_int_no = 0;
	htri = 0;

	/* config reg GPC_CNTR */
	reg = __raw_readl(MXC_GPC_CNTR);

	/* GPCIRQ=1, select ARM IRQ */
	reg |= MXC_GPCCNTR_GPCIRQ;
	/* ADU=1, select ARM domain */
	reg |= MXC_GPCCNTR_ADU;
	__raw_writel(reg, MXC_GPC_CNTR);

	/* Set DVFS regs for freq-down for the first dvfs routine */
	upthr = 53;
	dnthr = 33;
	pncthr = 55;
	div3ck = 3;
	ltbrsr = 1;
	reg = 0;
	reg |= upthr << MXC_DVFSTHRS_UPTHR_OFFSET;
	reg |= dnthr << MXC_DVFSTHRS_DNTHR_OFFSET;
	reg |= pncthr << MXC_DVFSTHRS_PNCTHR_OFFSET;
	__raw_writel(reg, MXC_DVFSTHRS);

	dncnt = 0x0a;
	upcnt = 0x05;
	reg = 0;
	reg |= dncnt << MXC_DVFSCOUN_DNCNT_OFFSET;
	reg |= upcnt << MXC_DVFSCOUN_UPCNT_OFFSET;
	__raw_writel(reg, MXC_DVFSCOUN);

	/* EMAC=0x100 */
	__raw_writel(0x100, MXC_DVFSEMAC);

	/* set MAXF, MINF according to ARM_PODF */
	maxf = 0;
	minf = 0;
	arm_podf = __raw_readl(MXC_CCM_CACRR) & MXC_CCM_CACRR_ARM_PODF_MASK;
	if (arm_podf == ARM_PODF_MIN)
		maxf = 1;
	if (arm_podf == ARM_PODF_MAX)
		minf = 1;
	vddgp = GP_VOLTAGE_MAX_uV;

	/* Set the voltage for the GP domain. */
	vddgp_reg = regulator_get(NULL, "DCDC1");

	spin_lock_irqsave(&mxc_dvfs_core_lock, flags);

	/* Mask load buffer full interrupt */
	reg = __raw_readl(MXC_DVFSCNTR);
	reg |= MXC_DVFSCNTR_LBMI;
	/* Select ARM domain */
	reg |= MXC_DVFSCNTR_DVFIS;
	/* Enable DVFS frequency adjustment interrupt */
	reg = (reg & ~MXC_DVFSCNTR_FSVAIM);
	/* Set MAXF, MINF */
	reg = (reg & ~(MXC_DVFSCNTR_MAXF_MASK | MXC_DVFSCNTR_MINF_MASK));
	reg |= maxf << MXC_DVFSCNTR_MAXF_OFFSET;
	reg |= minf << MXC_DVFSCNTR_MINF_OFFSET;
	/* Set load tracking buffer register source */
	reg = (reg & ~MXC_DVFSCNTR_LTBRSR_MASK);
	reg |= ltbrsr << MXC_DVFSCNTR_LTBRSR_OFFSET;
	/* DIV3CK=3 */
	reg = (reg & ~MXC_DVFSCNTR_DIV3CK_MASK);
	reg |= div3ck << MXC_DVFSCNTR_DIV3CK_OFFSET;
	/* Enable DVFS */
	reg |= MXC_DVFSCNTR_DVFEN;
	__raw_writel(reg, MXC_DVFSCNTR);

	dvfs_is_active = 1;

	spin_unlock_irqrestore(&mxc_dvfs_core_lock, flags);

	pr_info("DVFS is started\n");

	return 0;
}

/*!
 * This function is called for module initialization.
 * It sets up the DVFS hardware.
 * It sets default values for DVFS thresholds and counters. The default
 * values was chosen from a set of different reasonable values. They was tested
 * and the default values in the driver gave the best results.
 * More work should be done to find optimal values.
 *
 * @return   0 if successful; non-zero otherwise.
 *
 */
static int init_dvfs_controller(void)
{
	mxc_request_iomux(MX37_PIN_BOOT_MODE0, IOMUX_CONFIG_ALT5);
	mxc_iomux_set_pad(MX37_PIN_BOOT_MODE0, 0x0);

	mxc_request_iomux(MX37_PIN_BOOT_MODE1, IOMUX_CONFIG_ALT5);
	mxc_iomux_set_pad(MX37_PIN_BOOT_MODE1, 0x0);

	__raw_writel(0x01090080, MXC_CCM_CCOSR);
	return 0;
}

static irqreturn_t dvfs_irq(int irq, void *dev_id)
{
	u32 reg;

	/* Check if DVFS0 (ARM) id requesting for freqency/voltage update */
	if ((__raw_readl(MXC_GPC_CNTR) & MXC_GPCCNTR_DVFS0CR) == 0)
		return IRQ_HANDLED;

	dvfs_int_no++;

	/* Mask DVFS irq */
	reg = __raw_readl(MXC_DVFSCNTR);
	/* FSVAIM=1 */
	reg |= MXC_DVFSCNTR_FSVAIM;
	__raw_writel(reg, MXC_DVFSCNTR);

	schedule_delayed_work(&dvfs_core_work, 0);

	return IRQ_HANDLED;
}

static void dvfs_core_workqueue_handler(struct work_struct *work)
{
	u32 fsvai;
	u32 reg;

	/* Check DVFS frequency adjustment interrupt status */
	reg = __raw_readl(MXC_DVFSCNTR);
	fsvai = (reg & MXC_DVFSCNTR_FSVAI_MASK) >> MXC_DVFSCNTR_FSVAI_OFFSET;
	/* Get MAXF, MINF */
	maxf = (reg & MXC_DVFSCNTR_MAXF_MASK) >> MXC_DVFSCNTR_MAXF_OFFSET;
	minf = (reg & MXC_DVFSCNTR_MINF_MASK) >> MXC_DVFSCNTR_MINF_OFFSET;
	printk(KERN_WARNING "maxf = %d, minf = %d\n", maxf, minf);
	printk(KERN_WARNING "fsvai= 0x%x\n", fsvai);

	/* Check FSVAI, FSVAI=0 is error */
	if (fsvai == FSVAI_FREQ_NOCHANGE) {
		/* Do nothing. Freq change is not required */
		printk(KERN_WARNING "fsvai should not be 0\n");
		return;
	}

	/* If FSVAI indicate freq up, check arm-clk is not in highest mode */
	if ((fsvai == FSVAI_FREQ_INCREASE) || (fsvai == FSVAI_FREQ_EMERG)) {
		if (maxf == 1) {
			/* Do nothing. Freq change is not required */
			printk(KERN_WARNING "fsvai can not go up any more\n");
			return;
		} else {
			fupd = 1;
			vinc = 1;	/* freq up */
		}
	}

	/* If FSVAI indicate freq down, check arm-clk is not in lowest mode */
	if (fsvai == FSVAI_FREQ_DECREASE) {
		if (minf == 1) {
			/* Do nothing. Freq change is not required */
			printk(KERN_WARNING "fsvai can not go down any more\n");
			return;
		} else {
			fupd = 1;
			vinc = 0;	/* freq down */
		}
	}
	printk(KERN_WARNING "fupd = %d, vinc = %d\n", fupd, vinc);

	/* expect freq-down for first dvfs routine */
	if ((dvfs_int_no == 1) && (vinc == 1)) {
		/* Do nothing. Freq change is not required */
		printk(KERN_WARNING "fsvai can not go up any more\n");
		return;
	}

	/* ARM_FREQ_SHIFT_DIVIDER=1: change arm_podf only */
	ARM_FREQ_SHIFT_DIVIDER = 1;

	/* Set ARM_FREQ_SHIFT_DIVIDER = 1 */
	reg = __raw_readl(MXC_CCM_CDCR);
	reg = (reg & ~MXC_CCM_CDCR_ARM_FREQ_SHIFT_DIVIDER);
	reg |= MXC_CCM_CDCR_ARM_FREQ_SHIFT_DIVIDER;
	__raw_writel(reg, MXC_CCM_CDCR);

	/* Get ARM_PODF */
	reg = __raw_readl(MXC_CCM_CACRR);
	arm_podf =
	    (reg & MXC_CCM_CACRR_ARM_PODF_MASK) >>
	    MXC_CCM_CACRR_ARM_PODF_OFFSET;
	printk(KERN_WARNING "arm_podf = %d\n", arm_podf);

	if ((dvfs_int_no == 1) && (vinc == 0)) {
		arm_podf++;
		/* set ARM_PODF */
		reg = (reg & ~MXC_CCM_CACRR_ARM_PODF_MASK);
		reg |= arm_podf;
		__raw_writel(reg, MXC_CCM_CACRR);

		/* Set voltage VDDGP */
		vddgp -= VOLTAGE_CHANGED_STEP_uV;
		printk(KERN_WARNING "Change voltage to %d uV (1st)\n", vddgp);
		regulator_set_voltage(vddgp_reg, vddgp);
	} else if ((vinc == 0) && (arm_podf == ARM_PODF_MIN)) {
		arm_podf++;	/* freq down */
		/* Set ARM_PODF */
		reg = (reg & ~MXC_CCM_CACRR_ARM_PODF_MASK);
		reg |= arm_podf;
		__raw_writel(reg, MXC_CCM_CACRR);

		vddgp -= VOLTAGE_CHANGED_STEP_uV;
		printk(KERN_WARNING "Change voltage to %d uV\n", vddgp);
		regulator_set_voltage(vddgp_reg, vddgp);
	} else if ((vinc == 0) && (arm_podf > 0)) {
		if (arm_podf < ARM_PODF_MAX) {
			printk(KERN_WARNING "arm_podf is less than 7 now\n");
			arm_podf++;	/* freq down */
			/* set ARM_PODF */
			reg = (reg & ~MXC_CCM_CACRR_ARM_PODF_MASK);
			reg |= arm_podf;
			__raw_writel(reg, MXC_CCM_CACRR);

			vddgp -= VOLTAGE_CHANGED_STEP_uV;
			printk(KERN_WARNING "Change voltage to %d uV\n", vddgp);
			regulator_set_voltage(vddgp_reg, vddgp);
		} else {
			printk(KERN_WARNING "freq can not be changed\n");
			return;
		}

	} else if ((vinc == 1) && (arm_podf > 0)) {
		arm_podf -= 1;	/* freq up */
		if (arm_podf >= ARM_PODF_MIN) {
			vddgp += VOLTAGE_CHANGED_STEP_uV;
			printk(KERN_WARNING "Change voltage to %d uV\n", vddgp);
			regulator_set_voltage(vddgp_reg, vddgp);

			/* set ARM_PODF */
			reg = (reg & ~MXC_CCM_CACRR_ARM_PODF_MASK);
			reg |= arm_podf;
			__raw_writel(reg, MXC_CCM_CACRR);
		}
	} else {
		printk(KERN_WARNING "freq can not be changed\n");
		return;
	}

	reg = __raw_readl(MXC_CCM_CDHIPR);
	if (ARM_FREQ_SHIFT_DIVIDER == 1) {
		if ((reg & MXC_CCM_CDHIPR_ARM_PODF_BUSY) == 0x0) {
			/* ARM_PODF_BUSY = 1 */
			printk(KERN_WARNING "ARM_PODF_BUSY = 0\n");
			return;
		}
	} else {
		if ((reg & MXC_CCM_CDHIPR_ARM_PODF_BUSY) != 0x0) {
			/* ARM_PODF_BUSY = 0 */
			printk(KERN_WARNING "ARM_PODF_BUSY = 1\n");
			return;
		}
	}

	/* START the GPC main control FSM */
	/*  If FUPD=1, freq update needed */
	if (fupd == 1) {
		printk(KERN_WARNING "START the GPC main control FSM\n");
		/* set VINC */
		reg = __raw_readl(MXC_GPC_VCR);
		reg = (reg & ~MXC_GPCVCR_VINC_MASK);
		reg |= vinc << MXC_GPCVCR_VINC_OFFSET;
		__raw_writel(reg, MXC_GPC_VCR);

		if (vinc == 0) {
			/* freq down */
			printk(KERN_WARNING "freq down\n");
		}

		reg = __raw_readl(MXC_GPC_CNTR);
		/* STRT=1 */
		reg |= MXC_GPCCNTR_STRT;
		/* set FUPD */
		reg = (reg & ~MXC_GPCCNTR_FUPD);
		reg |= MXC_GPCCNTR_FUPD;
		/* set GPC_CNTR.HTRI */
		reg = (reg & ~MXC_GPCCNTR_HTRI_MASK);
		reg |= (htri << MXC_GPCCNTR_HTRI_OFFSET);
		__raw_writel(reg, MXC_GPC_CNTR);
	}

	htri++;
	if (htri > 15)
		htri = 0;

	if (arm_podf == ARM_PODF_MAX) {
		/* freq-up for all routines except the first routine */
		printk(KERN_WARNING "freq-up\n");
		upthr = 31;
		dnthr = 28;
		pncthr = 63;	/* freq up only */
		reg = 0;
		reg |= upthr << MXC_DVFSTHRS_UPTHR_OFFSET;
		reg |= dnthr << MXC_DVFSTHRS_DNTHR_OFFSET;
		reg |= pncthr;
		__raw_writel(reg, MXC_DVFSTHRS);

		dncnt = 33;
		upcnt = 33;
		reg = 0;
		reg |= dncnt << MXC_DVFSCOUN_DNCNT_OFFSET;
		reg |= upcnt << MXC_DVFSCOUN_UPCNT_OFFSET;
		__raw_writel(reg, MXC_DVFSCOUN);
	}

	if (arm_podf == ARM_PODF_MIN) {
		/* freq-up for all routines except the first routine */
		printk(KERN_WARNING "freq-down\n");
		upthr = 53;
		dnthr = 33;
		pncthr = 55;	/* freq up only */
		reg = 0;
		reg |= upthr << MXC_DVFSTHRS_UPTHR_OFFSET;
		reg |= dnthr << MXC_DVFSTHRS_DNTHR_OFFSET;
		reg |= pncthr << MXC_DVFSTHRS_PNCTHR_OFFSET;
		__raw_writel(reg, MXC_DVFSTHRS);

		dncnt = 0x0a;
		upcnt = 0x05;
		reg = 0;
		reg |= dncnt << MXC_DVFSCOUN_DNCNT_OFFSET;
		reg |= upcnt << MXC_DVFSCOUN_UPCNT_OFFSET;
		__raw_writel(reg, MXC_DVFSCOUN);
	}

	/* Set MAXF, MINF for next routine according to ARM_PODF */
	maxf = 0;
	minf = 0;
	arm_podf = __raw_readl(MXC_CCM_CACRR) & MXC_CCM_CACRR_ARM_PODF_MASK;
	if (arm_podf == ARM_PODF_MIN)
		maxf = 1;
	if (arm_podf == ARM_PODF_MAX)
		minf = 1;

	/* Enable FVFS interrupt */
	reg = __raw_readl(MXC_DVFSCNTR);
	/* FSVAIM=0 */
	reg = (reg & ~MXC_DVFSCNTR_FSVAIM);
	/* Set MAXF, MINF */
	reg = (reg & ~(MXC_DVFSCNTR_MAXF_MASK | MXC_DVFSCNTR_MINF_MASK));
	reg |= maxf << MXC_DVFSCNTR_MAXF_OFFSET;
	reg |= minf << MXC_DVFSCNTR_MINF_OFFSET;
	__raw_writel(reg, MXC_DVFSCNTR);
}

/*!
 * This function disables the DVFS module.
 */
static void stop_dvfs(void)
{
	u32 reg = 0;
	u32 flags;

	if (dvfs_is_active) {
		spin_lock_irqsave(&mxc_dvfs_core_lock, flags);

		/* Mask dvfs irq, disable DVFS */
		reg = __raw_readl(MXC_DVFSCNTR);
		/* FSVAIM=1 */
		reg |= MXC_DVFSCNTR_FSVAIM;
		reg = (reg & ~MXC_DVFSCNTR_DVFEN);
		__raw_writel(reg, MXC_DVFSCNTR);

		dvfs_is_active = 0;
		spin_unlock_irqrestore(&mxc_dvfs_core_lock, flags);

		if (arm_podf > 0) {
			arm_podf = 0;
			vddgp = GP_VOLTAGE_MAX_uV;
			printk(KERN_WARNING "Change voltage to %d uV\n", vddgp);
			regulator_set_voltage(vddgp_reg, vddgp);

			/* set ARM_PODF */
			reg = (reg & ~MXC_CCM_CACRR_ARM_PODF_MASK);
			reg |= arm_podf;
			__raw_writel(reg, MXC_CCM_CACRR);
		}

	}

	pr_info("DVFS is stopped\n");
}

static ssize_t dvfs_enable_store(struct sys_device *dev, const char *buf,
				 size_t size)
{
	if (strstr(buf, "1") != NULL) {
		if (start_dvfs() != 0)
			printk(KERN_ERR "Failed to start DVFS\n");
	} else if (strstr(buf, "0") != NULL)
		stop_dvfs();

	return size;
}

static ssize_t dvfs_status_show(struct sys_device *dev, char *buf)
{
	int size = 0;

	if (dvfs_is_active)
		size = sprintf(buf, "DVFS is enabled\n");
	else
		size = sprintf(buf, "DVFS is disabled\n");

	return size;
}

static ssize_t dvfs_status_store(struct sys_device *dev, const char *buf,
				 size_t size)
{
	if (strstr(buf, "reset") != NULL) {
		int i;
		for (i = 0; i < 4; i++) {
			dvfs_nr_up[i] = 0;
			dvfs_nr_dn[i] = 0;
		}
	}

	return size;
}

static SYSDEV_ATTR(enable, 0200, NULL, dvfs_enable_store);
static SYSDEV_ATTR(status, 0644, dvfs_status_show, dvfs_status_store);

static struct sysdev_class dvfs_sysclass = {
	set_kset_name("dvfs"),
};

static struct sys_device dvfs_device = {
	.id = 0,
	.cls = &dvfs_sysclass,
};

static int dvfs_sysdev_ctrl_init(void)
{
	int err;

	err = sysdev_class_register(&dvfs_sysclass);
	if (!err)
		err = sysdev_register(&dvfs_device);
	if (!err) {
		err = sysdev_create_file(&dvfs_device, &attr_enable);
		err = sysdev_create_file(&dvfs_device, &attr_status);
	}

	return err;
}

static void dvfs_sysdev_ctrl_exit(void)
{
	sysdev_remove_file(&dvfs_device, &attr_enable);
	sysdev_remove_file(&dvfs_device, &attr_status);
	sysdev_unregister(&dvfs_device);
	sysdev_class_unregister(&dvfs_sysclass);
}

static int dvfs_i2c_remove(struct i2c_client *client)
{
	int err;

	stop_dvfs();

	/* release the DVFS interrupt */
	free_irq(MXC_INT_GPC1, NULL);

	dvfs_sysdev_ctrl_exit();

	clk_put(cpu_clk);
	clk_put(ahb_clk);

	err = i2c_detach_client(client);
	if (err) {
		dev_err(&client->dev, "Client deregistration failed, "
			"client not detached.\n");
		return err;
	}

	return 0;
}

static int dvfs_i2c_probe(struct i2c_client *client)
{
	int err = 0;

	INIT_DELAYED_WORK(&dvfs_core_work, dvfs_core_workqueue_handler);

	cpu_clk = clk_get(NULL, "cpu_clk");
	ahb_clk = clk_get(NULL, "ahb_clk");
	err = init_dvfs_controller();
	if (err) {
		printk(KERN_ERR "DVFS: Unable to initialize DVFS");
		return err;
	}

	/* request the DVFS interrupt */
	err = request_irq(MXC_INT_GPC1, dvfs_irq, IRQF_DISABLED, "dvfs", NULL);
	if (err)
		printk(KERN_ERR "DVFS: Unable to attach to DVFS interrupt");

	err = dvfs_sysdev_ctrl_init();
	if (err) {
		printk(KERN_ERR
		       "DVFS: Unable to register sysdev entry for dvfs");
		return err;
	}

	return err;
}

static struct i2c_driver dvfs_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   },
	.probe = dvfs_i2c_probe,
	.remove = dvfs_i2c_remove,
	.command = NULL,
};

static int __init dvfs_init(void)
{
	printk(KERN_ERR "DVFS: dvfs_init");
	return i2c_add_driver(&dvfs_driver);
}

static void __exit dvfs_cleanup(void)
{
	i2c_del_driver(&dvfs_driver);
}

module_init(dvfs_init);
module_exit(dvfs_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("DVFS driver");
MODULE_LICENSE("GPL");
