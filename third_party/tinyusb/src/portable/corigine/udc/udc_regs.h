/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 MicroPython contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Register definitions for the Corigine UDC USB 2.0 HS device controller
// integrated on the Baochip-1x SoC.  Offsets and bit fields cross-checked
// against the vendor doc (baochip/baochip-1x docs/src/ch05-00-usb.md) and
// the bao1x-hal Rust reference (libs/bao1x-hal/src/usb/utra.rs in
// betrusted-io/xous-core).
//
// All TRB rings, EP contexts, and data buffers MUST live in IFRAM; the
// UDC's DMA engine cannot access regular SRAM.  Place buffers in the
// linker's .dma_buffers section.

#ifndef TUSB_PORTABLE_CORIGINE_UDC_REGS_H
#define TUSB_PORTABLE_CORIGINE_UDC_REGS_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Memory map                                                          */
/* ------------------------------------------------------------------ */

/* UDC CSR window: 0x50202000 - 0x50205000 (12 KiB). */
#define UDC_BASE                0x50202000UL
#define UDC_LEN                 0x3000UL

/* Device-level registers start 0x400 into the window. */
#define UDC_DEV_OFFSET          0x400UL
/* User-Interrupt Control Register block follows device regs. */
#define UDC_UICR_OFFSET         (UDC_DEV_OFFSET + 0x100UL)

#define UDC_DEV_BASE            (UDC_BASE + UDC_DEV_OFFSET)
#define UDC_UICR_BASE           (UDC_BASE + UDC_UICR_OFFSET)

/* ------------------------------------------------------------------ */
/* Device-level register offsets (from UDC_DEV_BASE)                  */
/* ------------------------------------------------------------------ */

#define UDC_DEVCAP_OFFSET       0x00
#define UDC_DEVCONFIG_OFFSET    0x10
#define UDC_EVENTCONFIG_OFFSET  0x14
#define UDC_USBCMD_OFFSET       0x20
#define UDC_USBSTS_OFFSET       0x24
#define UDC_DCBAPLO_OFFSET      0x28
#define UDC_DCBAPHI_OFFSET      0x2C
#define UDC_PORTSC_OFFSET       0x30
#define UDC_U3PORTPMSC_OFFSET   0x34
#define UDC_U2PORTPMSC_OFFSET   0x38
#define UDC_DOORBELL_OFFSET     0x40
#define UDC_MFINDEX_OFFSET      0x44
#define UDC_PTMCTRL_OFFSET      0x48
#define UDC_PTMSTS_OFFSET       0x4C
#define UDC_EPENABLE_OFFSET     0x60
#define UDC_EPRUNNING_OFFSET    0x64
#define UDC_CMDPARA0_OFFSET     0x70
#define UDC_CMDPARA1_OFFSET     0x74
#define UDC_CMDCTRL_OFFSET      0x78
#define UDC_ODBCAP_OFFSET       0x80
/* ODBCONFIG0..7 cover the 16-entry overflow-doorbell config table,
 * one register per pair of endpoints (EP0..15). */
#define UDC_ODBCONFIG0_OFFSET   0x90
#define UDC_DEBUG0_OFFSET       0xB0

/* ------------------------------------------------------------------ */
/* Interrupter register offsets (from UDC_UICR_BASE)                  */
/* ------------------------------------------------------------------ */

#define UDC_IMAN_OFFSET         0x00
#define UDC_IMOD_OFFSET         0x04
#define UDC_ERSTSZ_OFFSET       0x08
#define UDC_ERSTBALO_OFFSET     0x10
#define UDC_ERSTBAHI_OFFSET     0x14
#define UDC_ERDPLO_OFFSET       0x18
#define UDC_ERDPHI_OFFSET       0x1C

/* ------------------------------------------------------------------ */
/* Field shifts and masks                                              */
/* ------------------------------------------------------------------ */

/* DEVCONFIG */
#define UDC_DEVCONFIG_MAX_SPEED_SHIFT       0
#define UDC_DEVCONFIG_MAX_SPEED_MASK        0xFu
#define UDC_DEVCONFIG_MAX_SPEED_FS          (0x1 << 0)
#define UDC_DEVCONFIG_MAX_SPEED_HS          (0x3 << 0)

