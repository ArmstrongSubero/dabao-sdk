/*
 * bao_mmu.c: machine mode half of the BAO_MMU sandbox
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compiled only for BAO_MMU builds, as a machine mode object (its data
 * lives in the region of bao1x_mmu.ld supervisor code cannot reach).
 *
 * bao_mmu_init, called by crt0_mmu.S before main, builds identity mapped
 * Sv32 tables from 4 KB pages only:
 *
 *   the supervisor table   what main and the SDK drivers it runs need:
 *                          the program image read and execute, runtime
 *                          storage read only, the supervisor stacks and
 *                          data read and write, the page tables read only,
 *                          the IFRAM DMA buffers and the peripheral pages
 *                          of the supervisor drivers read and write. No
 *                          page of 0x40000000 to 0x4007FFFF, boot0, boot1,
 *                          the security region, the IFR or the machine
 *                          data is in it.
 *
 *   one table per group    the supervisor table plus the machine data and
 *   of services            stack and the register pages that one group of
 *                          drivers uses (SCE, RRAM, CGU, WDT, RTC). The
 *                          RRAM table makes runtime storage writable, never
 *                          the program image, where this code lives.
 *
 * It sets medeleg to 0 (every exception comes here), delegates only the
 * supervisor external interrupt, points stvec at _s_trap_entry and loads
 * satp with the supervisor table.
 *
 * bao_mmu_trap gets every machine trap. An ecall from supervisor mode is a
 * service call: t0 numbers a line of bao_mmu_services.h, a0 to a7 are the
 * arguments. The pointer and length arguments are checked (check_pointers),
 * then the driver runs with satp on the group's table and MPRV set
 * with MPP supervisor, so every load and store it makes is translated: a
 * buffer that runs off its pages faults instead of reaching a register,
 * and the boot chain stays unmapped even for the drivers. Instruction
 * fetch is never translated in machine mode. Anything else is a fault:
 * the cause, the faulting address and the pc are printed and the core
 * halts with every interrupt masked.
 */

#include <stdint.h>
#include "bao/platform.h"
#include "bao/stdlib.h"
#include "hardware/sha.h"
#include "hardware/aes.h"
#include "hardware/trng.h"
#include "hardware/pke.h"
#include "hardware/alu.h"
#include "hardware/sign.h"
#define RRAM_DRIVER_IMPL
#include "hardware/rram.h"
#include "hardware/wdt.h"
#include "hardware/rtc.h"
#include "hardware/power.h"
#include "bao_reset.h"
#include "bao_mmu_services.h"

#ifndef BAO_MMU_MACHINE_SIDE
#error "bao_mmu.c is built with bao_mmu_mside.h forced in (bao_flash.sh does this)"
#endif

#define PTE_V   0x001u
#define PTE_R   0x002u
#define PTE_W   0x004u
#define PTE_X   0x008u
#define PTE_A   0x040u
#define PTE_D   0x080u

/* A and D are set on every leaf: the walker in this core never sets them
 * (MmuPlugin), and a core that faults on clear A or D bits would then
 * fault on every access. */
#define MAP_RO  (PTE_V | PTE_R | PTE_A | PTE_D)
#define MAP_RW  (PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)
#define MAP_RX  (PTE_V | PTE_R | PTE_X | PTE_A | PTE_D)

#define PAGE    0x1000u

#define MSTATUS_MIE   0x00000008u
#define MSTATUS_MPP   0x00001800u
#define MSTATUS_MPRV  0x00020000u
#define MIP_SEIP      0x00000200u
#define SATP_SV32     0x80000000u

/* Every range forbidden to supervisor code, checked against each table the
 * supervisor runs under before satp is loaded. */
#define BOOT_CHAIN_REGS_START   0x40000000u
#define BOOT_CHAIN_REGS_END     0x40080000u
#define BOOT_START              0x60000000u
#define BOOT_END                0x60060000u
#define SECURITY_START          0x603DA000u
#define IFR_START               0x60400000u
#define IFR_END                 0x60800000u

#define RUNTIME_STORAGE_START   0x60160000u
#define RUNTIME_STORAGE_END     0x603DA000u

enum {
    BAO_MMU_G_SCE,
    BAO_MMU_G_RRAM,
    BAO_MMU_G_CGU,
    BAO_MMU_G_WDT,
    BAO_MMU_G_RTC,
    BAO_MMU_GROUPS
};

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
} mmu_range_t;

/* Register pages of the supervisor drivers (addressmap.h, bao1x_peri.svd,
 * core.svd). Every one is a 4 KB block of its own. */
