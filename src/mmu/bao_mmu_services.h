/*
 * bao_mmu_services.h: the machine mode services of the BAO_MMU sandbox
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * One line per SDK function that has to reach a register block supervisor
 * code has no mapping for. Each line is
 *
 *     X(name, group, read pointers, written pointers, byte lengths, word counts)
 *
 * name     the SDK function. Supervisor code links against a three
 *          instruction stub of that name (bao_mmu_stubs.S) that puts the
 *          line's index in t0 and executes ecall. The driver itself is
 *          compiled with bao_mmu_mside.h, which renames it bao_m_<name>,
 *          and the machine mode dispatcher (bao_mmu.c) calls that.
 * group    the page table the call runs under (BAO_MMU_G_*, bao_mmu.c):
 *          supervisor mappings plus only the register pages that driver
 *          uses.
 * pointers bit n set when argument n (a0 is 0) is a pointer the driver
 *          reads or writes. The dispatcher refuses a call whose pointer
 *          is not NULL and starts on a page supervisor code could not read
 *          (or write) itself.
 * lengths  bit n set when argument n is a length in bytes, or a count of
 *          32 bit words; the dispatcher refuses more than 2 MB.
 *
 * The order is the service number. This file holds only macros so the
 * assembler can include it.
 */

#ifndef BAO_MMU_SERVICES_H
#define BAO_MMU_SERVICES_H

