#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    UART_OK = 0,
    UART_ERR_PARAM,
    UART_ERR_TIMEOUT
} uart_status_t;

typedef struct {
    uint32_t baudrate;
    uint32_t periph_clk_hz;
} uart_config_t;

typedef struct {
    uint32_t rx_dropped;
    uint32_t overrun;
    uint32_t framing;
    uint32_t noise;
} uart_stats_t;

uart_status_t uart_init(const uart_config_t *cfg);

/* Queues as many bytes as fit in the TX buffer and returns immediately. */
size_t uart_write(const uint8_t *data, size_t len);

/* Queues all bytes, waiting only while the TX buffer is full. */
void   uart_write_all(const uint8_t *data, size_t len);
void   uart_write_string(const char *str);

/* Waits until every queued byte has left the TX pin. */
void   uart_flush(void);

size_t uart_available(void);
size_t uart_read(uint8_t *buf, size_t max_len);
void   uart_get_stats(uart_stats_t *out);

#endif
