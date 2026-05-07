/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM register offsets for the Baochip-1x.
 * Base address: PWM_BASE (0x50120000)
 *
 * The chip has 4 PWM slices (PWM0-PWM3), each with 4 output channels.
 * PWM1 channels map to PB0-PB3 (AF3). PWM2 channels map to PC0-PC3 (AF1).
 */

#ifndef _HARDWARE_REGS_PWM_H
#define _HARDWARE_REGS_PWM_H

/* Per-slice register offsets. Slice stride is 0x40. */
#define PWM_SLICE_STRIDE        0x040
#define PWM_SLICE_OFFSET(s)     ((s) * PWM_SLICE_STRIDE)

/* Registers within each slice (add to PWM_BASE + PWM_SLICE_OFFSET) */
#define PWM_CMD_OFFSET          0x00
#define PWM_CONFIG_OFFSET       0x04
#define PWM_THRESHOLD_OFFSET    0x08
#define PWM_TH_CH0_OFFSET      0x0C
#define PWM_TH_CH1_OFFSET      0x10
#define PWM_TH_CH2_OFFSET      0x14
#define PWM_TH_CH3_OFFSET      0x18
#define PWM_COUNTER_OFFSET      0x2C

/* Shared registers */
#define PWM_EVENT_CFG_OFFSET    0x100
#define PWM_CG_OFFSET           0x104

/* Pre-frequency divider per slice */
#define PWM_PREFD0_OFFSET       0x140
#define PWM_PREFD1_OFFSET       0x144
#define PWM_PREFD2_OFFSET       0x148
#define PWM_PREFD3_OFFSET       0x14C

/* CMD register bits */
#define PWM_CMD_START           (1 << 0)
#define PWM_CMD_STOP            (1 << 1)
#define PWM_CMD_UPDATE          (1 << 2)
#define PWM_CMD_RESET           (1 << 3)
#define PWM_CMD_ARM             (1 << 4)

/* CONFIG register fields */
#define PWM_CFG_CLKSEL_REF     (0 << 11)
#define PWM_CFG_CLKSEL_FLL     (1 << 11)
#define PWM_CFG_UPDOWN_UP      (0 << 12)
#define PWM_CFG_UPDOWN_UPDN    (1 << 12)
#define PWM_CFG_PRESC(n)       (((n) & 0xFF) << 16)

/* THRESHOLD register packing */
#define PWM_THRESHOLD_PACK(lo, hi) (((lo) & 0xFFFF) | (((hi) & 0xFFFF) << 16))

/* TH_CHx output modes */
#define PWM_TH_MODE_SET         (0 << 16)
#define PWM_TH_MODE_TOG_CLR     (1 << 16)
#define PWM_TH_MODE_SET_CLR     (2 << 16)
#define PWM_TH_MODE_TOGGLE      (3 << 16)
#define PWM_TH_MODE_CLEAR       (4 << 16)
#define PWM_TH_MODE_TOG_SET     (5 << 16)
#define PWM_TH_MODE_CLR_SET     (6 << 16)

#endif
