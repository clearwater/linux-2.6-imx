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
#include <linux/regulator/regulator.h>
#include <linux/input.h>

#include "iomux.h"
#include "crm_regs.h"

#define DRIVER_NAME "DVFSCORE"

#define MXC_DVFSTHRS		(MXC_DVFS_CORE_BASE + 0x00)
#define MXC_DVFSCOUN		(MXC_DVFS_CORE_BASE + 0x04)
#define MXC_DVFSSIG1		(MXC_DVFS_CORE_BASE + 0x08)
#define MXC_DVFSSIG0		(MXC_DVFS_CORE_BASE + 0x0C)
#define MXC_DVFSGPC0		(MXC_DVFS_CORE_BASE + 0x10)
#define MXC_DVFSGPC1		(MXC_DVFS_CORE_BASE + 0x14)
#define MXC_DVFSGPBT		(MXC_DVFS_CORE_BASE + 0x18)
#define MXC_DVFSEMAC		(MXC_DVFS_CORE_BASE + 0x1C)
#define MXC_DVFSCNTR		(MXC_DVFS_CORE_BASE + 0x20)
#define MXC_DVFSLTR0_0		(MXC_DVFS_CORE_BASE + 0x24)
#define MXC_DVFSLTR0_1		(MXC_DVFS_CORE_BASE + 0x28)
#define MXC_DVFSLTR1_0		(MXC_DVFS_CORE_BASE + 0x2C)
#define MXC_DVFSLTR1_1		(MXC_DVFS_CORE_BASE + 0x30)
#define MXC_DVFSPT0 		(MXC_DVFS_CORE_BASE + 0x34)
#define MXC_DVFSPT1 		(MXC_DVFS_CORE_BASE + 0x38)
#define MXC_DVFSPT2 		(MXC_DVFS_CORE_BASE + 0x3C)
#define MXC_DVFSPT3 		(MXC_DVFS_CORE_BASE + 0x40)

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

#define MXC_DVFSEMAC_EMAC_MASK               0x000001FF
#define MXC_DVFSEMAC_EMAC_OFFSET             0

#define MXC_DVFSCNTR_DVFEV                   0x10000000
#define MXC_DVFSCNTR_LBMI                    0x08000000
#define MXC_DVFSCNTR_LBFL                    0x06000000
#define MXC_DVFSCNTR_DVFIS                   0x01000000
#define MXC_DVFSCNTR_FSVAIM                  0x00400000
#define MXC_DVFSCNTR_FSVAI_MASK              0x00300000
#define MXC_DVFSCNTR_FSVAI_OFFSET            20
#define MXC_DVFSCNTR_WFIM                    0x00080000
#define MXC_DVFSCNTR_WFIM_OFFSET             19
#define MXC_DVFSCNTR_MAXF_MASK               0x00040000
#define MXC_DVFSCNTR_MAXF_OFFSET             18
#define MXC_DVFSCNTR_MINF_MASK               0x00020000
#define MXC_DVFSCNTR_MINF_OFFSET             17
#define MXC_DVFSCNTR_LTBRSR_MASK             0x00000018
#define MXC_DVFSCNTR_LTBRSR_OFFSET           3
#define MXC_DVFSCNTR_DIV3CK_MASK             0xE0000000
#define MXC_DVFSCNTR_DIV3CK_OFFSET           29
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
#define MXC_GPCVCR_VCNTU_MASK                0x00010000
#define MXC_GPCVCR_VCNTU_OFFSET              16
#define MXC_GPCVCR_VCNT_MASK                 0x00007FFF
#define MXC_GPCVCR_VCNT_OFFSET               0

static struct delayed_work dvfs_core_work;
static int dvfs_is_active;

/* Used for tracking the number of interrupts */
static u32 dvfs_nr_up[4];
static u32 dvfs_nr_dn[4];

/*
 * Clock structures
 */
static struct clk *cpu_clk;
static struct regulator *core_regulator;

enum {
	FSVAI_FREQ_NOCHANGE = 0x0,
	FSVAI_FREQ_INCREASE,
	FSVAI_FREQ_DECREASE,
	FSVAI_FREQ_EMERG,
};

#define ARM_NORMAL_CLK  665000000
#define ARM_200MHZ_CLK  200000000

#define CORE_200_VOLTAGE 775000
#define CORE_710_VOLTAGE 1050000

/*
 * Frequency increase threshold. Increase frequency change request
 * will be sent if DVFS counter value will be more than this value.
 */
#define DVFS_UPTHR		(25 << MXC_DVFSTHRS_UPTHR_OFFSET)

/*
 * Frequency decrease threshold. Decrease frequency change request
 * will be sent if DVFS counter value will be less than this value.
 */
#define DVFS_DNTHR		(9 << MXC_DVFSTHRS_DNTHR_OFFSET)

/*
 * With the CKIH clocked at 22579200 Hz,
 * this setting yields a DIV_3_CLK of 2.75 kHz.
 */
#define DVFS_DIV3CK		(2 << MXC_DVFSCNTR_DIV3CK_OFFSET)