/* EVENTCONFIG -- enables event reporting bits */
#define UDC_EVENTCONFIG_CSC                 (1u << 0)   /* Connect status change */
#define UDC_EVENTCONFIG_PEC                 (1u << 1)   /* Port enable change */
#define UDC_EVENTCONFIG_PPC                 (1u << 3)   /* Port power change */
#define UDC_EVENTCONFIG_PRC                 (1u << 4)   /* Port reset change */
#define UDC_EVENTCONFIG_PLC                 (1u << 5)   /* Port link change */
#define UDC_EVENTCONFIG_CEC                 (1u << 6)   /* Connect error change */
#define UDC_EVENTCONFIG_U3_PLC              (1u << 8)
#define UDC_EVENTCONFIG_L1_PLC              (1u << 9)
#define UDC_EVENTCONFIG_U3_RESUME_PLC       (1u << 10)
#define UDC_EVENTCONFIG_L1_RESUME_PLC       (1u << 11)
#define UDC_EVENTCONFIG_INACTIVE_PLC        (1u << 12)
#define UDC_EVENTCONFIG_USB3_RESUME_NO_PLC  (1u << 13)
#define UDC_EVENTCONFIG_USB2_RESUME_NO_PLC  (1u << 14)
#define UDC_EVENTCONFIG_SETUP               (1u << 16)
#define UDC_EVENTCONFIG_STOPPED_LEN_INVALID (1u << 17)
#define UDC_EVENTCONFIG_HALTED_LEN_INVALID  (1u << 18)
#define UDC_EVENTCONFIG_DISABLED_LEN_INVALID (1u << 19)
#define UDC_EVENTCONFIG_DISABLE_EVENT       (1u << 20)

/* USBCMD */
#define UDC_USBCMD_RUN_STOP                 (1u << 0)
#define UDC_USBCMD_SOFT_RESET               (1u << 1)
#define UDC_USBCMD_INT_ENABLE               (1u << 2)
#define UDC_USBCMD_SYS_ERR_ENABLE           (1u << 3)
#define UDC_USBCMD_EWE                      (1u << 10)
#define UDC_USBCMD_FORCE_TERMINATION        (1u << 11)

/* USBSTS */
#define UDC_USBSTS_CTL_HALTED               (1u << 0)
#define UDC_USBSTS_SYSTEM_ERR               (1u << 2)
#define UDC_USBSTS_EINT                     (1u << 3)   /* Event interrupt */
#define UDC_USBSTS_CTL_IDLE                 (1u << 12)

/* PORTSC -- USB 2.0 port status and control */
#define UDC_PORTSC_CCS                      (1u << 0)   /* Current connect status */
#define UDC_PORTSC_PP                       (1u << 3)   /* Port power */
#define UDC_PORTSC_PR                       (1u << 4)   /* Port reset */
#define UDC_PORTSC_PLS_SHIFT                5
#define UDC_PORTSC_PLS_MASK                 (0xFu << 5) /* Port link state */
#define UDC_PORTSC_SPEED_SHIFT              10
#define UDC_PORTSC_SPEED_MASK               (0xFu << 10)
#define UDC_PORTSC_SPEED_FS                 (0x1u << 10)
#define UDC_PORTSC_SPEED_HS                 (0x3u << 10)
#define UDC_PORTSC_LWS                      (1u << 16)
#define UDC_PORTSC_CSC                      (1u << 17)  /* Connect status change (W1C) */
#define UDC_PORTSC_PPC                      (1u << 20)  /* Port power change (W1C) */
#define UDC_PORTSC_PRC                      (1u << 21)  /* Port reset change (W1C) */
#define UDC_PORTSC_PLC                      (1u << 22)  /* Port link change (W1C) */
#define UDC_PORTSC_CEC                      (1u << 23)  /* Connect error change (W1C) */
#define UDC_PORTSC_WCE                      (1u << 25)
#define UDC_PORTSC_WDE                      (1u << 26)
#define UDC_PORTSC_WPR                      (1u << 31)  /* Warm port reset */

/* Mask of all PORTSC W1C change bits, used to clear pending events. */
#define UDC_PORTSC_CHANGE_BITS \
    (UDC_PORTSC_CSC | UDC_PORTSC_PPC | UDC_PORTSC_PRC | \
    UDC_PORTSC_PLC | UDC_PORTSC_CEC)

/* DOORBELL -- bottom 5 bits hold the PEI (physical endpoint index)
 * to ring.  Other bits reserved; write-only. */
#define UDC_DOORBELL_TARGET_SHIFT           0
#define UDC_DOORBELL_TARGET_MASK            0x1Fu

/* CMDCTRL */
#define UDC_CMDCTRL_ACTIVE                  (1u << 0)
#define UDC_CMDCTRL_IOC                     (1u << 1)
#define UDC_CMDCTRL_TYPE_SHIFT              4
#define UDC_CMDCTRL_TYPE_MASK               (0xFu << 4)
#define UDC_CMDCTRL_STATUS_SHIFT            16
#define UDC_CMDCTRL_STATUS_MASK             (0xFu << 16)

