/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * bao_bio.h - BIO Coprocessor Driver for Baochip-1x
 *
 * Bare metal C driver for the BIO (Bao I/O) coprocessor.
 * The BIO is a cluster of four PicoRV32 RISC-V cores (RV32-EMC)
 * running at up to 700 MHz, dedicated to I/O tasks.
 *
 * This header provides:
 *   - Memory-mapped register definitions for the BIO_BDMA block
 *   - IMEM base addresses for loading code into each core
 *   - IOX registers needed for BIO pin muxing
 *   - Inline functions for FIFO and event hot-path operations
 *   - Types and enums for the driver API
 *
 * Register addresses derived from:
 *   - bao1x_peri.h (auto-generated from RTL)
 *   - baochip-1x/rtl/modules/bio_bdma/rtl/bio.sv
 *   - bao1x-hal/src/bio_hw.rs (Rust HAL reference)
 *
 * Usage:
 *   #include "bao_bio.h"
 *   bio_init(700000000);       // call once at startup with fclk in Hz
 *   bio_load_code(0, code, len);
 *   bio_set_target_freq(0, 1000000);
 *   bio_map_pin(3);            // map BIO3 (PB3) to BIO
 *   bio_start_cores(0x1);      // start core 0
 */

#ifndef _HARDWARE_BIO_H
#define _HARDWARE_BIO_H

#include <stdint.h>

/* ========================================================================== */
/*                          BIO_BDMA Register Block                            */
/* ========================================================================== */

#define BIO_BDMA_BASE           0x50124000UL

/* Core control */
#define BIO_SFR_CTRL            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x00))
#define BIO_SFR_CFGINFO         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x04))
#define BIO_SFR_CONFIG          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x08))

/* FIFO level (read-only) */
#define BIO_SFR_FLEVEL          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x0C))

/* Host-to-BIO FIFO write (TX) */
#define BIO_SFR_TXF0            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x10))
#define BIO_SFR_TXF1            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x14))
#define BIO_SFR_TXF2            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x18))
#define BIO_SFR_TXF3            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x1C))

/* BIO-to-host FIFO read (RX) */
#define BIO_SFR_RXF0            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x20))
#define BIO_SFR_RXF1            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x24))
#define BIO_SFR_RXF2            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x28))
#define BIO_SFR_RXF3            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x2C))

/* Event system */
#define BIO_SFR_ELEVEL          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x30))
#define BIO_SFR_ETYPE           (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x34))
#define BIO_SFR_EVENT_SET       (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x38))
#define BIO_SFR_EVENT_CLR       (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x3C))
#define BIO_SFR_EVENT_STATUS    (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x40))

/* External clock */
#define BIO_SFR_EXTCLOCK        (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x44))

/* FIFO clear */
#define BIO_SFR_FIFO_CLR        (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x48))

/* Quantum clock dividers (one per core) */
#define BIO_SFR_QDIV0           (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x50))
#define BIO_SFR_QDIV1           (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x54))
#define BIO_SFR_QDIV2           (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x58))
#define BIO_SFR_QDIV3           (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x5C))

/* I/O signal conditioning */
#define BIO_SFR_SYNC_BYPASS     (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x60))
#define BIO_SFR_IO_OE_INV       (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x64))
#define BIO_SFR_IO_O_INV        (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x68))
#define BIO_SFR_IO_I_INV        (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x6C))

/* IRQ routing */
#define BIO_SFR_IRQMASK_0      (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x70))
#define BIO_SFR_IRQMASK_1      (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x74))
#define BIO_SFR_IRQMASK_2      (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x78))
#define BIO_SFR_IRQMASK_3      (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x7C))
#define BIO_SFR_IRQ_EDGE        (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x80))

/* Debug (read-only) */
#define BIO_SFR_DBG_PADOUT      (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x84))
#define BIO_SFR_DBG_PADOE       (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x88))
#define BIO_SFR_DBG0            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x90))
#define BIO_SFR_DBG1            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x94))
#define BIO_SFR_DBG2            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x98))
#define BIO_SFR_DBG3            (*(volatile uint32_t *)(BIO_BDMA_BASE + 0x9C))

/* DMA gutter addresses */
#define BIO_SFR_MEM_GUTTER      (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xA0))
#define BIO_SFR_PERI_GUTTER     (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xA4))