/*
 * DNCNT defines the amount of times the down threshold should be exceeded
 * before DVFS will trigger frequency decrease request.
 */
#define DVFS_DNCNT		(3 << MXC_DVFSCOUN_DNCNT_OFFSET)

/*
 * UPCNT defines the amount of times the up threshold should be exceeded
 * before DVFS will trigger frequency increase request.
 */
#define DVFS_UPCNT		(3 << MXC_DVFSCOUN_UPCNT_OFFSET)

/*
 * Panic threshold. Panic frequency change request
 * will be sent if DVFS counter value will be more than this value.
 */
#define DVFS_PNCTHR		(63 << MXC_DVFSTHRS_PNCTHR_OFFSET)

/*
 * Load tracking buffer source: 1 for ld_add; 0 for pre_ld_add; 2 for after EMA
 */
#define DVFS_LTBRSR		(2 << MXC_DVFSCNTR_LTBRSR_OFFSET)

/* EMAC defines how many samples are included in EMA calculation */
#define DVFS_EMAC		(0x10 << MXC_DVFSEMAC_EMAC_OFFSET)

DEFINE_SPINLOCK(mxc_dvfs_core_lock);

static void dvfs_load_config(void)
{
	u32 reg;

	reg = 0;
	reg |= DVFS_UPTHR;
	reg |= DVFS_DNTHR;
	reg |= DVFS_PNCTHR;
	__raw_writel(reg, MXC_DVFSTHRS);

	reg = 0;
	reg |= DVFS_DNCNT;
	reg |= DVFS_UPCNT;
	__raw_writel(reg, MXC_DVFSCOUN);
}

static int start_dvfs(void)
{
	u32 reg, flags;

	if (dvfs_is_active)
		return 0;

	spin_lock_irqsave(&mxc_dvfs_core_lock, flags);

	/* config reg GPC_CNTR */
	reg = __raw_readl(MXC_GPC_CNTR);

	/* GPCIRQ=1, select ARM IRQ */
	reg |= MXC_GPCCNTR_GPCIRQ;
	/* ADU=1, select ARM domain */
	reg |= MXC_GPCCNTR_ADU;
	__raw_writel(reg, MXC_GPC_CNTR);

	/* Enable DVFS interrupt */
	reg = __raw_readl(MXC_DVFSCNTR);
	/* FSVAIM=0 */
	reg = (reg & ~MXC_DVFSCNTR_FSVAIM);
	/* Set MAXF, MINF */
	reg = (reg & ~(MXC_DVFSCNTR_MAXF_MASK | MXC_DVFSCNTR_MINF_MASK));
	reg |= 1 << MXC_DVFSCNTR_MAXF_OFFSET;
	/* Select ARM domain */
	reg |= MXC_DVFSCNTR_DVFIS;
	/* Enable DVFS frequency adjustment interrupt */
	reg = (reg & ~MXC_DVFSCNTR_FSVAIM);
	/* Set load tracking buffer register source */
	reg = (reg & ~MXC_DVFSCNTR_LTBRSR_MASK);
	reg |= DVFS_LTBRSR;
	/* DIV3CK=4 */
	reg = (reg & ~MXC_DVFSCNTR_DIV3CK_MASK);
	reg |= DVFS_DIV3CK;
	/* Enable DVFS */
	reg |= MXC_DVFSCNTR_DVFEN;
	__raw_writel(reg, MXC_DVFSCNTR);

	dvfs_is_active = 1;

	spin_unlock_irqrestore(&mxc_dvfs_core_lock, flags);

	printk(KERN_DEBUG "DVFS is started\n");

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
	/* DVFS loading config */
	dvfs_load_config();

	/* EMAC=0x100 */
	__raw_writel(DVFS_EMAC, MXC_DVFSEMAC);

	return 0;
}

