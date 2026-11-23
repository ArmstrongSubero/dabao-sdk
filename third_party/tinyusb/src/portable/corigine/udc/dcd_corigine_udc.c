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

// TinyUSB DCD for the Corigine UDC IP found on the Baochip-1x SoC.

#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(TUP_USBIP_CORIGINE_UDC)

#include <string.h>

#include "device/dcd.h"

#include "bao/platform.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#include "dcd_corigine_udc.h"
#include "udc_regs.h"

// Default DMA buffer placement: 64-byte aligned.  Ports that require
// specific memory (e.g. IFRAM on Baochip-1x) must define
// TUD_CORIGINE_DMA_ATTR before including tusb_config.h.
#ifndef TUD_CORIGINE_DMA_ATTR
#define TUD_CORIGINE_DMA_ATTR  __attribute__((aligned(64)))
#endif

/* ------------------------------------------------------------------ */
/* DMA buffers                                                         */
/* ------------------------------------------------------------------ */

// The entire UDC working set (ERST, event ring, EP contexts, EP0 ring,
// per-EP transfer rings, EP0 buffer, per-EP app buffers) lives in a
// single DMA-accessible region.  Layout offsets come from udc_regs.h
// and mirror the Rust reference driver.
static volatile uint8_t udc_dma_region[UDC_TOTAL_BUF_BYTES] TUD_CORIGINE_DMA_ATTR;

// ERST entry layout (16 bytes, matches Rust ErstS).
typedef struct {
    uint32_t seg_addr_lo;
    uint32_t seg_addr_hi;
    uint32_t seg_size;
    uint32_t rsvd;
} udc_erst_entry_t;

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    udc_trb_t *event_ring_base;     // start of event ring
    udc_trb_t *event_ring_end;      // one-past-end (used for wrap)
    udc_trb_t *event_dq;            // current dequeue position
    uint8_t ccs;                    // expected cycle bit (toggles on wrap)
    uint8_t initialised;            // 1 once dcd_init completed

    // EP0 transfer ring.  The last TRB in the ring is a LINK TRB
    // pointing back at ep0_tr_base; ep0_tr_end is the address of
    // the LINK slot so increment can wrap on hitting it.
    udc_trb_t *ep0_tr_base;
    udc_trb_t *ep0_tr_link;         // address of the LINK TRB (last slot)
    udc_trb_t *ep0_enq;             // current enqueue position
    uint8_t ep0_pcs;                // producer cycle state (init = 1)

    // EP0 transfer state machine.
    uint8_t setup_tag;              // latched from most recent SETUP TRB
    uint8_t setup_packet[8];        // copied SETUP payload for TinyUSB
    uint8_t *ep0_out_user_buf;      // TinyUSB-owned OUT buffer (may be in SRAM)
    uint16_t ep0_out_len;           // bytes the host is expected to send
    uint8_t set_addr_pending;       // queue STATUS_STAGE_SET_ADDR on next IN status
    uint8_t dev_addr;               // pending device address from SET_ADDR

    // Bulk / interrupt endpoints.  Indexed by PEI; entries 0/1 are EP0
    // and unused here.  Each non-EP0 endpoint owns a UDC_TD_RING_LEN-
    // entry TRB ring carved out of the DMA region.
    udc_trb_t *ep_tr_base[UDC_PEI_COUNT];
    udc_trb_t *ep_link[UDC_PEI_COUNT];
    udc_trb_t *ep_enq[UDC_PEI_COUNT];
    uint8_t ep_pcs[UDC_PEI_COUNT];
    uint8_t *ep_user_buf[UDC_PEI_COUNT];
    uint16_t ep_user_len[UDC_PEI_COUNT];
    uint16_t ep_mps[UDC_PEI_COUNT];
} udc_state_t;

static udc_state_t udc_state;

// Diagnostic counters for bring-up.  Exposed at known addresses so the
// REPL can read machine.mem32 to see live IRQ activity.  Stored in a
// fixed-layout struct to keep address math simple.
typedef struct {
    uint32_t irq_count;
    uint32_t port_status_events;
    uint32_t setup_packets;
    uint32_t xfer_complete_events;
    uint32_t bus_resets;
    uint32_t last_portsc;
    uint32_t last_usbsts;
    // Detail captured at the moment a non-SUCCESS EP0 event lands.
    uint32_t err_count;          // total non-SUCCESS EP0 events
    uint32_t err_src_type;       // TRB type from src->dw3 (DATA_STAGE=3, STATUS_STAGE=4)
    uint32_t err_src_dir;        // 1 = OUT, 0 = IN
    uint32_t err_src_dw3;        // raw dw3 of source TRB at fault
    uint8_t err_setup[8];        // SETUP packet that initiated the failing transfer
    uint8_t err_setup_tag;       // setup_tag at time of fault
    uint8_t err_seen;            // 1 once an error has been captured
    uint32_t reinit_requests;    // controller re-bring-ups requested
    uint32_t reinit_done;        // controller re-bring-ups completed
} udc_diag_t;
udc_diag_t udc_diag __attribute__((used));

/* ------------------------------------------------------------------ */
/* Register helpers                                                    */
/* ------------------------------------------------------------------ */

static inline uint32_t udc_dev_read(uint32_t off) {
    return UDC_DEV_REG(off);
}

static inline void udc_dev_write(uint32_t off, uint32_t val) {
    UDC_DEV_REG(off) = val;
}

static inline uint32_t udc_uicr_read(uint32_t off) {
    return UDC_UICR_REG(off);
}

static inline void udc_uicr_write(uint32_t off, uint32_t val) {
    UDC_UICR_REG(off) = val;
}

/* ------------------------------------------------------------------ */
/* Command interface                                                   */
/* ------------------------------------------------------------------ */

/* Issue a synchronous controller command.  CMDCTRL.ACTIVE is set by us
 * and self-clears when the controller has consumed the command.  The
 * interface is single-threaded so we poll, with a bounded timeout so a
 * wedged controller cannot hang the system. */
#define UDC_CMD_TIMEOUT_MS      100u
#define UDC_CMD_STATUS_TIMEOUT  0xFFu    /* sentinel returned on timeout */

static uint8_t udc_issue_command(uint8_t type, uint32_t para0, uint32_t para1) {
    /* Wait for any in-flight command to drain. */
    uint32_t deadline = tusb_time_millis_api() + UDC_CMD_TIMEOUT_MS;
    while (udc_dev_read(UDC_CMDCTRL_OFFSET) & UDC_CMDCTRL_ACTIVE) {
        if ((int32_t)(tusb_time_millis_api() - deadline) >= 0) {
            return UDC_CMD_STATUS_TIMEOUT;
        }
    }

    udc_dev_write(UDC_CMDPARA0_OFFSET, para0);
    udc_dev_write(UDC_CMDPARA1_OFFSET, para1);
    memory_fence();
    udc_dev_write(UDC_CMDCTRL_OFFSET,
        UDC_CMDCTRL_ACTIVE |
        (((uint32_t)type << UDC_CMDCTRL_TYPE_SHIFT) & UDC_CMDCTRL_TYPE_MASK));
    memory_fence();

    /* Wait for the controller to consume the command. */
    deadline = tusb_time_millis_api() + UDC_CMD_TIMEOUT_MS;
    while (udc_dev_read(UDC_CMDCTRL_OFFSET) & UDC_CMDCTRL_ACTIVE) {
        if ((int32_t)(tusb_time_millis_api() - deadline) >= 0) {
            return UDC_CMD_STATUS_TIMEOUT;
        }
    }
    uint32_t ctrl = udc_dev_read(UDC_CMDCTRL_OFFSET);
    return (uint8_t)((ctrl & UDC_CMDCTRL_STATUS_MASK) >> UDC_CMDCTRL_STATUS_SHIFT);
}

static inline void udc_doorbell(uint8_t pei) {
    udc_dev_write(UDC_DOORBELL_OFFSET,
        ((uint32_t)pei << UDC_DOORBELL_TARGET_SHIFT) & UDC_DOORBELL_TARGET_MASK);
    memory_fence();
}

/* ------------------------------------------------------------------ */
/* EP0 transfer ring helpers                                           */
/* ------------------------------------------------------------------ */

/* Advance ep0_enq one TRB; on hitting the LINK slot, set its cycle bit
 * to the current PCS, toggle PCS, and wrap to base.  Mirrors the Rust
 * UdcEp::increment_enq_pt logic. */
static void udc_ep0_advance_enq(void) {
    udc_state.ep0_enq++;
    if (udc_state.ep0_enq == udc_state.ep0_tr_link) {
        /* Toggle the LINK TRB cycle bit so the controller follows it. */
        udc_trb_t *link = udc_state.ep0_tr_link;
        uint32_t dw3 = link->dw3 & ~UDC_TRB_DW3_CYCLE_BIT;
        if (udc_state.ep0_pcs) {
            dw3 |= UDC_TRB_DW3_CYCLE_BIT;
        }
        link->dw3 = dw3;
        memory_fence();
        udc_state.ep0_pcs ^= 1;
        udc_state.ep0_enq = udc_state.ep0_tr_base;
    }
}