/* DMA request mapping (6 map registers + 6 status registers) */
#define BIO_SFR_EVMAP0          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xB0))
#define BIO_SFR_EVMAP1          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xB4))
#define BIO_SFR_EVMAP2          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xB8))
#define BIO_SFR_EVMAP3          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xBC))
#define BIO_SFR_EVMAP4          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xC0))
#define BIO_SFR_EVMAP5          (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xC4))
#define BIO_SFR_EVSTAT0         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xC8))
#define BIO_SFR_EVSTAT1         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xCC))
#define BIO_SFR_EVSTAT2         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xD0))
#define BIO_SFR_EVSTAT3         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xD4))
#define BIO_SFR_EVSTAT4         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xD8))
#define BIO_SFR_EVSTAT5         (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xDC))

/* DMA whitelist filter windows (4 pairs of base/bounds) */
/* NOTE: these registers are write-only due to a silicon bug */
#define BIO_SFR_FILTER_BASE_0   (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xE0))
#define BIO_SFR_FILTER_BOUNDS_0 (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xE4))
#define BIO_SFR_FILTER_BASE_1   (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xE8))
#define BIO_SFR_FILTER_BOUNDS_1 (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xEC))
#define BIO_SFR_FILTER_BASE_2   (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xF0))
#define BIO_SFR_FILTER_BOUNDS_2 (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xF4))
#define BIO_SFR_FILTER_BASE_3   (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xF8))
#define BIO_SFR_FILTER_BOUNDS_3 (*(volatile uint32_t *)(BIO_BDMA_BASE + 0xFC))


/* ========================================================================== */
/*                     SFR_CTRL Field Helpers                                  */
/* ========================================================================== */

/* SFR_CTRL: bits [3:0] = EN, [7:4] = RESTART, [11:8] = CLKDIV_RESTART */
#define BIO_CTRL_EN(core)               (1U << (core))
#define BIO_CTRL_RESTART(core)          (1U << ((core) + 4))
#define BIO_CTRL_CLKDIV_RESTART(core)   (1U << ((core) + 8))

/* Start core N with restart + clkdiv_restart + enable */
#define BIO_CTRL_START(core)    (BIO_CTRL_EN(core) | BIO_CTRL_RESTART(core) | BIO_CTRL_CLKDIV_RESTART(core))

/* Start all four cores simultaneously */
#define BIO_CTRL_START_ALL      0xFFF


/* ========================================================================== */
/*                     SFR_CONFIG Field Helpers                                */
/* ========================================================================== */

#define BIO_CONFIG_SNAP_OUT_WHICH(core) ((core) & 0x3)
#define BIO_CONFIG_SNAP_OUT_EN          (1U << 2)
#define BIO_CONFIG_SNAP_IN_WHICH(core)  (((core) & 0x3) << 3)
#define BIO_CONFIG_SNAP_IN_EN           (1U << 5)
#define BIO_CONFIG_DISABLE_FILTER_PERI  (1U << 6)
#define BIO_CONFIG_DISABLE_FILTER_MEM   (1U << 7)
#define BIO_CONFIG_CLOCKING_MODE(m)     (((m) & 0x3) << 8)


/* ========================================================================== */
/*                     SFR_QDIV Encoding                                       */
/* ========================================================================== */

/*
 * SFR_QDIV register format:
 *   [31:16] = div_int    (integer part, 0-65535)
 *   [15:8]  = div_frac   (fractional part, 0-255)
 *   [7:0]   = unused     (write 0)
 *
 * Output frequency = fclk / (div_int + div_frac / 256)
 * If div_int == 0 and div_frac == 0, quantum fires every fclk cycle.
 */
#define BIO_QDIV(div_int, div_frac) \
    (((uint32_t)(div_int) << 16) | ((uint32_t)(div_frac) << 8))


/* ========================================================================== */
/*                     SFR_EXTCLOCK Field Helpers                              */
/* ========================================================================== */

#define BIO_EXTCLK_USE(core)            (1U << (core))
#define BIO_EXTCLK_GPIO_0(pin)          (((pin) & 0x1F) << 4)
#define BIO_EXTCLK_GPIO_1(pin)          (((pin) & 0x1F) << 9)
#define BIO_EXTCLK_GPIO_2(pin)          (((pin) & 0x1F) << 14)
#define BIO_EXTCLK_GPIO_3(pin)          (((pin) & 0x1F) << 19)


