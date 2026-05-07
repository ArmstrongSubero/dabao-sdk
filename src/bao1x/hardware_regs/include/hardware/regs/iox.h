/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * IOX (I/O Crossbar) register offsets.
 * Base address: IOX_BASE (0x5012F000)
 */

#ifndef _HARDWARE_REGS_IOX_H
#define _HARDWARE_REGS_IOX_H

/* Alternate function select (2 bits per pin, 8 pins per register) */
#define IOX_CRAFSEL0_OFFSET     0x000   /* PA[7:0]  */
#define IOX_CRAFSEL1_OFFSET     0x004   /* PA[15:8] */
#define IOX_CRAFSEL2_OFFSET     0x008   /* PB[7:0]  */
#define IOX_CRAFSEL3_OFFSET     0x00C   /* PB[15:8] */
#define IOX_CRAFSEL4_OFFSET     0x010   /* PC[7:0]  */
#define IOX_CRAFSEL5_OFFSET     0x014   /* PC[15:8] */
#define IOX_CRAFSEL6_OFFSET     0x018   /* PD[7:0]  */
#define IOX_CRAFSEL7_OFFSET     0x01C   /* PD[15:8] */
#define IOX_CRAFSEL8_OFFSET     0x020   /* PE[7:0]  */
#define IOX_CRAFSEL9_OFFSET     0x024   /* PE[15:8] */
#define IOX_CRAFSEL10_OFFSET    0x028   /* PF[7:0]  */
#define IOX_CRAFSEL11_OFFSET    0x02C   /* PF[15:8] */

/* GPIO output value (1 bit per pin, per port) */
#define IOX_GPIOOUT_PA_OFFSET   0x130
#define IOX_GPIOOUT_PB_OFFSET   0x134
#define IOX_GPIOOUT_PC_OFFSET   0x138
#define IOX_GPIOOUT_PD_OFFSET   0x13C
#define IOX_GPIOOUT_PE_OFFSET   0x140
#define IOX_GPIOOUT_PF_OFFSET   0x144

/* GPIO output enable (1 bit per pin, per port) */
#define IOX_GPIOOE_PA_OFFSET    0x148
#define IOX_GPIOOE_PB_OFFSET    0x14C
#define IOX_GPIOOE_PC_OFFSET    0x150
#define IOX_GPIOOE_PD_OFFSET    0x154
#define IOX_GPIOOE_PE_OFFSET    0x158
#define IOX_GPIOOE_PF_OFFSET    0x15C

/* GPIO pull-up enable (1 bit per pin, default all enabled) */
#define IOX_GPIOPU_PA_OFFSET    0x160
#define IOX_GPIOPU_PB_OFFSET    0x164
#define IOX_GPIOPU_PC_OFFSET    0x168
#define IOX_GPIOPU_PD_OFFSET    0x16C
#define IOX_GPIOPU_PE_OFFSET    0x170
#define IOX_GPIOPU_PF_OFFSET    0x174

/* GPIO input value (read-only, 1 bit per pin, per port) */
#define IOX_GPIOIN_PA_OFFSET    0x178
#define IOX_GPIOIN_PB_OFFSET    0x17C
#define IOX_GPIOIN_PC_OFFSET    0x180
#define IOX_GPIOIN_PD_OFFSET    0x184
#define IOX_GPIOIN_PE_OFFSET    0x188
#define IOX_GPIOIN_PF_OFFSET    0x18C

/* BIO mux select */
#define IOX_PIOSEL_OFFSET       0x200

/* Schmitt trigger enable (1 bit per pin, per port) */
#define IOX_SCHMITT_PA_OFFSET   0x230
#define IOX_SCHMITT_PB_OFFSET   0x234
#define IOX_SCHMITT_PC_OFFSET   0x238

/* Drive strength (2 bits per pin, same layout as CRAFSEL) */
#define IOX_DRIVESEL0_OFFSET    0x260
#define IOX_DRIVESEL1_OFFSET    0x264
#define IOX_DRIVESEL2_OFFSET    0x268
#define IOX_DRIVESEL3_OFFSET    0x26C
#define IOX_DRIVESEL4_OFFSET    0x270
#define IOX_DRIVESEL5_OFFSET    0x274

/* Port count and pins per port */
#define IOX_NUM_PORTS           6
#define IOX_PINS_PER_PORT       16

#endif