#define BAO_MMU_SERVICES(X)                                         \
    X(sha256_init,               SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(sha256_hash,               SCE,  0x01, 0x04, 0x02, 0x00)                  \
    X(aes_set_key,               SCE,  0x01, 0x00, 0x02, 0x00)                  \
    X(aes_encrypt,               SCE,  0x01, 0x02, 0x04, 0x00)                  \
    X(aes_decrypt,               SCE,  0x01, 0x02, 0x04, 0x00)                  \
    X(aes_set_iv,                SCE,  0x01, 0x00, 0x00, 0x00)                  \
    X(aes_encrypt_mode,          SCE,  0x02, 0x04, 0x08, 0x00)                  \
    X(aes_decrypt_mode,          SCE,  0x02, 0x04, 0x08, 0x00)                  \
    X(trng_init,                 SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(trng_generate,             SCE,  0x00, 0x01, 0x00, 0x02)                  \
    X(trng_random,               SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(pke_mod_add,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(pke_mod_sub,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(pke_mod_mul,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(pke_mod_exp,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(pke_mod_inv,               SCE,  0x05, 0x10, 0x00, 0x0A)                  \
    X(pke_gcd,                   SCE,  0x05, 0x10, 0x00, 0x0A)                  \
    X(pke_ecc_words,             SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(pke_ecc_base_point,        SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(pke_ecc_point_mul,         SCE,  0x06, 0x08, 0x00, 0x00)                  \
    X(pke_ecc_point_mul_base,    SCE,  0x02, 0x04, 0x00, 0x00)                  \
    X(pke_ecc_point_add,         SCE,  0x06, 0x08, 0x00, 0x00)                  \
    X(pke_ecc_point_double,      SCE,  0x02, 0x04, 0x00, 0x00)                  \
    X(pke_x25519,                SCE,  0x03, 0x04, 0x00, 0x00)                  \
    X(pke_x25519_public,         SCE,  0x01, 0x02, 0x00, 0x00)                  \
    X(pke_x25519_shared,         SCE,  0x03, 0x04, 0x00, 0x00)                  \
    X(pke_ed25519_point_mul,     SCE,  0x03, 0x04, 0x00, 0x00)                  \
    X(pke_ed25519_point_mul_base, SCE, 0x01, 0x02, 0x00, 0x00)                  \
    X(pke_ed25519_point_add,     SCE,  0x03, 0x04, 0x00, 0x00)                  \
    X(pke_ed25519_point_double,  SCE,  0x01, 0x02, 0x00, 0x00)                  \
    X(pke_clock_init,            SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(pke_set_clock_divider,     SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(pke_clock_status,          SCE,  0x00, 0x01, 0x00, 0x00)                  \
    X(alu_div,                   SCE,  0x05, 0xF0, 0x00, 0x0A)                  \
    X(alu_add,                   SCE,  0x05, 0x30, 0x00, 0x0A)                  \
    X(alu_sub,                   SCE,  0x05, 0x30, 0x00, 0x0A)                  \
    X(alu_shift_right,           SCE,  0x01, 0x08, 0x00, 0x02)                  \
    X(alu_shift_right_words,     SCE,  0x01, 0x08, 0x00, 0x06)                  \
    X(alu_logic,                 SCE,  0x05, 0x20, 0x00, 0x0A)                  \
    X(alu_byte_exchange,         SCE,  0x01, 0x04, 0x00, 0x02)                  \
    X(alu_mod_add,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(alu_mod_sub,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(alu_mod_mul,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(alu_mod_exp,               SCE,  0x15, 0x40, 0x00, 0x2A)                  \
    X(alu_mod_inv,               SCE,  0x05, 0x10, 0x00, 0x0A)                  \
    X(alu_gcd,                   SCE,  0x05, 0x10, 0x00, 0x0A)                  \
    X(rram_write,                RRAM, 0x02, 0x00, 0x04, 0x00)                  \
    X(rram_write_page,           RRAM, 0x02, 0x00, 0x00, 0x00)                  \
    X(rram_erase_page,           RRAM, 0x00, 0x00, 0x00, 0x00)                  \
    X(wdt_start,                 WDT,  0x00, 0x00, 0x00, 0x00)                  \
    X(wdt_start_raw,             WDT,  0x00, 0x00, 0x00, 0x00)                  \
    X(wdt_feed,                  WDT,  0x00, 0x00, 0x00, 0x00)                  \
    X(wdt_get_count,             WDT,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_init,                  RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_set_divider,           RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_start,                 RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_stop,                  RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_set_time,              RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_get_time,              RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(bao_mmu_bio_clock,         CGU,  0x00, 0x00, 0x00, 0x00)                  \
    X(bao_mmu_software_reset,    CGU,  0x00, 0x00, 0x00, 0x00)                  \
    X(sha256_stream_init,        SCE,  0x01, 0x01, 0x00, 0x00)                  \
    X(sha256_stream_update,      SCE,  0x03, 0x01, 0x04, 0x00)                  \
    X(sha256_stream_final,       SCE,  0x01, 0x03, 0x00, 0x00)                  \
    X(sha512_stream_init,        SCE,  0x01, 0x01, 0x00, 0x00)                  \
    X(sha512_stream_update,      SCE,  0x03, 0x01, 0x04, 0x00)                  \
    X(sha512_stream_final,       SCE,  0x01, 0x03, 0x00, 0x00)                  \
    X(sha512_hash,               SCE,  0x01, 0x04, 0x02, 0x00)                  \
    X(hmac_sha256,               SCE,  0x05, 0x10, 0x0A, 0x00)                  \
    X(hmac_sha256_init,          SCE,  0x03, 0x01, 0x04, 0x00)                  \
    X(hmac_sha256_update,        SCE,  0x03, 0x01, 0x04, 0x00)                  \
    X(hmac_sha256_final,         SCE,  0x01, 0x03, 0x00, 0x00)                  \
    X(hmac_sha512,               SCE,  0x05, 0x10, 0x0A, 0x00)                  \
    X(hmac_sha512_init,          SCE,  0x03, 0x01, 0x04, 0x00)                  \
    X(hmac_sha512_update,        SCE,  0x03, 0x01, 0x04, 0x00)                  \
    X(hmac_sha512_final,         SCE,  0x01, 0x03, 0x00, 0x00)                  \
    X(ecdsa_key_size,            SCE,  0x00, 0x00, 0x00, 0x00)                  \
    X(ecdsa_public_key,          SCE,  0x02, 0x04, 0x00, 0x00)                  \
    X(ecdsa_check_public_key,    SCE,  0x02, 0x00, 0x00, 0x00)                  \
    X(ecdsa_sign_hash,           SCE,  0x0A, 0x20, 0x10, 0x00)                  \
    X(ecdsa_verify_hash,         SCE,  0x16, 0x00, 0x08, 0x00)                  \
    X(ecdsa_sign,                SCE,  0x0A, 0x20, 0x10, 0x00)                  \
    X(ecdsa_verify,              SCE,  0x2A, 0x00, 0x10, 0x00)                  \
    X(ed25519_public_key,        SCE,  0x01, 0x02, 0x00, 0x00)                  \
    X(ed25519_sign,              SCE,  0x03, 0x08, 0x04, 0x00)                  \
    X(ed25519_verify,            SCE,  0x0B, 0x00, 0x04, 0x00)                  \
    X(rtc_set_alarm,             RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_alarm_fired,           RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_clear_alarm,           RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(rtc_alarm_wake,            RTC,  0x00, 0x00, 0x00, 0x00)                  \
    X(power_down,                RTC,  0x00, 0x00, 0x00, 0x00)

#endif
