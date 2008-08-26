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
 * @file cpufreq.c
 *
 * @brief A driver for the Freescale Semiconductor i.MX37 CPUfreq module.
 *
 * The CPUFREQ driver is for controling CPU frequency. It allows you to change
 * the CPU clock speed on the fly.
 *
 * @ingroup PM
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/regulator/regulator.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/arch/clock.h>
#include <asm/cacheflush.h>

/*
 * Frequencys can be set for CPU.
 */
#define CPU_FREQUENCY_200000_KHZ		200000
#define CPU_FREQUENCY_532000_KHZ		532000
#define AHB_FREQUENCY_133000_KHZ		133000

static struct clk *cpu_clk;
static struct clk *ahb_clk;
static struct regulator *gp_regulator;
static struct regulator *lp_regulator;

struct mx37_freq_volts {
	int gp_freq;
	int gp_uV;
};

/*
 * These voltage and frequency settings could be further refined.
 */
static const struct mx37_freq_volts freq_uV[] = {
	{CPU_FREQUENCY_200000_KHZ * 1000, 850 * 1000},
	{CPU_FREQUENCY_532000_KHZ * 1000, 1000 * 1000},
};

/* does need to be in ascending order for calc_frequency_khz() below */
static struct cpufreq_frequency_table imx37_freq_table[] = {
	{0x01, CPU_FREQUENCY_200000_KHZ},
	{0x02, CPU_FREQUENCY_532000_KHZ},
	{0, CPUFREQ_TABLE_END},
};

static int mx37_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	return cpufreq_frequency_table_verify(policy, imx37_freq_table);
}

static unsigned int mx37_get_speed(unsigned int cpu)
{
	if (cpu)
		return 0;
	return clk_get_rate(cpu_clk) / 1000;
}

static int calc_frequency_khz(int target, unsigned int relation)
{
	int i;

	if (relation == CPUFREQ_RELATION_H) {
		for (i = ARRAY_SIZE(imx37_freq_table) - 1; i > 0; i--) {
			if (imx37_freq_table[i].frequency <= target)
				return imx37_freq_table[i].frequency;
		}
	} else if (relation == CPUFREQ_RELATION_L) {
		for (i = 0; i < ARRAY_SIZE(imx37_freq_table) - 1; i++) {
			if (imx37_freq_table[i].frequency >= target)
				return imx37_freq_table[i].frequency;
		}
	}
	printk(KERN_ERR "Error: No valid cpufreq relation\n");
	return CPU_FREQUENCY_532000_KHZ;
}

static int mx37_set_voltage(struct cpufreq_policy *policy, int gp_freq)
{
	int i, ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(freq_uV); i++) {
		if ((freq_uV[i].gp_freq == gp_freq))
			goto found;
	}
	printk(KERN_ERR "%s: could not find voltage for gp_freq %d Hz\n",
	       __func__, gp_freq);
	return ret;

found:
	ret = regulator_set_voltage(gp_regulator, freq_uV[i].gp_uV);
	return ret;
}

