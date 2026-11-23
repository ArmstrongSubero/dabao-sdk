/*
 * bio.c - BIO Coprocessor Driver for Baochip-1x
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

/*
 * Clock gating unit registers. boot1 leaves BIO running at 350 MHz, and
 * reaching 700 MHz needs the fast mode divider written here before the
 * cores are started.
 */
#define SFR_CGUFD_CFGFDCR_0_4_0   (*(volatile uint32_t *)(0x40040000 + 0x14))
#define SYSCTRL_SFR_CGUSET        (*(volatile uint32_t *)(0x40040000 + 0x2c))
#ifdef BAO_MMU
/* The CGU page is machine mode only; src/mmu writes the same two values. */
void bao_mmu_bio_clock(int fast);
#endif
/*
 * We store fclk so bio_set_target_freq() can compute dividers.
 * This is the only piece of "state" in the driver.
 */
static uint32_t s_fclk_hz = 0;

/*
 * Starting a core without running anything stale
 *
 * A write to SFR_CTRL reaches the cores in stages (bio_bdma.sv 502, 565,
 * 566, 1842 to 1849, apb_ac2r 2331): the enable bits load into en_sync
 * three aclk after the pclk edge that takes the write, but the restart bits
 * load one pclk later and reach the core through three more aclk flops
 * (447 to 449, 1931). So a core that is enabled from a stopped state runs
 * on from wherever its clock stopped for about one pclk before its reset
 * arrives. Its RAM port is its own again by then (imem_wr_mode is en_sync,
 * 1738), so it fetches from the program the host just loaded, at its old pc
 * and with its old registers. A restart sent while the core is stopped does
 * nothing to it, because its clock is gated (stall includes ~en_sync, 695
 * to 702, 1867) and picorv32's reset is synchronous (picorv32.v 1545).
 *
 * The one state that does nothing for that pclk is the trap state:
 * cpu_state_trap only sets trap (picorv32.v 1575), the register file write
 * and read enables need cpu_state fetch and ld_rs1 (1460 to 1462), so no
 * FIFO, GPIO, event or quantum access is made and no load or store is
 * started, and the memory interface only drops mem_valid (622 to 627), so
 * the RAM is not read either. A trapped core is not stalled, so its clock
 * runs and the reset lands.
 *
 * One more effect: while the decoder holds x20 in rs1 or rs2, quanta_halt is
 * high whatever resetn is (bio_bdma.sv 2106, 2199, 2242), and it keeps
 * mem_valid_d high (1835), so the first fetch after the reset is ready at
 * once and takes the RAM output as it was (1839) instead of the word at pc
 * 0. A trapped core does not read its RAM, so that output is what the host
 * last read or wrote in that RAM (1826, 1762). bio_start_cores therefore
 * reads word 0 of each core last, right before the start write: the first
 * instruction is then word 0 either way.
 *
 * So every way this driver stops a core leaves it trapped ("parked"):
 * bio_init ends mem_init with ebreak and waits for the trap, bio_stop_cores
 * and bio_stop_all park what they stop, bio_load_code parks a stopped core
 * before it writes the new program, and bio_start_cores stops and parks any
 * running core first, so it only ever enables trapped cores.
 */

/* ebreak: with ENABLE_IRQ 0 and CATCH_ILLINSN 1 it goes to cpu_state_trap
 * (picorv32.v 1711). Its rs1 field is 0 and its rs2 field 1, so a core
 * trapped on it does not hold x20 in its decoder. */
#define BIO_EBREAK_WORD 0x00100073u

/* Bounded wait for parked cores to report trap. Each read is one APB
 * transfer, at least two pclk, so this is at least 256 pclk (about 3 us at
 * 87.5 MHz). The trap comes about 3 pclk and 20 aclk after the enable. The
 * bound is kept short because a core that did not trap is running its old
 * program at a quantum every aclk until the attempt ends. */
#define BIO_PARK_POLLS  128u

/* Bounded wait for mem_init to trap in bio_init (it goes through the
 * memory gutter, whose timing the BIO RTL does not give). */
#define BIO_INIT_POLLS  4096u

/* Whole RAM copy for the second park attempt (see bio_park). */
static uint32_t s_park_ram[BIO_IMEM_WORDS];

