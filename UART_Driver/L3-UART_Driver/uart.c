#include "uart.h"
#include "stm32u5_regs.h"

#define RCC_AHB1ENR_GPDMA1EN  (1UL << 0)
#define RCC_AHB2ENR1_GPIOAEN  (1UL << 0)
#define RCC_APB2ENR_USART1EN  (1UL << 14)

#define USART_CR1_UE     (1UL << 0)
#define USART_CR1_RE     (1UL << 2)
#define USART_CR1_TE     (1UL << 3)

#define USART_CR3_DMAR   (1UL << 6)
#define USART_CR3_DMAT   (1UL << 7)

#define USART_ISR_PE     (1UL << 0)
#define USART_ISR_FE     (1UL << 1)
#define USART_ISR_NE     (1UL << 2)
#define USART_ISR_ORE    (1UL << 3)
#define USART_ISR_TC     (1UL << 6)
#define USART_ISR_TEACK  (1UL << 21)
#define USART_ISR_REACK  (1UL << 22)

#define USART_ICR_PECF   (1UL << 0)
#define USART_ICR_FECF   (1UL << 1)
#define USART_ICR_NECF   (1UL << 2)
#define USART_ICR_ORECF  (1UL << 3)
#define USART_ICR_TCCF   (1UL << 6)

#define USART_ISR_ERRORS (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)
#define USART_ICR_ERRORS (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF)

#define DMA_CCR_EN       (1UL << 0)
#define DMA_CCR_TCIE     (1UL << 8)
#define DMA_CCR_DTEIE    (1UL << 10)
#define DMA_CCR_USEIE    (1UL << 12)

#define DMA_CSR_IDLEF    (1UL << 0)
#define DMA_CSR_TCF      (1UL << 8)
#define DMA_CSR_DTEF     (1UL << 10)
#define DMA_CSR_USEF     (1UL << 12)
#define DMA_FLAGS_ALL    (0x7FUL << 8)

#define DMA_CTR1_SINC    (1UL << 3)
#define DMA_CTR1_DINC    (1UL << 19)
#define DMA_CTR2_DREQ    (1UL << 10)
#define DMA_CBR1_BNDT    0xFFFFUL

#define GPDMA1_REQ_USART1_RX  24UL
#define GPDMA1_REQ_USART1_TX  25UL

#define DMA_TX  GPDMA1_CH0
#define DMA_RX  GPDMA1_CH1

#define GPDMA1_CH0_IRQn  29U
#define GPDMA1_CH1_IRQn  30U
#define DMA_IRQ_PRIO     5U
#define NVIC_PRIO_BITS   4U

#define GPIO_MODE_AF    0x2UL
#define GPIO_PULL_UP    0x1UL
#define AF7_USART1      7UL

#define TX_PIN  9U
#define RX_PIN  10U

#define INIT_TIMEOUT  100000UL

#define RX_BUF_SIZE  256U

/* The DMA writes into this buffer on its own; rx_tail is the app's read position. */
static volatile uint8_t  rx_buf[RX_BUF_SIZE] __attribute__((aligned(4)));
static volatile uint16_t rx_tail;

static volatile bool tx_busy;
static volatile uart_stats_t stats;

static inline void data_memory_barrier(void)
{
    /* make sure buffer writes reach memory before the DMA is started */
    __asm volatile ("dmb" ::: "memory");
}

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

static void dma_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPDMA1EN;
    (void)RCC->AHB1ENR;

    /* TX: memory (incrementing) -> USART1 TDR (fixed), bytes, USART is the destination */
    DMA_TX->CFCR = DMA_FLAGS_ALL;
    DMA_TX->CTR1 = DMA_CTR1_SINC;
    DMA_TX->CTR2 = GPDMA1_REQ_USART1_TX | DMA_CTR2_DREQ;
    DMA_TX->CDAR = (uint32_t)(uintptr_t)&USART1->TDR;
    DMA_TX->CLLR = 0U;

    /* RX: USART1 RDR (fixed) -> rx_buf (incrementing), bytes, USART is the source */
    DMA_RX->CFCR = DMA_FLAGS_ALL;
    DMA_RX->CTR1 = DMA_CTR1_DINC;
    DMA_RX->CTR2 = GPDMA1_REQ_USART1_RX;
    DMA_RX->CSAR = (uint32_t)(uintptr_t)&USART1->RDR;
    DMA_RX->CDAR = (uint32_t)(uintptr_t)rx_buf;
    DMA_RX->CBR1 = RX_BUF_SIZE;
    DMA_RX->CLLR = 0U;
    DMA_RX->CCR  = DMA_CCR_TCIE | DMA_CCR_DTEIE | DMA_CCR_USEIE | DMA_CCR_EN;

    nvic_enable_irq(GPDMA1_CH0_IRQn, DMA_IRQ_PRIO);
    nvic_enable_irq(GPDMA1_CH1_IRQn, DMA_IRQ_PRIO);
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

    rx_tail = 0U;
    tx_busy = false;

    uart_gpio_init();

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* 8N1, 16x oversampling, FIFO off, no flow control, DMA requests on */
    USART1->CR1   = 0U;
    USART1->CR2   = 0U;
    USART1->CR3   = USART_CR3_DMAR | USART_CR3_DMAT;
    USART1->PRESC = 0U;
    USART1->BRR   = brr;

    dma_init();

    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    uart_status_t st = uart_wait_flag(USART_ISR_TEACK, INIT_TIMEOUT);
    if (st != UART_OK) {
        return st;
    }
    return uart_wait_flag(USART_ISR_REACK, INIT_TIMEOUT);
}

