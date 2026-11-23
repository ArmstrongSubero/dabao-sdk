/*
 * main.c - Signed update demo
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Checks an Ed25519 signature on an incoming image before storing it in
 * RRAM, and refuses an image that is damaged, signed by another key or
 * older than the one already stored.
 *
 * An image is a 16 byte header (magic, version, payload length, reserved),
 * the payload and a 64 byte signature over the header and payload. Two
 * slots in runtime storage hold images. A new image always goes into the
 * slot that does not hold the current one, so a power loss in the middle
 * of a write leaves the current image intact.
 *
 * The images are stored and checked only. Nothing here runs them, and the
 * program image, boot0 and boot1 are never written.
 *
 * Run it twice. The second run finds release 2 already stored and accepts
 * nothing.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/rram.h"
#include "hardware/sign.h"
#include "images.h"

#define IMG_MAGIC        0x554F4142UL    /* "BAOU" */
#define IMG_HDR_SIZE     16
#define IMG_SIG_SIZE     64
#define IMG_MAX_PAYLOAD  256
#define IMG_MAX_SIZE     (IMG_HDR_SIZE + IMG_MAX_PAYLOAD + IMG_SIG_SIZE)

#define SLOT_A           (RRAM_USER_BASE + 0x20000UL)
#define SLOT_B           (RRAM_USER_BASE + 0x21000UL)

typedef enum {
    IMG_OK = 0,
    IMG_BAD_FORMAT,
    IMG_BAD_SIGNATURE,
    IMG_HW_ERROR,
} img_status_t;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static img_status_t image_check(const uint8_t *img, uint32_t size,
                                uint32_t *version, uint32_t *payload_len)
{
    if (size < IMG_HDR_SIZE + IMG_SIG_SIZE)
        return IMG_BAD_FORMAT;
    if (rd32(img) != IMG_MAGIC)
        return IMG_BAD_FORMAT;

    uint32_t len = rd32(img + 8);
    if (len > IMG_MAX_PAYLOAD || size < IMG_HDR_SIZE + len + IMG_SIG_SIZE)
        return IMG_BAD_FORMAT;
    if (rd32(img + 4) == 0)
        return IMG_BAD_FORMAT;

    int rc = ed25519_verify(vendor_pub, img, IMG_HDR_SIZE + len,
                            img + IMG_HDR_SIZE + len);
    if (rc == SIGN_BAD)
        return IMG_BAD_SIGNATURE;
    if (rc != SIGN_OK)
        return IMG_HW_ERROR;

    *version = rd32(img + 4);
    *payload_len = len;
    return IMG_OK;
}

/* Version of the valid image in a slot, or 0 */
static uint32_t slot_version(uint32_t addr, uint8_t *buf)
{
    uint32_t version, len;

    rram_read(addr, buf, IMG_MAX_SIZE);
    if (image_check(buf, IMG_MAX_SIZE, &version, &len) != IMG_OK)
        return 0;
    return version;
}

static void print_payload(const uint8_t *img)
{
    uint32_t len = rd32(img + 8);

    for (uint32_t i = 0; i < len && img[IMG_HDR_SIZE + i] != 0; i++)
        mini_printf("%c", img[IMG_HDR_SIZE + i]);
}

static void show_slots(void)
{
    static uint8_t buf[IMG_MAX_SIZE];
    uint32_t va = slot_version(SLOT_A, buf);
    uint32_t vb = slot_version(SLOT_B, buf);

    mini_printf("  slot A: ");
    if (va) mini_printf("version %u\r\n", va); else mini_printf("empty\r\n");
    mini_printf("  slot B: ");
    if (vb) mini_printf("version %u\r\n", vb); else mini_printf("empty\r\n");

    if (va == 0 && vb == 0) {
        mini_printf("  current: none\r\n");
        return;
    }
    rram_read(va >= vb ? SLOT_A : SLOT_B, buf, IMG_MAX_SIZE);
    mini_printf("  current: \"");
    print_payload(buf);
    mini_printf("\"\r\n");
}

static void receive(const char *name, const uint8_t *img, uint32_t size)
{
    static uint8_t buf[IMG_MAX_SIZE];
    uint32_t version, len, check_version, check_len;

    mini_printf("\r\n[%s] %u bytes\r\n", name, size);

    img_status_t st = image_check(img, size, &version, &len);
    if (st == IMG_BAD_FORMAT) {
        mini_printf("  rejected: not an image\r\n");
        return;
    }
    if (st == IMG_BAD_SIGNATURE) {
        mini_printf("  rejected: signature does not verify\r\n");
        return;
    }
    if (st != IMG_OK) {
        mini_printf("  rejected: signature engine error\r\n");
        return;
    }
    mini_printf("  signature good, version %u\r\n", version);

    uint32_t va = slot_version(SLOT_A, buf);
    uint32_t vb = slot_version(SLOT_B, buf);
    uint32_t current = va >= vb ? va : vb;
    if (version <= current) {
        mini_printf("  rejected: version %u is not newer than %u\r\n",
                    version, current);
        return;
    }

    uint32_t target = va > vb ? SLOT_B : SLOT_A;
    uint32_t total = IMG_HDR_SIZE + len + IMG_SIG_SIZE;
    int rc = rram_write(target, img, total);
    if (rc != 0) {
        mini_printf("  write failed: %d\r\n", rc);
        return;
    }

    rram_read(target, buf, IMG_MAX_SIZE);
    if (image_check(buf, IMG_MAX_SIZE, &check_version, &check_len) != IMG_OK ||
        check_version != version) {
        mini_printf("  stored copy does not verify\r\n");
        return;
    }
    mini_printf("  stored in slot %c and verified\r\n",
                target == SLOT_A ? 'A' : 'B');
}

int main(void)
{
    static uint8_t damaged[sizeof(image_v2)];

    bao_init();

    mini_printf("\r\n========================================\r\n");
    mini_printf("  Signed Update Demo\r\n");
    mini_printf("========================================\r\n");
    mini_printf("At start:\r\n");
    show_slots();

    receive("release 1", image_v1, sizeof(image_v1));

    for (uint32_t i = 0; i < sizeof(image_v2); i++)
        damaged[i] = image_v2[i];
    damaged[IMG_HDR_SIZE + 3] ^= 0x01;
    receive("release 2, one bit changed", damaged, sizeof(damaged));

    receive("release 3, signed by another key",
            image_v3_other_key, sizeof(image_v3_other_key));

    receive("release 2", image_v2, sizeof(image_v2));

    receive("release 1 again", image_v1, sizeof(image_v1));

    mini_printf("\r\nAt end:\r\n");
    show_slots();
    mini_printf("\r\n[Done] Signed update demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