static irqreturn_t dvfs_irq(int irq, void *dev_id)
{
	u32 reg;
	u32 flags;

	/* Check if DVFS0 (ARM) id requesting for freqency/voltage update */
	if ((__raw_readl(MXC_GPC_CNTR) & MXC_GPCCNTR_DVFS0CR) == 0)
		return IRQ_HANDLED;

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
	u32 curr_cpu;
	unsigned long rate;
	int ret = 0;
	int uvol;

	/* Check DVFS frequency adjustment interrupt status */
	reg = __raw_readl(MXC_DVFSCNTR);
	fsvai = (reg & MXC_DVFSCNTR_FSVAI_MASK) >> MXC_DVFSCNTR_FSVAI_OFFSET;

	/* Check FSVAI, FSVAI=0 is error */
	if (fsvai == FSVAI_FREQ_NOCHANGE) {
		/* Do nothing. Freq change is not required */
		goto END;
	}

	curr_cpu = clk_get_rate(cpu_clk);

	/* If FSVAI indicate freq down,
	   check arm-clk is not in lowest frequency 200 MHz */
	if (fsvai == FSVAI_FREQ_DECREASE) {
		if (curr_cpu == ARM_200MHZ_CLK) {
			/* Do nothing. Freq change is not required */
			printk(KERN_WARNING "fsvai can not go down any more\n");
			goto END;
		} else {
			/* freq down */
			rate = ARM_200MHZ_CLK;
			uvol = CORE_200_VOLTAGE;

			ret = clk_set_rate(cpu_clk, rate);
			if (ret != 0) {
				printk(KERN_DEBUG
				       "cannot set CPU clock rate\n");
				goto END;
			}

			/* START the GPC main control FSM */
			/* set VINC */
			reg = __raw_readl(MXC_GPC_VCR);
			reg &=
			    ~(MXC_GPCVCR_VINC_MASK | MXC_GPCVCR_VCNTU_MASK |
			      MXC_GPCVCR_VCNT_MASK);
			reg |=
			    (1 << MXC_GPCVCR_VCNTU_OFFSET) |
			    (100 << MXC_GPCVCR_VCNT_OFFSET);
			__raw_writel(reg, MXC_GPC_VCR);

			/* Set the voltage for the GP domain. */
			ret = regulator_set_voltage(core_regulator, uvol);
			if (ret < 0) {
				printk(KERN_DEBUG
				       "COULD NOT SET CORE VOLTAGE!!!!!\n");
				goto END;
			}
			udelay(30);
		}
	} else {
		if (curr_cpu == ARM_NORMAL_CLK) {
			/* Do nothing. Freq change is not required */
			printk(KERN_WARNING "fsvai can not go up any more\n");
			goto END;
		} else {
			/* freq up */
			rate = ARM_NORMAL_CLK;
			/* START the GPC main control FSM */
			/* set VINC */
			reg = __raw_readl(MXC_GPC_VCR);
			reg &=
			    ~(MXC_GPCVCR_VINC_MASK | MXC_GPCVCR_VCNTU_MASK |
			      MXC_GPCVCR_VCNT_MASK);
			reg |=
			    (1 << MXC_GPCVCR_VCNTU_OFFSET) |
			    (100 << MXC_GPCVCR_VCNT_OFFSET);
			__raw_writel(reg, MXC_GPC_VCR);

			ret =
			    regulator_set_voltage(core_regulator,
						  CORE_710_VOLTAGE);
			if (ret < 0) {
				printk(KERN_DEBUG
				       "COULD NOT SET CORE VOLTAGE!!!!\n");
				goto END;
			}
			udelay(30);

			ret = clk_set_rate(cpu_clk, ARM_NORMAL_CLK);
			if (ret != 0)
				printk(KERN_DEBUG
				       "cannot set CPU clock rate\n");
		}
	}

END:			/* Set MAXF, MINF */
	reg = __raw_readl(MXC_DVFSCNTR);
	reg = (reg & ~(MXC_DVFSCNTR_MAXF_MASK | MXC_DVFSCNTR_MINF_MASK));
	curr_cpu = clk_get_rate(cpu_clk);
	if (curr_cpu == ARM_NORMAL_CLK)
		reg |= 1 << MXC_DVFSCNTR_MAXF_OFFSET;
	else if (curr_cpu == ARM_200MHZ_CLK)
		reg |= 1 << MXC_DVFSCNTR_MINF_OFFSET;

	/* Enable FVFS interrupt */
	/* FSVAIM=0 */
	reg = (reg & ~MXC_DVFSCNTR_FSVAIM);
	/* LBFL=1 */
	reg = (reg & ~MXC_DVFSCNTR_LBFL);
	reg |= MXC_DVFSCNTR_LBFL;
	__raw_writel(reg, MXC_DVFSCNTR);
}

/*!
 * This function disables the DVFS module.
 */
static void stop_dvfs(void)
{
	u32 reg = 0;
	u32 flags;
	u32 curr_cpu;

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

		curr_cpu = clk_get_rate(cpu_clk);
		if (curr_cpu != ARM_NORMAL_CLK) {
			if (regulator_set_voltage(core_regulator, 1000 * 1000)
			    == 0)
				clk_set_rate(cpu_clk, ARM_NORMAL_CLK);
		}
	}

	printk(KERN_DEBUG "DVFS is stopped\n");
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
	.name = "dvfs",
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

static int __init dvfs_init(void)
{
	int err = 0;

	INIT_DELAYED_WORK(&dvfs_core_work, dvfs_core_workqueue_handler);

	cpu_clk = clk_get(NULL, "cpu_clk");
	if (IS_ERR(cpu_clk)) {
		printk(KERN_ERR "%s: failed to get cpu clock\n", __func__);
		return PTR_ERR(cpu_clk);
	}

	core_regulator = regulator_get(NULL, "SW1");
	if (IS_ERR(core_regulator)) {
		clk_put(cpu_clk);
		printk(KERN_ERR "%s: failed to get gp regulator\n", __func__);
		return PTR_ERR(core_regulator);
	}

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

static void __exit dvfs_cleanup(void)
{
	stop_dvfs();

	/* release the DVFS interrupt */
	free_irq(MXC_INT_GPC1, NULL);

	dvfs_sysdev_ctrl_exit();

	clk_put(cpu_clk);
}

module_init(dvfs_init);
module_exit(dvfs_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("DVFS driver");
MODULE_LICENSE("GPL");