/*
 * Wait out the crossing of a CTRL write. Each APB transfer takes at least
 * two pclk at the AHB to APB bridge (setup and access, soc_ifsub.sv 158 to
 * 168), and transfers do not overlap, so three reads put six pclk between
 * the write and what follows. The write's reset ends 3 pclk plus 3 aclk
 * after its edge, and the action pulse acknowledge, which blinds
 * cdc_blinded to a second CTRL write until it is back (lib/cdc_blinded.v),
 * is back by then too.
 */
static void ctrl_settle(void)
{
    (void)BIO_SFR_CFGINFO;
    (void)BIO_SFR_CFGINFO;
    (void)BIO_SFR_CFGINFO;
}

/* Write SFR_CTRL and wait out its crossing. */
static void ctrl_write(uint32_t v)
{
    BIO_SFR_CTRL = v;
    ctrl_settle();
}

/* SFR_CTRL reads back only the four enable bits (apb_ac2r prdata32 is
 * prdata[3:0], bio_bdma.sv 2357). */
static uint32_t ctrl_enabled(void)
{
    return BIO_SFR_CTRL & 0xFu;
}

static volatile uint32_t *bio_reg(uint32_t offset)
{
    return (volatile uint32_t *)(BIO_BDMA_BASE + offset);
}

/* the cores of mask that do not report trap */
static uint32_t not_trapped(uint32_t mask)
{
    uint32_t c, left = 0;
    for (c = 0; c < 4; c++) {
        if ((mask & (1u << c)) && !bio_core_trapped(c)) {
            left |= 1u << c;
        }
    }
    return left;
}

/*
 * One park attempt for the stopped cores in need. With whole set (one core
 * only) every RAM word is ebreak during the attempt, else only word 0.
 * Returns the cores that did not trap.
 *
 * The core is enabled with its restart bit. It runs on from where it
 * stopped for about a pclk, in the program it was stopped in (still in its
 * RAM), then the reset takes it to pc 0, where it traps on ebreak. It is
 * stopped again and its RAM put back.
 *
 * The reset needs the core clock. A FIFO or event stall drops when resetn
 * falls (ren1, ren2 and wen are gated by resetn, picorv32.v 1460 to 1462,
 * and event_mask resets with it, bio_bdma.sv 2093). Only a quantum wait
 * holds the clock (quanta_rd is not gated, 2199, 2242), so the core's
 * divider is set to 0, a quantum every aclk (pio_divider.v 52: with the
 * divider 0, use_divider and divint_1 are 0, so penable is 1), and its
 * external clock is turned off while it parks.
 *
 * Word 0 is read back last before the enable, so the RAM output the core
 * sees (1826, 1835 to 1839) is ebreak.
 */
static uint32_t park_attempt(uint32_t need, int whole)
{
    uint32_t c, i, ext, word0[4] = {0, 0, 0, 0}, qdiv[4] = {0, 0, 0, 0};

    ext = BIO_SFR_EXTCLOCK;
    for (c = 0; c < 4; c++) {
        volatile uint32_t *imem = bio_imem(c);
        if (!(need & (1u << c))) {
            continue;
        }
        qdiv[c] = *bio_reg(0x50u + 4u * c);
        *bio_reg(0x50u + 4u * c) = 0;
        if (whole) {
            for (i = 0; i < BIO_IMEM_WORDS; i++) {
                s_park_ram[i] = imem[i];
                imem[i] = BIO_EBREAK_WORD;
            }
        } else {
            word0[c] = imem[0];
            imem[0] = BIO_EBREAK_WORD;
        }
    }
    BIO_SFR_EXTCLOCK = ext & ~need;
    for (c = 0; c < 4; c++) {
        if (need & (1u << c)) {
            (void)bio_imem(c)[0];
        }
    }

    /* enable and restart only these cores, leave the others as they are */
    ctrl_write(ctrl_enabled() | need | (need << 4));
    for (i = 0; i < BIO_PARK_POLLS && not_trapped(need); i++) {
    }
    BIO_SFR_CTRL = ctrl_enabled() & ~need;

    /* The cores stop three aclk after that write's edge; each access below
     * is a later APB transfer, so the RAM ports are the host's again. */
    for (c = 0; c < 4; c++) {
        volatile uint32_t *imem = bio_imem(c);
        if (!(need & (1u << c))) {
            continue;
        }
        if (whole) {
            for (i = 0; i < BIO_IMEM_WORDS; i++) {
                imem[i] = s_park_ram[i];
            }
        } else {
            imem[0] = word0[c];
        }
        *bio_reg(0x50u + 4u * c) = qdiv[c];
    }
    BIO_SFR_EXTCLOCK = (BIO_SFR_EXTCLOCK & ~need) | (ext & need);
    ctrl_settle();
    return not_trapped(need);
}

