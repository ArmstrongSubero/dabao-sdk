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

#ifndef TUSB_DCD_CORIGINE_UDC_H
#define TUSB_DCD_CORIGINE_UDC_H

#include <stdint.h>

// Workaround for a Corigine UDC hardware bug: occasionally the IRQ chain
// stops delivering edges while the controller continues writing events to the
// event ring.  At wedge time: USBSTS.EINT=1, IMAN.IE=1, IMAN.IP=0 (NOT
// asserting), EV_PENDING=0.  This function reads USBSTS.EINT and, if set,
// runs dcd_int_handler() directly to drain pending events.
//
// Wire into the port's event pump (e.g. MICROPY_INTERNAL_EVENT_HOOK for
// MicroPython).  In the healthy path this is a single register read.
void dcd_corigine_poll_pending_events(uint8_t rhport);

// Run a full Corigine UDC controller re-bring-up from task context.  Masks
// the interrupt, quiesces the controller, drives the board SE0 pin (via
// TUD_CORIGINE_SE0_ASSERT/DEASSERT macros), applies PHY magic, programs all
// descriptor structures, then restarts.  Blocks for ~0.5 s.
//
// The port is responsible for scheduling this call in task context (not ISR)
// and for driving any deferred-reinit state machine on its side.
void dcd_corigine_reinit(uint8_t rhport);

// Halt the Corigine UDC and zero the IFRAM working set prior to a chip-level
// reset.  Masks the IRQ, halts the controller, drives the board SE0 pin via
// TUD_CORIGINE_SE0_ASSERT() to signal a wire-level disconnect (and to hold
// the PROG/boot-strap pin), waits, soft-resets the UDC, then zeros IFRAM.
// SE0 is left asserted on return; the caller is responsible for the
// subsequent chip reset.
void dcd_corigine_teardown(uint8_t rhport);

#endif // TUSB_DCD_CORIGINE_UDC_H
