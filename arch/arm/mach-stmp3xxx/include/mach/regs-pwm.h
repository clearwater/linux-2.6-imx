/*
 * STMP PWM Register Definitions
 *
 * Copyright 2008-2009 Freescale Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __ARCH_ARM___PWM_H
#define __ARCH_ARM___PWM_H  1

#include <mach/stmp3xxx_regs.h>

#define REGS_PWM_BASE (REGS_BASE + 0x64000)
#define REGS_PWM_BASE_PHYS (0x80064000)
#define REGS_PWM_SIZE 0x00002000
HW_REGISTER(HW_PWM_CTRL, REGS_PWM_BASE, 0x00000000)
#define HW_PWM_CTRL_ADDR (REGS_PWM_BASE + 0x00000000)
#define BM_PWM_CTRL_SFTRST 0x80000000
#define BM_PWM_CTRL_CLKGATE 0x40000000
#define BM_PWM_CTRL_PWM4_PRESENT 0x20000000
#define BM_PWM_CTRL_PWM3_PRESENT 0x10000000
#define BM_PWM_CTRL_PWM2_PRESENT 0x08000000
#define BM_PWM_CTRL_PWM1_PRESENT 0x04000000
#define BM_PWM_CTRL_PWM0_PRESENT 0x02000000
#define BM_PWM_CTRL_OUTPUT_CUTOFF_EN 0x00000040
#define BM_PWM_CTRL_PWM2_ANA_CTRL_ENABLE 0x00000020
#define BM_PWM_CTRL_PWM4_ENABLE 0x00000010
#define BM_PWM_CTRL_PWM3_ENABLE 0x00000008
#define BM_PWM_CTRL_PWM2_ENABLE 0x00000004
#define BM_PWM_CTRL_PWM1_ENABLE 0x00000002
#define BM_PWM_CTRL_PWM0_ENABLE 0x00000001
/*
 *  multi-register-define name HW_PWM_ACTIVEn
 *	      base 0x00000010
 *	      count 5
 *	      offset 0x20
 */
HW_REGISTER_INDEXED(HW_PWM_ACTIVEn, REGS_PWM_BASE, 0x00000010, 0x20)
#define BP_PWM_ACTIVEn_INACTIVE      16
#define BM_PWM_ACTIVEn_INACTIVE 0xFFFF0000
#define BF_PWM_ACTIVEn_INACTIVE(v) \
	(((v) << 16) & BM_PWM_ACTIVEn_INACTIVE)
#define BP_PWM_ACTIVEn_ACTIVE      0
#define BM_PWM_ACTIVEn_ACTIVE 0x0000FFFF
#define BF_PWM_ACTIVEn_ACTIVE(v)  \
	(((v) << 0) & BM_PWM_ACTIVEn_ACTIVE)
/*
 *  multi-register-define name HW_PWM_PERIODn
 *	      base 0x00000020
 *	      count 5
 *	      offset 0x20
 */
HW_REGISTER_INDEXED(HW_PWM_PERIODn, REGS_PWM_BASE, 0x00000020, 0x20)
#define BM_PWM_PERIODn_MATT_SEL 0x01000000
#define BM_PWM_PERIODn_MATT 0x00800000
#define BP_PWM_PERIODn_CDIV      20
#define BM_PWM_PERIODn_CDIV 0x00700000
#define BF_PWM_PERIODn_CDIV(v)  \
	(((v) << 20) & BM_PWM_PERIODn_CDIV)
#define BV_PWM_PERIODn_CDIV__DIV_1    0x0
#define BV_PWM_PERIODn_CDIV__DIV_2    0x1
#define BV_PWM_PERIODn_CDIV__DIV_4    0x2
#define BV_PWM_PERIODn_CDIV__DIV_8    0x3
#define BV_PWM_PERIODn_CDIV__DIV_16   0x4
#define BV_PWM_PERIODn_CDIV__DIV_64   0x5
#define BV_PWM_PERIODn_CDIV__DIV_256  0x6
#define BV_PWM_PERIODn_CDIV__DIV_1024 0x7
#define BP_PWM_PERIODn_INACTIVE_STATE      18
#define BM_PWM_PERIODn_INACTIVE_STATE 0x000C0000
#define BF_PWM_PERIODn_INACTIVE_STATE(v)  \
	(((v) << 18) & BM_PWM_PERIODn_INACTIVE_STATE)
#define BV_PWM_PERIODn_INACTIVE_STATE__HI_Z 0x0
#define BV_PWM_PERIODn_INACTIVE_STATE__0    0x2
#define BV_PWM_PERIODn_INACTIVE_STATE__1    0x3
#define BP_PWM_PERIODn_ACTIVE_STATE      16
#define BM_PWM_PERIODn_ACTIVE_STATE 0x00030000
#define BF_PWM_PERIODn_ACTIVE_STATE(v)  \
	(((v) << 16) & BM_PWM_PERIODn_ACTIVE_STATE)
#define BV_PWM_PERIODn_ACTIVE_STATE__HI_Z 0x0
#define BV_PWM_PERIODn_ACTIVE_STATE__0    0x2
#define BV_PWM_PERIODn_ACTIVE_STATE__1    0x3
#define BP_PWM_PERIODn_PERIOD      0
#define BM_PWM_PERIODn_PERIOD 0x0000FFFF
#define BF_PWM_PERIODn_PERIOD(v)  \
	(((v) << 0) & BM_PWM_PERIODn_PERIOD)
HW_REGISTER_0(HW_PWM_VERSION, REGS_PWM_BASE, 0x000000b0)
#define HW_PWM_VERSION_ADDR (REGS_PWM_BASE + 0x000000b0)
#define BP_PWM_VERSION_MAJOR      24
#define BM_PWM_VERSION_MAJOR 0xFF000000
#define BF_PWM_VERSION_MAJOR(v) \
	(((v) << 24) & BM_PWM_VERSION_MAJOR)
#define BP_PWM_VERSION_MINOR      16
#define BM_PWM_VERSION_MINOR 0x00FF0000
#define BF_PWM_VERSION_MINOR(v)  \
	(((v) << 16) & BM_PWM_VERSION_MINOR)
#define BP_PWM_VERSION_STEP      0
#define BM_PWM_VERSION_STEP 0x0000FFFF
#define BF_PWM_VERSION_STEP(v)  \
	(((v) << 0) & BM_PWM_VERSION_STEP)
#endif /* __ARCH_ARM___PWM_H */