/* CMDPARA0 layout per command type (utra.rs:104-106). */
#define UDC_CMDPARA0_CMD0_INIT_EP0_DCS      (1u << 0)
#define UDC_CMDPARA0_CMD1_UPDATE_EP0_MPS_SHIFT  16
#define UDC_CMDPARA0_CMD1_UPDATE_EP0_MPS_MASK   (0xFFFFu << 16)
#define UDC_CMDPARA0_CMD2_SET_ADDR_MASK     0xFFu

/* Command type values for CMDCTRL.TYPE. */
#define UDC_CMD_INIT_EP0                    0
#define UDC_CMD_UPDATE_EP0_MPS              1
#define UDC_CMD_SET_ADDR                    2
#define UDC_CMD_SEND_DEV_NOTIFICATION       3
#define UDC_CMD_CONFIG_EP                   4
#define UDC_CMD_SET_HALT                    5
#define UDC_CMD_CLEAR_HALT                  6
#define UDC_CMD_RESET_EP                    7
#define UDC_CMD_STOP_EP                     8
#define UDC_CMD_REQUEST_SLOT                9

/* IMAN -- interrupter management */
#define UDC_IMAN_IP                         (1u << 0)   /* Interrupt pending (W1C) */
#define UDC_IMAN_IE                         (1u << 1)   /* Interrupt enable */

/* ERDPLO -- event ring dequeue pointer (low 32 bits) */
#define UDC_ERDPLO_DESI_MASK                0x7u        /* Dequeue ERST index */
#define UDC_ERDPLO_EHB                      (1u << 3)   /* Event handler busy (W1C) */
#define UDC_ERDPLO_DQ_PTR_MASK              0xFFFFFFF0u /* 16-byte aligned */

/* ------------------------------------------------------------------ */
/* Endpoint nomenclature                                               */
/* ------------------------------------------------------------------ */

/* Corigine direction is reversed from USB spec terminology:
 *   USB IN  (device-to-host)  == Corigine "outbound" / SEND  (false)
 *   USB OUT (host-to-device)  == Corigine "inbound"  / RECV  (true)
 *
 * Physical Endpoint Index (PEI) packs both:
 *   PEI = ep_num * 2 + dir
 * EP0 IN  = 0,  EP0 OUT = 1,
 * EP1 IN  = 2,  EP1 OUT = 3, etc.
 *
 * Hardware wires EP0..EP4 (PEI 0..9).
 */
#define UDC_DIR_IN                          0
#define UDC_DIR_OUT                         1
#define UDC_EP_COUNT                        5
#define UDC_PEI_COUNT                       (UDC_EP_COUNT * 2)
#define UDC_PEI(ep_num, dir)                ((ep_num) * 2 + (dir))

/* ------------------------------------------------------------------ */
/* TRB layouts                                                         */
/* ------------------------------------------------------------------ */

/* All TRBs are 16 bytes (four LE uint32_t).  The C struct uses raw
 * uint32_t fields; helper macros below extract/insert bit fields. */
typedef struct {
    uint32_t dw0;       /* data pointer low (or per-type) */
    uint32_t dw1;       /* data pointer high (or per-type) */
    uint32_t dw2;       /* transfer length + intr target */
    uint32_t dw3;       /* type + flags + cycle bit */
} udc_trb_t;

/* TRB type values placed in dw3[15:10] of any TRB. */
typedef enum {
    UDC_TRB_RSVD                  = 0,
    UDC_TRB_XFER_NORMAL           = 1,
    UDC_TRB_DATA_STAGE            = 3,
    UDC_TRB_STATUS_STAGE          = 4,
    UDC_TRB_DATA_ISOCH            = 5,
    UDC_TRB_LINK                  = 6,
    UDC_TRB_EVENT_TRANSFER        = 32,
    UDC_TRB_EVENT_CMD_COMPLETION  = 33,
    UDC_TRB_EVENT_PORT_STATUS     = 34,
    UDC_TRB_EVENT_MFINDEX_WRAP    = 39,
    UDC_TRB_SETUP_PKT             = 40,
} udc_trb_type_t;

