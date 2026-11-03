#include "board.h"
#include "cx32l003.h"

static volatile uint32_t ticks;

void SysTick_Handler(void) { ++ticks; }
uint32_t board_millis(void) { return ticks; }
void board_delay_ms(uint32_t ms) {
    uint32_t start = ticks;
    while ((uint32_t)(ticks - start) < ms) { __NOP(); }
}

static void pin(GPIO_TypeDef *port, unsigned n, unsigned af, bool output) {
    port->AFR = (port->AFR & ~(15u << (4u * n))) | (af << (4u * n));
    port->PUPDR &= ~(3u << (2u * n));
    port->OTYPER &= ~(1u << n);
    if (output) { port->DIRCR |= 1u << n; }
    else { port->DIRCR &= ~(1u << n); }
}

void board_init(void) {
    /* SystemInit selected reset-default HIRC 4 MHz, HCLK=PCLK. No PLL/crystal. */
    RCC->HCLKEN |= RCC_HCLKEN_GPIOACKEN | RCC_HCLKEN_GPIOBCKEN |
                    RCC_HCLKEN_GPIOCCKEN | RCC_HCLKEN_GPIODCKEN;
    RCC->PCLKEN |= RCC_PCLKEN_UART0CKEN | RCC_PCLKEN_SPICKEN;
    /* Preload inactive output levels before enabling output drivers. */
    GPIOA->ODSET = 1u << RADIO_NSS_PIN;
    GPIOB->ODSET = 1u << RADIO_RESET_PIN;
    GPIOD->ODSET = 1u << UART_TX_PIN;
    pin(GPIOA, RADIO_NSS_PIN, 0, true);
    pin(GPIOB, RADIO_RESET_PIN, 0, true);
    pin(GPIOB, RADIO_BUSY_PIN, 0, false);
    pin(GPIOD, RADIO_DIO1_PIN, 0, false);
    pin(GPIOA, RADIO_SCK_PIN, 3, true);
    pin(GPIOD, RADIO_MOSI_PIN, 3, true);
    pin(GPIOD, RADIO_MISO_PIN, 3, false);
    pin(GPIOD, UART_TX_PIN, 7, true);
    pin(GPIOD, UART_RX_PIN, 7, false);
    pin(GPIOA, AUX_PIN, 0, false);
    pin(GPIOC, M0_PIN, 0, false);
    pin(GPIOC, M1_PIN, 0, false);
    /* PC7/PD1 SWD and NRST are untouched. */
    SPI->SSN = SPI_SSN_SSN_Msk;
    /* Mode 0, MSB first, master, Fsys/8 = 500 kHz. */
    SPI->CR = SPI_CR_MSTR_Msk | SPI_CR_SPEN_Msk | SPI_CR_SPR1_Msk;
    /* 8N1, double baud, internal BRG. 4 MHz/(16*26) = 9615.38 baud. */
    UART0->BAUDCR = UART_BAUDCR_SELF_BRG_Msk | 25u;
    UART0->INTCLR = UART_INTCLR_RICLR_Msk | UART_INTCLR_TICLR_Msk | UART_INTCLR_FECLR_Msk;
    /* Unmask status flags for polling; leave UART0 disabled at the NVIC. */
    UART0->SCON = (1u << UART_SCON_SM0_SM1_Pos) | UART_SCON_DBAUND_Msk |
                  UART_SCON_REN_Msk | UART_SCON_RIEN_Msk | UART_SCON_TIEN_Msk;
    (void)SysTick_Config(SystemCoreClock / 1000u);
}

bool board_uart_read(uint8_t *byte) {
    uint32_t status = UART0->INTSR;
    if (status & UART_INTSR_FE_Msk) {
        UART0->INTCLR = UART_INTCLR_FECLR_Msk | UART_INTCLR_RICLR_Msk;
        return false;
    }
    if (!(status & UART_INTSR_RI_Msk)) { return false; }
    *byte = (uint8_t)UART0->SBUF;
    UART0->INTCLR = UART_INTCLR_RICLR_Msk;
    return true;
}

bool board_uart_write(uint8_t byte) {
    UART0->INTCLR = UART_INTCLR_TICLR_Msk;
    UART0->SBUF = byte;
    uint32_t start = ticks;
    while (!(UART0->INTSR & UART_INTSR_TI_Msk)) {
        if ((uint32_t)(ticks - start) >= 10u) { return false; }
    }
    return true;
}

bool board_radio_busy(void) { return (GPIOB->IDR & (1u << RADIO_BUSY_PIN)) != 0; }
bool board_radio_dio1(void) { return (GPIOD->IDR & (1u << RADIO_DIO1_PIN)) != 0; }
void board_radio_select(bool active) {
    if (active) { GPIOA->ODCLR = 1u << RADIO_NSS_PIN; SPI->SSN = 0; }
    else { GPIOA->ODSET = 1u << RADIO_NSS_PIN; SPI->SSN = SPI_SSN_SSN_Msk; }
}
void board_radio_reset(bool asserted) {
    if (asserted) { GPIOB->ODCLR = 1u << RADIO_RESET_PIN; }
    else { GPIOB->ODSET = 1u << RADIO_RESET_PIN; }
}

bool board_spi_transfer(uint8_t tx, uint8_t *rx) {
    SPI->DATA = tx;
    uint32_t start = ticks;
    uint32_t status;
    do {
        status = SPI->SR;
        if ((status & (SPI_SR_WCOL_Msk | SPI_SR_MDF_Msk | SPI_SR_SSERR_Msk)) ||
            (uint32_t)(ticks - start) >= 10u) { return false; }
    } while (!(status & SPI_SR_SPIF_Msk));
    /* Reading DATA clears SPIF (manual 24.3.1.1). */
    *rx = (uint8_t)SPI->DATA;
    return true;
}