static const mmu_range_t s_periph[] = {
    { 0x50100000u, 0x50101000u, MAP_RW },   /* UDMA_CTRL */
    { 0x50101000u, 0x50105000u, MAP_RW },   /* UDMA UART0 to UART3 */
    { 0x50105000u, 0x50109000u, MAP_RW },   /* UDMA SPIM0 to SPIM3 */
    { 0x50109000u, 0x5010D000u, MAP_RW },   /* UDMA I2C0 to I2C3 */
    { 0x50114000u, 0x50115000u, MAP_RW },   /* UDMA ADC */
    { 0x50120000u, 0x50121000u, MAP_RW },   /* PWM */
    { 0x50124000u, 0x50125000u, MAP_RW },   /* BIO_BDMA */
    { 0x50125000u, 0x50129000u, MAP_RW },   /* BIO IMEM0 to IMEM3 */
    { 0x50129000u, 0x5012D000u, MAP_RW },   /* BIO FIFO0 to FIFO3 */
    { 0x5012F000u, 0x50130000u, MAP_RW },   /* IOX */
    { 0xE0000000u, 0xE0001000u, MAP_RW },   /* D11CTIME */
    { 0xE0004000u, 0xE0018000u, MAP_RW },   /* IRQARRAY0 to IRQARRAY19 */
    { 0xE001B000u, 0xE001C000u, MAP_RW },   /* TICKTIMER */
    { 0xE001C000u, 0xE001D000u, MAP_RW },   /* TIMER0 */
};

static const mmu_range_t s_group_sce[] = {
    { 0x40020000u, 0x40023000u, MAP_RW },   /* SCE RAM segments 0x0000 to 0x23FF */
    { 0x40028000u, 0x40029000u, MAP_RW },   /* SCE_GLBSFR */
    { 0x40029000u, 0x4002A000u, MAP_RW },   /* SCEDMA */
    { 0x4002B000u, 0x40030000u, MAP_RW },   /* COMBOHASH, PKE, AES, TRNG, ALU */
    { 0x40040000u, 0x40041000u, MAP_RW },   /* SYSCTRL (PKE clock) */
};

static const mmu_range_t s_group_rram[] = {
    { 0x40000000u, 0x40001000u, MAP_RW },   /* RRC: RRC_CR, written only */
    { RUNTIME_STORAGE_START, RUNTIME_STORAGE_END, MAP_RW },  /* RRAM_USER_BASE to RRAM_USER_TOP */
};

static const mmu_range_t s_group_cgu[] = {
    { 0x40040000u, 0x40041000u, MAP_RW },   /* SYSCTRL: BIO divider, RCURST0 */
};

static const mmu_range_t s_group_wdt[] = {
    { 0x40041000u, 0x40042000u, MAP_RW },   /* WDG_INTF and its lock at 0xC00 */
};

static const mmu_range_t s_group_rtc[] = {
    { 0x40060000u, 0x40061000u, MAP_RW },   /* AO_SYSCTRL: 1 Hz divider */
    { 0x40061000u, 0x40062000u, MAP_RW },   /* RTC */
};

typedef struct {
    const mmu_range_t *r;
    uint32_t n;
} mmu_group_t;

static const mmu_group_t s_groups[BAO_MMU_GROUPS] = {
    { s_group_sce,  sizeof(s_group_sce)  / sizeof(s_group_sce[0])  },
    { s_group_rram, sizeof(s_group_rram) / sizeof(s_group_rram[0]) },
    { s_group_cgu,  sizeof(s_group_cgu)  / sizeof(s_group_cgu[0])  },
    { s_group_wdt,  sizeof(s_group_wdt)  / sizeof(s_group_wdt[0])  },
    { s_group_rtc,  sizeof(s_group_rtc)  / sizeof(s_group_rtc[0])  },
};

extern char _program_start[], _image_end[];
extern char _main_stack_bottom[], _main_stack_top[];
extern char _trap_stack_bottom[], _trap_stack_top[];
extern char _s_data_end[];
extern char _mmu_tables_start[], _mmu_tables_end[];
extern char _m_region_start[], _m_region_end[];
extern char _dma_start[], _dma_end[];
extern char _s_trap_entry[];

/* Read by the host tests: satp for supervisor code and for each group,
 * and the SRAM layout of bao1x_mmu.ld (main stack bottom and top, trap
 * stack bottom and top, end of supervisor data, page tables start and end,
 * end of the machine region, which starts at the bottom of SRAM). */