/* ========================================================================== */
/*                     Instruction Memory                                      */
/* ========================================================================== */

#define BIO_IMEM0_BASE          0x50125000UL
#define BIO_IMEM1_BASE          0x50126000UL
#define BIO_IMEM2_BASE          0x50127000UL
#define BIO_IMEM3_BASE          0x50128000UL
#define BIO_IMEM_SIZE           0x1000          /* 4096 bytes per core */
#define BIO_IMEM_WORDS          (BIO_IMEM_SIZE / 4)

/* Pointer to instruction memory for a given core (0-3) */
static inline volatile uint32_t *bio_imem(uint32_t core)
{
    static const uintptr_t bases[4] = {
        BIO_IMEM0_BASE, BIO_IMEM1_BASE, BIO_IMEM2_BASE, BIO_IMEM3_BASE
    };
    return (volatile uint32_t *)(bases[core & 3]);
}


/* ========================================================================== */
/*                     FIFO Page Aliases                                       */
/* ========================================================================== */

/*
 * Each FIFO has a page-aliased register set that can be mapped into
 * a separate address space (used by Xous for per-process FIFO access).
 * In bare metal, you can use either the main SFR_TXF/RXF registers
 * or these aliases; they point to the same hardware.
 */
#define BIO_FIFO0_BASE          0x50129000UL
#define BIO_FIFO1_BASE          0x5012A000UL
#define BIO_FIFO2_BASE          0x5012B000UL
#define BIO_FIFO3_BASE          0x5012C000UL


/* ========================================================================== */
/*                     IOX Registers for BIO Pin Muxing                        */
/* ========================================================================== */

#define IOX_BASE                0x5012F000UL

/* Alternate function select: 2 bits per pin, 8 pins per register */
#define IOX_CRAFSEL_BL          (*(volatile uint32_t *)(IOX_BASE + 0x008))  /* PB[7:0]  */
#define IOX_CRAFSEL_BH          (*(volatile uint32_t *)(IOX_BASE + 0x00C))  /* PB[15:8] */
#define IOX_CRAFSEL_CL          (*(volatile uint32_t *)(IOX_BASE + 0x010))  /* PC[7:0]  */
#define IOX_CRAFSEL_CH          (*(volatile uint32_t *)(IOX_BASE + 0x014))  /* PC[15:8] */

/* BIO mux select: bit N = 1 hands BIO channel N to the BIO coprocessor */
#define IOX_SFR_PIOSEL          (*(volatile uint32_t *)(IOX_BASE + 0x200))

/* Alternate function codes */
#define IOX_FUNC_GPIO           0x00
#define IOX_FUNC_AF1            0x01    /* BIO lives here */
#define IOX_FUNC_AF2            0x02
#define IOX_FUNC_AF3            0x03


/* ========================================================================== */
/*                     SFR_DBG Field Helpers                                   */
/* ========================================================================== */

#define BIO_DBG_PC(val)         ((val) & 0xFFF)
#define BIO_DBG_TRAP(val)       (((val) >> 12) & 1)


/* ========================================================================== */
/*                     SFR_CFGINFO Expected Values                             */
/* ========================================================================== */

#define BIO_CFGINFO_VERSION     8
#define BIO_CFGINFO_CORES       4
#define BIO_CFGINFO_RAM_SIZE    4096


/* ========================================================================== */
/*                     FIFO Level Helpers                                      */
/* ========================================================================== */

/*
 * SFR_FLEVEL: 4 bits per FIFO, reports current fill level (0-8).
 *   [3:0]   = FIFO 0
 *   [7:4]   = FIFO 1
 *   [11:8]  = FIFO 2
 *   [15:12] = FIFO 3
 */
#define BIO_FIFO_DEPTH          8

static inline uint32_t bio_fifo_level(uint32_t fifo)
{
    return (BIO_SFR_FLEVEL >> (fifo * 4)) & 0xF;
}

static inline int bio_fifo_empty(uint32_t fifo)
{
    return bio_fifo_level(fifo) == 0;
}