/* dw3 layout (transfer/control TRBs). */
#define UDC_TRB_DW3_CYCLE_BIT               (1u << 0)
#define UDC_TRB_DW3_LINK_TOGGLE_CYCLE       (1u << 1)
#define UDC_TRB_DW3_INTR_ON_SHORT_PKT       (1u << 2)
#define UDC_TRB_DW3_NO_SNOOP                (1u << 3)
#define UDC_TRB_DW3_CHAIN                   (1u << 4)
#define UDC_TRB_DW3_IOC                     (1u << 5)
#define UDC_TRB_DW3_APPEND_ZLP              (1u << 7)
#define UDC_TRB_DW3_BLOCK_EVENT             (1u << 9)
#define UDC_TRB_DW3_TYPE_SHIFT              10
#define UDC_TRB_DW3_TYPE_MASK               (0x3Fu << 10)
#define UDC_TRB_DW3_DIR                     (1u << 16)
#define UDC_TRB_DW3_SETUP_TAG_SHIFT         17
#define UDC_TRB_DW3_SETUP_TAG_MASK          (0x3u << 17)
#define UDC_TRB_DW3_STATUS_STAGE_STALL      (1u << 19)
#define UDC_TRB_DW3_STATUS_STAGE_SET_ADDR   (1u << 20)

/* dw2 layout (transfer TRBs). */
#define UDC_TRB_DW2_TRANSFER_LEN_MASK       0x3FFFFu      /* [17:0] */
#define UDC_TRB_DW2_TD_SIZE_SHIFT           18
#define UDC_TRB_DW2_TD_SIZE_MASK            (0xFu << 18)
#define UDC_TRB_DW2_INTR_TARGET_SHIFT       22
#define UDC_TRB_DW2_INTR_TARGET_MASK        (0x3FFu << 22)

/* Event TRB dw2 -- transfer length and completion code. */
#define UDC_EVENT_TRB_DW2_TRAN_LEN_MASK     0x1FFFFu      /* [16:0] */
#define UDC_EVENT_TRB_DW2_COMPL_CODE_SHIFT  24
#define UDC_EVENT_TRB_DW2_COMPL_CODE_MASK   (0xFFu << 24)

/* Event TRB dw3 cycle bit and endpoint id fields. */
#define UDC_EVENT_TRB_DW3_CYCLE_BIT         (1u << 0)
#define UDC_EVENT_TRB_DW3_TYPE_SHIFT        10
#define UDC_EVENT_TRB_DW3_TYPE_MASK         (0x3Fu << 10)
#define UDC_EVENT_TRB_DW3_ENDPOINT_ID_SHIFT 16
#define UDC_EVENT_TRB_DW3_ENDPOINT_ID_MASK  (0x1Fu << 16)
#define UDC_EVENT_TRB_DW3_SETUP_TAG_SHIFT   21
#define UDC_EVENT_TRB_DW3_SETUP_TAG_MASK    (0x3u << 21)

/* TRB completion codes. */
typedef enum {
    UDC_COMPL_INVALID                 = 0,
    UDC_COMPL_SUCCESS                 = 1,
    UDC_COMPL_USB_TRANSACTION_ERROR   = 4,
    UDC_COMPL_SHORT_PACKET            = 13,
    UDC_COMPL_EVENT_RING_FULL_ERROR   = 21,
    UDC_COMPL_MISSED_SERVICE_ERROR    = 23,
    UDC_COMPL_STOPPED                 = 26,
    UDC_COMPL_STOPPED_LEN_INVALID     = 27,
    UDC_COMPL_PROTOCOL_STALL_ERROR    = 192,
    UDC_COMPL_SETUP_TAG_MISMATCH      = 193,
    UDC_COMPL_HALTED                  = 194,
    UDC_COMPL_HALTED_LEN_INVALID      = 195,
} udc_completion_code_t;

/* ------------------------------------------------------------------ */
/* Endpoint context entry (EP context layout, 16 bytes per EP)        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t dw0;       /* EP number + interval */
    uint32_t dw1;       /* EP type + max-burst + MPS */
    uint32_t dw2;       /* Deq ptr low + DCS */
    uint32_t dw3;       /* Reserved (vendor-specific use elsewhere) */
} udc_epcx_t;

#define UDC_EPCX_DW0_EP_NUM_SHIFT           3
#define UDC_EPCX_DW0_EP_NUM_MASK            (0xFu << 3)
#define UDC_EPCX_DW0_INTERVAL_SHIFT         16
#define UDC_EPCX_DW0_INTERVAL_MASK          (0xFFu << 16)

#define UDC_EPCX_DW1_EP_TYPE_SHIFT          3
#define UDC_EPCX_DW1_EP_TYPE_MASK           (0x7u << 3)
#define UDC_EPCX_DW1_MAX_BURST_SHIFT        8
#define UDC_EPCX_DW1_MAX_BURST_MASK         (0xFFu << 8)
#define UDC_EPCX_DW1_MPS_SHIFT              16
#define UDC_EPCX_DW1_MPS_MASK               (0xFFFFu << 16)