uint32_t bao_mmu_satp_app;
uint32_t bao_mmu_satp_group[BAO_MMU_GROUPS];
const uint32_t bao_mmu_layout[8] = {
    (uint32_t)_main_stack_bottom, (uint32_t)_main_stack_top,
    (uint32_t)_trap_stack_bottom, (uint32_t)_trap_stack_top,
    (uint32_t)_s_data_end, (uint32_t)_mmu_tables_start,
    (uint32_t)_mmu_tables_end, (uint32_t)_m_region_end,
};

static uint32_t *s_app_root;
static uint32_t *s_base_root;
static uint32_t *s_group_root[BAO_MMU_GROUPS];
static uint32_t s_tables_used;
static int32_t s_active_service = -1;

static void halt(void) __attribute__((noreturn));
static void halt(void)
{
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    __asm__ volatile ("csrci mstatus, 0x8" ::: "memory");
    for (;;)
        __asm__ volatile ("wfi" ::: "memory");
}

static void fail(const char *what) __attribute__((noreturn));
static void fail(const char *what)
{
    mini_printf("\r\nMMU: %s\r\nMMU: halted\r\n", what);
    halt();
}

static uint32_t page_down(uint32_t a)
{
    return a & ~(PAGE - 1u);
}

static uint32_t page_up(uint32_t a)
{
    return (a + PAGE - 1u) & ~(PAGE - 1u);
}

static uint32_t *new_table(void)
{
    uint32_t *t;
    uint32_t i;
    uint32_t max = ((uint32_t)_mmu_tables_end - (uint32_t)_mmu_tables_start) / PAGE;

    if (s_tables_used >= max)
        fail("out of page tables");
    t = (uint32_t *)((uint32_t)_mmu_tables_start + s_tables_used * PAGE);
    s_tables_used++;
    for (i = 0; i < 1024u; i++)
        t[i] = 0;
    return t;
}

static uint32_t *copy_table(const uint32_t *from)
{
    uint32_t *t = new_table();
    uint32_t i;

    for (i = 0; i < 1024u; i++)
        t[i] = from[i];
    return t;
}

static uint32_t *l0_of(uint32_t pte)
{
    return (uint32_t *)((pte >> 10) << 12);
}

/* An L0 table a root shares with the supervisor or base table is copied
 * before this root changes it. */
static void map_page(uint32_t *root, uint32_t va, uint32_t flags)
{
    uint32_t i1 = va >> 22;
    uint32_t e = root[i1];
    uint32_t *l0;

    if (!(e & PTE_V)) {
        l0 = new_table();
        root[i1] = (((uint32_t)l0 >> 12) << 10) | PTE_V;
    } else {
        if (root != s_app_root &&
            ((s_app_root && e == s_app_root[i1]) ||
             (root != s_base_root && s_base_root && e == s_base_root[i1]))) {
            l0 = copy_table(l0_of(e));
            root[i1] = (((uint32_t)l0 >> 12) << 10) | PTE_V;
        }
        l0 = l0_of(root[i1]);
    }
    l0[(va >> 12) & 0x3FFu] = ((va >> 12) << 10) | flags;
}

static void map_range(uint32_t *root, uint32_t start, uint32_t end, uint32_t flags)
{
    uint32_t va;

    for (va = page_down(start); va < page_up(end); va += PAGE)
        map_page(root, va, flags);
}

static uint32_t leaf(const uint32_t *root, uint32_t va)
{
    uint32_t e = root[va >> 22];

    if (!(e & PTE_V))
        return 0;
    if (e & (PTE_R | PTE_X))
        return e;
    e = l0_of(e)[(va >> 12) & 0x3FFu];
    return (e & PTE_V) ? e : 0;
}

static int any_mapped(const uint32_t *root, uint32_t start, uint32_t end)
{
    uint32_t va;

    for (va = start; va < end; va += PAGE)
        if (leaf(root, va))
            return 1;
    return 0;
}

static int any_writable(const uint32_t *root, uint32_t start, uint32_t end)
{
    uint32_t va;

    for (va = start; va < end; va += PAGE)
        if (leaf(root, va) & PTE_W)
            return 1;
    return 0;
}

static uint32_t satp_of(const uint32_t *root)
{
    return SATP_SV32 | ((uint32_t)root >> 12);
}

