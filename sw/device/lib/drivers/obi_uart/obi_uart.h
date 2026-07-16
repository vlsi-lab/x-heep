// Copyright 2022 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Driver for pulp-platform's obi_uart / obi_uartTMR, adapted from Croc's
// sw-lib (Nils Wistoff, Paul Scheffler) to X-HEEP's mmio_region_t/error.h
// conventions, matching the style of sw/device/lib/drivers/uart (the
// internal OpenTitan UART driver).
//
// This targets the *external* obi_uart instance wired into the testharness
// (see docs/source/reliable_uart_integration_strategy.md), reachable only
// when the internal UART is excluded from the user peripheral domain
// (OBI_UART_EXT_START_ADDRESS, defined in core_v_mini_mcu.h).

#ifndef _DRIVERS_OBI_UART_H_
#define _DRIVERS_OBI_UART_H_

#include <stddef.h>
#include <stdint.h>

#include "mmio.h"
#include "error.h"

#include "core_v_mini_mcu.h"

#ifdef __cplusplus
extern "C" {
#endif

// obi_uart is a 16550-style UART, but unlike a real 16550 its 8 registers
// are 32-bit-word-aligned (RegAlignBytes=4 in obi_uart_pkg.sv), not
// byte-packed — each logical register lives at (index * 4), only the low
// byte is meaningful (RegWidth=8 in obi_uart_pkg.sv; the rest of the OBI
// response word reads back as 0). All accesses use full-word
// mmio_region_{read,write}32 (never the 8-bit variants) so every register
// access is a plain OBI transaction with be=4'b1111 — the same convention
// sw/device/lib/drivers/uart uses, and the one the SoC's OBI/CPU load-store
// path is actually exercised with everywhere else. Aliased by the
// line-control register's DLAB bit for the baud-rate divisor.
#define OBI_UART_RBR_REG_OFFSET 0x00          // Receive Buffer Register (read)
#define OBI_UART_THR_REG_OFFSET 0x00          // Transmitter Holding Register (write)
#define OBI_UART_INTR_ENABLE_REG_OFFSET 0x04
#define OBI_UART_INTR_IDENT_REG_OFFSET 0x08   // read
#define OBI_UART_FIFO_CONTROL_REG_OFFSET 0x08 // write
#define OBI_UART_LINE_CONTROL_REG_OFFSET 0x0C
#define OBI_UART_MODEM_CONTROL_REG_OFFSET 0x10
#define OBI_UART_LINE_STATUS_REG_OFFSET 0x14
#define OBI_UART_MODEM_STATUS_REG_OFFSET 0x18
#define OBI_UART_SCRATCH_REG_OFFSET 0x1C
#define OBI_UART_DLAB_LSB_REG_OFFSET 0x00     // valid when LCR[7] (DLAB) = 1
#define OBI_UART_DLAB_MSB_REG_OFFSET 0x04     // valid when LCR[7] (DLAB) = 1

#define OBI_UART_LINE_STATUS_DATA_READY_BIT 0
#define OBI_UART_LINE_STATUS_THR_EMPTY_BIT 5
#define OBI_UART_LINE_STATUS_TMIT_EMPTY_BIT 6

/**
 * Initialization parameters for the external obi_uart.
 */
typedef struct obi_uart {
  /**
   * The base address for the obi_uart hardware registers.
   */
  mmio_region_t base_addr;
  /**
   * The desired baudrate of the UART.
   */
  uint32_t baudrate;
  /**
   * The peripheral clock frequency (used to compute the baud-rate divisor).
   */
  uint32_t clk_freq_hz;
} obi_uart_t;

/**
 * Initialize the external obi_uart with the requested parameters.
 *
 * @param uart Pointer to obi_uart_t with the requested parameters.
 * @return kErrorOk if successful, else an error code.
 */
system_error_t obi_uart_init(const obi_uart_t *uart);

/**
 * Enable/disable the modem-control loopback bit (MCR[4]). Flushes any
 * in-flight transmission first.
 */
void obi_uart_loopback_enable(const obi_uart_t *uart);
void obi_uart_loopback_disable(const obi_uart_t *uart);

/**
 * @return non-zero if a byte is available to read.
 */
int obi_uart_read_ready(const obi_uart_t *uart);

/**
 * Write a single byte to the UART, blocking until the transmit holding
 * register is free.
 */
void obi_uart_putchar(const obi_uart_t *uart, uint8_t byte);

/**
 * Write `len` bytes to the UART, blocking as needed.
 *
 * @return Number of bytes written.
 */
size_t obi_uart_write(const obi_uart_t *uart, const uint8_t *data, size_t len);

/**
 * Block until the transmitter is fully idle (holding register and shift
 * register both empty).
 */
void obi_uart_write_flush(const obi_uart_t *uart);

/**
 * Read a single byte from the UART, blocking until one is available.
 *
 * @return Always 1.
 */
size_t obi_uart_getchar(const obi_uart_t *uart, uint8_t *data);

/**
 * Read `len` bytes from the UART, blocking as needed.
 *
 * @return Number of bytes read.
 */
size_t obi_uart_read(const obi_uart_t *uart, uint8_t *data, size_t len);

/**
 * Wrapper for obi_uart_write conforming to the type signature required by
 * the print library (see uart_sink in sw/device/lib/drivers/uart).
 */
size_t obi_uart_sink(void *uart, const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // _DRIVERS_OBI_UART_H_
