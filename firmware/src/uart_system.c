/* CX32L003 manual 6.4.8-10, 6.4.19, 19.4; no persistent writes. */
#include "cx32l003.h"
#include "uart_diag.h"

uint32_t SystemCoreClock = DIAG_CLOCK_HZ;
volatile struct diagnostic_state diagnostic;

#ifdef UART_RADIO_PROTOCOL
/* Single producer ISR / single consumer main; power-of-two ring, no heap. */
static uint8_t receive_ring[512];
static volatile uint16_t receive_head, receive_tail;
static volatile uint32_t receive_losses;
static volatile bool transmit_done;
uint32_t diag_uart_loss_count(void) { return receive_losses; }
void UART0_IRQHandler(void) {
    uint32_t flags = UART0->INTSR;
    if (flags & UART_INTSR_RI_Msk) {
        uint8_t byte = (uint8_t)UART0->SBUF;
        uint16_t next = (receive_head + 1u) & 511u;
        if (!(flags & UART_INTSR_FE_Msk) && next != receive_tail) {
            receive_ring[receive_head] = byte;
            receive_head = next;
            ++diagnostic.rx_bytes;
        } else { ++receive_losses; }
        UART0->INTCLR = UART_INTCLR_RICLR_Msk;
    }
    if (flags & UART_INTSR_FE_Msk) {
        UART0->INTCLR = UART_INTCLR_FECLR_Msk;
        if (!(flags & UART_INTSR_RI_Msk)) { ++receive_losses; }
    }
    if (flags & UART_INTSR_TI_Msk) {
        UART0->INTCLR = UART_INTCLR_TICLR_Msk;
        transmit_done = true;
    }
}
#endif

void diag_watchdog_feed(void) {
    if (IWDG->CFGR & IWDG_CFGR_IWDGRUNF_Msk) { IWDG->CMDCR = 0xaau; }
}

void SystemInit(void) {
    diagnostic.stage = 1;
    diagnostic.reset_flags = RCC->RSTSR;
    /* USERCFG2 can start IWDG before application entry. Keep it serviced. */
    if (IWDG->CFGR & IWDG_CFGR_IWDGRUNF_Msk) {
        IWDG->UNLOCK = 0x55aa6699u;
        IWDG->CFGR = IWDG_CFGR_IWDGINTMSK_Msk; /* masked, reset mode */
        IWDG->RLOAD = 0xfffffu;
        IWDG->UNLOCK = 0x55aa6698u;
        diag_watchdog_feed();
    }
    RCC->UNLOCK = 0x55aa6699u;
    RCC->SYSCLKCR = 0x5a690005u; /* internal HIRC + LIRC, no crystal */
    const uint32_t trim = *(const volatile uint16_t *)0x180000a2u;
    if (trim == 0 || trim > 0xfffu) {
        diagnostic.errors = 1;
        for (;;) { diag_watchdog_feed(); }
    }
    RCC->HIRCCR = 0x5a690000u | trim; /* packaged-part factory 22.12 MHz trim */
    uint32_t remaining = 1000000;
    while (!(RCC->HIRCCR & RCC_HIRCCR_HIRCRDY_Msk)) {
        diag_watchdog_feed();
        if (--remaining == 0) {
            diagnostic.errors = 2;
            for (;;) { diag_watchdog_feed(); }
        }
    }
    RCC->SYSCLKSEL = 0x5a690001u; /* Explicitly select HIRC; unlike old template. */
    RCC->HCLKDIV = 0;
    RCC->PCLKDIV = 0;
    /* Keep Flash, SWD/debug and IWDG state; UART test never enables SPI. */
    RCC->HCLKEN |= 0x100u | RCC_HCLKEN_GPIOACKEN | RCC_HCLKEN_GPIOCCKEN | RCC_HCLKEN_GPIODCKEN;
    RCC->PCLKEN |= RCC_PCLKEN_UART0CKEN | 0x00100080u; /* DBG + SYSCON */
    RCC->UNLOCK = 0x55aa6698u;
    SCB->VTOR = 0;
    SystemCoreClock = DIAG_CLOCK_HZ;
    diagnostic.clock_hz = SystemCoreClock;
    diagnostic.stage = 2;
}

