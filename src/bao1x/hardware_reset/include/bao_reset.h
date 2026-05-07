#ifndef _BAO_RESET_H
#define _BAO_RESET_H

#include "bao/platform.h"

#define BAO_SFR_RCURST0  (*(volatile uint32_t *)0x40040080UL)

static inline void bao_software_reset(void) {
    BAO_SFR_RCURST0 = 0x000055AAUL;
    while (1) {}
}

#endif