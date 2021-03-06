/*
 * Freescale STMP378X low level RAM timings tables for Micron mDDR
 *
 * Author: Vitaly Wool <vital@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

// Change the integer and fractional dividers, necessary to make the EMI frequency change.
//  Load new HW_DRAM_CTL* registers, necessary for the new target frequency.
//  Each {target frequency, dram_type} pair needs a unique "stmp3xxx_*_set_timings" routine,
//   with unique parameters.

__stmp_emi_set_values:
	stmfd	r9!, {r0 - r4, lr}                             // Push Link Register and Registers on Stack.
	
	mov	r1,     #(HW_CLKCTRL_EMI_ADDR & 0x000000FF)    // Move HW_CLK_CTRL_EMI address into R1 so we can work on it.
	orr	r1, r1, #(HW_CLKCTRL_EMI_ADDR & 0x0000FF00)    // Q - Why did EA do all this OR'ing instead of just "mov r1, #(HW_CLKCTRL_EMI_ADDR)" ?
	orr	r1, r1, #(HW_CLKCTRL_EMI_ADDR & 0x00FF0000)    // A - Perhaps it was b/c of endian-ness ?
	orr	r1, r1, #(HW_CLKCTRL_EMI_ADDR & 0xFF000000)

	//mov	r3, #BM_CLKCTRL_EMI_DCC_RESYNC_ENABLE          // DCC_RESYNC functionality is DEPRECATED in mx233.

	mov	r0, #(HW_CLKCTRL_FRAC_ADDR & 0x000000FF)       // Load value of HW_CLKCTRL_FRAC_ADDR into R2.
	orr	r0, r0, #(HW_CLKCTRL_FRAC_ADDR & 0x0000FF00)
	orr	r0, r0, #(HW_CLKCTRL_FRAC_ADDR & 0x00FF0000)
	orr	r0, r0, #(HW_CLKCTRL_FRAC_ADDR & 0xFF000000)
	ldr	r2, [r0]

	and	r4, r2, #BM_CLKCTRL_FRAC_EMIFRAC
	lsr	r4, r4, #8
	/* new pll div > cur pll div? */
	cmp	r4, r8
	bgt	1f
	bic	r4, r2, #BM_CLKCTRL_FRAC_EMIFRAC
	orr	r4, r4, r8, lsl #8
	str	r4, [r0]
	nop
	nop
	nop

// Change integer/fractional dividers.

    // The fractional divider and integer divider must be written in such
    // an order to guarantee that when going from a lower frequency to a
    // higher frequency that any intermediate frequencies do not exceed
    // the final frequency. For this reason, we must make sure to check
    // the current divider values with the new divider values and write
    // them in the correct order.

1:	ldr	r4, [r1]                                       // Put values of HW_CLKCTRL_EMI into R4.
	and	r4, r4, #BM_CLKCTRL_EMI_DIV_EMI                // Single out EMI_DIV_EMI field, to be used for comparison in following routine.
	
	/* new emi div > cur emi div? */

    
	cmp	r4, r7                                         // I guess R7 has the "new" EMI DIV.
	bgt	2f                                             // branch to where?
	mov	r4, r7
	orr	r4, r4, #0x100
	//orr	r4, r4, r3     // This was for DCC_RESYNC_ENABLE, which is deprecated in mx233
	str	r4, [r1]
	
        // Wait on BUSY_REF_EMI and BUSY_REF_XTAL
11:	ldr	r4, [r1]
	tst	r4, #BM_CLKCTRL_EMI_BUSY_REF_EMI
	bne	11b
	tst	r4, #BM_CLKCTRL_EMI_BUSY_REF_XTAL
	bne	11b

2:	ldr	r2, [r0]

	and	r4, r2, #BM_CLKCTRL_FRAC_EMIFRAC
	lsr	r4, r4, #8
	/* new pll div != cur pll div? */
	cmp	r4, r8
	beq	3f
	bic	r4, r2, #BM_CLKCTRL_FRAC_EMIFRAC
	orr	r4, r4, r8, lsl #8
	str	r4, [r0]
	nop
	nop
	nop