/*
 * Park the stopped cores in mask: leave them trapped, on ebreak, with
 * their RAM, divider, external clock setting and FIFO as they were.
 *
 * The first attempt changes only word 0 and costs about 25 APB transfers,
 * a microsecond or so; a core that was not trapped gets a second one of
 * those once it has trapped (see below). It can fail only if the reset lands while the
 * decoder holds x20 and the stale RAM output it then runs at pc 0 leads
 * somewhere that never reaches word 0. The second attempt makes every RAM
 * word ebreak, so whatever the core fetches or takes from the RAM output
 * traps it; only an instruction already in flight when it stopped can
 * finish first. It costs about 3000 APB transfers (about 70 us), and only
 * runs after a first attempt failed.
 *
 * The restart clears the FIFO with the core's number (bio_bdma.sv 1969),
 * so with keep_fifo its words are read out first and pushed back after.
 * That FIFO is cleared and refilled while other cores run: a running core
 * that pushes into it or pops it during the park can lose a word or see
 * one twice (host pushes win over core pushes, 716 to 756, and a pop of an
 * empty FIFO returns its last word, 433), and its level events (796 to
 * 805) pass through the levels on the way, which can raise a pioirq or wake
 * a core waiting on x30. Not safe to call from an interrupt handler
 * (s_park_ram and the CTRL read, modify, write).
 */
static void bio_park(uint32_t mask, int keep_fifo)
{
    uint32_t level, c, i, n, left, running;
    uint32_t count[4];
    uint32_t keep[4][9];

    mask &= 0xFu & ~ctrl_enabled();
    if (mask == 0) {
        return;
    }

    level = BIO_SFR_FLEVEL;
    for (c = 0; c < 4; c++) {
        count[c] = 0;
        if (!keep_fifo || !(mask & (1u << c))) {
            continue;
        }
        /* a FIFO holds at most nine words (eight and the output register) */
        n = (level >> (4u * c)) & 0xFu;
        if (n > 9u) {
            n = 9u;
        }
        count[c] = n;
        for (i = 0; i < n; i++) {
            keep[c][i] = *bio_reg(0x20u + 4u * c);
        }
    }

    /*
     * A core that is not trapped yet may trap on something other than the
     * ebreak placed here (an illegal word of its old program, whose rs
     * fields could hold 20). So once it has trapped it gets one more
     * attempt. From the trap state that attempt cannot go wrong: nothing
     * runs before the reset, the reset lands (divider 0), and the first
     * fetch after it is ebreak whether or not x20 was decoded, so it ends
     * trapped on ebreak with the decoder holding ebreak.
     */
    running = not_trapped(mask);
    left = park_attempt(mask, 0);
    for (c = 0; c < 4; c++) {
        if (left & (1u << c)) {
            (void)park_attempt(1u << c, 1);
        }
    }
    if (running) {
        (void)park_attempt(running, 0);
    }

    for (c = 0; c < 4; c++) {
        for (i = 0; i < count[c]; i++) {
            *bio_reg(0x10u + 4u * c) = keep[c][i];
        }
    }
}

/** @brief Open one DMA whitelist window so BIO cores can reach a memory range.
 *
 * BIO load and store access to main memory is blocked by a whitelist that
 * is empty at reset. A core reading an unlisted address gets zero back
 * rather than a fault, which makes the failure look like a dead bus. Four
 * windows are available.
 *
 * The hardware compares addr[31:12], so base and length are both counted
 * in 4 KB pages:
 *
 *   match = (addr[31:12] >= base) && (addr[31:12] < base + length)
 *
 * The start is rounded down to a page and the end up, so a window always
 * covers at least the range asked for.
 *
 * Example, opening all of SRAM:
 *   bio_allow_range(0, 0x61000000u, 0x200000u);
 *
 *  @param[in] window Window index, 0 to 3.
 *  @param[in] addr Start address.
 *  @param[in] len Length in bytes.
 *  @return 0 on success, -1 on a bad window index or zero length.
 *  @req REQ-DABAO-BIO-0012 */