/* Write a DATA_STAGE TRB at the current enqueue position. */
static void udc_ep0_queue_data_trb(uint32_t addr, uint16_t length, uint8_t dir) {
    udc_trb_t *trb = udc_state.ep0_enq;
    trb->dw0 = addr;
    trb->dw1 = 0;
    trb->dw2 = ((uint32_t)length & UDC_TRB_DW2_TRANSFER_LEN_MASK);
    uint32_t dw3 = 0;
    if (udc_state.ep0_pcs) {
        dw3 |= UDC_TRB_DW3_CYCLE_BIT;
    }
    /* INTR_ON_SHORT_PKT matches the bao1x-hal Rust reference (always
     * set on data TRBs).  Without ISP the controller's EP0 state
     * machine can stall when an IN data stage ends with a short
     * packet (any descriptor read smaller than wLength or shorter
     * than EP0_MAX_PACKET), because the data-stage completion event
     * never fires; the host then issues STATUS before our handler
     * sees DATA complete, and the controller flags PROTOCOL_STALL
     * (completion code 192). */
    dw3 |= UDC_TRB_DW3_INTR_ON_SHORT_PKT;
    /* IOC on both directions: every data stage raises a TRANSFER event
     * with the data-stage TRB as its source so the handler can report
     * the completion to TinyUSB without inferring it from the later
     * status event. */
    dw3 |= UDC_TRB_DW3_IOC;
    dw3 |= ((uint32_t)UDC_TRB_DATA_STAGE << UDC_TRB_DW3_TYPE_SHIFT) & UDC_TRB_DW3_TYPE_MASK;
    if (dir == UDC_DIR_IN) {
        /* USB IN (device-to-host); Corigine DIR bit clear for SEND. */
    } else {
        dw3 |= UDC_TRB_DW3_DIR;
    }
    dw3 |= ((uint32_t)udc_state.setup_tag << UDC_TRB_DW3_SETUP_TAG_SHIFT) & UDC_TRB_DW3_SETUP_TAG_MASK;
    trb->dw3 = dw3;
    memory_fence();
    udc_ep0_advance_enq();
}

/* Write a STATUS_STAGE TRB at the current enqueue position.
 * dir is the USB direction of the status stage (opposite of data). */
static void udc_ep0_queue_status_trb(uint8_t dir, uint8_t set_addr) {
    udc_trb_t *trb = udc_state.ep0_enq;
    trb->dw0 = 0;
    trb->dw1 = 0;
    trb->dw2 = 0;
    uint32_t dw3 = 0;
    if (udc_state.ep0_pcs) {
        dw3 |= UDC_TRB_DW3_CYCLE_BIT;
    }
    dw3 |= UDC_TRB_DW3_IOC;
    dw3 |= ((uint32_t)UDC_TRB_STATUS_STAGE << UDC_TRB_DW3_TYPE_SHIFT) & UDC_TRB_DW3_TYPE_MASK;
    if (dir == UDC_DIR_OUT) {
        dw3 |= UDC_TRB_DW3_DIR;
    }
    dw3 |= ((uint32_t)udc_state.setup_tag << UDC_TRB_DW3_SETUP_TAG_SHIFT) & UDC_TRB_DW3_SETUP_TAG_MASK;
    if (set_addr) {
        dw3 |= UDC_TRB_DW3_STATUS_STAGE_SET_ADDR;
    }
    trb->dw3 = dw3;
    memory_fence();
    udc_ep0_advance_enq();
}

/* Build the EP0 transfer ring (16 TRBs, last slot is LINK back to base)
 * and issue INIT_EP0 to point the controller at it. */
