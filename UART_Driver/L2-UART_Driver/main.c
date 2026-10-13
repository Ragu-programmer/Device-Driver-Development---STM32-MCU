#include "uart.h"

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
    char line[160];
    size_t p = 0U;

    uart_get_stats(&s);
    p = append_str(line, p, sizeof line, "\r\n[idle loops: ");
    p = append_u32(line, p, sizeof line, idle_loops);
    p = append_str(line, p, sizeof line, " | rx dropped: ");
    p = append_u32(line, p, sizeof line, s.rx_dropped);
    p = append_str(line, p, sizeof line, " | overrun: ");
    p = append_u32(line, p, sizeof line, s.overrun);
    p = append_str(line, p, sizeof line, " | framing: ");
    p = append_u32(line, p, sizeof line, s.framing);
    p = append_str(line, p, sizeof line, " | noise: ");
    p = append_u32(line, p, sizeof line, s.noise);
    (void)append_str(line, p, sizeof line, "]\r\n");

    uart_write_string(line);
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

    uart_write_string("\r\nL2 interrupt-driven UART driver on STM32U575\r\n"
                      "Type to echo. Press '?' for stats.\r\n");

    uint32_t idle_loops = 0U;

    for (;;) {
        uint8_t buf[32];
        size_t n = uart_read(buf, sizeof buf);

        if (n == 0U) {
            idle_loops++;
            continue;
        }

        for (size_t i = 0U; i < n; i++) {
            if (buf[i] == '?') {
                print_stats(idle_loops);
                idle_loops = 0U;
            } else if (buf[i] == '\r') {
                uart_write_string("\r\n");
            } else {
                uart_write_all(&buf[i], 1U);
            }
        }
    }
}
