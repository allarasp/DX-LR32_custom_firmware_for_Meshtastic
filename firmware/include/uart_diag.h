#ifndef UART_DIAG_H
#define UART_DIAG_H
#include <stdbool.h>
#include <stdint.h>

#define DIAG_CLOCK_HZ 22120000u
#define DIAG_BAUD 115200u
#define DIAG_BOOT_MAGIC 0xb007b007u
struct diagnostic_state {
    uint32_t magic, stage, ticks, loops, tx_bytes, rx_bytes, commands, errors;
    uint32_t clock_hz, reset_flags, data_cookie, bss_zero;
};
extern volatile struct diagnostic_state diagnostic;
void diag_watchdog_feed(void);
void diag_uart_init(void);
bool diag_uart_get(uint8_t *byte);
bool diag_uart_put(uint8_t byte);
#ifdef UART_RADIO_LIVE
unsigned diag_uart_tx_free(void);
uint32_t diag_uart_tx_faults(void);
bool diag_uart_tx_poll(void);
#endif

struct diagnostic_line {
    char text[32];
    unsigned used;
    bool discard;
};
const char *diag_line_feed(struct diagnostic_line *line, uint8_t byte);
#endif
