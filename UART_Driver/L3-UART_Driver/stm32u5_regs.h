#ifndef STM32U5_REGS_H
#define STM32U5_REGS_H

#include <stdint.h>
#include <stddef.h>

#define __IO volatile

typedef struct {
    uint32_t      RESERVED0[34];
    __IO uint32_t AHB1ENR;
    __IO uint32_t AHB2ENR1;
    __IO uint32_t AHB2ENR2;
    __IO uint32_t AHB3ENR;
    uint32_t      RESERVED1;
    __IO uint32_t APB1ENR1;
    __IO uint32_t APB1ENR2;
    __IO uint32_t APB2ENR;
    __IO uint32_t APB3ENR;
} RCC_TypeDef;

typedef struct {
    __IO uint32_t MODER;
    __IO uint32_t OTYPER;
    __IO uint32_t OSPEEDR;
    __IO uint32_t PUPDR;
    __IO uint32_t IDR;
    __IO uint32_t ODR;
    __IO uint32_t BSRR;
    __IO uint32_t LCKR;
    __IO uint32_t AFR[2];
} GPIO_TypeDef;

typedef struct {
    __IO uint32_t CR1;
    __IO uint32_t CR2;
    __IO uint32_t CR3;
    __IO uint32_t BRR;
    __IO uint32_t GTPR;
    __IO uint32_t RTOR;
    __IO uint32_t RQR;
    __IO uint32_t ISR;
    __IO uint32_t ICR;
    __IO uint32_t RDR;
    __IO uint32_t TDR;
    __IO uint32_t PRESC;
} USART_TypeDef;

typedef struct {
    __IO uint32_t ISER[16];
    uint32_t      RESERVED0[16];
    __IO uint32_t ICER[16];
    uint32_t      RESERVED1[16];
    __IO uint32_t ISPR[16];
    uint32_t      RESERVED2[16];
    __IO uint32_t ICPR[16];
    uint32_t      RESERVED3[16];
    __IO uint32_t IABR[16];
    uint32_t      RESERVED4[16];
    __IO uint32_t ITNS[16];
    uint32_t      RESERVED5[16];
    __IO uint8_t  IPR[496];
} NVIC_TypeDef;

/* One GPDMA channel. Channel x starts at GPDMA1 base + 0x50 + 0x80 * x. */
typedef struct {
    __IO uint32_t CLBAR;
    uint32_t      RESERVED0[2];
    __IO uint32_t CFCR;
    __IO uint32_t CSR;
    __IO uint32_t CCR;
    uint32_t      RESERVED1[10];
    __IO uint32_t CTR1;
    __IO uint32_t CTR2;
    __IO uint32_t CBR1;
    __IO uint32_t CSAR;
    __IO uint32_t CDAR;
    uint32_t      RESERVED2[10];
    __IO uint32_t CLLR;
} DMA_Channel_TypeDef;

_Static_assert(offsetof(RCC_TypeDef, AHB1ENR)  == 0x88, "RCC AHB1ENR offset");
_Static_assert(offsetof(RCC_TypeDef, AHB2ENR1) == 0x8C, "RCC AHB2ENR1 offset");
_Static_assert(offsetof(RCC_TypeDef, APB2ENR)  == 0xA4, "RCC APB2ENR offset");
_Static_assert(offsetof(GPIO_TypeDef, AFR)     == 0x20, "GPIO AFR offset");
_Static_assert(offsetof(USART_TypeDef, ISR)    == 0x1C, "USART ISR offset");
_Static_assert(offsetof(USART_TypeDef, TDR)    == 0x28, "USART TDR offset");
_Static_assert(offsetof(NVIC_TypeDef, IPR)     == 0x300, "NVIC IPR offset");
_Static_assert(offsetof(DMA_Channel_TypeDef, CSR)  == 0x10, "DMA CSR offset");
_Static_assert(offsetof(DMA_Channel_TypeDef, CTR1) == 0x40, "DMA CTR1 offset");
_Static_assert(offsetof(DMA_Channel_TypeDef, CLLR) == 0x7C, "DMA CLLR offset");
_Static_assert(sizeof(DMA_Channel_TypeDef)         == 0x80, "DMA channel size");

/* Non-secure aliases (TrustZone disabled, the board's factory default) */
#define RCC_BASE            0x46020C00UL
#define GPIOA_BASE          0x42020000UL
#define USART1_BASE         0x40013800UL
#define NVIC_BASE           0xE000E100UL
#define GPDMA1_BASE         0x40020000UL
#define GPDMA1_CH0_BASE     (GPDMA1_BASE + 0x50UL)
#define GPDMA1_CH1_BASE     (GPDMA1_BASE + 0xD0UL)

#define RCC         ((RCC_TypeDef         *) RCC_BASE)
#define GPIOA       ((GPIO_TypeDef        *) GPIOA_BASE)
#define USART1      ((USART_TypeDef       *) USART1_BASE)
#define NVIC        ((NVIC_TypeDef        *) NVIC_BASE)
#define GPDMA1_CH0  ((DMA_Channel_TypeDef *) GPDMA1_CH0_BASE)
#define GPDMA1_CH1  ((DMA_Channel_TypeDef *) GPDMA1_CH1_BASE)

#endif