3:	ldr	r4, [r1]
	and	r4, r4, #BM_CLKCTRL_EMI_DIV_EMI
	/* new emi div != cur emi div? */
	cmp	r4, r7
	beq	4f
	mov	r4, r7
	orr	r4, r4, #0x100
	//orr	r4, r4, r3     // This was for DCC_RESYNC_ENABLE, which is deprecated in mx233
	str	r4, [r1]
31:	ldr	r4, [r1]
	tst	r4, #BM_CLKCTRL_EMI_BUSY_REF_EMI
	bne	31b
	tst	r4, #BM_CLKCTRL_EMI_BUSY_REF_XTAL
	bne	31b
	//tst	r4, #BM_CLKCTRL_EMI_BUSY_DCC_RESYNC   // DCC_RESYNC is deprecated in mx233.
	//bne	31b

4:	ldmfd	r9!, {r0 - r4, lr}
	mov	pc, lr

stmp3xxx_ram_24M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_24M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_48M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_48M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_60M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_60M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_80M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_80M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_96M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_96M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_120M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_120M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_133M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_133M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	str	r3, [r0, r2, lsl #2]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

stmp3xxx_ram_150M_set_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_150M_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	str	r3, [r0, r2, lsl #2]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

__stmp_dram_ctl00:
	.word	IO_ADDRESS(0x800E0000)   // DRAM registers
__stmp_dram_emi00:
	.word	IO_ADDRESS(0x80020000)   // HW_EMI_CTRL address
__stmp_power_vdddctrl:
	.word	IO_ADDRESS(0x80044040)

stmp3xxx_ram_save_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_saved_values
1:	ldr	r2, [r1]
	mov	r4, r2, lsl #2
	ldr	r3, [r0, r4]
	str	r3, [r1, #4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

#ifdef CONFIG_STMP378X_RAM_MDDR
__stmp_dram_24M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000101
	.word	12
	.word	0x02010002
	.word	13
	.word	0x06060a02
	.word	15
	.word	0x01030000
	.word	17
	.word	0x2d000002
	.word	18
	.word	0x20200000
	.word	19
	.word	0x027f1010
	.word	20
	.word	0x01021021
	.word	21
	.word	0x00000002
	.word	26
	.word	0x000000b3
	.word	32
	.word	0x00030687
	.word	33
	.word	0x00000003
	.word	34
	.word	0x000012c1
	.word	40
	.word	0x00010000

__stmp_dram_48M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000101
	.word	12
	.word	0x02010002
	.word	13
	.word	0x06060a02
	.word	15
	.word	0x02040000
	.word	17
	.word	0x2d000104
	.word	18
	.word	0x1f1f0000
	.word	19
	.word	0x027f1010
	.word	20
	.word	0x02030a10
	.word	21
	.word	0x00000004
	.word	26
	.word	0x0000016f
	.word	32
	.word	0x00060d17
	.word	33
	.word	0x00000006
	.word	34
	.word	0x00002582
	.word	40
	.word	0x00020000

__stmp_dram_60M_values:
__stmp_dram_80M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000101
	.word	11
	.word	0x00070204
	.word	12
	.word	0x02010002
	.word	13
	.word	0x06060a02
	.word	15
	.word	0x02040000
	.word	16
	.word	0x02000000
	.word	17
	.word	0x2d000105
	.word	18
	.word	0x1f1f0000
	.word	19
	.word	0x027f1010
	.word	20
	.word	0x02031021
	.word	21
	.word	0x00000006
	.word	26
	.word	0x000001cc
	.word	32
	.word	0x00081060
	.word	33
	.word	0x00000008
	.word	34
	.word	0x00002ee5
	.word	40
	.word	0x00020000

__stmp_dram_96M_values:
	.word	4
	.word	0x00000101
	.word	7
	.word	0x01000001
	.word	12
	.word	0x02020002
	.word	13
	.word	0x06060a02
	.word	15
	.word	0x03050000
	.word	17
	.word	0x25001508
	.word	18
	.word	0x1f1f0000
	.word	19
	.word	0x02141a1a
	.word	20
	.word	0x03051c22
	.word	21
	.word	0x00000007
	.word	26
	.word	0x000002e6
	.word	32
	.word	0x000c1a3b
	.word	33
	.word	0x0000000c
	.word	34
	.word	0x00004b0d
	.word	40
	.word	0x00030000

__stmp_dram_120M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000001
	.word	12
	.word	0x02020002
	.word	13
	.word	0x06060a02
	.word	15
	.word	0x03050000
	.word	17
	.word	0x1900110a
	.word	18
	.word	0x1f1f0000
	.word	19
	.word	0x02141515
	.word	20
	.word	0x03061523
	.word	21
	.word	0x00000009
	.word	26
	.word	0x000003a1
	.word	32
	.word	0x000f20ca
	.word	33
	.word	0x0000000f
	.word	34
	.word	0x00005dca
	.word	40
	.word	0x00040000

__stmp_dram_133M_values:
__stmp_dram_150M_values:
	.word	4
	.word	0x00000101
	.word	7
	.word	0x01000001
	.word	12
	.word	0x02020002
	.word	13
	.word	0x06070a02
	.word	15
	.word	0x03050000
	.word	17
	.word	0x19000f0a
	.word	18
	.word	0x1f1f0000
	.word	19
	.word	0x027f1313 /* 0x02141313 */
	.word	20
	.word	0x0306131f /* 0x03061323 */
	.word	21
	.word	0x0000000a
	.word	26
	.word	0x000003f7
	.word	32
	.word	0x001023cd
	.word	33
	.word	0x00000010
	.word	34
	.word	0x00006665
	.word	40
	.word	0x00040000

#elif CONFIG_STMP378X_RAM_DDR
/* XXX: not quite ready yet */
__stmp_dram_24M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000101
	.word	11
	.word	0x00070203
	.word	12
	.word	0x01010000 @ t_wr 1, t_rrd 1, t_cke 0
	.word	13
	.word	0x06060a01 @ t_wtr 1
	.word	15
	.word	0x01020000 @ t_rp 1, t_dal 2
	.word	17
	.word	0x3d000302 @ t_rc 2
	.word	20
	.word	0x01020508
	.word	21
	.word	0x00000002 @ t_rfc 2
	.word	26
	.word	0x000000b3 /* 0xd20 */ @ t_ref
	.word	32
	.word	0x00020690 @ t_xsnr 2, t_rasmax 0x690
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x000012c1 @ t_init
	.word	40
	.word	0x00010000

@ not yet
__stmp_dram_48M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000101
	.word	11
	.word	0x00070203
	.word	12
	.word	0x01010000 @ t_wr 1, t_rrd 1, t_cke 0
	.word	13
	.word	0x06060a01 @ t_wtr 1
	.word	15
	.word	0x01020000 @ t_rp 1, t_dal 2
	.word	17
	.word	0x39000104 @ t_rc 4
	.word	19
	.word	0x027f1010
	.word	20
	.word	0x02030a10
	.word	21
	.word	0x00000004 @ t_rfc
	.word	26
	.word	0x00000173 /* 0x1a42 */ @ t_ref
	.word	32
	.word	0x00040d21 @ t_xsnr 4, t_rasmax 0xd21
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x00002586 @ t_init
	.word	40
	.word	0x00010000

__stmp_dram_60M_values:
	.word	4
	.word	0x01000101
	.word	7
	.word	0x01000101
	.word	11
	.word	0x00070203
	.word	12
	.word	0x01010000 @ t_wr 1, t_rrd 1, t_cke 0
	.word	13
	.word	0x06060a01 @ t_wtr 1
	.word	15
	.word	0x01020000 @ t_rp 1, t_dal 2
	.word	17
	.word	0x3d000105 @ t_rc 5
	.word	19
	.word	0x027f1313
	.word	20
	.word	0x01031523 @ t_rcd 1, t_rasmin 3
	.word	21
	.word	0x00000005 @ t_rfc 5
	.word	26
	.word	0x000001cc /* 0x20cd */ @ t_ref
	.word	32
	.word	0x00051068 @ t_xsnr 5, t_rasmax 0x1068
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x00002ee5 @ t_init
	.word	40
	.word	0x00010000

__stmp_dram_80M_values:
	.word	4
	.word	0x00000101
	.word	7
	.word	0x01000001
	.word	11
	.word	0x00070203
	.word	12
	.word	0x02010000 @ t_wr 2, t_rrd 1, t_cke 0
	.word	13
	.word	0x06060a01 @ t_wtr 1
	.word	15
	.word	0x02040000 @ t_rp 2, t_dal 4
	.word	17
	.word	0x20001c05 @ dll_start_point 0x20, dll_increment 0x1c, t_rc 5
	.word	19
	.word	0x027f1313
	.word	20
	.word	0x02041522 @ t_rcd 2, t_rasmin 4, wr_dqs_shift 0x22
	.word	21
	.word	0x00000006 @ t_rfc 6
	.word	26
	.word	0x00000269 @ t_ref
	.word	32
	.word	0x000615d6 @ t_xsnr 6, t_rasmax 0x15d6
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x00003e80 @ t_init
	.word	40
	.word	0x00010000

__stmp_dram_96M_values:
	.word	4
	.word	0x00000101 //dll_bypass_mode,dlllockreg,concurrentap,bank_split_en
	.word	7
	.word	0x01000001 //rw_same_en,reg_dimm_enable,rd2rd_turn,priority_en
	.word	11
	.word	0x00070203 //max_cs_reg,command_age_count,column_size,caslat
	.word	12
	.word	0x02010000 @ t_wr 2, t_rrd 2, t_cke 0
	.word	13
	.word	0x06060a02 //caslat_lin_gate,caslat_lin,aprebit,twtr
	.word	15
	.word	0x02040000 @ t_rp 2, t_dal 4
	.word	17
	.word	0x0a000506 @ dll_start_point 0x0a, dll_increment 0x05, t_rc 6
	.word	19
	.word	0x02021a1a //dqs_out_shift_bypass,dqs_out_shift,dll_dqs_delay_bypass_1,dll_dqs_delay_bypass_0
	.word	20
	.word	0x02041c21 //trcd_int, tras_min, wr_dqs_shift_bypass, wr_dqs_shift
	.word	21
	.word	0x00000007 @ t_rfc 7
	.word	26
	.word	0x00000305 /* 0x347b */ @ t_ref
	.word	32
	.word	0x00081b4e @ t_xsnr 8, t_rasmax 0x1a3e
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x00004e20 @ t_init
	.word	40
	.word	0x00010000

__stmp_dram_120M_values:
	.word	4
	.word	0x00000101 //dll_bypass_mode,dlllockreg,concurrentap,bank_split_en
	.word	7
	.word	0x01000001 //rw_same_en,reg_dimm_enable,rd2rd_turn,priority_en
	.word	11
	.word	0x00070203 //max_cs_reg,command_age_count,column_size,caslat
	.word	12
	.word	0x02020000 @ t_wr 2, t_rrd 2, t_cke 0
	.word	13
	.word	0x06060a02 //caslat_lin_gate,caslat_lin,aprebit,twtr
	.word	15
	.word	0x02040000 @ t_rp 2, t_dal 4
	.word	17
	.word	0x0a000507 @ dll_start_point 0x0a, dll_increment 0x05, t_rc 8
	.word	19
	.word	0x02021a1a //dqs_out_shift_bypass,dqs_out_shift,dll_dqs_delay_bypass_1,dll_dqs_delay_bypass_0
	.word	20
	.word	0x02051c21 //trcd_int, tras_min, wr_dqs_shift_bypass, wr_dqs_shift
	.word	21
	.word	0x00000009 @ t_rfc 9
	.word	26
	.word	0x000003a1 /* 0x41a6 */ @ t_ref
	.word	32
	.word	0x000a20ca @ t_xsnr 9, t_rasmax 0x20ca
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x00005dca @ t_init
	.word	40
	.word	0x00010000

__stmp_dram_133M_values:
	.word	4
	.word	0x00000101 //dll_bypass_mode,dlllockreg,concurrentap,bank_split_en
	.word	7
	.word	0x01000001 //rw_same_en,reg_dimm_enable,rd2rd_turn,priority_en
	.word	11
	.word	0x00070203 //max_cs_reg,command_age_count,column_size,caslat
	.word	12
	.word	0x02020000
	.word	13
	.word	0x06060a02 //caslat_lin_gate,caslat_lin,aprebit,twtr
	.word	15
	.word	0x02040000 @ t_rp 2, t_dal 4
	.word	17
	.word	0x0a000508 @ dll_start_point 0x0a, dll_increment 0x05, t_rc 8
	.word	19
	.word	0x02021a1a //dqs_out_shift_bypass,dqs_out_shift,dll_dqs_delay_bypass_1,dll_dqs_delay_bypass_0
	.word	20
	.word	0x02061c21 //trcd_int, tras_min, wr_dqs_shift_bypass, wr_dqs_shift
	.word	21
	.word	0x0000000a
	.word	26
	.word	0x000003f7 /* 0x48b9 */
	.word	32
	.word	0x000a23cd
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x00006665
	.word	40
	.word	0x00010000

__stmp_dram_150M_values:
	.word	4
	.word	0x00000101 //dll_bypass_mode,dlllockreg,concurrentap,bank_split_en
	.word	7
	.word	0x01000001 //rw_same_en,reg_dimm_enable,rd2rd_turn,priority_en
	.word	11
	.word	0x00070203 //max_cs_reg,command_age_count,column_size,caslat
	.word	12
	.word	0x02020000 @ t_wr 2, t_rrd 2, t_cke 0
	.word	13
	.word	0x06060a02 //caslat_lin_gate,caslat_lin,aprebit,twtr
	.word	15
	.word	0x03050000 @ t_rp 3, t_dal 6
	.word	17
	.word	0x0a000509 @ dll_start_point 0x0a, dll_increment 0x5, t_rc 0xc
	.word	19
	.word	0x02020f0f //dqs_out_shift_bypass,dqs_out_shift,dll_dqs_delay_bypass_1,dll_dqs_delay_bypass_0
	.word	20
	.word	0x03061121 //trcd_int, tras_min, wr_dqs_shift_bypass, wr_dqs_shift
	.word	21
	.word	0x0000000b @ t_rfc 0xc
	.word	26
	.word	0x0000047d /* 0x20cd */ @ t_ref
	.word	32
	.word	0x000c2879 @ t_xsnr 0xc, t_rasmax 0x2860
	.word	33
	.word	0x000000c8 @ t_xsr 0xc8
	.word	34
	.word	0x000073be @ t_init
	.word	40
	.word	0x00010000

#else
#error RAM chip not defined
#endif

stmp3xxx_ram_restore_timings:
	ldr	r0, __stmp_dram_ctl00
	adr	r1, __stmp_dram_saved_values
1:	ldr	r2, [r1]
	ldr	r3, [r1, #4]
	mov	r4, r2, lsl #2
	str	r3, [r0, r4]
	add	r1, r1, #8
	cmp	r2, #40
	bne	1b
	mov	pc, lr

__stmp_dram_saved_values:
	.word	4
	.word	0
	.word	7
	.word	0
	.word	12
	.word	0
	.word	13
	.word	0
	.word	15
	.word	0
	.word	17
	.word	0
	.word	18
	.word	0
	.word	19
	.word	0
	.word	20
	.word	0
	.word	21
	.word	0
	.word	26
	.word	0
	.word	32
	.word	0
	.word	33
	.word	0
	.word	34
	.word	0
	.word	40
	.word	0

