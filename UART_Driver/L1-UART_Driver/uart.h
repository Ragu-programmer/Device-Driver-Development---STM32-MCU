#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

#define UART_WAIT_FOREVER  0xFFFFFFFFUL

typedef enum {
    UART_OK = 0,
    UART_ERR_PARAM,
    UART_ERR_TIMEOUT,
    UART_ERR_OVERRUN,
    UART_ERR_FRAMING,
    UART_ERR_NOISE
} uart_status_t;

typedef struct {
    uint32_t baudrate;
    uint32_t periph_clk_hz;
} uart_config_t;

/* Timeouts are in polling-loop iterations, not milliseconds. */
uart_status_t uart_init(const uart_config_t *cfg);
uart_status_t uart_write_byte(uint8_t byte, uint32_t timeout);
uart_status_t uart_write(const uint8_t *data, size_t len, uint32_t timeout);
uart_status_t uart_write_string(const char *str);
uart_status_t uart_read_byte(uint8_t *byte, uint32_t timeout);

#endif