static inline int bio_fifo_full(uint32_t fifo)
{
    return bio_fifo_level(fifo) >= BIO_FIFO_DEPTH;
}


/* ========================================================================== */
/*                     Inline FIFO Push/Pop                                    */
/* ========================================================================== */

/*
 * IMPORTANT: Host writes to a full FIFO are silently dropped.
 *            Host reads from an empty FIFO return the last value.
 *            Check levels with bio_fifo_level() if you need reliability.
 */

static inline void bio_fifo_push(uint32_t fifo, uint32_t value)
{
    volatile uint32_t *txf = (volatile uint32_t *)(BIO_BDMA_BASE + 0x10 + fifo * 4);
    *txf = value;
}

static inline uint32_t bio_fifo_pop(uint32_t fifo)
{
    volatile uint32_t *rxf = (volatile uint32_t *)(BIO_BDMA_BASE + 0x20 + fifo * 4);
    return *rxf;
}

/* Typed versions for clarity */
static inline void bio_push_fifo0(uint32_t v) { BIO_SFR_TXF0 = v; }
static inline void bio_push_fifo1(uint32_t v) { BIO_SFR_TXF1 = v; }
static inline void bio_push_fifo2(uint32_t v) { BIO_SFR_TXF2 = v; }
static inline void bio_push_fifo3(uint32_t v) { BIO_SFR_TXF3 = v; }

static inline uint32_t bio_pop_fifo0(void) { return BIO_SFR_RXF0; }
static inline uint32_t bio_pop_fifo1(void) { return BIO_SFR_RXF1; }
static inline uint32_t bio_pop_fifo2(void) { return BIO_SFR_RXF2; }
static inline uint32_t bio_pop_fifo3(void) { return BIO_SFR_RXF3; }


/* ========================================================================== */
/*                     Inline Event Helpers                                    */
/* ========================================================================== */

static inline void bio_event_set(uint32_t bits)
{
    BIO_SFR_EVENT_SET = bits & 0x00FFFFFF;
}

static inline void bio_event_clear(uint32_t bits)
{
    BIO_SFR_EVENT_CLR = bits & 0x00FFFFFF;
}

static inline uint32_t bio_event_status(void)
{
    return BIO_SFR_EVENT_STATUS;
}


/* ========================================================================== */
/*                     Inline Debug Helpers                                    */
/* ========================================================================== */

static inline uint32_t bio_core_pc(uint32_t core)
{
    volatile uint32_t *dbg = (volatile uint32_t *)(BIO_BDMA_BASE + 0x90 + core * 4);
    return BIO_DBG_PC(*dbg);
}

static inline int bio_core_trapped(uint32_t core)
{
    volatile uint32_t *dbg = (volatile uint32_t *)(BIO_BDMA_BASE + 0x90 + core * 4);
    return BIO_DBG_TRAP(*dbg);
}


/* ========================================================================== */
/*                     Two-instruction BIO Halt Word                           */
/* ========================================================================== */

/*
 * 0xA001A001 = two compressed "j ." (jump-to-self) instructions.
 * Used to fill unused IMEM so cores safely halt instead of running garbage.
 */
#define BIO_HALT_WORD           0xA001A001UL


/* ========================================================================== */
/*                     Driver API (implemented in bao_bio.c)                   */
/* ========================================================================== */

/*
 * bio_init - Full BIO initialization.
 *
 * Stops all cores, sets clocking mode to isochronous, fills all IMEM
 * with halt instructions, drains FIFOs, and clears all configuration.
 * Call once at startup before using any other BIO function.
 *
 * @fclk_hz: The BIO core clock frequency in Hz (typically 700000000).
 *           Used for clock divider calculations.
 *
 * Returns 0 on success, -1 if CFGINFO readback looks wrong.
 */
int bio_init(uint32_t fclk_hz);

/*
 * bio_load_code - Load machine code into a BIO core's instruction memory.
 *
 * The target core must be stopped before calling this. If core 0 is the
 * target, all cores must be stopped (core 0's IMEM port is shared with
 * the host write path).
 *
 * @core: Core number (0-3).
 * @code: Pointer to the machine code bytes.
 * @len:  Length in bytes. Must be <= 4096.
 *
 * Returns 0 on success, -1 on verification failure.
 */
int bio_load_code(uint32_t core, const uint8_t *code, uint32_t len);

