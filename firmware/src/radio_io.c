#include "cx32l003.h"
#include "board.h"
#include "uart_diag.h"
#include "radio_diag.h"

uint32_t radio_io_millis(void) { return diagnostic.ticks; }
void radio_io_idle(void) { diag_watchdog_feed(); }

bool radio_io_delay(uint32_t milliseconds) {
    uint32_t start = diagnostic.ticks, remaining = 2000000u;
    while ((uint32_t)(diagnostic.ticks - start) < milliseconds) {
        diag_watchdog_feed();
        if (--remaining == 0) { return false; }
    }
    return true;
}

static void pin(GPIO_TypeDef *port, unsigned n, unsigned af, bool output) {
    port->AFR = (port->AFR & ~(15u << (4u*n))) | (af << (4u*n));
    port->PUPDR &= ~(3u << (2u*n));
    port->OTYPER &= ~(1u << n);
    if (output) { port->DIRCR |= 1u << n; }
    else { port->DIRCR &= ~(1u << n); }
}

void radio_io_init(void) {
    RCC->UNLOCK = 0x55aa6699u;
    RCC->HCLKEN |= RCC_HCLKEN_GPIOACKEN | RCC_HCLKEN_GPIOBCKEN | RCC_HCLKEN_GPIODCKEN;
    RCC->PCLKEN |= RCC_PCLKEN_SPICKEN;
    RCC->UNLOCK = 0x55aa6698u;
    /* Preload inactive levels before enabling output drivers. UART/SWD untouched. */
    GPIOA->ODSET = 1u << RADIO_NSS_PIN;
    GPIOA->ODCLR = 1u << RADIO_SCK_PIN;
    GPIOB->ODSET = 1u << RADIO_RESET_PIN;
    GPIOD->ODCLR = 1u << RADIO_MOSI_PIN;
    pin(GPIOA, RADIO_NSS_PIN, 0, true);
    pin(GPIOA, RADIO_SCK_PIN, 3, true);
    pin(GPIOD, RADIO_MOSI_PIN, 3, true);
    pin(GPIOD, RADIO_MISO_PIN, 3, false);
    pin(GPIOB, RADIO_RESET_PIN, 0, true);
    pin(GPIOB, RADIO_BUSY_PIN, 0, false);
    pin(GPIOD, RADIO_DIO1_PIN, 0, false);
    SPI->CR = 0;
    SPI->SSN = SPI_SSN_SSN_Msk;
    (void)SPI->SR;
    (void)SPI->DATA; /* clear any stale transfer completion */
    /* Mode 0, master, Fsys/64 = 345625 Hz with the verified UART clock. */
    SPI->CR = SPI_CR_MSTR_Msk | SPI_CR_SPEN_Msk | SPI_CR_SPR2_Msk | SPI_CR_SPR0_Msk;
}

bool radio_io_busy(void) { return (GPIOB->IDR & (1u << RADIO_BUSY_PIN)) != 0; }
bool radio_io_dio1(void) { return (GPIOD->IDR & (1u << RADIO_DIO1_PIN)) != 0; }
void radio_io_select(bool active) {
    if (active) { GPIOA->ODCLR = 1u << RADIO_NSS_PIN; SPI->SSN = 0; }
    else { GPIOA->ODSET = 1u << RADIO_NSS_PIN; SPI->SSN = SPI_SSN_SSN_Msk; }
}
void radio_io_reset(bool asserted) {
    if (asserted) { GPIOB->ODCLR = 1u << RADIO_RESET_PIN; }
    else { GPIOB->ODSET = 1u << RADIO_RESET_PIN; }
}
uint32_t radio_io_spi_flags(void) { return SPI->SR; }

bool radio_io_spi(uint8_t tx, uint8_t *rx) {
    uint32_t start = diagnostic.ticks, remaining = 2000000u;
    SPI->DATA = tx;
    for (;;) {
        uint32_t flags = SPI->SR;
        if (flags & (SPI_SR_WCOL_Msk | SPI_SR_MDF_Msk | SPI_SR_SSERR_Msk)) { return false; }
        if (flags & SPI_SR_SPIF_Msk) {
            *rx = (uint8_t)SPI->DATA; /* DATA read clears SPIF, CX32 manual 24.3.1.1 */
            return true;
        }
        diag_watchdog_feed();
        if ((uint32_t)(diagnostic.ticks - start) >= 10u || --remaining == 0) { return false; }
    }
}
