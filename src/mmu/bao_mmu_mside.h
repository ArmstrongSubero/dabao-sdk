/*
 * bao_mmu_mside.h: names of the drivers that run in machine mode
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * With BAO_MMU, bao_flash.sh compiles the SHA, AES, TRNG, PKE, ALU, RRAM,
 * WDT and RTC drivers, the signature module (hardware_sign, which calls
 * the SHA, PKE and ALU drivers) and bao_mmu.c with this header forced in front of
 * every other include. Their public functions become bao_m_<name>, so
 * calls between them stay inside machine mode, and the plain names are
 * left free for the supervisor stubs in bao_mmu_stubs.S. The driver
 * sources themselves are unchanged.
 *
 * rram_read is not renamed: it only reads, supervisor code calls it
 * directly, and the page table decides what it may read.
 *
 * The objects these files make are linked into the machine mode data
 * region of bao1x_mmu.ld, which supervisor code has no mapping for.
 */

#ifndef BAO_MMU_MSIDE_H
#define BAO_MMU_MSIDE_H

#define BAO_MMU_MACHINE_SIDE 1

#define sha256_init                 bao_m_sha256_init
#define sha256_hash                 bao_m_sha256_hash
#define sha256_stream_init          bao_m_sha256_stream_init
#define sha256_stream_update        bao_m_sha256_stream_update
#define sha256_stream_final         bao_m_sha256_stream_final
#define sha512_stream_init          bao_m_sha512_stream_init
#define sha512_stream_update        bao_m_sha512_stream_update
#define sha512_stream_final         bao_m_sha512_stream_final
#define sha512_hash                 bao_m_sha512_hash
#define hmac_sha256                 bao_m_hmac_sha256
#define hmac_sha256_init            bao_m_hmac_sha256_init
#define hmac_sha256_update          bao_m_hmac_sha256_update
#define hmac_sha256_final           bao_m_hmac_sha256_final
#define hmac_sha512                 bao_m_hmac_sha512
#define hmac_sha512_init            bao_m_hmac_sha512_init
#define hmac_sha512_update          bao_m_hmac_sha512_update
#define hmac_sha512_final           bao_m_hmac_sha512_final

#define aes_set_key                 bao_m_aes_set_key
#define aes_encrypt                 bao_m_aes_encrypt
#define aes_decrypt                 bao_m_aes_decrypt
#define aes_set_iv                  bao_m_aes_set_iv
#define aes_encrypt_mode            bao_m_aes_encrypt_mode
#define aes_decrypt_mode            bao_m_aes_decrypt_mode

#define trng_init                   bao_m_trng_init
#define trng_generate               bao_m_trng_generate
#define trng_random                 bao_m_trng_random

#define pke_mod_add                 bao_m_pke_mod_add
#define pke_mod_sub                 bao_m_pke_mod_sub
#define pke_mod_mul                 bao_m_pke_mod_mul
#define pke_mod_exp                 bao_m_pke_mod_exp
#define pke_mod_inv                 bao_m_pke_mod_inv
#define pke_gcd                     bao_m_pke_gcd
#define pke_ecc_words               bao_m_pke_ecc_words
#define pke_ecc_base_point          bao_m_pke_ecc_base_point
#define pke_ecc_point_mul           bao_m_pke_ecc_point_mul
#define pke_ecc_point_mul_base      bao_m_pke_ecc_point_mul_base
#define pke_ecc_point_add           bao_m_pke_ecc_point_add
#define pke_ecc_point_double        bao_m_pke_ecc_point_double
#define pke_x25519                  bao_m_pke_x25519
#define pke_x25519_public           bao_m_pke_x25519_public
#define pke_x25519_shared           bao_m_pke_x25519_shared
#define pke_ed25519_point_mul       bao_m_pke_ed25519_point_mul
#define pke_ed25519_point_mul_base  bao_m_pke_ed25519_point_mul_base
#define pke_ed25519_point_add       bao_m_pke_ed25519_point_add
#define pke_ed25519_point_double    bao_m_pke_ed25519_point_double
#define pke_clock_init              bao_m_pke_clock_init
#define pke_set_clock_divider       bao_m_pke_set_clock_divider
#define pke_clock_status            bao_m_pke_clock_status
#define pke_sce_prepare             bao_m_pke_sce_prepare
#define pke_wait_fr                 bao_m_pke_wait_fr
#define pke_run                     bao_m_pke_run
#define pke_seg_write               bao_m_pke_seg_write
#define pke_seg_zero                bao_m_pke_seg_zero
#define pke_seg_read                bao_m_pke_seg_read
#define pke_bn_words                bao_m_pke_bn_words
#define pke_bn_bits                 bao_m_pke_bn_bits

#define alu_div                     bao_m_alu_div
#define alu_add                     bao_m_alu_add
#define alu_sub                     bao_m_alu_sub
#define alu_shift_right             bao_m_alu_shift_right
#define alu_shift_right_words       bao_m_alu_shift_right_words
#define alu_logic                   bao_m_alu_logic
#define alu_byte_exchange           bao_m_alu_byte_exchange
#define alu_mod_add                 bao_m_alu_mod_add
#define alu_mod_sub                 bao_m_alu_mod_sub
#define alu_mod_mul                 bao_m_alu_mod_mul
#define alu_mod_exp                 bao_m_alu_mod_exp
#define alu_mod_inv                 bao_m_alu_mod_inv
#define alu_gcd                     bao_m_alu_gcd

#define ecdsa_key_size              bao_m_ecdsa_key_size
#define ecdsa_public_key            bao_m_ecdsa_public_key
#define ecdsa_check_public_key      bao_m_ecdsa_check_public_key
#define ecdsa_sign_hash             bao_m_ecdsa_sign_hash
#define ecdsa_verify_hash           bao_m_ecdsa_verify_hash
#define ecdsa_sign                  bao_m_ecdsa_sign
#define ecdsa_verify                bao_m_ecdsa_verify
#define ed25519_public_key          bao_m_ed25519_public_key
#define ed25519_sign                bao_m_ed25519_sign
#define ed25519_verify              bao_m_ed25519_verify

#define rram_write                  bao_m_rram_write
#define rram_write_page             bao_m_rram_write_page
#define rram_erase_page             bao_m_rram_erase_page

#define wdt_start                   bao_m_wdt_start
#define wdt_start_raw               bao_m_wdt_start_raw
#define wdt_feed                    bao_m_wdt_feed
#define wdt_get_count               bao_m_wdt_get_count

#define rtc_init                    bao_m_rtc_init
#define rtc_set_divider             bao_m_rtc_set_divider
#define rtc_start                   bao_m_rtc_start
#define rtc_stop                    bao_m_rtc_stop
#define rtc_set_time                bao_m_rtc_set_time
#define rtc_get_time                bao_m_rtc_get_time
#define rtc_set_alarm               bao_m_rtc_set_alarm
#define rtc_alarm_fired             bao_m_rtc_alarm_fired
#define rtc_clear_alarm             bao_m_rtc_clear_alarm
#define rtc_alarm_wake              bao_m_rtc_alarm_wake
#define power_down                  bao_m_power_down

#define bao_mmu_bio_clock           bao_m_bao_mmu_bio_clock
#define bao_mmu_software_reset      bao_m_bao_mmu_software_reset

#endif
