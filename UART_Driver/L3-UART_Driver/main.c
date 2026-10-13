#include "uart.h"

/* Buffers handed to the DMA must outlive the transfer, so they are static. */
static uint8_t rx_chunk[64];
static uint8_t tx_chunk[2U * sizeof rx_chunk];
static char    stats_line[160];

static size_t append_str(char *dst, size_t pos, size_t cap, const char *s)
{
    while (*s != '\0' && pos + 1U < cap) {
        dst[pos++] = *s++;
    }
    dst[pos] = '\0';
    return pos;
}

static size_t append_u32(char *dst, size_t pos, size_t cap, uint32_t v)
{
    char tmp[11];
    size_t n = 0U;
    do {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (n > 0U && pos + 1U < cap) {
        dst[pos++] = tmp[--n];
    }
    dst[pos] = '\0';
    return pos;
}

static void print_stats(uint32_t idle_loops)
{
    uart_stats_t s;
    size_t p = 0U;

    uart_flush();
    uart_get_stats(&s);
    p = append_str(stats_line, p, sizeof stats_line, "\r\n[idle loops: ");
    p = append_u32(stats_line, p, sizeof stats_line, idle_loops);
    p = append_str(stats_line, p, sizeof stats_line, " | overrun: ");
    p = append_u32(stats_line, p, sizeof stats_line, s.overrun);
    p = append_str(stats_line, p, sizeof stats_line, " | framing: ");
    p = append_u32(stats_line, p, sizeof stats_line, s.framing);
    p = append_str(stats_line, p, sizeof stats_line, " | noise: ");
    p = append_u32(stats_line, p, sizeof stats_line, s.noise);
    p = append_str(stats_line, p, sizeof stats_line, " | dma errors: ");
    p = append_u32(stats_line, p, sizeof stats_line, s.dma_errors);
    (void)append_str(stats_line, p, sizeof stats_line, "]\r\n");

    (void)uart_write_string(stats_line);
}

int main(void)
{
    /* Reset default: MSIS 4 MHz, APB2 undivided, USART1 clocked from PCLK2.
       Update if you configure the PLL. */
    const uart_config_t cfg = {
        .baudrate      = 115200U,
        .periph_clk_hz = 4000000U,
    };

    if (uart_init(&cfg) != UART_OK) {
        while (1) { }
    }

    (void)uart_write_string("\r\nL3 DMA UART driver on STM32U575\r\n"
                            "Type to echo. Press '?' for stats.\r\n");

    uint32_t idle_loops = 0U;

    for (;;) {
        if (uart_tx_busy()) {
            idle_loops++;
            continue;
        }

        size_t n = uart_read(rx_chunk, sizeof rx_chunk);
        if (n == 0U) {
            idle_loops++;
            continue;
        }

        size_t out = 0U;
        bool want_stats = false;
        for (size_t i = 0U; i < n; i++) {
            if (rx_chunk[i] == '?') {
                want_stats = true;
            } else if (rx_chunk[i] == '\r') {
                tx_chunk[out++] = '\r';
                tx_chunk[out++] = '\n';
            } else {
                tx_chunk[out++] = rx_chunk[i];
            }
        }

        if (out > 0U) {
            (void)uart_write(tx_chunk, out);
        }
        if (want_stats) {
            print_stats(idle_loops);
            idle_loops = 0U;
        }
    }
}
