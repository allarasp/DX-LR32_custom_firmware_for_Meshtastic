#include "uart_diag.h"

volatile uint32_t startup_cookie = 0x13579bdfu; /* Check actual .data copying. */
volatile uint32_t zero_probe;                 /* Check actual .bss clearing. */

static void write_text(const char *text) {
    while (*text) {
        if (!diag_uart_put((uint8_t)*text++)) { break; }
    }
}

int main(void) {
    diagnostic.data_cookie = startup_cookie;
    diagnostic.bss_zero = zero_probe == 0;
    if (startup_cookie != 0x13579bdfu || zero_probe != 0) {
        diagnostic.errors |= 16;
        for (;;) { diag_watchdog_feed(); }
    }
    zero_probe = 0x76543210u;
    diag_uart_init();
    diagnostic.stage = 4;
    write_text("DXLR32 CUSTOM FW BOOT\r\n");
    diagnostic.stage = 5;
    diagnostic.magic = DIAG_BOOT_MAGIC;
    struct diagnostic_line line = {0};
    for (;;) {
        ++diagnostic.loops;
        diag_watchdog_feed();
        uint8_t byte;
        if (diag_uart_get(&byte)) {
            const char *reply = diag_line_feed(&line, byte);
            if (reply) { ++diagnostic.commands; write_text(reply); }
        }
    }
}
