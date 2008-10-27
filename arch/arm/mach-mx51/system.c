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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/proc-fns.h>
#include <asm/system.h>

/*!
 * @defgroup MSL_MX51 i.MX51 Machine Specific Layer (MSL)
 */

/*!
 * @file mach-mx51/system.c
 * @brief This file contains idle and reset functions.
 *
 * @ingroup MSL_MX51
 */

extern int mxc_jtag_enabled;
static struct clk *gpc_dvfs_clk;

void mxc_pg_enable(struct platform_device *pdev)
{
}
EXPORT_SYMBOL(mxc_pg_enable);

void mxc_pg_disable(struct platform_device *pdev)
{
}
EXPORT_SYMBOL(mxc_pg_disable);

/*!
 * This function puts the CPU into idle mode. It is called by default_idle()
 * in process.c file.
 */
void arch_idle(void)
{
	if (likely(!mxc_jtag_enabled)) {
		if (gpc_dvfs_clk == NULL)
			gpc_dvfs_clk = clk_get(NULL, "gpc_dvfs_clk");
		/* gpc clock is needed for SRPG */
		clk_enable(gpc_dvfs_clk);
		cpu_do_idle();
		clk_disable(gpc_dvfs_clk);
	}

}

/*
 * This function resets the system. It is called by machine_restart().
 *
 * @param  mode         indicates different kinds of resets
 */
void arch_reset(char mode)
{
	/* Assert SRS signal */
	mxc_wd_reset();
}
