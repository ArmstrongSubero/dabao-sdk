/*
 * bao_bio.c - BIO Coprocessor Driver for Baochip-1x
 *
 * Implements the BIO driver API defined in bao_bio.h.
 * See that header for full documentation of each function.
 *
 * This driver is stateless: it does not track which cores or pins are
 * in use. The programmer is responsible for avoiding resource conflicts.
 * This is appropriate for bare metal single-developer environments.
 *
 * The init sequence follows the Rust HAL (bao1x-hal/src/bio_hw.rs)
 * reference implementation.
 */

#include "hardware/bio.h"
#include "sevs_runtime.h"

/* ========================================================================== */
/*                     Module State                                            */
/* ========================================================================== */

/*
 * We store fclk so bio_set_target_freq() can compute dividers.
 * This is the only piece of "state" in the driver.
 */
static uint32_t s_fclk_hz = 0;

/* ========================================================================== */
/*                     Initialization                                          */
/* ========================================================================== */

/** @brief Initialize the BIO coprocessor: halt all cores, fill IMEM, drain FIFOs.
 *  @param[in] fclk_hz Core clock frequency in Hz (used for divider calculations).
 *  @return 0 on success, -1 if IMEM or CFGINFO verification fails.
 *  @req REQ-DABAO-BIO-0001 */