static void udc_init_ep0(void) {
    uintptr_t base = (uintptr_t)udc_dma_region;
    udc_trb_t *ring = (udc_trb_t *)(base + UDC_EP0_TR_OFFSET_IN_BUF);
    size_t ring_len = UDC_EP0_TR_BYTES / sizeof(udc_trb_t);    /* 16 */

    /* Zero the ring, then build the LINK TRB in the last slot pointing
     * back at the first slot.  The LINK toggles cycle on traversal. */
    memset(ring, 0, UDC_EP0_TR_BYTES);
    udc_trb_t *link = &ring[ring_len - 1];
    link->dw0 = (uint32_t)(uintptr_t)ring & 0xFFFFFFF0u;
    link->dw1 = 0;
    link->dw2 = 0;
    link->dw3 = (((uint32_t)UDC_TRB_LINK << UDC_TRB_DW3_TYPE_SHIFT) & UDC_TRB_DW3_TYPE_MASK)
        | UDC_TRB_DW3_LINK_TOGGLE_CYCLE;
    memory_fence();

    udc_state.ep0_tr_base = ring;
    udc_state.ep0_tr_link = link;
    udc_state.ep0_enq = ring;
    udc_state.ep0_pcs = 1;
    udc_state.setup_tag = 0;
    udc_state.ep0_out_user_buf = NULL;
    udc_state.ep0_out_len = 0;
    udc_state.set_addr_pending = 0;
    udc_state.dev_addr = 0;

    /* INIT_EP0 command: CMDPARA0 carries the ring address (aligned) ORed
     * with the initial DCS bit (= PCS, which we initialise to 1).
     * CMDPARA1 is 0; IFRAM addresses fit in 32 bits. */
    uint32_t para0 = ((uint32_t)(uintptr_t)ring & 0xFFFFFFF0u) | 0x1u;
    (void)udc_issue_command(UDC_CMD_INIT_EP0, para0, 0);
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */

// PHY tuning registers (offsets relative to UDC_BASE).  These are
// vendor-specific magic numbers from the bao1x-hal Rust reference
// (libs/bao1x-hal/src/usb/driver.rs, MAGIC_TABLE inside reset()).
// They are not in the public UDC doc but are required for the
// Corigine UDC's USB 2.0 PHY to negotiate with a host.  Without
// them, PORTSC reads connected but no SETUP packets ever arrive.
// Must be applied BEFORE the SOFT_RESET pulse.
static const struct {
    uint16_t off;
    uint32_t val;
} udc_phy_magic[] = {
    { 0x0FC, 0x00000001 },
    { 0x084, 0x01401388 },
    { 0x0F4, 0x0000F023 },
    { 0x088, 0x3B066409 },
    { 0x08C, 0x0D020407 },
    { 0x090, 0x04055050 },
    { 0x094, 0x03030A07 },
    { 0x098, 0x05131304 },
    { 0x09C, 0x3B4B0D15 },
    { 0x0A0, 0x14168C6E },
    { 0x0A4, 0x18060408 },
    { 0x0A8, 0x4B120C0F },
    { 0x0AC, 0x03190D05 },
    { 0x0B0, 0x08080D09 },
    { 0x0B4, 0x20060B03 },
    { 0x0B8, 0x040A8C0E },
    { 0x0BC, 0x44087D5A },
    { 0x110, 0x00000000 },
};

static void udc_apply_phy_magic(void) {
    for (size_t i = 0; i < sizeof(udc_phy_magic) / sizeof(udc_phy_magic[0]); i++) {
        REG32(UDC_BASE + udc_phy_magic[i].off) = udc_phy_magic[i].val;
    }
    memory_fence();
}

// Run the full Corigine UDC controller bring-up: drive the board SE0 pin
// to force a clean disconnect, apply the PHY tuning magic, double
// SOFT_RESET, program DCBAP / event ring / EP0, set RUN_STOP, and issue
// the initial address-set transaction.  Used both at first init (dcd_init)
// and to recover from a mid-session controller drop (dcd_corigine_reinit),
// so it must contain no one-shot-only side effects: every step here is
// idempotent or self-contained.  Returns false only if a SOFT_RESET pulse
// times out (controller wedged), leaving USB disabled.
static bool udc_bringup(void) {
    // Clear the entire DMA working set up front.  All structures
    // (TRBs, EP contexts, app buffers) start zeroed.
    memset((void *)udc_dma_region, 0, sizeof(udc_dma_region));

    // Drop any bulk/interrupt endpoint tracking: the rings they point at
    // were just zeroed, and dcd_edpt_open repopulates these on the next
    // SET_CONFIG.  Matters for the recovery path, where class endpoints
    // were open before the drop.
    for (size_t i = 0; i < UDC_PEI_COUNT; i++) {
        udc_state.ep_tr_base[i] = NULL;
        udc_state.ep_link[i] = NULL;
        udc_state.ep_enq[i] = NULL;
        udc_state.ep_pcs[i] = 0;
        udc_state.ep_user_buf[i] = NULL;
        udc_state.ep_user_len[i] = 0;
        udc_state.ep_mps[i] = 0;
    }

    uintptr_t base = (uintptr_t)udc_dma_region;

    // ---- Drive the board SE0 pin LOW to force a bus-level disconnect
    // ---- from any previous boot1 USB enumeration.
    //
    // The port provides TUD_CORIGINE_SE0_ASSERT() / DEASSERT() macros
    // that drive the board-specific D+/D- shunt pin.  On the Baochip-1x
    // DABAO eval board this is PC13.
    //
    // Use a non-pumping busy wait, NOT the RTOS delay: on the recovery
    // path (dcd_corigine_reinit) this runs as a scheduled task, and an
    // RTOS delay could pump events and re-enter the DCD against the
    // half-torn-down controller we are mid-rebuild on.  Nothing needs
    // servicing during the SE0 hold anyway (the controller is stopped
    // here), so a plain busy wait is both safe for recovery and correct
    // at first init.
    TUD_CORIGINE_SE0_ASSERT();
    memory_fence();
    {
        uint32_t _se0_start = tusb_time_millis_api();
        while ((tusb_time_millis_api() - _se0_start) < 200u) {}
    }
    TUD_CORIGINE_SE0_DEASSERT();
    memory_fence();

    // The Rust reference splits bring-up into three phases (boot1
    // glue.rs:129-131):
    //   1. reset() -- magic PHY table, INT_ENABLE=0, SOFT_RESET, wait,
    //                 dummy readback.  Programs the PHY tuning into
    //                 hardware.
    //   2. init()  -- zero IFRAM, INT_ENABLE=0, RUN_STOP=0, SOFT_RESET,
    //                 wait, DEVCONFIG/EVENTCONFIG/DCBAP/rings/EPCX.
    //                 Resets controller state cleanly while preserving
    //                 the PHY tuning from phase 1.
    //   3. start() -- RUN_STOP | INT_ENABLE | SYS_ERR_ENABLE.
    // Doing only one SOFT_RESET leaves the PHY in an indeterminate
    // state on warm boot from boot1, which is why hosts never see
    // disconnect and our DCD never receives SETUP packets.

    // Clear RUN_STOP / INT_ENABLE / FORCE_TERMINATION.  Per the
    // vendor doc (ch05-00-usb.md USBCMD bit 0): "Write 0 to stop
    // (performs soft disconnect unless Keep Connect is asserted)".
    // In practice the chip's PHY does NOT actually drop D+ during
    // this write (verified by reading host-side DWC2.HPRT
    // PRT_CONN_STS, which stays high throughout) -- the soft
    // disconnect happens at the controller-CSR level only, not at
    // the wire level.  So no long hold here; aggressive hosts like
    // xhci_hcd start enumerating within a second of port-power-on
    // and will give up if dcd_init isn't ready by then.
    uint32_t cmd_pre = udc_dev_read(UDC_USBCMD_OFFSET);
    uint32_t cmd = cmd_pre
        & ~(UDC_USBCMD_INT_ENABLE | UDC_USBCMD_RUN_STOP
            | UDC_USBCMD_FORCE_TERMINATION);
    udc_dev_write(UDC_USBCMD_OFFSET, cmd);
    memory_fence();

    // Brief poll for Controller Halted -- doc says UDC idles within
    // microseconds after RUN_STOP=0.  Bounded 10 ms timeout.
    {
        uint32_t start = tusb_time_millis_api();
        while (!(udc_dev_read(UDC_USBSTS_OFFSET) & UDC_USBSTS_CTL_HALTED)
               && (tusb_time_millis_api() - start) < 10u) {
        }
    }
    (void)cmd_pre;

    // ---- Phase 1: reset() ----
    // Apply PHY tuning magic, then SOFT_RESET to push it into hardware.
    udc_apply_phy_magic();
    memory_fence();

    udc_dev_write(UDC_USBCMD_OFFSET, cmd | UDC_USBCMD_SOFT_RESET);
    memory_fence();
    {
        uint32_t start = tusb_time_millis_api();
        while (udc_dev_read(UDC_USBCMD_OFFSET) & UDC_USBCMD_SOFT_RESET) {
            if ((tusb_time_millis_api() - start) > 100u) {
                TU_LOG1("dcd_corigine: phase 1 SOFT_RESET timeout, USB disabled\n");
                return false;
            }
        }
    }

    // Dummy readback of the first 288 bytes of the UDC window.
    // The Rust reference does the same after reset(); it appears to
    // sync internal state machines.  Use a volatile sink so the
    // compiler can't elide the reads.
    {
        volatile uint32_t sink = 0;
        for (size_t i = 0; i < 72; ++i) {
            sink += REG32(UDC_BASE + i * 4u);
        }
        (void)sink;
    }

    // ---- Phase 2: init() ----
    // Second SOFT_RESET cleanly resets controller state; PHY tuning
    // from phase 1 persists.
    udc_dev_write(UDC_USBCMD_OFFSET, cmd | UDC_USBCMD_SOFT_RESET);
    memory_fence();
    {
        uint32_t start = tusb_time_millis_api();
        while (udc_dev_read(UDC_USBCMD_OFFSET) & UDC_USBCMD_SOFT_RESET) {
            if ((tusb_time_millis_api() - start) > 100u) {
                TU_LOG1("dcd_corigine: phase 2 SOFT_RESET timeout, USB disabled\n");
                return false;
            }
        }
    }

    // ---- DEVCONFIG / EVENTCONFIG ----
    uint32_t devcfg = udc_dev_read(UDC_DEVCONFIG_OFFSET);
    devcfg &= ~UDC_DEVCONFIG_MAX_SPEED_MASK;
    devcfg |= UDC_DEVCONFIG_MAX_SPEED_HS;
    udc_dev_write(UDC_DEVCONFIG_OFFSET, devcfg);

    uint32_t evtcfg = UDC_EVENTCONFIG_CSC | UDC_EVENTCONFIG_PEC
        | UDC_EVENTCONFIG_PPC | UDC_EVENTCONFIG_PRC
        | UDC_EVENTCONFIG_PLC | UDC_EVENTCONFIG_CEC
        | UDC_EVENTCONFIG_SETUP;
    udc_dev_write(UDC_EVENTCONFIG_OFFSET, evtcfg);
    memory_fence();

    // ---- DCBAP: Device Context Base Address Pointer (EPCX array) ----
    uintptr_t epcx_addr = base + UDC_EPCX_OFFSET_IN_BUF;
    udc_dev_write(UDC_DCBAPLO_OFFSET, (uint32_t)epcx_addr);
    udc_dev_write(UDC_DCBAPHI_OFFSET, 0);
    memory_fence();

    // ---- Event ring ----
    uintptr_t erst_addr = base + UDC_ERST_OFFSET_IN_BUF;
    uintptr_t event_ring_addr = base + UDC_EVENT_RING_OFFSET_IN_BUF;

    udc_erst_entry_t *erst = (udc_erst_entry_t *)(uintptr_t)erst_addr;
    erst->seg_addr_lo = (uint32_t)event_ring_addr;
    erst->seg_addr_hi = 0;
    erst->seg_size = (uint32_t)UDC_EVENT_RING_LEN;
    erst->rsvd = 0;
    memory_fence();

    // ERSTSZ: number of segments = 1
    udc_uicr_write(UDC_ERSTSZ_OFFSET, 1u);
    // ERSTBA: segment table base address
    udc_uicr_write(UDC_ERSTBALO_OFFSET, (uint32_t)erst_addr);
    udc_uicr_write(UDC_ERSTBAHI_OFFSET, 0);
    // ERDP: initial dequeue pointer at the first TRB
    udc_uicr_write(UDC_ERDPHI_OFFSET, 0);
    udc_uicr_write(UDC_ERDPLO_OFFSET, (uint32_t)event_ring_addr & UDC_ERDPLO_DQ_PTR_MASK);
    memory_fence();

    // Drive software state to match the hardware view: dequeue pointer
    // at the first TRB, expecting cycle bit 1 (the controller writes
    // each event with the current cycle, starting at 1 after reset).
    udc_state.event_ring_base = (udc_trb_t *)(uintptr_t)event_ring_addr;
    udc_state.event_ring_end = udc_state.event_ring_base + UDC_EVENT_RING_LEN;
    udc_state.event_dq = udc_state.event_ring_base;
    udc_state.ccs = 1;

    // ---- interrupter ----
    // Clear any latched IP and enable the interrupter (IMAN.IE).
    udc_uicr_write(UDC_IMAN_OFFSET, UDC_IMAN_IE | UDC_IMAN_IP);
    udc_uicr_write(UDC_IMOD_OFFSET, 0);
    memory_fence();

    // ---- EP0 transfer ring + INIT_EP0 command ----
    udc_init_ep0();

    // ---- power-management/LPM disabled for now ----
    udc_dev_write(UDC_U3PORTPMSC_OFFSET, 0);
    udc_dev_write(UDC_U2PORTPMSC_OFFSET, 0);
    memory_fence();

    // ---- run ----
    // Set RUN_STOP plus INT_ENABLE (and SYS_ERR_ENABLE, mirroring the
    // Rust reference's start()).  The controller starts looking at the
    // bus after this write.
    cmd = udc_dev_read(UDC_USBCMD_OFFSET);
    cmd |= UDC_USBCMD_INT_ENABLE | UDC_USBCMD_SYS_ERR_ENABLE | UDC_USBCMD_RUN_STOP;
    udc_dev_write(UDC_USBCMD_OFFSET, cmd);
    memory_fence();

    udc_state.initialised = 1;

    // Pre-initialise the device address to 0 and queue a paired IN
    // STATUS_STAGE TRB with the SET_ADDR bit.  Matches the Rust HAL's
    // start() (driver.rs:1505).  The Corigine controller appears to
    // need this initial "address-set" transaction to fully enable
    // EP0; without it, multi-packet IN-data + OUT-STATUS control
    // transfers (e.g. host's GET_DESCRIPTOR(CONFIG, 75) read of the
    // CDC composite config) complete with PROTOCOL_STALL_ERROR (192)
    // on the OUT STATUS TRB.
    (void)udc_issue_command(UDC_CMD_SET_ADDR, 0u, 0);
    udc_ep0_queue_status_trb(UDC_DIR_IN, /*set_addr=*/ 1);
    udc_doorbell(UDC_PEI(0, UDC_DIR_IN));
    return true;
}

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t *rh_init) {
    (void)rhport;
    (void)rh_init;
    return udc_bringup();
}