#define UDC_EPCX_DW2_DCS                    (1u << 0)
#define UDC_EPCX_DW2_DEQ_PTR_LO_MASK        0xFFFFFFF0u

/* Endpoint type values for EPCX dw1[5:3].  Note: Corigine direction
 * is reversed; inbound (USB OUT) type = base type + 4. */
typedef enum {
    UDC_EPTYPE_CONTROL_OR_INVALID = 0,
    UDC_EPTYPE_ISOCH_OUTBOUND     = 1, /* USB IN */
    UDC_EPTYPE_BULK_OUTBOUND      = 2, /* USB IN */
    UDC_EPTYPE_INTR_OUTBOUND      = 3, /* USB IN */
    UDC_EPTYPE_INVALID2           = 4,
    UDC_EPTYPE_ISOCH_INBOUND      = 5, /* USB OUT */
    UDC_EPTYPE_BULK_INBOUND       = 6, /* USB OUT */
    UDC_EPTYPE_INTR_INBOUND       = 7, /* USB OUT */
} udc_ep_type_t;

/* ------------------------------------------------------------------ */
/* IFRAM layout for UDC DMA buffers                                    */
/* ------------------------------------------------------------------ */

/* Sizes (in bytes) taken from the Rust reference driver, sized so the
 * whole footprint fits in 5 IFRAM pages (20 KiB). */
#define UDC_ERSTSIZE                        0x100
#define UDC_EVENT_RING_LEN                  32      /* TRBs */
#define UDC_EVENT_RING_BYTES                (UDC_EVENT_RING_LEN * 16)
#define UDC_EPCX_BYTES                      0x200   /* EPCX_NUM * 16 */
#define UDC_EP0_TR_BYTES                    0x100   /* 16 TRBs */
#define UDC_TD_RING_LEN                     64      /* TRBs per EP */
#define UDC_EP_TR_BYTES                     (UDC_TD_RING_LEN * UDC_PEI_COUNT * 16)
#define UDC_EP0_BUF_BYTES                   256
#define UDC_APP_BUF_LEN                     512
#define UDC_APP_BUF_BYTES                   (UDC_PEI_COUNT * UDC_APP_BUF_LEN)

/* Relative offsets within the UDC IFRAM region. */
#define UDC_ERST_OFFSET_IN_BUF              0
#define UDC_EVENT_RING_OFFSET_IN_BUF        (UDC_ERST_OFFSET_IN_BUF + UDC_ERSTSIZE)
#define UDC_EPCX_OFFSET_IN_BUF             (UDC_EVENT_RING_OFFSET_IN_BUF + UDC_EVENT_RING_BYTES)
#define UDC_EP0_TR_OFFSET_IN_BUF           (UDC_EPCX_OFFSET_IN_BUF + UDC_EPCX_BYTES)
#define UDC_EP_TR_OFFSET_IN_BUF            (UDC_EP0_TR_OFFSET_IN_BUF + UDC_EP0_TR_BYTES)
#define UDC_EP0_BUF_OFFSET_IN_BUF          (UDC_EP_TR_OFFSET_IN_BUF + UDC_EP_TR_BYTES)
#define UDC_APP_BUF_OFFSET_IN_BUF          (UDC_EP0_BUF_OFFSET_IN_BUF + UDC_EP0_BUF_BYTES)
#define UDC_TOTAL_BUF_BYTES                (UDC_APP_BUF_OFFSET_IN_BUF + UDC_APP_BUF_BYTES)

/* ------------------------------------------------------------------ */
/* IRQ wiring                                                          */
/* ------------------------------------------------------------------ */

/* The UDC raises its primary event line on IRQARRAY1 (= IRQ_ARRAY1
 * in lib/dabao-sdk hardware/irq.h).  EV_ENABLE_USBC_DUPE bit 0 is the
 * main USB interrupt source; bit 1 is a software-triggered interrupt
 * we do not use. */
#define UDC_IRQARRAY                        1
#define UDC_IRQARRAY_USBC_BIT               (1u << 0)
#define UDC_IRQARRAY_SW_BIT                 (1u << 1)

/* ------------------------------------------------------------------ */
/* Convenience accessors                                               */
/* ------------------------------------------------------------------ */

#ifndef REG32
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif

#define UDC_DEV_REG(off)        REG32(UDC_DEV_BASE + (off))
#define UDC_UICR_REG(off)       REG32(UDC_UICR_BASE + (off))

#endif // TUSB_PORTABLE_CORIGINE_UDC_REGS_H