int bio_init(uint32_t fclk_hz)
{
    SEVS_ASSERT(fclk_hz > 0);

    uint32_t i;

    s_fclk_hz = fclk_hz;

    /* Stop all cores */
    BIO_SFR_CTRL = 0x0;

    /* Set clocking mode to isochronous (0b11) for lowest latency */
    BIO_SFR_CONFIG = BIO_CONFIG_CLOCKING_MODE(3);

    /* Clear all clock dividers and external clock config */
    BIO_SFR_EXTCLOCK = 0;
    BIO_SFR_QDIV0 = 0;
    BIO_SFR_QDIV1 = 0;
    BIO_SFR_QDIV2 = 0;
    BIO_SFR_QDIV3 = 0;

    /*
     * Fill all IMEM with halt instructions (jump-to-self).
     * Core 0 must be disabled for the host to access IMEM.
     */
    for (i = 0; i < 4; i++) {
        volatile uint32_t *imem = bio_imem(i);
        uint32_t w;
        for (w = 0; w < BIO_IMEM_WORDS; w++) {
            imem[w] = BIO_HALT_WORD;
        }
    }

    /* Verify IMEM fill for each core */
    for (i = 0; i < 4; i++) {
        volatile uint32_t *imem = bio_imem(i);
        uint32_t w;
        for (w = 0; w < BIO_IMEM_WORDS; w++) {
            if (imem[w] != BIO_HALT_WORD) {
                return -1;
            }
        }
    }

    /*
     * Briefly run all cores to flush any internal pipeline state,
     * then drain all FIFOs. This follows the Rust HAL init sequence.
     */
    BIO_SFR_CTRL = BIO_CTRL_START_ALL;

    /* Read each FIFO 16 times to drain any stale data */
    for (i = 0; i < 16; i++) {
        (void)BIO_SFR_RXF0;
        (void)BIO_SFR_RXF1;
        (void)BIO_SFR_RXF2;
        (void)BIO_SFR_RXF3;
    }

    /* Clear FIFO pointers */
    BIO_SFR_FIFO_CLR = 0xF;

    /* Stop all cores again */
    BIO_SFR_CTRL = 0x0;

    /*
     * Now run the memory init code on each core individually,
     * just like the Rust HAL does. This tests the load/store
     * paths through each core's memory port.
     *
     * The mem_init program:
     *   sw x0, 0x20(x0)   - store zero at address 0x20
     *   lw t0, 0x20(x0)   - load it back
     *   li sp, 0x61200000  - set stack to SRAM
     *   addi sp, sp, -4    - adjust
     *   sw x0, 0(sp)       - store through stack pointer
     *   lw t0, 0(sp)       - load back
     *   j .                - halt
     *
     * We inline the machine code directly rather than depending on
     * an assembler, keeping this file self-contained.
     */
    static const uint32_t mem_init[] = {
        0x0200A023,  /* sw x0, 0x20(x0) */
        0x02002283,  /* lw t0, 0x20(x0) */
        0x61200137,  /* lui sp, 0x61200 */
        0xFFC10113,  /* addi sp, sp, -4 */
        0x00012023,  /* sw x0, 0(sp)    */
        0x00012283,  /* lw t0, 0(sp)    */
        0x0000006F,  /* j . (loop forever) */
    };
    uint32_t mem_init_len = sizeof(mem_init) / sizeof(mem_init[0]);

    for (i = 0; i < 4; i++) {
        volatile uint32_t *imem = bio_imem(i);
        uint32_t w;

        /* Load the init code */
        for (w = 0; w < mem_init_len; w++) {
            imem[w] = mem_init[w];
        }

        /* Run just this core briefly */
        BIO_SFR_CTRL = BIO_CTRL_START(i);

        /* Drain FIFOs again */
        uint32_t j;
        for (j = 0; j < 16; j++) {
            (void)BIO_SFR_RXF0;
            (void)BIO_SFR_RXF1;
            (void)BIO_SFR_RXF2;
            (void)BIO_SFR_RXF3;
        }

        /* Stop the core */
        BIO_SFR_CTRL = 0x0;
    }

    /* Final FIFO clear */
    BIO_SFR_FIFO_CLR = 0xF;

    /* Clear I/O configuration */
    BIO_SFR_SYNC_BYPASS = 0;
    BIO_SFR_IO_OE_INV = 0;
    BIO_SFR_IO_O_INV = 0;
    BIO_SFR_IO_I_INV = 0;

    /* Clear IRQ masks */
    BIO_SFR_IRQMASK_0 = 0;
    BIO_SFR_IRQMASK_1 = 0;
    BIO_SFR_IRQMASK_2 = 0;
    BIO_SFR_IRQMASK_3 = 0;
    BIO_SFR_IRQ_EDGE = 0;

    /* Sanity check: read CFGINFO */
    uint32_t info = BIO_SFR_CFGINFO;
    uint32_t version = info & 0xFF;
    uint32_t cores = (info >> 8) & 0xFF;
    uint32_t ram = (info >> 16) & 0xFFFF;

    if (version != BIO_CFGINFO_VERSION || cores != BIO_CFGINFO_CORES
        || ram != BIO_CFGINFO_RAM_SIZE) {
        return -1;
    }

    return 0;
}

/* ========================================================================== */
/*                     Code Loading                                            */
/* ========================================================================== */

/** @brief Load a byte-oriented program into a BIO core instruction memory.
 *  @param[in] core Core index (0-3).
 *  @param[in] code Program bytes (little-endian).
 *  @param[in] len Program length in bytes.
 *  @return 0 on success, -1 if readback verification fails.
 *  @req REQ-DABAO-BIO-0002 */