/* ------------------------------------------------------------------ */
/* USB controller re-bring-up (manual recovery)                        */
/* ------------------------------------------------------------------ */
/*
 * Under host port open/close the Corigine UDC can stochastically drop its
 * own D+ pullup (a device-side disconnect, not a host reset).  A bare drop
 * self-heals: the host re-enumerates and the CDC works again.  But if the
 * device is hammered with further open/close DURING that post-drop
 * re-enumeration window (e.g. ModemManager auto-probing the fresh CDC), the
 * controller can wedge: it stays "connected" yet completes no transfers
 * (EP0 + bulk both stall, host SET_CONFIG times out), with NO
 * firmware-visible USB event to detect it (PORTSC/USBSTS/bus-reset all
 * stay silent).  The behaviour reproduces on the bao1x-hal Rust reference,
 * so it is controller/PHY level, not a ring-desync bug.
 *
 * Re-running the full controller bring-up (udc_bringup) clears the wedge
 * without a chip reset (verified on the bench: the host re-enumerates,
 * fails the first control read once with -110, then its retry succeeds and
 * the CDC passes data again).  Because no firmware signal flags the wedge,
 * the bring-up is exposed as a manual recovery via dcd_corigine_reinit().
 * It must run in task context, not an ISR: it busy-waits hundreds of ms,
 * rebuilds the event ring, and logs.  The port is responsible for
 * scheduling this call in task context.
 */
void dcd_corigine_reinit(uint8_t rhport) {
    // Mask the UDC interrupt and stand down the handler so no event is
    // serviced while the controller is torn down and its rings rebuilt.
    dcd_int_disable(rhport);
    udc_state.initialised = 0;
    memory_fence();

    // Quiesce the still-live controller before udc_bringup rebuilds it.
    // Unlike the cold-boot path the controller has been running, so clear
    // RUN_STOP / INT_ENABLE to stop it raising interrupts and clear any
    // latched interrupt status (USBSTS.EINT, IMAN.IP).  Otherwise an event
    // latched during udc_bringup's 200 ms SE0 hold could be taken the
    // moment udc_bringup re-arms the IRQ line, against half-rebuilt rings.
    uint32_t cmd = udc_dev_read(UDC_USBCMD_OFFSET)
        & ~(UDC_USBCMD_INT_ENABLE | UDC_USBCMD_RUN_STOP
            | UDC_USBCMD_FORCE_TERMINATION);
    udc_dev_write(UDC_USBCMD_OFFSET, cmd);
    memory_fence();
    udc_dev_write(UDC_USBSTS_OFFSET, UDC_USBSTS_EINT);
    udc_uicr_write(UDC_IMAN_OFFSET, udc_uicr_read(UDC_IMAN_OFFSET) | UDC_IMAN_IP);
    memory_fence();

    bool ok = udc_bringup();

    udc_diag.reinit_done++;
    memory_fence();

    if (!ok) {
        TU_LOG1("dcd_corigine: re-init bring-up failed, USB disabled\n");
    }
}

// Workaround for the Corigine UDC IRQ-propagation bug.  See header
// for the wedge signature and rationale.  In the healthy path this is
// a single register read on USBSTS.EINT and returns immediately.  In
// the wedged path (EINT set with no IRQ asserting) it runs the
// handler directly to drain pending events from the ring.
void dcd_corigine_poll_pending_events(uint8_t rhport) {
    if (!udc_state.initialised) {
        return;
    }
    if (udc_dev_read(UDC_USBSTS_OFFSET) & UDC_USBSTS_EINT) {
        dcd_int_handler(rhport);
    }
}

/* ------------------------------------------------------------------ */
/* Teardown                                                            */
/* ------------------------------------------------------------------ */

void dcd_corigine_teardown(uint8_t rhport) {
    // Stop the IRQ handler from doing further work even if udc_state
    // claims initialised.  Both: mask the line (no further event
    // delivery) AND clear the initialised flag so any IRQ that does
    // sneak through returns immediately at the top of dcd_int_handler.
    udc_state.initialised = 0;
    dcd_int_disable(rhport);
    memory_fence();

    // Halt the controller: clear RUN_STOP / INT_ENABLE / FORCE_TERM.
    // Wait briefly for CTL_HALTED.  Don't trust this to actually
    // disconnect at the wire; that's the SE0 pin's job below.
    uint32_t cmd = udc_dev_read(UDC_USBCMD_OFFSET)
        & ~(UDC_USBCMD_INT_ENABLE | UDC_USBCMD_RUN_STOP
            | UDC_USBCMD_FORCE_TERMINATION);
    udc_dev_write(UDC_USBCMD_OFFSET, cmd);
    memory_fence();
    {
        uint32_t start = tusb_time_millis_api();
        while (!(udc_dev_read(UDC_USBSTS_OFFSET) & UDC_USBSTS_CTL_HALTED)
               && (tusb_time_millis_api() - start) < 10u) {
        }
    }

    // Drive the board SE0 pin LOW.  This is the board-level D+/D- shunt;
    // the host sees a real wire-level disconnect within milliseconds.
    // Hold for 100 ms so the host has time to register the disconnect
    // and run its detach handlers before the chip resets.
    //
    // SE0 is left asserted on return -- the caller is expected to
    // immediately reset the chip, so the asserted SE0 doubles as the
    // PROG/boot-strap pin assertion for the boot0 ROM check.
    TUD_CORIGINE_SE0_ASSERT();
    memory_fence();
    {
        uint32_t start = tusb_time_millis_api();
        while ((tusb_time_millis_api() - start) < 100u) {}
    }

    // Soft-reset the UDC.  After this, controller state machines are
    // back to power-on values; PHY tuning is lost too, but that's fine
    // because boot1's own re-init reapplies it.  If this times out,
    // the chip reset coming next will scrub the controller anyway.
    udc_dev_write(UDC_USBCMD_OFFSET, cmd | UDC_USBCMD_SOFT_RESET);
    memory_fence();
    {
        uint32_t start = tusb_time_millis_api();
        while (udc_dev_read(UDC_USBCMD_OFFSET) & UDC_USBCMD_SOFT_RESET) {
            if ((tusb_time_millis_api() - start) > 50u) {
                break;
            }
        }
    }

    // Zero the entire DMA working set: event ring, EPCX array, EP0
    // ring, per-EP rings, app buffers.  Boot1's USB init re-zeroes its
    // own working set at its own offsets, but the regions overlap
    // unpredictably across firmware versions; clearing everything we
    // own removes any stale TRBs / EP contexts boot1 might otherwise
    // misinterpret if it walks our memory before zeroing.
    memset((void *)udc_dma_region, 0, sizeof(udc_dma_region));
    memory_fence();
}

/* ------------------------------------------------------------------ */
/* Interrupt enable / disable                                          */
/* ------------------------------------------------------------------ */

void dcd_int_enable(uint8_t rhport) {
    (void)rhport;
    // Re-enable the interrupter-level enable (IMAN.IE) and the
    // IRQARRAY1 event bit.  TinyUSB calls this every time it leaves a
    // critical section; both writes are idempotent so the cost is
    // small.
    uint32_t iman = udc_uicr_read(UDC_IMAN_OFFSET);
    udc_uicr_write(UDC_IMAN_OFFSET, iman | UDC_IMAN_IE);
    memory_fence();
    irq_enable_events(IRQ_ARRAY1, UDC_IRQARRAY_USBC_BIT | UDC_IRQARRAY_SW_BIT);
}

void dcd_int_disable(uint8_t rhport) {
    (void)rhport;
    // Drop just the interrupter enable.  TinyUSB calls this from
    // non-ISR contexts to fence USB events while it mutates state;
    // leaving the IRQARRAY enable alone avoids racing other users of
    // the array (none today, but future-proof) and matches the
    // behaviour TinyUSB expects.
    uint32_t iman = udc_uicr_read(UDC_IMAN_OFFSET);
    udc_uicr_write(UDC_IMAN_OFFSET, iman & ~UDC_IMAN_IE);
    memory_fence();
}

/* ------------------------------------------------------------------ */
/* Event ring processing                                               */
/* ------------------------------------------------------------------ */

/* Forward decl: stage-buffer address is shared with the dcd_edpt_xfer
 * path below.  Pulled up so dcd_int_handler can recover it without
 * pulling in the full Phase 2.6 helper section above. */
static inline uintptr_t udc_ep_stage_addr(uint32_t pei) {
    return (uintptr_t)udc_dma_region + UDC_APP_BUF_OFFSET_IN_BUF + pei * UDC_APP_BUF_LEN;
}

// Read TRB type out of dw3.
static inline uint32_t event_trb_type(const udc_trb_t *trb) {
    return (trb->dw3 & UDC_EVENT_TRB_DW3_TYPE_MASK) >> UDC_EVENT_TRB_DW3_TYPE_SHIFT;
}

// Read endpoint id (PEI) out of dw3.
static inline uint32_t event_trb_pei(const udc_trb_t *trb) {
    return (trb->dw3 & UDC_EVENT_TRB_DW3_ENDPOINT_ID_MASK) >> UDC_EVENT_TRB_DW3_ENDPOINT_ID_SHIFT;
}

// Read completion code out of dw2.
static inline uint32_t event_trb_compl_code(const udc_trb_t *trb) {
    return (trb->dw2 & UDC_EVENT_TRB_DW2_COMPL_CODE_MASK) >> UDC_EVENT_TRB_DW2_COMPL_CODE_SHIFT;
}

// Read residual length out of dw2.
static inline uint32_t event_trb_residual(const udc_trb_t *trb) {
    return trb->dw2 & UDC_EVENT_TRB_DW2_TRAN_LEN_MASK;
}