/* TX complete: the whole buffer has been handed to the USART. */
void GPDMA1_Channel0_IRQHandler(void)
{
    uint32_t sr = DMA_TX->CSR;
    DMA_TX->CFCR = sr & DMA_FLAGS_ALL;

    if (sr & (DMA_CSR_DTEF | DMA_CSR_USEF)) {
        stats.dma_errors++;
    }
    tx_busy = false;
}

/* RX block full: restart at the beginning of rx_buf, which makes it circular.
   A byte arriving meanwhile waits safely in RDR, since the restart is far
   shorter than one byte time. */
void GPDMA1_Channel1_IRQHandler(void)
{
    uint32_t sr = DMA_RX->CSR;
    DMA_RX->CFCR = sr & DMA_FLAGS_ALL;

    if (sr & (DMA_CSR_DTEF | DMA_CSR_USEF)) {
        stats.dma_errors++;
    }

    while ((DMA_RX->CSR & DMA_CSR_IDLEF) == 0U) { }

    DMA_RX->CDAR = (uint32_t)(uintptr_t)rx_buf;
    DMA_RX->CBR1 = RX_BUF_SIZE;
    DMA_RX->CCR |= DMA_CCR_EN;
}

uart_status_t uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U || len > DMA_CBR1_BNDT) {
        return UART_ERR_PARAM;
    }
    if (tx_busy) {
        return UART_ERR_BUSY;
    }

    tx_busy = true;
    data_memory_barrier();

    USART1->ICR  = USART_ICR_TCCF;
    DMA_TX->CFCR = DMA_FLAGS_ALL;
    DMA_TX->CSAR = (uint32_t)(uintptr_t)data;
    DMA_TX->CBR1 = (uint32_t)len;
    DMA_TX->CCR  = DMA_CCR_TCIE | DMA_CCR_DTEIE | DMA_CCR_USEIE | DMA_CCR_EN;

    return UART_OK;
}

uart_status_t uart_write_string(const char *str)
{
    if (str == NULL) {
        return UART_ERR_PARAM;
    }
    size_t len = 0U;
    while (str[len] != '\0') {
        len++;
    }
    if (len == 0U) {
        return UART_OK;
    }
    while (tx_busy) { }
    return uart_write((const uint8_t *)str, len);
}

bool uart_tx_busy(void)
{
    return tx_busy;
}

void uart_flush(void)
{
    while (tx_busy) { }
    /* DMA done means the last byte reached TDR; TC means it left the pin */
    while ((USART1->ISR & USART_ISR_TC) == 0U) { }
}

static uint16_t rx_head(void)
{
    /* BNDT counts down as the DMA writes, so the write position is size - remaining */
    uint32_t remaining = DMA_RX->CBR1 & DMA_CBR1_BNDT;
    return (uint16_t)((RX_BUF_SIZE - remaining) % RX_BUF_SIZE);
}

static void check_usart_errors(void)
{
    uint32_t isr = USART1->ISR;
    if (isr & USART_ISR_ERRORS) {
        USART1->ICR = USART_ICR_ERRORS;
        if (isr & USART_ISR_ORE) stats.overrun++;
        if (isr & USART_ISR_FE)  stats.framing++;
        if (isr & USART_ISR_NE)  stats.noise++;
    }
}

size_t uart_available(void)
{
    uint16_t head = rx_head();
    return (size_t)((head + RX_BUF_SIZE - rx_tail) % RX_BUF_SIZE);
}

size_t uart_read(uint8_t *buf, size_t max_len)
{
    if (buf == NULL) {
        return 0U;
    }

    check_usart_errors();

    uint16_t head = rx_head();
    size_t n = 0U;
    while (n < max_len && rx_tail != head) {
        buf[n++] = rx_buf[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % RX_BUF_SIZE);
    }
    return n;
}

void uart_get_stats(uart_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->overrun    = stats.overrun;
    out->framing    = stats.framing;
    out->noise      = stats.noise;
    out->dma_errors = stats.dma_errors;
}
