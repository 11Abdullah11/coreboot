/******************************************************************************
 * Copyright (c) 2004, 2008 IBM Corporation
 * All rights reserved.
 * This program and the accompanying materials
 * are made available under the terms of the BSD License
 * which accompanies this distribution, and is available at
 * http://www.opensource.org/licenses/bsd-license.php
 *
 * Contributors:
 *     IBM Corporation - initial implementation
 *****************************************************************************/
#ifndef _BIOSEMU_DEBUG_H_
#define _BIOSEMU_DEBUG_H_

#include <types.h>

extern u32 debug_flags;
// from x86emu...needed for debugging
extern void x86emu_dump_xregs(void);

/* printf is not available in coreboot... use printk */
#ifdef COREBOOT_V2
#include <console/console.h>
#else
#include <console.h>
#endif
/* uurgs... yuck... x86emu/x86emu.h is redefining printk... we include it here
 * and use its redefinition of printk
 * TODO: FIX!!!! */
#include "x86emu/x86emu.h"
#define printf printk

/* PH: empty versions of set/clr_ci
 * TODO: remove! */
static inline void clr_ci(void) {};
static inline void set_ci(void) {};

#define DEBUG_IO 0x1
#define DEBUG_MEM 0x2
// set this to print messages for certain virtual memory accesses (Interrupt Vectors, ...)
#define DEBUG_CHECK_VMEM_ACCESS 0x4
#define DEBUG_INTR 0x8
#define DEBUG_PRINT_INT10 0x10	// set to have the INT10 routine print characters
#define DEBUG_VBE 0x20
#define DEBUG_PMM 0x40
#define DEBUG_DISK 0x80
#define DEBUG_PNP 0x100

#define DEBUG_TRACE_X86EMU 0x1000
// set to enable tracing of JMPs in x86emu
#define DEBUG_JMP 0x2000

//#define DEBUG
//#undef DEBUG
#ifdef DEBUG

#define CHECK_DBG(_flag) if (debug_flags & _flag)

#define DEBUG_PRINTF(_x...) printf(_x);
// prints the CS:IP before the printout, NOTE: actually its CS:IP of the _next_ instruction
// to be executed, since the x86emu advances CS:IP _before_ actually executing an instruction
#define DEBUG_PRINTF_CS_IP(_x...) DEBUG_PRINTF("%x:%x ", M.x86.R_CS, M.x86.R_IP); DEBUG_PRINTF(_x);

#define DEBUG_PRINTF_IO(_x...) CHECK_DBG(DEBUG_IO) { DEBUG_PRINTF_CS_IP(_x) }
#define DEBUG_PRINTF_MEM(_x...) CHECK_DBG(DEBUG_MEM) { DEBUG_PRINTF_CS_IP(_x) }
#define DEBUG_PRINTF_INTR(_x...) CHECK_DBG(DEBUG_INTR) { DEBUG_PRINTF_CS_IP(_x) }
#define DEBUG_PRINTF_VBE(_x...) CHECK_DBG(DEBUG_VBE) { DEBUG_PRINTF_CS_IP(_x) }
#define DEBUG_PRINTF_PMM(_x...) CHECK_DBG(DEBUG_PMM) { DEBUG_PRINTF_CS_IP(_x) }
#define DEBUG_PRINTF_DISK(_x...) CHECK_DBG(DEBUG_DISK) { DEBUG_PRINTF_CS_IP(_x) }
#define DEBUG_PRINTF_PNP(_x...) CHECK_DBG(DEBUG_PNP) { DEBUG_PRINTF_CS_IP(_x) }

#else

#define CHECK_DBG(_flag) if (0)

#define DEBUG_PRINTF(_x...)
#define DEBUG_PRINTF_CS_IP(_x...)

#define DEBUG_PRINTF_IO(_x...)
#define DEBUG_PRINTF_MEM(_x...)
#define DEBUG_PRINTF_INTR(_x...)
#define DEBUG_PRINTF_VBE(_x...)
#define DEBUG_PRINTF_PMM(_x...)
#define DEBUG_PRINTF_DISK(_x...)
#define DEBUG_PRINTF_PNP(_x...)

#endif				//DEBUG

void dump(u8 * addr, u32 len);

#endif