// Handle a port-status-change event TRB.  PORTSC W1C bits get cleared
// here; if PRC fired and PR is no longer asserted, the host has
// finished its reset sequence and TinyUSB needs to be told.
static void udc_handle_port_status(void) {
    uint32_t portsc = udc_dev_read(UDC_PORTSC_OFFSET);
    // Clear W1C change bits by writing them back; preserve other
    // fields exactly as the Rust reference does
    // (driver.rs:2907-2908 writes the full register value back).
    udc_dev_write(UDC_PORTSC_OFFSET, portsc);
    memory_fence();

    // PRC asserted and PR de-asserted == host finished bus reset.
    // Report speed to TinyUSB so it can size EP0.  The doc encodes
    // speed at PORTSC[13:10]: 1=FS, 3=HS.
    bool reset_done = (portsc & UDC_PORTSC_PRC) && !(portsc & UDC_PORTSC_PR);
    bool cable_connect = (portsc & UDC_PORTSC_CSC) && (portsc & UDC_PORTSC_PPC)
        && (portsc & UDC_PORTSC_PP) && (portsc & UDC_PORTSC_CCS);

    if (reset_done || cable_connect) {
        // Update EP0 max packet size for the negotiated speed.  Without
        // this, the controller's EP0 stays at boot1's MPS and SETUP
        // packets from the host don't make it into our event ring.
        // Rust reference: update_current_speed() at driver.rs:2917,
        // which calls update_ep0_maxpacketsize -> UPDATE_EP0_MPS cmd.
        uint32_t speed_field = portsc & UDC_PORTSC_SPEED_MASK;
        uint32_t mps = 64;  // both FS and HS use 64 bytes for EP0
        (void)speed_field;
        (void)udc_issue_command(UDC_CMD_UPDATE_EP0_MPS,
            (mps << UDC_CMDPARA0_CMD1_UPDATE_EP0_MPS_SHIFT)
            & UDC_CMDPARA0_CMD1_UPDATE_EP0_MPS_MASK,
            0);

        if (reset_done) {
            // INIT_EP0 is intentionally one-shot at boot; reissuing
            // from IRQ context during a host port reset destabilises
            // the controller (matches bao1x-hal driver.rs).
            tusb_speed_t speed = TUSB_SPEED_FULL;
            if ((portsc & UDC_PORTSC_SPEED_MASK) == UDC_PORTSC_SPEED_HS) {
                speed = TUSB_SPEED_HIGH;
            }
            udc_diag.bus_resets++;
            // Abandon any in-flight bulk / interrupt OUT transfers
            // (TinyUSB will re-queue from cdcd_reset after re-enumeration
            // but only if we explicitly tell it the previous transfer
            // failed).  The Rust HAL boot1 reference does not do this
            // because its CDC implementation does not sit between an OS-
            // style host stack and the DCD, but TinyUSB's bulk-EP
            // accounting needs the XFER_RESULT_FAILED event to unwind
            // the perpetually-armed _prep_out_transaction TRB.  Mirrors
            // the documented expectation in TinyUSB's other DCDs (e.g.
            // synopsys/dwc2 dcd_synopsys.c which reports xfer complete
            // with FAILED result on bus reset).
            for (uint32_t pei = 2; pei < UDC_PEI_COUNT; pei++) {
                if (udc_state.ep_user_buf[pei] != NULL
                    || udc_state.ep_user_len[pei] != 0) {
                    uint8_t ep_num = (uint8_t)(pei / 2u);
                    uint8_t ep_addr = ep_num | (((pei & 1u) == 0u) ? 0x80u : 0u);
                    dcd_event_xfer_complete(0, ep_addr, 0,
                        XFER_RESULT_FAILED, true);
                    udc_state.ep_user_buf[pei] = NULL;
                    udc_state.ep_user_len[pei] = 0;
                }
            }
            dcd_event_bus_reset(0, speed, true);
        }
    }

    // Re-arm SETUP event reporting after every port status event.
    // The Rust reference does this unconditionally
    // (driver.rs:2925 in EventPortStatusChange handler).  The bit
    // appears to be auto-cleared by the controller; without re-arming
    // here, SETUP packets after the first bus reset go unreported and
    // enumeration stalls.
    uint32_t evtcfg = udc_dev_read(UDC_EVENTCONFIG_OFFSET);
    udc_dev_write(UDC_EVENTCONFIG_OFFSET, evtcfg | UDC_EVENTCONFIG_SETUP);
    memory_fence();
}

// Advance the dequeue pointer one TRB, wrapping and toggling ccs at
// the segment boundary.  Mirrors process_event_ring in driver.rs.
static void udc_advance_event_dq(void) {
    udc_trb_t *next = udc_state.event_dq + 1;
    if (next >= udc_state.event_ring_end) {
        udc_state.event_dq = udc_state.event_ring_base;
        udc_state.ccs ^= 1;
    } else {
        udc_state.event_dq = next;
    }
}