int bio_load_code(uint32_t core, const uint8_t *code, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(code);
    volatile uint32_t *imem = bio_imem(core);
    uint32_t i;
    uint32_t full_words = len / 4;
    uint32_t remainder = len % 4;

    /* Write full 32-bit words */
    for (i = 0; i < full_words; i++) {
        uint32_t word = (uint32_t)code[i * 4]
                      | ((uint32_t)code[i * 4 + 1] << 8)
                      | ((uint32_t)code[i * 4 + 2] << 16)
                      | ((uint32_t)code[i * 4 + 3] << 24);
        imem[i] = word;
    }

    /* Handle any trailing bytes */
    if (remainder > 0) {
        uint32_t word = 0;
        uint32_t j;
        for (j = 0; j < remainder; j++) {
            word |= (uint32_t)code[full_words * 4 + j] << (j * 8);
        }
        imem[full_words] = word;
    }

    /* Verify: read back and compare */
    for (i = 0; i < full_words; i++) {
        uint32_t expected = (uint32_t)code[i * 4]
                          | ((uint32_t)code[i * 4 + 1] << 8)
                          | ((uint32_t)code[i * 4 + 2] << 16)
                          | ((uint32_t)code[i * 4 + 3] << 24);
        uint32_t readback = imem[i];
        if (readback != expected) {
            return -1;
        }
    }

    if (remainder > 0) {
        uint32_t expected = 0;
        uint32_t j;
        for (j = 0; j < remainder; j++) {
            expected |= (uint32_t)code[full_words * 4 + j] << (j * 8);
        }
        if (imem[full_words] != expected) {
            return -1;
        }
    }

    return 0;
}

/** @brief Load a word-oriented program into a BIO core instruction memory.
 *  @param[in] core Core index (0-3).
 *  @param[in] words Array of 32-bit instruction words.
 *  @param[in] num_words Number of words to load.
 *  @return 0 on success, -1 if readback verification fails.
 *  @req REQ-DABAO-BIO-0003 */
int bio_load_code_words(uint32_t core, const uint32_t *words, uint32_t num_words)
{
    SEVS_REQUIRE_NOT_NULL(words);
    volatile uint32_t *imem = bio_imem(core);
    uint32_t i;

    /* Write words */
    for (i = 0; i < num_words; i++) {
        imem[i] = words[i];
    }

    /* Verify */
    for (i = 0; i < num_words; i++) {
        if (imem[i] != words[i]) {
            return -1;
        }
    }

    return 0;
}

/* ========================================================================== */
/*                     Clock Divider Configuration                             */
/* ========================================================================== */

/** @brief Set the clock divider for a BIO core.
 *  @param[in] core Core index (0-3).
 *  @param[in] div_int Integer part of divider.
 *  @param[in] div_frac Fractional part (0-255, units of 1/256).
 *  @req REQ-DABAO-BIO-0004 */
void bio_set_divider(uint32_t core, uint16_t div_int, uint8_t div_frac)
{
    SEVS_ASSERT(core <= 3);

    uint32_t val = BIO_QDIV(div_int, div_frac);

    switch (core & 3) {
    case 0: BIO_SFR_QDIV0 = val; break;
    case 1: BIO_SFR_QDIV1 = val; break;
    case 2: BIO_SFR_QDIV2 = val; break;
    case 3: BIO_SFR_QDIV3 = val; break;
    }

    /* Also clear any external clock setting for this core */
    bio_clear_extclk(core);
}

/*
 * Internal: compute actual frequency from divider values.
 * freq = fclk / (div_int + div_frac / 256)
 *      = (fclk * 256) / (div_int * 256 + div_frac)
 *
 * We avoid 64-bit division (which pulls in __udivdi3 from libgcc)
 * by doing the multiply and divide in 32-bit steps. Since the divisor
 * is at most 65535*256+255 = 16,776,960 (~16M), and fclk is at most
 * 700M, we can split: fclk / divisor_int first, then handle the
 * fractional remainder.
 *
 * For integer-only dividers (div_frac == 0), this is just fclk / div_int.
 */
