#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    UART_OK = 0,
    UART_ERR_PARAM,
    UART_ERR_TIMEOUT,
    UART_ERR_BUSY
} uart_status_t;

typedef struct {
    uint32_t baudrate;
    uint32_t periph_clk_hz;
} uart_config_t;

typedef struct {
    uint32_t overrun;
    uint32_t framing;
    uint32_t noise;
    uint32_t dma_errors;
} uart_stats_t;

uart_status_t uart_init(const uart_config_t *cfg);

/* Starts a DMA transfer and returns immediately. The data is NOT copied:
   the buffer must stay unchanged until uart_tx_busy() returns false. */
uart_status_t uart_write(const uint8_t *data, size_t len);

/* Waits for any previous transfer, then starts this one. */
uart_status_t uart_write_string(const char *str);

bool   uart_tx_busy(void);
void   uart_flush(void);

size_t uart_available(void);
size_t uart_read(uint8_t *buf, size_t max_len);
void   uart_get_stats(uart_stats_t *out);

#endif