/*
 * bio_load_code_words - Load pre-assembled 32-bit words into IMEM.
 *
 * Convenience function for loading code already in uint32_t form.
 * Same requirements as bio_load_code regarding core state.
 *
 * @core:      Core number (0-3).
 * @words:     Pointer to the machine code words.
 * @num_words: Number of 32-bit words.
 *
 * Returns 0 on success, -1 on verification failure.
 */
int bio_load_code_words(uint32_t core, const uint32_t *words, uint32_t num_words);

/*
 * bio_set_divider - Set a core's quantum clock divider directly.
 *
 * @core:     Core number (0-3).
 * @div_int:  Integer part of divider (0-65535).
 * @div_frac: Fractional part (0-255). Use 0 for jitter-free operation.
 */
void bio_set_divider(uint32_t core, uint16_t div_int, uint8_t div_frac);

/*
 * bio_set_target_freq - Set a core's quantum to approximate a target frequency.
 *
 * Computes the best integer divider for the current fclk. For jitter-free
 * output, only integer dividers are used (no fractional component).
 *
 * @core:      Core number (0-3).
 * @target_hz: Desired quantum frequency in Hz.
 *
 * Returns the actual achieved frequency in Hz.
 */
uint32_t bio_set_target_freq(uint32_t core, uint32_t target_hz);

/*
 * bio_set_extclk - Configure a core to use an external GPIO pin as quantum.
 *
 * The quantum will fire on the rising edge of the specified BIO pin.
 * This disables the internal clock divider for that core.
 *
 * @core:    Core number (0-3).
 * @bio_pin: BIO pin number (0-31) to use as clock source.
 */
void bio_set_extclk(uint32_t core, uint32_t bio_pin);

/*
 * bio_clear_extclk - Switch a core back to internal clock divider.
 *
 * @core: Core number (0-3).
 */
void bio_clear_extclk(uint32_t core);

/*
 * bio_start_cores - Start one or more BIO cores.
 *
 * Each bit in the mask corresponds to a core. Setting a bit enables,
 * restarts, and resets the clock divider for that core. Cores started
 * in the same call will have their dividers in lock-step.
 *
 * @core_mask: Bitmask of cores to start (e.g., 0x3 for cores 0 and 1).
 */
void bio_start_cores(uint32_t core_mask);

/*
 * bio_stop_cores - Stop one or more BIO cores.
 *
 * @core_mask: Bitmask of cores to stop.
 */
void bio_stop_cores(uint32_t core_mask);

/*
 * bio_stop_all - Stop all BIO cores immediately.
 */
void bio_stop_all(void);

/*
 * bio_map_pin - Map a BIO channel to its physical pin.
 *
 * Sets the pin's alternate function to AF1 and enables the BIO mux
 * in SFR_PIOSEL. After this call, the BIO controls the pin.
 *
 * BIO 0-15  = PB0-PB15
 * BIO 16-31 = PC0-PC15
 *
 * @bio_pin: BIO channel number (0-31).
 */
void bio_map_pin(uint32_t bio_pin);

/*
 * bio_unmap_pin - Release a BIO channel back to GPIO.
 *
 * Clears the SFR_PIOSEL bit and sets the pin back to GPIO function.
 *
 * @bio_pin: BIO channel number (0-31).
 */
void bio_unmap_pin(uint32_t bio_pin);

/*
 * bio_map_pins_mask - Map multiple BIO channels at once.
 *
 * @mask: Bitmask where bit N = 1 maps BIO channel N.
 */
void bio_map_pins_mask(uint32_t mask);

/*
 * bio_fifo_drain - Drain a FIFO by reading and discarding all entries.
 *
 * @fifo: FIFO number (0-3).
 */
void bio_fifo_drain(uint32_t fifo);

/*
 * bio_fifo_drain_all - Drain all four FIFOs.
 */
void bio_fifo_drain_all(void);

/*
 * bio_get_version - Read the hardware version from SFR_CFGINFO.
 *
 * Returns the version byte (expected: 8 for Baochip-1x).
 */
uint32_t bio_get_version(void);

/*
 * bio_get_fclk - Returns the fclk value passed to bio_init().
 */
uint32_t bio_get_fclk(void);

#endif /* _HARDWARE_BIO_H */