static uint32_t compute_freq(uint16_t div_int, uint8_t div_frac)
{
    SEVS_ASSERT(s_fclk_hz > 0);

    if (div_int == 0 && div_frac == 0) {
        return s_fclk_hz;
    }

    if (div_frac == 0) {
        /* Pure integer division, no overflow risk */
        return (s_fclk_hz + div_int / 2) / div_int;
    }

    /* Fractional case: freq = (fclk * 256) / (div_int * 256 + div_frac)
     * divisor fits in 24 bits (max ~16.7M), so we can do this with
     * a manual long division to stay in 32-bit land.
     *
     * Split fclk * 256 into high and low parts:
     *   fclk * 256 = (fclk >> 8) * 65536 + (fclk & 0xFF) * 256
     * But simpler: since divisor <= 16,776,960 and fclk <= 700,000,000,
     * fclk / divisor <= 700M / 1 = 700M which fits in 32 bits.
     * So we just need to compute (fclk * 256) / divisor without overflow.
     *
     * fclk * 256 overflows 32 bits when fclk > 16.7M, which it always does.
     * Use: result = fclk / divisor * 256 + (fclk % divisor) * 256 / divisor
     */
    uint32_t divisor = (uint32_t)div_int * 256 + (uint32_t)div_frac;
    uint32_t quotient = s_fclk_hz / divisor;
    uint32_t remainder = s_fclk_hz % divisor;
    /* remainder < divisor <= 16.7M, so remainder * 256 <= ~4.29G, fits in u32 */
    return quotient * 256 + (remainder * 256 + divisor / 2) / divisor;
}

/** @brief Set a BIO core clock divider to approximate the target frequency.
 *  @param[in] core Core index (0-3).
 *  @param[in] target_hz Desired output frequency in Hz.
 *  @return Actual frequency achieved after divider rounding.
 *  @req REQ-DABAO-BIO-0005 */
uint32_t bio_set_target_freq(uint32_t core, uint32_t target_hz)
{
    SEVS_ASSERT(core <= 3);

    if (s_fclk_hz == 0) {
        return 0;
    }

    /* If target is at or above fclk, use bypass (no division) */
    if (target_hz >= s_fclk_hz) {
        bio_set_divider(core, 0, 0);
        return s_fclk_hz;
    }

    /* Integer division only for jitter-free output */
    uint32_t div_ideal = s_fclk_hz / target_hz;
    if (div_ideal < 1) div_ideal = 1;
    if (div_ideal > 65535) div_ideal = 65535;

    /* Try both floor and ceil to find closest match */
    uint16_t d1 = (uint16_t)div_ideal;
    uint16_t d2 = (div_ideal < 65535) ? (uint16_t)(div_ideal + 1) : d1;

    uint32_t f1 = compute_freq(d1, 0);
    uint32_t f2 = compute_freq(d2, 0);

    uint32_t err1 = (f1 > target_hz) ? (f1 - target_hz) : (target_hz - f1);
    uint32_t err2 = (f2 > target_hz) ? (f2 - target_hz) : (target_hz - f2);

    if (err1 <= err2) {
        bio_set_divider(core, d1, 0);
        return f1;
    } else {
        bio_set_divider(core, d2, 0);
        return f2;
    }
}

/** @brief Configure a BIO core to use an external clock from a BIO pin.
 *  @param[in] core Core index (0-3).
 *  @param[in] bio_pin BIO pin number (0-31) for clock input.
 *  @req REQ-DABAO-BIO-0006 */
void bio_set_extclk(uint32_t core, uint32_t bio_pin)
{
    SEVS_ASSERT(core <= 3);
    SEVS_ASSERT(bio_pin < 32);

    uint32_t c = core & 3;
    uint32_t p = bio_pin & 0x1F;

    /* Clear this core's divider */
    switch (c) {
    case 0: BIO_SFR_QDIV0 = 0; break;
    case 1: BIO_SFR_QDIV1 = 0; break;
    case 2: BIO_SFR_QDIV2 = 0; break;
    case 3: BIO_SFR_QDIV3 = 0; break;
    }

    /* Set the pin for this core's external clock, and enable it */
    uint32_t reg = BIO_SFR_EXTCLOCK;
    /* Clear the pin field for this core */
    uint32_t pin_shift = 4 + c * 5;
    reg &= ~(0x1FU << pin_shift);
    /* Set the new pin */
    reg |= (p << pin_shift);
    /* Enable external clock for this core */
    reg |= BIO_EXTCLK_USE(c);
    BIO_SFR_EXTCLOCK = reg;
}

