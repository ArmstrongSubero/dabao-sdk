#ifndef _BAO_RESET_H
#define _BAO_RESET_H

#include "bao/platform.h"

#ifdef BAO_MMU

/* RCURST0 sits in the CGU page, which supervisor code has no mapping for.
 * Machine mode writes the same value behind an ecall (src/mmu). */
void bao_mmu_software_reset(void);

static inline void bao_software_reset(void) {
    bao_mmu_software_reset();
    while (1) {}
}

#else

#define BAO_SFR_RCURST0  (*(volatile uint32_t *)0x40040080UL)

static inline void bao_software_reset(void) {
    BAO_SFR_RCURST0 = 0x000055AAUL;
    while (1) {}
}

#endif

#endif