int bio_allow_range(uint32_t window, uint32_t addr, uint32_t len)
{
    volatile uint32_t *base_reg;
    volatile uint32_t *bounds_reg;
    uint32_t first_page, last_page;

    SEVS_ASSERT(window < 4);
    if (window >= 4) return -1;
    if (len == 0) return -1;

    /*
     * Never open a window over boot0, boot1 or the security region.
     * 0x60000000 is boot0 and 0x60020000 is boot1; a BIO core with
     * write access there can brick the part, and there is no recovery
     * path because every recovery mechanism lives inside boot1. From
     * 0x603DA000 up are the one way counters and key slots. The program
     * image and runtime storage in between are allowed. The end is
     * computed as a last byte so a length that would wrap cannot pass.
     */
    if (len - 1u > 0xFFFFFFFFu - addr) return -1;
    {
        uint32_t last = addr + (len - 1u);
        if (last >= 0x60000000u && addr < 0x60060000u) {
            SEVS_ASSERT(0);   /* window overlaps boot0 or boot1 */
            return -1;
        }
        if (last >= 0x603DA000u && addr < 0x60400000u) {
            SEVS_ASSERT(0);   /* window overlaps the security region */
            return -1;
        }
        first_page = addr >> 12;
        last_page  = last >> 12;
    }

    base_reg   = (volatile uint32_t *)(BIO_BDMA_BASE + 0xE0 + window * 8);
    bounds_reg = (volatile uint32_t *)(BIO_BDMA_BASE + 0xE4 + window * 8);

    *base_reg   = first_page & 0xFFFFFu;
    *bounds_reg = (last_page - first_page + 1u) & 0xFFFFFu;
    __asm__ volatile ("fence" ::: "memory");

    return 0;
}

/** @brief Initialize the BIO coprocessor: halt all cores, fill IMEM, drain FIFOs.
 *  @param[in] fclk_hz Core clock frequency in Hz (used for divider calculations).
 *  @return 0 on success, -1 if IMEM or CFGINFO verification fails.
 *  @req REQ-DABAO-BIO-0001 */