/** @brief Disable external clock for a BIO core, reverting to internal divider.
 *  @param[in] core Core index (0-3).
 *  @req REQ-DABAO-BIO-0007 */
void bio_clear_extclk(uint32_t core)
{
    SEVS_ASSERT(core <= 3);

    uint32_t c = core & 3;
    uint32_t reg = BIO_SFR_EXTCLOCK;
    reg &= ~BIO_EXTCLK_USE(c);
    BIO_SFR_EXTCLOCK = reg;
}

/* ========================================================================== */
/*                     Core Control                                            */
/* ========================================================================== */

/** @brief Start one or more BIO cores by bitmask.
 *  @param[in] core_mask Bitmask of cores to start (bit 0 = core 0, etc.).
 *  @req REQ-DABAO-BIO-0008 */
void bio_start_cores(uint32_t core_mask)
{
    SEVS_ASSERT((core_mask & 0xF) == core_mask);

    core_mask &= 0xF;

    /* Build SFR_CTRL value: EN + RESTART + CLKDIV_RESTART for each core */
    uint32_t ctrl = core_mask | (core_mask << 4) | (core_mask << 8);
    BIO_SFR_CTRL = ctrl;

    /*
     * Wait for RESTART and CLKDIV_RESTART to self-clear.
     * These are bits [11:4] and should drop to 0 after the
     * pulse crosses the clock domain.
     */
    uint32_t timeout = 100000;
    for (int s_poll = 0; s_poll < 1000000 && (timeout > 0); s_poll++) {
        uint32_t check = BIO_SFR_CTRL & 0xFF0;
        if (check == 0) break;
        timeout--;
    }
}

/** @brief Stop one or more BIO cores by bitmask.
 *  @param[in] core_mask Bitmask of cores to stop.
 *  @req REQ-DABAO-BIO-0009 */
void bio_stop_cores(uint32_t core_mask)
{
    SEVS_ASSERT((core_mask & ~0xFu) == 0);

    core_mask &= 0xF;

    uint32_t ctrl = BIO_SFR_CTRL;
    uint32_t stop_mask = core_mask | (core_mask << 4) | (core_mask << 8);
    BIO_SFR_CTRL = ctrl & ~stop_mask;
}

/** @brief Stop all four BIO cores immediately.
 *  @req REQ-DABAO-BIO-0010 */
void bio_stop_all(void)
{
    SEVS_ASSERT(BIO_SFR_CTRL != (uint32_t)-1);

    BIO_SFR_CTRL = 0x0;
}

/* ========================================================================== */
/*                     Pin Muxing                                              */
/* ========================================================================== */

/*
 * Internal: set the alternate function for a specific port/pin.
 * Each CRAFSEL register holds 2 bits per pin for 8 pins (16 bits used).
 *
 * Port B pins 0-7  = IOX_CRAFSEL_BL
 * Port B pins 8-15 = IOX_CRAFSEL_BH
 * Port C pins 0-7  = IOX_CRAFSEL_CL
 * Port C pins 8-15 = IOX_CRAFSEL_CH
 */
static void set_pin_af(uint32_t bio_pin, uint32_t func)
{
    SEVS_ASSERT(bio_pin < 32);
    SEVS_ASSERT(func <= 3);

    volatile uint32_t *reg;
    uint32_t pin_in_reg;

    if (bio_pin < 8) {
        /* PB0-PB7 */
        reg = &IOX_CRAFSEL_BL;
        pin_in_reg = bio_pin;
    } else if (bio_pin < 16) {
        /* PB8-PB15 */
        reg = &IOX_CRAFSEL_BH;
        pin_in_reg = bio_pin - 8;
    } else if (bio_pin < 24) {
        /* PC0-PC7 */
        reg = &IOX_CRAFSEL_CL;
        pin_in_reg = bio_pin - 16;
    } else if (bio_pin < 32) {
        /* PC8-PC15 */
        reg = &IOX_CRAFSEL_CH;
        pin_in_reg = bio_pin - 24;
    } else {
        return;
    }

    uint32_t shift = pin_in_reg * 2;
    uint32_t val = *reg;
    val &= ~(0x3U << shift);           /* clear AF bits */
    val |= ((func & 0x3) << shift);    /* set new AF */
    *reg = val;
}