void dcd_int_handler(uint8_t rhport) {
    (void)rhport;

    udc_diag.irq_count++;

    if (!udc_state.initialised) {
        return;
    }

    // Surface system errors first; if SYSTEM_ERR is latched the
    // controller has wedged and we have nothing useful to do beyond
    // clearing the latch so further events can be reported.
    uint32_t status = udc_dev_read(UDC_USBSTS_OFFSET);
    udc_diag.last_usbsts = status;
    udc_diag.last_portsc = udc_dev_read(UDC_PORTSC_OFFSET);
    if (status & UDC_USBSTS_SYSTEM_ERR) {
        udc_dev_write(UDC_USBSTS_OFFSET, UDC_USBSTS_SYSTEM_ERR);
        memory_fence();
        return;
    }

    // Acknowledge the event interrupt (W1C EINT).  This must happen
    // before draining the ring; otherwise USBSTS.EINT stays set and
    // the controller will not raise a new edge.
    if (status & UDC_USBSTS_EINT) {
        udc_dev_write(UDC_USBSTS_OFFSET, UDC_USBSTS_EINT);
        memory_fence();
    }

    // Clear IMAN.IP (W1C) so the next interrupter edge is observable.
    uint32_t iman = udc_uicr_read(UDC_IMAN_OFFSET);
    udc_uicr_write(UDC_IMAN_OFFSET, iman | UDC_IMAN_IP);
    memory_fence();

    // OUTER loop: re-check the event ring after the ERDP write below.
    // The wedge-time evt_ring dump shows the controller has events
    // (incl the SETUP we're waiting for) WITH MATCHING cycle bit
    // sitting in the ring, but a new IRQ never fires.  Hypothesis:
    // the controller wrote events after our last "drain && ERDP write"
    // but before the ERDP-write-flushed the EHB bit -- so the controller
    // never raised a fresh IP edge for them.  This outer loop catches
    // that case: if the ERDP write was raced, the next event in the
    // ring already has cycle == ccs and we drain it in the next outer
    // pass.  Bounded to avoid an infinite-loop if events arrive
    // faster than we can process them.
    int outer_attempts = 0;
    while (outer_attempts++ < 4) {

        // Drain every event whose cycle bit matches our expected ccs.
        for (;;) {
            udc_trb_t *event = udc_state.event_dq;
            uint32_t dw3 = event->dw3;
            uint8_t event_ccs = (uint8_t)(dw3 & UDC_EVENT_TRB_DW3_CYCLE_BIT);
            if (event_ccs != udc_state.ccs) {
                break;
            }

            uint32_t type = event_trb_type(event);
            switch (type) {
                case UDC_TRB_EVENT_PORT_STATUS:
                    udc_diag.port_status_events++;
                    udc_handle_port_status();
                    break;

                case UDC_TRB_EVENT_CMD_COMPLETION:
                    // No outstanding commands at this phase; consume
                    // and drop.
                    break;

                case UDC_TRB_EVENT_TRANSFER: {
                    udc_diag.xfer_complete_events++;
                    uint32_t pei = event_trb_pei(event);
                    uint32_t compl_code = event_trb_compl_code(event);
                    uint32_t residual = event_trb_residual(event);
                    if (pei >= 2 && pei < UDC_PEI_COUNT &&
                        (compl_code == UDC_COMPL_SUCCESS ||
                         compl_code == UDC_COMPL_SHORT_PACKET)) {
                        /* Bulk / interrupt completion.  Decode the USB
                         * endpoint address from PEI: even PEI is USB IN
                         * (0x80 | ep_num), odd PEI is USB OUT. */
                        uint8_t ep_num = (uint8_t)(pei / 2u);
                        uint8_t ep_addr = ep_num;
                        if ((pei & 1u) == 0u) {
                            ep_addr |= 0x80u;
                        }
                        uint16_t stash_len = udc_state.ep_user_len[pei];
                        uint32_t xferred = (stash_len >= residual)
                        ? ((uint32_t)stash_len - residual) : 0;

                        /* For USB OUT, copy from the staging buffer
                         * into the TinyUSB-owned user buffer. */
                        if ((pei & 1u) && xferred > 0
                            && udc_state.ep_user_buf[pei] != NULL) {
                            uintptr_t stage = udc_ep_stage_addr(pei);
                            memcpy(udc_state.ep_user_buf[pei],
                                (void *)stage, xferred);
                        }
                        udc_state.ep_user_buf[pei] = NULL;
                        udc_state.ep_user_len[pei] = 0;

                        dcd_event_xfer_complete(0, ep_addr, xferred,
                            XFER_RESULT_SUCCESS, true);
                        break;
                    }
                    if (pei <= 1) {
                        if (compl_code != UDC_COMPL_SUCCESS &&
                            compl_code != UDC_COMPL_SHORT_PACKET) {
                            udc_diag.err_count++;
                            udc_trb_t *src_e = (udc_trb_t *)(uintptr_t)event->dw0;
                            if (src_e != NULL) {
                                udc_diag.err_src_dw3 = src_e->dw3;
                                udc_diag.err_src_type =
                                    (src_e->dw3 & UDC_TRB_DW3_TYPE_MASK) >> UDC_TRB_DW3_TYPE_SHIFT;
                                udc_diag.err_src_dir =
                                    (src_e->dw3 & UDC_TRB_DW3_DIR) ? 1u : 0u;
                            }
                            for (int i = 0; i < 8; i++) {
                                udc_diag.err_setup[i] = udc_state.setup_packet[i];
                            }
                            udc_diag.err_setup_tag = udc_state.setup_tag;
                            udc_diag.err_seen = 1;
                        }
                    }
                    if (pei <= 1 && (compl_code == UDC_COMPL_SUCCESS ||
                                     compl_code == UDC_COMPL_SHORT_PACKET)) {
                        /* EP0 transfer completion.  The Corigine controller
                         * reports ALL EP0 events at PEI=0 regardless of
                         * data/status direction (per the bao1x-hal Rust
                         * reference, driver.rs:2944 only handles `pei == 0`
                         * for EP0; no events ever fire at PEI=1 for EP0).
                         * To determine what completed, decode the source
                         * TRB pointed at by event->dw0: TYPE field
                         * distinguishes DATA_STAGE vs STATUS_STAGE and
                         * DIR field distinguishes IN vs OUT.
                         *
                         * Total transferred is residual subtracted from
                         * the original TRB's transfer length. */
                        udc_trb_t *src = (udc_trb_t *)(uintptr_t)event->dw0;
                        uint32_t src_type = 0;
                        uint32_t src_dir = 0;
                        uint32_t total = 0;
                        if (src != NULL) {
                            src_type = (src->dw3 & UDC_TRB_DW3_TYPE_MASK) >> UDC_TRB_DW3_TYPE_SHIFT;
                            src_dir = (src->dw3 & UDC_TRB_DW3_DIR) ? 1u : 0u;
                            total = src->dw2 & UDC_TRB_DW2_TRANSFER_LEN_MASK;
                        }
                        uint32_t xferred = (total >= residual) ? (total - residual) : 0;

                        /* src_dir==1 means OUT (host-to-device) per
                         * Corigine's DIR bit. */
                        bool is_status = (src_type == UDC_TRB_STATUS_STAGE);
                        bool is_data = (src_type == UDC_TRB_DATA_STAGE);
                        bool out_dir = (src_dir != 0);

                        if (is_data && out_dir) {
                            /* OUT data stage just completed.  Copy stage
                             * buffer -> TinyUSB user buffer, report
                             * ep_addr=0x00 (OUT) data complete. */
                            if (xferred > 0 && udc_state.ep0_out_user_buf != NULL) {
                                uint32_t copy = xferred;
                                if (copy > udc_state.ep0_out_len) {
                                    copy = udc_state.ep0_out_len;
                                }
                                uintptr_t stage = (uintptr_t)udc_dma_region + UDC_EP0_BUF_OFFSET_IN_BUF;
                                memcpy(udc_state.ep0_out_user_buf, (void *)stage, copy);
                                udc_state.ep0_out_user_buf = NULL;
                                udc_state.ep0_out_len = 0;
                            }
                            dcd_event_xfer_complete(0, 0x00u, xferred, XFER_RESULT_SUCCESS, true);
                        } else if (is_data && !out_dir) {
                            /* IN data stage completed.  TinyUSB just needs
                             * the byte count. */
                            dcd_event_xfer_complete(0, 0x80u, xferred, XFER_RESULT_SUCCESS, true);
                        } else if (is_status) {
                            /* Status stage completion.  Report zero-length
                             * in the direction opposite to the data stage
                             * (IN status for OUT-data, OUT status for
                             * IN-data). */
                            dcd_event_xfer_complete(0, out_dir ? 0x00u : 0x80u, 0,
                                XFER_RESULT_SUCCESS, true);
                        }
                        /* else: unexpected TRB type at EP0; leave silent
                         * so the event ring keeps draining. */
                    }
                    break;
                }

                case UDC_TRB_EVENT_MFINDEX_WRAP:
                    // Not interesting for the current bring-up.
                    break;

                case UDC_TRB_SETUP_PKT: {
                    udc_diag.setup_packets++;
                    /* SETUP TRBs carry the raw 8-byte SETUP packet in
                     * dw0+dw1 (little-endian).  The 2-bit setup tag lives
                     * in dw3[22:21] and must be echoed in the matching
                     * DATA/STATUS stage TRBs. */
                    uint32_t dw0 = event->dw0;
                    uint32_t dw1 = event->dw1;
                    udc_state.setup_packet[0] = (uint8_t)(dw0);
                    udc_state.setup_packet[1] = (uint8_t)(dw0 >> 8);
                    udc_state.setup_packet[2] = (uint8_t)(dw0 >> 16);
                    udc_state.setup_packet[3] = (uint8_t)(dw0 >> 24);
                    udc_state.setup_packet[4] = (uint8_t)(dw1);
                    udc_state.setup_packet[5] = (uint8_t)(dw1 >> 8);
                    udc_state.setup_packet[6] = (uint8_t)(dw1 >> 16);
                    udc_state.setup_packet[7] = (uint8_t)(dw1 >> 24);
                    udc_state.setup_tag = (uint8_t)(
                        (dw3 & UDC_EVENT_TRB_DW3_SETUP_TAG_MASK) >> UDC_EVENT_TRB_DW3_SETUP_TAG_SHIFT);
                    /* Any previous OUT staging is invalidated by a new
                     * SETUP; the host has restarted the control transfer. */
                    udc_state.ep0_out_user_buf = NULL;
                    udc_state.ep0_out_len = 0;
                    /* Keep ep0_enq advancing across transfers; the
                     * Corigine controller does not reset its EP0 TR
                     * dequeue on SETUP, and resetting our side breaks
                     * enumeration. */
                    dcd_event_setup_received(0, udc_state.setup_packet, true);
                    break;
                }

                default:
                    // Unknown event type; ignore so the ring keeps draining.
                    break;
            }

            udc_advance_event_dq();
        }

        // Publish the updated ERDP back to hardware with EHB set so the
        // controller knows we consumed up to this point.
        uint32_t dq = (uint32_t)(uintptr_t)udc_state.event_dq;
        udc_uicr_write(UDC_ERDPHI_OFFSET, 0);
        udc_uicr_write(UDC_ERDPLO_OFFSET,
            (dq & UDC_ERDPLO_DQ_PTR_MASK) | UDC_ERDPLO_EHB);
        memory_fence();

        // Outer loop tail: check if more events landed in the ring while
        // we were processing.  Peek at the current DQ slot; if its cycle
        // bit matches ours, more events arrived and we need to re-drain
        // (otherwise the controller might not raise a fresh IRQ for them).
        {
            udc_trb_t *next_evt = udc_state.event_dq;
            uint8_t next_ccs = (uint8_t)(next_evt->dw3 & UDC_EVENT_TRB_DW3_CYCLE_BIT);
            if (next_ccs != udc_state.ccs) {
                break; // no more pending events, exit outer loop
            }
            // Also clear EINT and IMAN.IP again before another pass.
            if (udc_dev_read(UDC_USBSTS_OFFSET) & UDC_USBSTS_EINT) {
                udc_dev_write(UDC_USBSTS_OFFSET, UDC_USBSTS_EINT);
            }
            uint32_t iman2 = udc_uicr_read(UDC_IMAN_OFFSET);
            udc_uicr_write(UDC_IMAN_OFFSET, iman2 | UDC_IMAN_IP);
            memory_fence();
        }
    }  // end outer while loop
}

/* ------------------------------------------------------------------ */
/* Remaining DCD callbacks                                             */
/* ------------------------------------------------------------------ */

void dcd_set_address(uint8_t rhport, uint8_t dev_addr) {
    (void)rhport;
    /* Unlike DWC2-style controllers, the Corigine UDC does NOT snoop
     * SET_ADDRESS off the wire.  We must explicitly issue the SET_ADDR
     * command then enqueue the EP0 IN status stage with the
     * STATUS_STAGE_SET_ADDR bit so the controller switches address only
     * after the host has ACKed the status stage. */
    udc_state.dev_addr = dev_addr;
    (void)udc_issue_command(UDC_CMD_SET_ADDR, (uint32_t)dev_addr & 0xFFu, 0);
    udc_state.set_addr_pending = 1;
    /* Enqueue the status stage on EP0 IN (no data stage). */
    (void)dcd_edpt_xfer(0, 0x80u, NULL, 0);
}

void dcd_remote_wakeup(uint8_t rhport) {
    (void)rhport;
}

void dcd_connect(uint8_t rhport) {
    (void)rhport;
}

void dcd_disconnect(uint8_t rhport) {
    dcd_corigine_teardown(rhport);
}

void dcd_sof_enable(uint8_t rhport, bool en) {
    (void)rhport;
    (void)en;
}

/* ------------------------------------------------------------------ */
/* Bulk / interrupt endpoint helpers                                   */
/* ------------------------------------------------------------------ */

