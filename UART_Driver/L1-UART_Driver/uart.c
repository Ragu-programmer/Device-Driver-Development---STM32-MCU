#include "uart.h"
#include "stm32u5_regs.h"

#define RCC_AHB2ENR1_GPIOAEN  (1UL << 0)
#define RCC_APB2ENR_USART1EN  (1UL << 14)

#define USART_CR1_UE     (1UL << 0)
#define USART_CR1_RE     (1UL << 2)
#define USART_CR1_TE     (1UL << 3)

#define USART_ISR_PE     (1UL << 0)
#define USART_ISR_FE     (1UL << 1)
#define USART_ISR_NE     (1UL << 2)
#define USART_ISR_ORE    (1UL << 3)
#define USART_ISR_RXNE   (1UL << 5)
#define USART_ISR_TC     (1UL << 6)
#define USART_ISR_TXE    (1UL << 7)
#define USART_ISR_TEACK  (1UL << 21)
#define USART_ISR_REACK  (1UL << 22)

#define USART_ICR_PECF   (1UL << 0)
#define USART_ICR_FECF   (1UL << 1)
#define USART_ICR_NECF   (1UL << 2)
#define USART_ICR_ORECF  (1UL << 3)

#define USART_ISR_ERRORS (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)
#define USART_ICR_ERRORS (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF)

#define GPIO_MODE_AF    0x2UL
#define GPIO_PULL_UP    0x1UL
#define AF7_USART1      7UL

#define TX_PIN  9U
#define RX_PIN  10U

#define INIT_TIMEOUT  100000UL

static void uart_gpio_init(void)
{
    RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOAEN;
    (void)RCC->AHB2ENR1;   /* dummy read: delay after clock enable */

    GPIOA->MODER &= ~((3UL << (TX_PIN * 2)) | (3UL << (RX_PIN * 2)));
    GPIOA->MODER |=  (GPIO_MODE_AF << (TX_PIN * 2)) | (GPIO_MODE_AF << (RX_PIN * 2));

    /* pins 8-15 use AFR[1] (AFRH) */
    GPIOA->AFR[1] &= ~((0xFUL << ((TX_PIN - 8U) * 4)) | (0xFUL << ((RX_PIN - 8U) * 4)));
    GPIOA->AFR[1] |=  (AF7_USART1 << ((TX_PIN - 8U) * 4)) | (AF7_USART1 << ((RX_PIN - 8U) * 4));

    GPIOA->PUPDR &= ~(3UL << (RX_PIN * 2));
    GPIOA->PUPDR |=  (GPIO_PULL_UP << (RX_PIN * 2));
}

static uart_status_t uart_wait_flag(uint32_t flag, uint32_t timeout)
{
    while ((USART1->ISR & flag) == 0U) {
        if (timeout != UART_WAIT_FOREVER) {
            if (timeout == 0U) {
                return UART_ERR_TIMEOUT;
            }
            timeout--;
        }
    }
    return UART_OK;
}

uart_status_t uart_init(const uart_config_t *cfg)
{
    if (cfg == NULL || cfg->baudrate == 0U || cfg->periph_clk_hz == 0U) {
        return UART_ERR_PARAM;
    }

    /* 16x oversampling, prescaler /1: BRR = f_clk / baud, rounded */
    uint32_t brr = (cfg->periph_clk_hz + cfg->baudrate / 2U) / cfg->baudrate;
    if (brr < 16U || brr > 0xFFFFU) {
        return UART_ERR_PARAM;
    }

    uart_gpio_init();

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* 8N1, 16x oversampling, FIFO off, no flow control */
    USART1->CR1   = 0U;
    USART1->CR2   = 0U;
    USART1->CR3   = 0U;
    USART1->PRESC = 0U;
    USART1->BRR   = brr;

    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    /* TE/RE take effect only once the hardware acknowledges them */
    uart_status_t st = uart_wait_flag(USART_ISR_TEACK, INIT_TIMEOUT);
    if (st != UART_OK) {
        return st;
    }
    return uart_wait_flag(USART_ISR_REACK, INIT_TIMEOUT);
}

uart_status_t uart_write_byte(uint8_t byte, uint32_t timeout)
{
    uart_status_t st = uart_wait_flag(USART_ISR_TXE, timeout);
    if (st != UART_OK) {
        return st;
    }
    USART1->TDR = byte;
    return UART_OK;
}

uart_status_t uart_write(const uint8_t *data, size_t len, uint32_t timeout)
{
    if (data == NULL && len > 0U) {
        return UART_ERR_PARAM;
    }
    for (size_t i = 0; i < len; i++) {
        uart_status_t st = uart_write_byte(data[i], timeout);
        if (st != UART_OK) {
            return st;
        }
    }
    /* wait until the last byte has fully left the shift register */
    return uart_wait_flag(USART_ISR_TC, timeout);
}

uart_status_t uart_write_string(const char *str)
{
    if (str == NULL) {
        return UART_ERR_PARAM;
    }
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return uart_write((const uint8_t *)str, len, UART_WAIT_FOREVER);
}

uart_status_t uart_read_byte(uint8_t *byte, uint32_t timeout)
{
    if (byte == NULL) {
        return UART_ERR_PARAM;
    }

    uart_status_t st = uart_wait_flag(USART_ISR_RXNE, timeout);
    if (st != UART_OK) {
        return st;
    }

    uint32_t isr = USART1->ISR;
    *byte = (uint8_t)(USART1->RDR & 0xFFU);

    if ((isr & USART_ISR_ERRORS) == 0U) {
        return UART_OK;
    }

    /* unlike the F4, error flags are cleared by writing ICR */
    USART1->ICR = USART_ICR_ERRORS;

    if (isr & USART_ISR_ORE) return UART_ERR_OVERRUN;
    if (isr & USART_ISR_FE)  return UART_ERR_FRAMING;
    return UART_ERR_NOISE;
}