void bao_mmu_init(void)
{
    uint32_t g, i;

    s_app_root = new_table();
    map_range(s_app_root, (uint32_t)_program_start, (uint32_t)_image_end, MAP_RX);
    map_range(s_app_root, RUNTIME_STORAGE_START, RUNTIME_STORAGE_END, MAP_RO);
    map_range(s_app_root, (uint32_t)_main_stack_bottom, (uint32_t)_main_stack_top, MAP_RW);
    map_range(s_app_root, (uint32_t)_trap_stack_bottom, (uint32_t)_s_data_end, MAP_RW);
    map_range(s_app_root, (uint32_t)_mmu_tables_start, (uint32_t)_mmu_tables_end, MAP_RO);
    if ((uint32_t)_dma_end != (uint32_t)_dma_start)
        map_range(s_app_root, (uint32_t)_dma_start, (uint32_t)_dma_end, MAP_RW);
    for (i = 0; i < sizeof(s_periph) / sizeof(s_periph[0]); i++)
        map_range(s_app_root, s_periph[i].start, s_periph[i].end, s_periph[i].flags);

    s_base_root = copy_table(s_app_root);
    map_range(s_base_root, (uint32_t)_m_region_start, (uint32_t)_m_region_end, MAP_RW);

    for (g = 0; g < BAO_MMU_GROUPS; g++) {
        s_group_root[g] = copy_table(s_base_root);
        for (i = 0; i < s_groups[g].n; i++)
            map_range(s_group_root[g], s_groups[g].r[i].start, s_groups[g].r[i].end,
                      s_groups[g].r[i].flags);
        bao_mmu_satp_group[g] = satp_of(s_group_root[g]);
    }

    /* The forbidden ranges, checked on the table itself rather than trusted
     * to the lists above. */
    if (any_mapped(s_app_root, BOOT_CHAIN_REGS_START, BOOT_CHAIN_REGS_END) ||
        any_mapped(s_app_root, BOOT_START, BOOT_END) ||
        any_mapped(s_app_root, SECURITY_START, IFR_END) ||
        any_mapped(s_app_root, 0x60FFF000u, (uint32_t)_m_region_end))
        fail("supervisor table maps a forbidden page");
    for (g = 0; g < BAO_MMU_GROUPS; g++)
        if (any_mapped(s_group_root[g], BOOT_START, BOOT_END) ||
            any_mapped(s_group_root[g], SECURITY_START, IFR_END) ||
            any_writable(s_group_root[g], (uint32_t)_program_start, RUNTIME_STORAGE_START))
            fail("service table maps a forbidden page");

    bao_mmu_satp_app = satp_of(s_app_root);

    __asm__ volatile ("csrw medeleg, zero" ::: "memory");
    __asm__ volatile ("csrw mideleg, %0" :: "r"(MIP_SEIP) : "memory");
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    __asm__ volatile ("csrw stvec, %0" :: "r"(_s_trap_entry) : "memory");
    __asm__ volatile ("csrw satp, %0" :: "r"(bao_mmu_satp_app) : "memory");
    __asm__ volatile ("sfence.vma" ::: "memory");
}

/* The two CGU writes bio_init makes (bio.c, values unchanged). */
void bao_mmu_bio_clock(int fast)
{
    REG32(0x40040014u) = fast ? 0x070001FFu : 0x0700017Fu;
    REG32(0x4004002Cu) = 0x32u;
}

/* bao_software_reset (bao_reset.h, value unchanged). */
void bao_mmu_software_reset(void)
{
    REG32(0x40040080u) = 0x000055AAu;
}

typedef uint32_t (*svc_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t,
                             uint32_t, uint32_t, uint32_t, uint32_t);

typedef struct {
    uintptr_t fn;
    uint8_t group;
    uint8_t rd;
    uint8_t wr;
    uint8_t bytes;
    uint8_t words;
    const char *name;
} svc_t;

