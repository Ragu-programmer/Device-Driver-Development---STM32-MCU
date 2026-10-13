#include "uart.h"
#include "stm32u5_regs.h"

#define RCC_AHB2ENR1_GPIOAEN  (1UL << 0)
#define RCC_APB2ENR_USART1EN  (1UL << 14)

#define USART_CR1_UE      (1UL << 0)
#define USART_CR1_RE      (1UL << 2)
#define USART_CR1_TE      (1UL << 3)
#define USART_CR1_RXNEIE  (1UL << 5)
#define USART_CR1_TXEIE   (1UL << 7)

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

#define USART1_IRQn      61U
#define USART1_IRQ_PRIO  5U
#define NVIC_PRIO_BITS   4U

#define INIT_TIMEOUT  100000UL

#define RX_BUF_SIZE  256U
#define TX_BUF_SIZE  256U
#define RX_MASK      (RX_BUF_SIZE - 1U)
#define TX_MASK      (TX_BUF_SIZE - 1U)

_Static_assert((RX_BUF_SIZE & RX_MASK) == 0U, "RX_BUF_SIZE must be a power of two");
_Static_assert((TX_BUF_SIZE & TX_MASK) == 0U, "TX_BUF_SIZE must be a power of two");

/* Ring buffers: the ISR only moves rx_head and tx_tail, the application only
   moves rx_tail and tx_head, so no locking is needed. */
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

static volatile uint8_t  tx_buf[TX_BUF_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;

static volatile uart_stats_t stats;

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
        if (timeout == 0U) {
            return UART_ERR_TIMEOUT;
        }
        timeout--;
    }
    return UART_OK;
}

static void nvic_enable_irq(uint32_t irq, uint32_t prio)
{
    /* only the top NVIC_PRIO_BITS of each priority byte are implemented */
    NVIC->IPR[irq] = (uint8_t)(prio << (8U - NVIC_PRIO_BITS));
    NVIC->ICPR[irq >> 5] = 1UL << (irq & 31U);
    NVIC->ISER[irq >> 5] = 1UL << (irq & 31U);
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

    rx_head = 0U;
    rx_tail = 0U;
    tx_head = 0U;
    tx_tail = 0U;

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

    uart_status_t st = uart_wait_flag(USART_ISR_TEACK, INIT_TIMEOUT);
    if (st != UART_OK) {
        return st;
    }
    st = uart_wait_flag(USART_ISR_REACK, INIT_TIMEOUT);
    if (st != UART_OK) {
        return st;
    }

    USART1->CR1 |= USART_CR1_RXNEIE;
    nvic_enable_irq(USART1_IRQn, USART1_IRQ_PRIO);

    return UART_OK;
}

/* Name must match the vector table entry in the startup file. */
void USART1_IRQHandler(void)
{
    uint32_t isr = USART1->ISR;

    if (isr & USART_ISR_ERRORS) {
        /* must be cleared here, or ORE keeps re-triggering this interrupt */
        USART1->ICR = USART_ICR_ERRORS;
        if (isr & USART_ISR_ORE) stats.overrun++;
        if (isr & USART_ISR_FE)  stats.framing++;
        if (isr & USART_ISR_NE)  stats.noise++;
    }

    if (isr & USART_ISR_RXNE) {
        uint8_t byte = (uint8_t)(USART1->RDR & 0xFFU);
        uint16_t next = (uint16_t)((rx_head + 1U) & RX_MASK);
        if (next != rx_tail) {
            rx_buf[rx_head] = byte;
            rx_head = next;
        } else {
            stats.rx_dropped++;
        }
    }

    /* TXE is almost always 1 when idle, so only act on it while TXEIE is on */
    if ((USART1->CR1 & USART_CR1_TXEIE) && (isr & USART_ISR_TXE)) {
        if (tx_tail != tx_head) {
            USART1->TDR = tx_buf[tx_tail];
            tx_tail = (uint16_t)((tx_tail + 1U) & TX_MASK);
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

size_t uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return 0U;
    }

    size_t n = 0U;
    while (n < len) {
        uint16_t next = (uint16_t)((tx_head + 1U) & TX_MASK);
        if (next == tx_tail) {
            break;
        }
        tx_buf[tx_head] = data[n++];
        tx_head = next;
    }

    if (n > 0U) {
        USART1->CR1 |= USART_CR1_TXEIE;
    }
    return n;
}

void uart_write_all(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return;
    }
    while (len > 0U) {
        size_t sent = uart_write(data, len);
        data += sent;
        len  -= sent;
    }
}

void uart_write_string(const char *str)
{
    if (str == NULL) {
        return;
    }
    size_t len = 0U;
    while (str[len] != '\0') {
        len++;
    }
    uart_write_all((const uint8_t *)str, len);
}

void uart_flush(void)
{
    while (tx_head != tx_tail) { }
    while ((USART1->ISR & USART_ISR_TC) == 0U) { }
}

size_t uart_available(void)
{
    return (size_t)((uint16_t)(rx_head - rx_tail) & RX_MASK);
}

size_t uart_read(uint8_t *buf, size_t max_len)
{
    if (buf == NULL) {
        return 0U;
    }

    size_t n = 0U;
    while (n < max_len && rx_tail != rx_head) {
        buf[n++] = rx_buf[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) & RX_MASK);
    }
    return n;
}

void uart_get_stats(uart_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->rx_dropped = stats.rx_dropped;
    out->overrun    = stats.overrun;
    out->framing    = stats.framing;
    out->noise      = stats.noise;
}
