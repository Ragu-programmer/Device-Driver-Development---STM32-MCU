#include "uart.h"

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

    uart_write_string("\r\nHello from my own UART driver on STM32U575!\r\nType something: ");

    for (;;) {
        uint8_t c;
        uart_status_t st = uart_read_byte(&c, UART_WAIT_FOREVER);

        if (st == UART_OK) {
            uart_write_byte(c, UART_WAIT_FOREVER);
            if (c == '\r') {
                uart_write_byte('\n', UART_WAIT_FOREVER);
            }
        } else if (st == UART_ERR_FRAMING) {
            uart_write_string("\r\n[framing error]\r\n");
        } else if (st == UART_ERR_OVERRUN) {
            uart_write_string("\r\n[overrun]\r\n");
        }
    }
}