/* Map a TinyUSB endpoint descriptor's transfer type + USB direction to
 * the Corigine EPCX ep-type encoding.  Corigine names the device-to-
 * host direction "outbound" and host-to-device "inbound"; the inbound
 * variant of each ep type sits at base + 4.  See udc_ep_type_t. */
static udc_ep_type_t udc_eptype_for(uint8_t usb_xfer_type, uint8_t usb_dir) {
    /* usb_xfer_type: 0=control, 1=iso, 2=bulk, 3=interrupt. */
    udc_ep_type_t base;
    switch (usb_xfer_type) {
        case TUSB_XFER_BULK:
            base = UDC_EPTYPE_BULK_OUTBOUND;
            break;
        case TUSB_XFER_INTERRUPT:
            base = UDC_EPTYPE_INTR_OUTBOUND;
            break;
        case TUSB_XFER_ISOCHRONOUS:
            base = UDC_EPTYPE_ISOCH_OUTBOUND;
            break;
        default:
            return UDC_EPTYPE_CONTROL_OR_INVALID;
    }
    if (usb_dir == UDC_DIR_OUT) {
        /* USB OUT (host-to-device) == Corigine inbound; +4 to base. */
        return (udc_ep_type_t)((uint32_t)base + 4u);
    }
    return base;
}

/* Advance enqueue pointer for a non-EP0 endpoint, wrapping at the LINK
 * TRB.  Mirrors udc_ep0_advance_enq. */
static void udc_ep_advance_enq(uint32_t pei) {
    udc_state.ep_enq[pei]++;
    if (udc_state.ep_enq[pei] == udc_state.ep_link[pei]) {
        udc_trb_t *link = udc_state.ep_link[pei];
        uint32_t dw3 = link->dw3 & ~UDC_TRB_DW3_CYCLE_BIT;
        if (udc_state.ep_pcs[pei]) {
            dw3 |= UDC_TRB_DW3_CYCLE_BIT;
        }
        link->dw3 = dw3;
        memory_fence();
        udc_state.ep_pcs[pei] ^= 1;
        udc_state.ep_enq[pei] = udc_state.ep_tr_base[pei];
    }
}

bool dcd_edpt_open(uint8_t rhport, const tusb_desc_endpoint_t *desc_ep) {
    (void)rhport;

    uint8_t ep_num = desc_ep->bEndpointAddress & 0x0Fu;
    if (ep_num == 0 || ep_num >= UDC_EP_COUNT) {
        return false;
    }
    uint8_t usb_dir = (desc_ep->bEndpointAddress & 0x80u) ? UDC_DIR_IN : UDC_DIR_OUT;
    uint32_t pei = UDC_PEI(ep_num, usb_dir);
    if (pei >= UDC_PEI_COUNT) {
        return false;
    }

    uint16_t mps = (uint16_t)(desc_ep->wMaxPacketSize & 0x7FFu);
    uint8_t usb_xfer_type = desc_ep->bmAttributes.xfer;
    udc_ep_type_t ep_type = udc_eptype_for(usb_xfer_type, usb_dir);

    /* Allocate the per-PEI transfer ring.  Layout matches the Rust
     * reference: ring slot 0 is at UDC_EP_TR_OFFSET_IN_BUF + (pei - 2)
     * * UDC_TD_RING_LEN * 16.  PEI 0/1 are EP0 and use a different
     * ring (UDC_EP0_TR_OFFSET_IN_BUF). */
    uintptr_t base = (uintptr_t)udc_dma_region;
    size_t ring_bytes = (size_t)UDC_TD_RING_LEN * sizeof(udc_trb_t);
    udc_trb_t *ring = (udc_trb_t *)(base + UDC_EP_TR_OFFSET_IN_BUF + (pei - 2u) * ring_bytes);

    memset(ring, 0, ring_bytes);
    udc_trb_t *link = &ring[UDC_TD_RING_LEN - 1];
    link->dw0 = (uint32_t)(uintptr_t)ring & 0xFFFFFFF0u;
    link->dw1 = 0;
    link->dw2 = 0;
    link->dw3 = (((uint32_t)UDC_TRB_LINK << UDC_TRB_DW3_TYPE_SHIFT) & UDC_TRB_DW3_TYPE_MASK)
        | UDC_TRB_DW3_LINK_TOGGLE_CYCLE;
    memory_fence();

    udc_state.ep_tr_base[pei] = ring;
    udc_state.ep_link[pei] = link;
    udc_state.ep_enq[pei] = ring;
    udc_state.ep_pcs[pei] = 1;
    udc_state.ep_user_buf[pei] = NULL;
    udc_state.ep_user_len[pei] = 0;
    udc_state.ep_mps[pei] = mps;

    /* Program the EPCX entry.  CRITICAL: the EPCX array is indexed
     * starting at PEI 2 (boot1's `p_epcx.add(pei - 2)` pattern in
     * bao1x-hal driver.rs:2034).  PEI 0 and 1 are owned by EP0 and
     * are NOT in this array -- the chip configures them via the
     * INIT_EP0 command instead.  Indexing here by raw PEI caused
     * the chip to read garbage EP context, so SET_CONFIG couldn't
     * bring up bulk endpoints cleanly and the host timed out. */
    udc_epcx_t *epcx_array = (udc_epcx_t *)(base + UDC_EPCX_OFFSET_IN_BUF);
    udc_epcx_t *epcx = &epcx_array[pei - 2u];
    epcx->dw0 = ((uint32_t)ep_num << UDC_EPCX_DW0_EP_NUM_SHIFT) & UDC_EPCX_DW0_EP_NUM_MASK;
    /* CRG_UDC_MAX_BURST=15 per bao1x-hal driver.rs:44.  Programming a
     * non-zero max_burst is REQUIRED on this Corigine UDC: an EPCX with
     * max_burst=0 will accept TRBs onto the ring (so EPRUNNING reads as
     * set) but the per-EP scheduler issues no IN packets to the host.
     * After a few churn cycles the host-perceived bulk OUT stops being
     * ACKed while the firmware sees no fault.  Rust HAL sets this on
     * every ep_enable (driver.rs:665). */
    epcx->dw1 = (((uint32_t)ep_type << UDC_EPCX_DW1_EP_TYPE_SHIFT) & UDC_EPCX_DW1_EP_TYPE_MASK)
        | ((15u << UDC_EPCX_DW1_MAX_BURST_SHIFT) & UDC_EPCX_DW1_MAX_BURST_MASK)
        | (((uint32_t)mps << UDC_EPCX_DW1_MPS_SHIFT) & UDC_EPCX_DW1_MPS_MASK);
    epcx->dw2 = ((uint32_t)(uintptr_t)ring & UDC_EPCX_DW2_DEQ_PTR_LO_MASK) | UDC_EPCX_DW2_DCS;
    epcx->dw3 = 0;
    memory_fence();

    /* The Rust reference passes a one-hot mask (1 << pei) as CMDPARA0
     * to ConfigEp / StopEp / SetHalt / ClearHalt rather than the raw
     * PEI value.  Track the proven convention rather than the spec
     * text. */
    (void)udc_issue_command(UDC_CMD_CONFIG_EP, 1u << pei, 0);

    /* CRITICAL: wait for EPENABLE bit to be set by the controller
     * before returning.  TinyUSB queues the SET_CONFIG status stage
     * immediately after we return; if the EP isn't actually enabled
     * yet, the status stage races with the controller's internal
     * configuration and the host sees the SET_CONFIG time out.
     * boot1's ep_enable does the same wait (driver.rs:2051-2061).
     * Bounded with 50 ms timeout so a hardware failure doesn't hang
     * the IRQ context. */
    {
        uint32_t start = tusb_time_millis_api();
        while ((REG32(UDC_DEV_BASE + UDC_EPENABLE_OFFSET) & (1u << pei)) == 0) {
            if ((tusb_time_millis_api() - start) > 50u) {
                TU_LOG1("dcd_corigine: EPENABLE wait timeout for PEI %u\n",
                    (unsigned)pei);
                break;
            }
        }
    }
    return true;
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr) {
    (void)rhport;
    uint8_t ep_num = ep_addr & 0x0Fu;
    if (ep_num == 0 || ep_num >= UDC_EP_COUNT) {
        return;
    }
    uint8_t usb_dir = (ep_addr & 0x80u) ? UDC_DIR_IN : UDC_DIR_OUT;
    uint32_t pei = UDC_PEI(ep_num, usb_dir);
    if (pei >= UDC_PEI_COUNT) {
        return;
    }
    uint32_t param0 = 1u << pei;

    /* Mirror bao1x-hal driver.rs:2066 (ep_disable) exactly.  Only issue
     * STOP_EP if the endpoint is still running, and poll EPRUNNING
     * back to 0 before tearing down the EPCX entry -- otherwise the
     * controller may be mid-fetch of the EPCX we're about to wipe.
     * The previous implementation issued STOP_EP unconditionally plus
     * a follow-up RESET_EP; the Rust HAL does neither RESET_EP nor an
     * unconditional STOP_EP, and the extra RESET_EP appears to have
     * been a churn-cycle wedge trigger. */
    if (udc_dev_read(UDC_EPRUNNING_OFFSET) & param0) {
        (void)udc_issue_command(UDC_CMD_STOP_EP, param0, 0);
        uint32_t start = tusb_time_millis_api();
        while (udc_dev_read(UDC_EPRUNNING_OFFSET) & param0) {
            if ((tusb_time_millis_api() - start) > 50u) {
                break;
            }
        }
    }

    /* Zero the entire EPCX entry, not just the type field.  Leaving
     * dw0/dw2/dw3 stale (deq_ptr in particular) lets the controller's
     * per-EP scheduler pick up an obsolete TRB pointer on a later
     * re-open, which manifests as bulk OUT silently stopping after a
     * few churn cycles even though EPRUNNING remains set.  EPCX array
     * is indexed by (pei - 2); see comment in dcd_edpt_open. */
    udc_epcx_t *epcx_array =
        (udc_epcx_t *)((uintptr_t)udc_dma_region + UDC_EPCX_OFFSET_IN_BUF);
    udc_epcx_t *epcx = &epcx_array[pei - 2u];
    memset(epcx, 0, sizeof(*epcx));
    memory_fence();

    /* Disable the EP at the controller level.  EPENABLE is W1-to-disable
     * on this Corigine UDC variant (Rust HAL driver.rs:2086 writes
     * `wo(EPENABLE, 1 << pei)` to disable -- the surrounding context
     * confirms it's not a register-wide overwrite). */
    udc_dev_write(UDC_EPENABLE_OFFSET, param0);
    memory_fence();

    udc_state.ep_enq[pei] = udc_state.ep_tr_base[pei];
    udc_state.ep_pcs[pei] = 1;
    udc_state.ep_user_buf[pei] = NULL;
    udc_state.ep_user_len[pei] = 0;
}