/** @brief Hand a GPIO pin to the BIO coprocessor via PIOSEL.
 *  @param[in] bio_pin BIO pin number (0-31).
 *  @req REQ-DABAO-BIO-0011 */
void bio_map_pin(uint32_t bio_pin)
{

    if (bio_pin >= 32) return;

    /* Step 1: Set pin alternate function to AF1 */
    set_pin_af(bio_pin, IOX_FUNC_AF1);

    /* Step 2: Set the PIOSEL bit to hand the pin to BIO */
    IOX_SFR_PIOSEL = IOX_SFR_PIOSEL | (1U << bio_pin);
}

/** @brief Return a BIO pin to normal GPIO control.
 *  @param[in] bio_pin BIO pin number (0-31).
 *  @req REQ-DABAO-BIO-0012 */
void bio_unmap_pin(uint32_t bio_pin)
{

    if (bio_pin >= 32) return;

    /* Clear the PIOSEL bit */
    IOX_SFR_PIOSEL = IOX_SFR_PIOSEL & ~(1U << bio_pin);

    /* Set pin back to GPIO function */
    set_pin_af(bio_pin, IOX_FUNC_GPIO);
}

/** @brief Map multiple pins to BIO by bitmask.
 *  @param[in] mask 32-bit mask of BIO pins to map.
 *  @req REQ-DABAO-BIO-0013 */
void bio_map_pins_mask(uint32_t mask)
{
    SEVS_ASSERT(sizeof(uint32_t) == 4);

    uint32_t i;
    for (i = 0; i < 32; i++) {
        if (mask & (1U << i)) {
            bio_map_pin(i);
        }
    }
}

/* ========================================================================== */
/*                     FIFO Utilities                                          */
/* ========================================================================== */

/** @brief Drain a single BIO FIFO by reading it 16 times.
 *  @param[in] fifo FIFO index (0-3).
 *  @req REQ-DABAO-BIO-0014 */
void bio_fifo_drain(uint32_t fifo)
{
    SEVS_ASSERT(fifo <= 3);

    uint32_t i;
    for (i = 0; i < 16; i++) {
        (void)bio_fifo_pop(fifo);
    }
}

/** @brief Drain all four BIO FIFOs and reset their pointers.
 *  @req REQ-DABAO-BIO-0015 */
void bio_fifo_drain_all(void)
{
    SEVS_ASSERT(sizeof(uint32_t) == 4);

    bio_fifo_drain(0);
    bio_fifo_drain(1);
    bio_fifo_drain(2);
    bio_fifo_drain(3);
    BIO_SFR_FIFO_CLR = 0xF;
}

/* ========================================================================== */
/*                     Info                                                     */
/* ========================================================================== */

/** @brief Read the BIO hardware version from CFGINFO.
 *  @return Version byte from the CFGINFO register.
 *  @req REQ-DABAO-BIO-0016 */
uint32_t bio_get_version(void)
{
    SEVS_ASSERT(BIO_CFGINFO_VERSION <= 0xFF);

    return BIO_SFR_CFGINFO & 0xFF;
}

/** @brief Return the fclk value stored during bio_init.
 *  @return Core clock frequency in Hz, or 0 if not initialized.
 *  @req REQ-DABAO-BIO-0010 */
uint32_t bio_get_fclk(void)
{
    SEVS_ASSERT(sizeof(s_fclk_hz) == 4);

    return s_fclk_hz;
}