static int mx37_set_target(struct cpufreq_policy *policy,
			   unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	long freq_Hz;
	int ret = 0;

	/*
	 * Some governors do not respects CPU and policy lower limits
	 * which leads to bad things (division by zero etc), ensure
	 * that such things do not happen.
	 */
	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;

	if (target_freq < policy->min)
		target_freq = policy->min;

	freq_Hz = calc_frequency_khz(target_freq, relation) * 1000;
	freqs.old = clk_get_rate(cpu_clk) / 1000;
	freqs.new = freq_Hz / 1000;
	freqs.cpu = 0;
	freqs.flags = 0;

	if (freqs.old < freqs.new) {
		ret = mx37_set_voltage(policy, freq_Hz);

		if (ret < 0) {
			printk(KERN_ERR
			       "cant raise voltage for CPU frequency %ld\n",
			       freq_Hz);
			return -EIO;
		}
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	printk(KERN_ERR "CPU frequency %ld\n", freq_Hz);
	ret = clk_set_rate(cpu_clk, freq_Hz);
	if (ret != 0)
		printk(KERN_DEBUG "cannot set CPU clock rate\n");

	if (freqs.old > freqs.new) {
		ret = mx37_set_voltage(policy, freq_Hz);
		if (ret < 0) {
			printk(KERN_ERR
			       "cant lower voltage for CPU frequency %ld\n",
			       freq_Hz);
		}
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	return 0;
}

static int __init mx37_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	int ret;

	printk(KERN_INFO "i.MX37 CPU frequency driver\n");

	if (policy->cpu != 0)
		return -EINVAL;

	cpu_clk = clk_get(NULL, "cpu_clk");
	if (IS_ERR(cpu_clk)) {
		printk(KERN_ERR "%s: failed to get cpu clock\n", __func__);
		return PTR_ERR(cpu_clk);
	}

	ahb_clk = clk_get(NULL, "ahb_clk");
	if (IS_ERR(ahb_clk)) {
		printk(KERN_ERR "%s: failed to get ahb clock\n", __func__);
		return PTR_ERR(ahb_clk);
	}

	gp_regulator = regulator_get(NULL, "DCDC1");
	if (IS_ERR(gp_regulator)) {
		clk_put(cpu_clk);
		printk(KERN_ERR "%s: failed to get gp regulator\n", __func__);
		return PTR_ERR(gp_regulator);
	}

	lp_regulator = regulator_get(NULL, "DCDC4");
	if (IS_ERR(lp_regulator)) {
		clk_put(ahb_clk);
		printk(KERN_ERR "%s: failed to get lp regulator\n", __func__);
		return PTR_ERR(lp_regulator);
	}

	policy->cur = policy->min = policy->max = clk_get_rate(cpu_clk) / 1000;
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.min_freq = CPU_FREQUENCY_200000_KHZ;
	policy->cpuinfo.max_freq = CPU_FREQUENCY_532000_KHZ;

	/* Manual states, that PLL stabilizes in two CLK32 periods */
	policy->cpuinfo.transition_latency = 10;

	ret = cpufreq_frequency_table_cpuinfo(policy, imx37_freq_table);
	if (ret < 0) {
		clk_put(cpu_clk);
		regulator_put(gp_regulator, NULL);
		clk_put(ahb_clk);
		regulator_put(lp_regulator, NULL);
		printk(KERN_ERR "%s: failed to register i.MX37 CPUfreq\n",
		       __func__);
		return ret;
	}
	cpufreq_frequency_table_get_attr(imx37_freq_table, policy->cpu);
	return 0;
}

static int mx37_cpufreq_driver_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);

	/* reset CPU to 532MHz */
	if (regulator_set_voltage(gp_regulator, 1000 * 1000) == 0)
		clk_set_rate(cpu_clk, CPU_FREQUENCY_532000_KHZ * 1000);

	if (regulator_set_voltage(lp_regulator, 1200 * 1000) == 0)
		clk_set_rate(ahb_clk, AHB_FREQUENCY_133000_KHZ * 1000);

	clk_put(cpu_clk);
	regulator_put(gp_regulator, NULL);
	clk_put(ahb_clk);
	regulator_put(lp_regulator, NULL);
	return 0;
}

static struct cpufreq_driver mx37_driver = {
	.flags = CPUFREQ_STICKY,
	.verify = mx37_verify_speed,
	.target = mx37_set_target,
	.get = mx37_get_speed,
	.init = mx37_cpufreq_driver_init,
	.exit = mx37_cpufreq_driver_exit,
	.name = "imx37",
};

static int __devinit mx37_cpufreq_init(void)
{
	return cpufreq_register_driver(&mx37_driver);
}

static void mx37_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&mx37_driver);
}

module_init(mx37_cpufreq_init);
module_exit(mx37_cpufreq_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("CPUfreq driver for i.mx37");
MODULE_LICENSE("GPL");
