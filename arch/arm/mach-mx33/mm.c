/*
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *    - add MX33 specific definitions
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

/*!
 * @file mm.c
 *
 * @brief This file creates static mapping between physical to virtual memory.
 *
 * @ingroup Memory
 */

/*!
 * This structure defines the MX33 memory map.
 */
static struct map_desc mxc_io_desc[] __initdata = {
	/*  Virtual Address      Physical Address  Size         Type    */
	{
	 .virtual = IRAM_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(IRAM_BASE_ADDR),
	 .length = IRAM_SIZE,
	 .type = MT_NONSHARED_DEVICE},
	{
	 .virtual = CS4_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(CS4_BASE_ADDR),
	 .length = CS4_SIZE,
	 .type = MT_DEVICE},
	{
	 .virtual = L2CC_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(L2CC_BASE_ADDR),
	 .length = L2CC_SIZE,
//       .type = MT_DEVICE
	 .type = MT_NONSHARED_DEVICE},
	{
	 .virtual = PLATFORM_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(PLATFORM_BASE_ADDR),
	 .length = PLATFORM_SIZE,
	 .type = MT_DEVICE},
	{
	 .virtual = TZIC_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(TZIC_BASE_ADDR),
	 .length = TZIC_SIZE,
	 .type = MT_NONSHARED_DEVICE},
	{
	 .virtual = DEBUG_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(DEBUG_BASE_ADDR),
	 .length = DEBUG_SIZE,
	 .type = MT_NONSHARED_DEVICE},
	{
	 .virtual = SPBA0_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(SPBA0_BASE_ADDR),
	 .length = SPBA0_SIZE,
	 .type = MT_NONSHARED_DEVICE},
	{
	 .virtual = AIPS1_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(AIPS1_BASE_ADDR),
	 .length = AIPS1_SIZE,
	 .type = MT_NONSHARED_DEVICE},
	{
	 .virtual = AIPS2_BASE_ADDR_VIRT,
	 .pfn = __phys_to_pfn(AIPS2_BASE_ADDR),
	 .length = AIPS2_SIZE,
	 .type = MT_NONSHARED_DEVICE},
};

/*!
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory map for
 * the IO modules.
 */
void __init mxc_map_io(void)
{
	iotable_init(mxc_io_desc, ARRAY_SIZE(mxc_io_desc));
}