void SystemCoreClockUpdate(void) { SystemCoreClock = DIAG_CLOCK_HZ; }

void SysTick_Handler(void) { ++diagnostic.ticks; }

void diag_uart_init(void) {
    /* AF7 PD5=UART0_TX, PD6=UART0_RX; preserve PD1 SWDCLK and all radio pins. */
    GPIOD->ODSET = 1u << 5;
    GPIOD->OTYPER &= ~(1u << 5);
    GPIOD->PUPDR = (GPIOD->PUPDR & ~((3u << 10) | (3u << 12))) | (1u << 12);
    GPIOD->DIRCR = (GPIOD->DIRCR | (1u << 5)) & ~(1u << 6);
    GPIOD->AFR = (GPIOD->AFR & ~((15u << 20) | (15u << 24))) | (7u << 20) | (7u << 24);
    /* AUX/M0/M1 remain undriven inputs; preserve PC7 SWDIO field. */
    GPIOA->DIRCR &= ~(1u << 3);
    GPIOA->AFR &= ~(15u << 12);
    GPIOC->DIRCR &= ~((1u << 5) | (1u << 6));
    GPIOC->AFR &= ~((15u << 20) | (15u << 24));
    UART0->SCON = 0;
    UART0->IRDACR = 0;
    /* 22.12 MHz / (16 * 12) = 115208.3 baud; nominal error +0.0072%. */
    UART0->BAUDCR = UART_BAUDCR_SELF_BRG_Msk | 11u;
    UART0->INTCLR = 7;
    /* CX32 flags are masked unless RIEN/TIEN are set, even when polling.
     * Vendor HAL_UART_Transmit/Receive explicitly unmasks them. Recovery keeps
     * NVIC off; the binary controller enables its bounded ISR below. */
    UART0->SCON = (1u << UART_SCON_SM0_SM1_Pos) | UART_SCON_DBAUND_Msk |
                  UART_SCON_REN_Msk | UART_SCON_RIEN_Msk | UART_SCON_TIEN_Msk;
    (void)SysTick_Config(SystemCoreClock / 1000u);
#ifdef UART_RADIO_PROTOCOL
    NVIC_ClearPendingIRQ(UART0_IRQn);
    NVIC_SetPriority(UART0_IRQn, 2);
    NVIC_EnableIRQ(UART0_IRQn);
#endif
    diagnostic.stage = 3;
}

bool diag_uart_get(uint8_t *byte) {
#ifdef UART_RADIO_PROTOCOL
    if (receive_tail == receive_head) { return false; }
    *byte = receive_ring[receive_tail];
    receive_tail = (receive_tail + 1u) & 511u;
    return true;
#else
    uint32_t flags = UART0->INTSR;
    if (flags & UART_INTSR_FE_Msk) {
        UART0->INTCLR = UART_INTCLR_FECLR_Msk | UART_INTCLR_RICLR_Msk;
        diagnostic.errors |= 4;
        return false;
    }
    if (!(flags & UART_INTSR_RI_Msk)) { return false; }
    *byte = (uint8_t)UART0->SBUF;
    UART0->INTCLR = UART_INTCLR_RICLR_Msk;
    ++diagnostic.rx_bytes;
    return true;
#endif
}

bool diag_uart_put(uint8_t byte) {
    UART0->INTCLR = UART_INTCLR_TICLR_Msk;
#ifdef UART_RADIO_PROTOCOL
    transmit_done = false;
#endif
    UART0->SBUF = byte;
    uint32_t start = diagnostic.ticks, remaining = 1000000;
#ifdef UART_RADIO_PROTOCOL
    while (!transmit_done) {
#else
    while (!(UART0->INTSR & UART_INTSR_TI_Msk)) {
#endif
        diag_watchdog_feed();
        if ((uint32_t)(diagnostic.ticks - start) >= 10u || --remaining == 0) {
            diagnostic.errors |= 8;
            return false;
        }
    }
    ++diagnostic.tx_bytes;
    return true;
}
