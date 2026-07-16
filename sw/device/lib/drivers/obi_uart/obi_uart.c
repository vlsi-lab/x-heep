// Copyright 2022 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Driver for pulp-platform's obi_uart / obi_uartTMR, adapted from Croc's
// sw-lib (Nils Wistoff, Paul Scheffler) to X-HEEP's mmio_region_t/error.h
// conventions.

#ifdef __cplusplus
extern "C" {
#endif

#include "obi_uart.h"

#include <stddef.h>
#include <stdint.h>

#include "mmio.h"
#include "error.h"

#define OBI_UART_DIVISOR(freq, baud) ((freq) / ((baud) << 4))

system_error_t obi_uart_init(const obi_uart_t *uart) {
  if (uart == NULL) {
    return kErrorUartInvalidArgument;
  }

  if (uart->baudrate == 0 || uart->clk_freq_hz == 0) {
    return kErrorUartInvalidArgument;
  }

  uint16_t divisor = (uint16_t)OBI_UART_DIVISOR(uart->clk_freq_hz, uart->baudrate);
  // obi_uart_baudgen.sv computes its internal reload value as
  // {DLM,DLL} - 1 and treats that value being 0 (i.e. DLL/DLM == 1) as an
  // invalid/unconfigured divisor, permanently gating off the baud clock --
  // the TX FIFO then fills and never drains once its initial capacity is
  // exhausted. 2 is the smallest value the baud generator actually runs
  // with, so clamp to it (halving the effective baud rate at the extreme
  // high end, e.g. when clk_freq_hz/baudrate is close to 16).
  if (divisor < 2) {
    divisor = 2;
  }
  uint8_t dlo = (uint8_t)(divisor);
  uint8_t dhi = (uint8_t)(divisor >> 8);

  // obi_uart's registers are RegWidth=8 but bus-word-aligned (see
  // obi_uart.h) -- like sw/device/lib/drivers/uart, always access them as
  // full 32-bit OBI transactions (be=4'b1111), never sub-word (sb/lb):
  // obi_uart_register.sv only ever reads/drives bits [7:0] of wdata/rdata,
  // so this is safe and matches what the SoC's OBI/CPU LSU path is
  // exercised with everywhere else.
  mmio_region_write32(uart->base_addr, OBI_UART_INTR_ENABLE_REG_OFFSET, 0x00);   // disable interrupts
  mmio_region_write32(uart->base_addr, OBI_UART_LINE_CONTROL_REG_OFFSET, 0x80);  // enable DLAB
  mmio_region_write32(uart->base_addr, OBI_UART_DLAB_LSB_REG_OFFSET, dlo);
  mmio_region_write32(uart->base_addr, OBI_UART_DLAB_MSB_REG_OFFSET, dhi);
  mmio_region_write32(uart->base_addr, OBI_UART_LINE_CONTROL_REG_OFFSET, 0x03);  // 8 bits, no parity, 1 stop bit
  mmio_region_write32(uart->base_addr, OBI_UART_FIFO_CONTROL_REG_OFFSET, 0xC7);  // enable & clear FIFOs
  mmio_region_write32(uart->base_addr, OBI_UART_MODEM_CONTROL_REG_OFFSET, 0x20); // autoflow mode

  return kErrorOk;
}

void obi_uart_loopback_enable(const obi_uart_t *uart) {
  obi_uart_write_flush(uart);
  uint32_t mcr = mmio_region_read32(uart->base_addr, OBI_UART_MODEM_CONTROL_REG_OFFSET);
  mmio_region_write32(uart->base_addr, OBI_UART_MODEM_CONTROL_REG_OFFSET, mcr | (1 << 4));
}

void obi_uart_loopback_disable(const obi_uart_t *uart) {
  obi_uart_write_flush(uart);
  uint32_t mcr = mmio_region_read32(uart->base_addr, OBI_UART_MODEM_CONTROL_REG_OFFSET);
  mmio_region_write32(uart->base_addr, OBI_UART_MODEM_CONTROL_REG_OFFSET, mcr & ~(1 << 4));
}

int obi_uart_read_ready(const obi_uart_t *uart) {
  return mmio_region_read32(uart->base_addr, OBI_UART_LINE_STATUS_REG_OFFSET) &
         (1 << OBI_UART_LINE_STATUS_DATA_READY_BIT);
}

static inline int obi_uart_write_ready(const obi_uart_t *uart) {
  return mmio_region_read32(uart->base_addr, OBI_UART_LINE_STATUS_REG_OFFSET) &
         (1 << OBI_UART_LINE_STATUS_THR_EMPTY_BIT);
}

static inline int obi_uart_write_idle(const obi_uart_t *uart) {
  return obi_uart_write_ready(uart) &&
         (mmio_region_read32(uart->base_addr, OBI_UART_LINE_STATUS_REG_OFFSET) &
          (1 << OBI_UART_LINE_STATUS_TMIT_EMPTY_BIT));
}

void obi_uart_putchar(const obi_uart_t *uart, uint8_t byte) {
  while (!obi_uart_write_ready(uart)) {
  }
  mmio_region_write32(uart->base_addr, OBI_UART_THR_REG_OFFSET, byte);
}

size_t obi_uart_write(const obi_uart_t *uart, const uint8_t *data, size_t len) {
  size_t total = len;
  while (len) {
    obi_uart_putchar(uart, *data);
    data++;
    len--;
  }
  return total;
}

void obi_uart_write_flush(const obi_uart_t *uart) {
  while (!obi_uart_write_idle(uart)) {
  }
}

static uint8_t obi_uart_rx_fifo_read(const obi_uart_t *uart) {
  return (uint8_t)mmio_region_read32(uart->base_addr, OBI_UART_RBR_REG_OFFSET);
}

size_t obi_uart_getchar(const obi_uart_t *uart, uint8_t *data) {
  while (!obi_uart_read_ready(uart)) {
  }
  *data = obi_uart_rx_fifo_read(uart);
  return 1;
}

size_t obi_uart_read(const obi_uart_t *uart, uint8_t *data, size_t len) {
  size_t total = len;
  while (len) {
    obi_uart_getchar(uart, data);
    data++;
    len--;
  }
  return total;
}

size_t obi_uart_sink(void *uart, const char *data, size_t len) {
  return obi_uart_write((const obi_uart_t *)uart, (const uint8_t *)data, len);
}

#ifdef __cplusplus
}
#endif