void dcd_edpt_close_all(uint8_t rhport) {
    (void)rhport;
    /* PEI 2..UDC_PEI_COUNT-1 covers every non-EP0 endpoint. */
    for (uint8_t ep_num = 1; ep_num < UDC_EP_COUNT; ep_num++) {
        dcd_edpt_close(rhport, (uint8_t)(ep_num | 0x80u));
        dcd_edpt_close(rhport, ep_num);
    }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes) {
    (void)rhport;

    uint8_t ep_num = ep_addr & 0x0Fu;
    uint8_t usb_dir_e = (ep_addr & 0x80u) ? UDC_DIR_IN : UDC_DIR_OUT;

    if (ep_num != 0) {
        /* Bulk / interrupt endpoint path.  One TRB per call; multi-TRB
         * chaining is a future optimisation. */
        if (ep_num >= UDC_EP_COUNT) {
            return false;
        }
        uint32_t pei = UDC_PEI(ep_num, usb_dir_e);
        if (pei >= UDC_PEI_COUNT || udc_state.ep_tr_base[pei] == NULL) {
            return false;
        }

        uintptr_t stage = udc_ep_stage_addr(pei);
        uint16_t len = total_bytes;
        if (len > UDC_APP_BUF_LEN) {
            len = UDC_APP_BUF_LEN;
        }

        if (usb_dir_e == UDC_DIR_IN) {
            if (buffer != NULL && len > 0) {
                memcpy((void *)stage, buffer, len);
            }
            udc_state.ep_user_buf[pei] = NULL;
            udc_state.ep_user_len[pei] = len;
        } else {
            /* USB OUT: receive into the staging buffer; copy out to
             * the user buffer on completion. */
            udc_state.ep_user_buf[pei] = buffer;
            udc_state.ep_user_len[pei] = len;
        }

        udc_trb_t *trb = udc_state.ep_enq[pei];
        trb->dw0 = (uint32_t)stage;
        trb->dw1 = 0;
        /* td_size=1 mirrors bao1x-hal driver.rs:2226 (`TD_SIZE` const).
         * Without it, the Corigine UDC's per-EP scheduler appears to
         * stall on the bulk OUT TRB whose IRQ-completion would have
         * advanced the host's bulk-OUT pipe -- the wedge bisection
         * shows that exactly one bulk OUT doorbell goes un-completed
         * per churn, and the only DW2 field divergence vs Rust is this. */
        trb->dw2 = ((uint32_t)len & UDC_TRB_DW2_TRANSFER_LEN_MASK)
            | ((1u << UDC_TRB_DW2_TD_SIZE_SHIFT) & UDC_TRB_DW2_TD_SIZE_MASK);
        uint32_t dw3 = 0;
        if (udc_state.ep_pcs[pei]) {
            dw3 |= UDC_TRB_DW3_CYCLE_BIT;
        }
        dw3 |= UDC_TRB_DW3_IOC;
        /* Do NOT set INTR_ON_SHORT_PKT on bulk OUT TRBs.  An earlier
         * revision set it for OUT to surface short reads early, but Rust
         * HAL bulk_xfer leaves ISP=0 on bulk and a working baremetal
         * baseline shows ISP is what TinyUSB expects.  ISP=1 on bulk
         * OUT may interact badly with the Corigine scheduler on the
         * last TRB of a churn cycle (only mismatch vs Rust on the bulk
         * path that survives the diff). */
        dw3 |= ((uint32_t)UDC_TRB_XFER_NORMAL << UDC_TRB_DW3_TYPE_SHIFT) & UDC_TRB_DW3_TYPE_MASK;
        /* The Rust reference does NOT program DIR for XferNormal TRBs
         * (driver.rs prepare_transfer_trb only sets DIR under
         * b_setup_stage).  Direction is carried by the EPCX. */
        trb->dw3 = dw3;
        memory_fence();

        udc_ep_advance_enq(pei);
        udc_doorbell((uint8_t)pei);
        return true;
    }

    /* EP0 control transfer.  TinyUSB drives the data and status stages
     * as separate dcd_edpt_xfer calls, AND chunks the data stage by
     * CFG_TUD_ENDPOINT0_SIZE (usbd_control.c: a 75-byte config
     * descriptor arrives as dcd_edpt_xfer(IN,64) then
     * dcd_edpt_xfer(IN,11), then dcd_edpt_xfer(OUT,0) for status).
     *
     * Data stage (either direction): queue ONE data TRB per chunk and
     * nothing else.  The data TRB is IOC=true, so the controller raises
     * a TRANSFER event per chunk; the handler reports it to TinyUSB,
     * which re-calls for the next chunk (IN/OUT data is chunked by
     * CFG_TUD_ENDPOINT0_SIZE) and finally makes an explicit
     * zero-length call for the STATUS stage.  This matches the xHCI
     * control TD model (SETUP + N*DATA + STATUS).  Pre-queuing the
     * status alongside the first data chunk -- which an earlier
     * revision did -- completes the control transfer prematurely and
     * makes the next chunk's status hit PROTOCOL_STALL (192).
     *
     * Status-only transfers (SET_ADDRESS, SET_CONFIG) and the status
     * stage of a data transfer take the same len==0 path: queue one
     * STATUS TRB. */
    uint8_t usb_dir = (ep_addr & 0x80u) ? UDC_DIR_IN : UDC_DIR_OUT;
    uintptr_t stage_base = (uintptr_t)udc_dma_region + UDC_EP0_BUF_OFFSET_IN_BUF;

    if (total_bytes > 0) {
        /* Data stage chunk: queue one data TRB. */
        if (usb_dir == UDC_DIR_IN) {
            if (buffer != NULL) {
                memcpy((void *)stage_base, buffer, total_bytes);
            }
            udc_state.ep0_out_user_buf = NULL;
            udc_state.ep0_out_len = 0;
        } else {
            /* OUT data lands in the EP0 staging buffer; copy out to the
             * TinyUSB-owned buffer when the data event completes. */
            udc_state.ep0_out_user_buf = buffer;
            udc_state.ep0_out_len = total_bytes;
        }
        udc_ep0_queue_data_trb((uint32_t)stage_base, total_bytes, usb_dir);
    } else {
        /* Status stage (no data). */
        uint8_t set_addr = 0;
        if (usb_dir == UDC_DIR_IN && udc_state.set_addr_pending) {
            set_addr = 1;
            udc_state.set_addr_pending = 0;
        }
        udc_ep0_queue_status_trb(usb_dir, set_addr);
    }

    /* Doorbell is always rung on PEI 0 (EP0 IN); the controller walks
     * the EP0 transfer ring from that doorbell regardless of the
     * direction encoded in the TRBs.  Matches Rust's knock_doorbell(0)
     * after every EP0 enqueue. */
    udc_doorbell(UDC_PEI(0, UDC_DIR_IN));
    return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
    (void)rhport;
    uint8_t ep_num = ep_addr & 0x0Fu;
    if (ep_num >= UDC_EP_COUNT) {
        return;
    }
    uint8_t usb_dir = (ep_addr & 0x80u) ? UDC_DIR_IN : UDC_DIR_OUT;
    uint32_t pei = UDC_PEI(ep_num, usb_dir);
    if (pei >= UDC_PEI_COUNT) {
        return;
    }
    (void)udc_issue_command(UDC_CMD_SET_HALT, 1u << pei, 0);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
    (void)rhport;
    uint8_t ep_num = ep_addr & 0x0Fu;
    if (ep_num >= UDC_EP_COUNT) {
        return;
    }
    uint8_t usb_dir = (ep_addr & 0x80u) ? UDC_DIR_IN : UDC_DIR_OUT;
    uint32_t pei = UDC_PEI(ep_num, usb_dir);
    if (pei >= UDC_PEI_COUNT) {
        return;
    }
    (void)udc_issue_command(UDC_CMD_CLEAR_HALT, 1u << pei, 0);
}

#endif // CFG_TUD_ENABLED && TUP_USBIP_CORIGINE_UDC