int bio_init(uint32_t fclk_hz)
{
    SEVS_ASSERT(fclk_hz > 0);

    uint32_t i;

    s_fclk_hz = fclk_hz;

    /*
     * Select the BIO clock domain before anything else. boot1 hands the
     * block over at 350 MHz; asking for more than 400 MHz switches the
     * divider to fast mode. The threshold is a mode selection, not a
     * rounding: 700 MHz needs fast mode, 350 MHz needs slow, and there
     * is nothing in between.
     */
#ifdef BAO_MMU
    bao_mmu_bio_clock(fclk_hz > 400000000u);
#else
    if (fclk_hz > 400000000u) {
        SFR_CGUFD_CFGFDCR_0_4_0 = 0x070001ff;   /* fast mode  */
        SYSCTRL_SFR_CGUSET      = 0x32;
    } else {
        SFR_CGUFD_CFGFDCR_0_4_0 = 0x0700017f;   /* slow mode  */
        SYSCTRL_SFR_CGUSET      = 0x32;
    }
#endif

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
     *   ebreak             (trap: leaves the core parked, see the
     *                        note at the top of this file)
     *
     * We inline the machine code directly rather than depending on
     * an assembler, keeping this file self-contained.
     */
    static const uint32_t mem_init[] = {
        0x02002023,  /* sw x0, 0x20(x0) */
        0x02002283,  /* lw t0, 0x20(x0) */
        0x61200137,  /* lui sp, 0x61200 */
        0xFFC10113,  /* addi sp, sp, -4 */
        0x00012023,  /* sw x0, 0(sp)    */
        0x00012283,  /* lw t0, 0(sp)    */
        BIO_EBREAK_WORD,  /* ebreak (trap, parked) */
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

        /* Wait for the trap, then stop the core. A trapped core is
         * parked: the next start runs nothing stale. If it never traps,
         * park it, which needs the core stopped. */
        for (j = 0; j < BIO_INIT_POLLS && !bio_core_trapped(i); j++) {
        }
        ctrl_write(0x0);
        if (!bio_core_trapped(i)) {
            bio_park(1u << i, 1);
        }
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
/** @brief Load a byte-oriented program into a BIO core instruction memory.
 *  @param[in] core Core index (0-3).
 *  @param[in] code Program bytes (little-endian).
 *  @param[in] len Program length in bytes.
 *  @return 0 on success, -1 if readback verification fails.
 *  @req REQ-DABAO-BIO-0002 */
int bio_load_code(uint32_t core, const uint8_t *code, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(code);
    SEVS_ASSERT(core < 4);
    SEVS_ASSERT(len <= BIO_IMEM_SIZE);
    if (core >= 4 || len == 0 || len > BIO_IMEM_SIZE) return -1;
    volatile uint32_t *imem = bio_imem(core);

    /* Park the core here, while its RAM still holds the program it was
     * stopped in. This covers a core stopped by a plain SFR_CTRL write, and
     * leaves it trapped on ebreak, so a start that does not read word 0
     * first (bio_dma_init) is safe too. A running core is left alone; its
     * RAM is not the host's. */
    bio_park(1u << core, 1);
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
    SEVS_ASSERT(core < 4);
    SEVS_ASSERT(num_words <= BIO_IMEM_WORDS);
    if (core >= 4 || num_words == 0 || num_words > BIO_IMEM_WORDS) return -1;
    volatile uint32_t *imem = bio_imem(core);
    uint32_t i;

    /* as in bio_load_code */
    bio_park(1u << core, 1);

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
/** @brief Start one or more BIO cores by bitmask.
 *  @param[in] core_mask Bitmask of cores to start (bit 0 = core 0, etc.).
 *  @req REQ-DABAO-BIO-0008 */
void bio_start_cores(uint32_t core_mask)
{
    SEVS_ASSERT((core_mask & 0xF) == core_mask);

    core_mask &= 0xF;

    /*
     * Stop and park every running core first. The start write below has
     * only the started cores' enable bits, so as before every other running
     * core stops; and a running core being restarted is stopped too, so
     * that it is trapped, not halfway through its program, when its reset
     * comes.
     */
    uint32_t running = ctrl_enabled();
    if (running & ~core_mask) {
        bio_stop_cores(running & ~core_mask);
    }
    if (running & core_mask) {
        /* their FIFOs are cleared by the start anyway */
        ctrl_write(ctrl_enabled() & ~(running & core_mask));
        bio_park(running & core_mask, 0);
    }

    /*
     * Every core here is now trapped, unless it was stopped by a plain
     * SFR_CTRL write and not loaded since. A trapped core does nothing in
     * the pclk between en_sync and its reset; then the reset takes it to
     * pc 0 and clears its FIFO. Word 0 is read last so that the RAM output
     * is word 0 even if the decoder holds x20 (see the note at the top).
     */
    uint32_t c;
    for (c = 0; c < 4; c++) {
        if (core_mask & (1u << c)) {
            (void)bio_imem(c)[0];
        }
    }

    /*
     * EN + RESTART + CLKDIV_RESTART for each core. The restart bits read
     * back as 0 (only EN is readable), so instead of polling them the
     * write is followed by six pclk of APB transfers, after which the reset
     * is over and the FIFO takes pushes again.
     */
    ctrl_write(core_mask | (core_mask << 4) | (core_mask << 8));
}

/** @brief Stop one or more BIO cores by bitmask.
 *  @param[in] core_mask Bitmask of cores to stop.
 *  @req REQ-DABAO-BIO-0009 */
void bio_stop_cores(uint32_t core_mask)
{
    SEVS_ASSERT((core_mask & ~0xFu) == 0);

    core_mask &= 0xF;

    ctrl_write(ctrl_enabled() & ~core_mask);

    /* Leave them trapped so the next start runs nothing stale. The FIFO
     * with each core's number keeps its words. */
    bio_park(core_mask, 1);
}

/** @brief Stop all four BIO cores immediately.
 *  @req REQ-DABAO-BIO-0010 */
void bio_stop_all(void)
{
    ctrl_write(0x0);
    bio_park(0xFu, 1);
}
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

    uint32_t i;
    for (i = 0; i < 32; i++) {
        if (mask & (1U << i)) {
            bio_map_pin(i);
        }
    }
}
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

    bio_fifo_drain(0);
    bio_fifo_drain(1);
    bio_fifo_drain(2);
    bio_fifo_drain(3);
    BIO_SFR_FIFO_CLR = 0xF;
}
/** @brief Read the BIO hardware version from CFGINFO.
 *  @return Version byte from the CFGINFO register.
 *  @req REQ-DABAO-BIO-0016 */
uint32_t bio_get_version(void)
{

    return BIO_SFR_CFGINFO & 0xFF;
}

/** @brief Return the fclk value stored during bio_init.
 *  @return Core clock frequency in Hz, or 0 if not initialized.
 *  @req REQ-DABAO-BIO-0010 */
uint32_t bio_get_fclk(void)
{

    return s_fclk_hz;
}