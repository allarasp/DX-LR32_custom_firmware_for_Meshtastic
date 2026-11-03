#ifndef RADIO_DIAG_H
#define RADIO_DIAG_H
#include <stdbool.h>
#include <stdint.h>

enum radio_diag_command { RADIO_RESET, RADIO_STATUS, RADIO_ERRORS, RADIO_TEST };
const char *radio_diag_init(void);
const char *radio_diag_execute(enum radio_diag_command command);
const char *radio_diag_summary(void);

enum radio_link_error { RADIO_LINK_OK, RADIO_LINK_BUSY_TIMEOUT,
    RADIO_LINK_SPI_FAULT, RADIO_LINK_CLOCK_STALL, RADIO_LINK_BAD_ARGUMENT,
    RADIO_LINK_COMMAND_BLOCKED };
struct radio_link_trace {
    enum radio_link_error error;
    uint32_t transactions, resets, busy_timeouts, spi_failures;
    uint16_t length;
    uint8_t tx[8], rx[8];
    bool reset_busy_high;
};
extern struct radio_link_trace radio_link;

/* Platform boundary; every wait includes both a deadline and a spin backstop. */
void radio_io_init(void);
uint32_t radio_io_millis(void);
void radio_io_idle(void);
bool radio_io_delay(uint32_t milliseconds);
bool radio_io_busy(void);
bool radio_io_dio1(void);
void radio_io_select(bool active);
void radio_io_reset(bool asserted);
bool radio_io_spi(uint8_t tx, uint8_t *rx);
uint32_t radio_io_spi_flags(void);
#endif