/* name expands to the bao_m_ driver function, #name stays the SDK name. */
#define BAO_MMU_ENTRY(name, group, rd, wr, bytes, words) \
    { (uintptr_t)name, BAO_MMU_G_##group, rd, wr, bytes, words, #name },

static const svc_t s_services[] = {
    BAO_MMU_SERVICES(BAO_MMU_ENTRY)
};

#define SERVICE_COUNT (sizeof(s_services) / sizeof(s_services[0]))

static void refuse(const svc_t *s, const char *what, uint32_t arg, uint32_t v)
    __attribute__((noreturn));
static void refuse(const svc_t *s, const char *what, uint32_t arg, uint32_t v)
{
    mini_printf("\r\nMMU: %s refused %s 0x%08x in argument %u\r\nMMU: halted\r\n",
                s->name, what, (unsigned)v, (unsigned)arg);
    halt();
}

/*
 * A pointer argument must start on a page supervisor code could read (or
 * write) itself; NULL is passed on for the driver's own check, and the
 * group tables map nothing below 0x40000000. Byte lengths are capped at
 * 2 MB and word counts at 2 MB of words. With every supervisor page at or
 * above 0x50000000, and the machine region at the bottom of SRAM, no
 * buffer that starts on a supervisor page can reach a register page or
 * machine data within that, wherever the driver touches it first.
 */
#define MAX_BYTES  0x200000u
#define MAX_WORDS  (MAX_BYTES / 4u)

static void check_pointers(const svc_t *s, const uint32_t *a)
{
    uint32_t n, app, need;

    for (n = 0; n < 8u; n++) {
        if (((s->bytes >> n) & 1u) && a[n] > MAX_BYTES)
            refuse(s, "length", n, a[n]);
        if (((s->words >> n) & 1u) && a[n] > MAX_WORDS)
            refuse(s, "word count", n, a[n]);
        if (!(((s->rd | s->wr) >> n) & 1u) || a[n] == 0u)
            continue;
        need = ((s->wr >> n) & 1u) ? (PTE_R | PTE_W) : PTE_R;
        app = leaf(s_app_root, a[n]);
        if ((app & need) != need)
            refuse(s, "pointer", n, a[n]);
    }
}

static uint32_t run_service(const svc_t *s, const uint32_t *a)
{
    uint32_t r;
    svc_fn_t fn = (svc_fn_t)s->fn;

    __asm__ volatile ("csrw satp, %0\n\tsfence.vma\n\tcsrs mstatus, %1"
                      :: "r"(bao_mmu_satp_group[s->group]), "r"(MSTATUS_MPRV) : "memory");
    /* Every driver argument is a 32 bit integer, pointer or enum under the
     * ilp32 ABI, and a callee ignores the argument registers it does not
     * declare. */
    r = fn(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    __asm__ volatile ("csrc mstatus, %1\n\tcsrw satp, %0\n\tsfence.vma"
                      :: "r"(bao_mmu_satp_app), "r"(MSTATUS_MPRV) : "memory");
    return r;
}

static const char *cause_name(uint32_t c)
{
    switch (c) {
    case 0:  return "instruction address misaligned";
    case 1:  return "instruction access fault";
    case 2:  return "illegal instruction";
    case 3:  return "breakpoint";
    case 4:  return "load address misaligned";
    case 5:  return "load access fault";
    case 6:  return "store address misaligned";
    case 7:  return "store access fault";
    case 8:  return "ecall from user mode";
    case 11: return "ecall from machine mode";
    case 12: return "instruction page fault";
    case 13: return "load page fault";
    case 15: return "store page fault";
    default: return "exception";
    }
}

/* Frame slots from crt0_mmu.S. */
#define F_T0      4
#define F_A0      9
#define F_EPC     31
#define F_STATUS  32

void bao_mmu_trap(uint32_t *frame)
{
    uint32_t cause, tval, from;
    const svc_t *s;

    __asm__ volatile ("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile ("csrr %0, mtval" : "=r"(tval));
    from = (frame[F_STATUS] & MSTATUS_MPP) >> 11;

    if (cause == 9u && s_active_service < 0) {
        if (frame[F_T0] >= SERVICE_COUNT) {
            mini_printf("\r\nMMU: unknown service %u, pc 0x%08x\r\nMMU: halted\r\n",
                        (unsigned)frame[F_T0], (unsigned)frame[F_EPC]);
            halt();
        }
        s = &s_services[frame[F_T0]];
        check_pointers(s, &frame[F_A0]);
        s_active_service = (int32_t)frame[F_T0];
        frame[F_A0] = run_service(s, &frame[F_A0]);
        s_active_service = -1;
        frame[F_EPC] += 4u;
        return;
    }

    /* Only the supervisor external interrupt is delegated; supervisor code
     * that raises its own software or timer interrupt lands here. */
    if (cause & 0x80000000u) {
        mini_printf("\r\nMMU: interrupt %u not delegated, pc 0x%08x\r\nMMU: halted\r\n",
                    (unsigned)(cause & 0x1Fu), (unsigned)frame[F_EPC]);
        halt();
    }

    mini_printf("\r\nMMU: %s at 0x%08x, pc 0x%08x, %s mode\r\n", cause_name(cause),
                (unsigned)tval, (unsigned)frame[F_EPC],
                from == 3u ? "machine" : from == 1u ? "supervisor" : "user");
    if (s_active_service >= 0)
        mini_printf("MMU: inside %s\r\n", s_services[s_active_service].name);
    if (cause == 15u && tval < (uint32_t)_main_stack_bottom &&
        tval >= (uint32_t)_main_stack_bottom - PAGE)
        mini_printf("MMU: below the main stack, stack overflow\r\n");
    if (tval >= (uint32_t)_main_stack_top && tval < (uint32_t)_trap_stack_bottom)
        mini_printf("MMU: guard page below the trap stack\r\n");
    mini_printf("MMU: halted\r\n");
    halt();
}
