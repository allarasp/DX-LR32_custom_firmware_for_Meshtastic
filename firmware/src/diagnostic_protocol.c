#include <stddef.h>
#include "uart_diag.h"
#ifdef RADIO_DIAGNOSTICS
#include "radio_diag.h"
#endif

static bool equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

const char *diag_line_feed(struct diagnostic_line *line, uint8_t byte) {
    if (byte == '\r' || byte == '\n') {
        if (line->discard) {
            line->discard = false;
            line->used = 0;
            return "ERR LINE TOO LONG\r\n";
        }
        if (line->used == 0) { return NULL; }
        line->text[line->used] = 0;
        line->used = 0;
        if (equal(line->text, "PING")) { return "PONG\r\n"; }
        if (equal(line->text, "VERSION")) {
#ifdef RADIO_DIAGNOSTICS
            return "DXLR32 RADIODIAG 0.2.1\r\n";
#else
            return "DXLR32 UARTDIAG 0.1.1\r\n";
#endif
        }
        if (equal(line->text, "STATUS")) {
#ifdef RADIO_DIAGNOSTICS
            return radio_diag_summary();
#else
            return "STATUS CLK=22120000 BAUD=115200 RADIO=UNINITIALIZED\r\n";
#endif
        }
#ifdef RADIO_DIAGNOSTICS
        if (equal(line->text, "RADIO_RESET")) { return radio_diag_execute(RADIO_RESET); }
        if (equal(line->text, "RADIO_STATUS")) { return radio_diag_execute(RADIO_STATUS); }
        if (equal(line->text, "RADIO_ERRORS")) { return radio_diag_execute(RADIO_ERRORS); }
        if (equal(line->text, "RADIO_TEST")) { return radio_diag_execute(RADIO_TEST); }
#endif
        return "ERR UNKNOWN COMMAND\r\n";
    }
    if (line->discard) { return NULL; }
    if (line->used >= sizeof(line->text) - 1 || byte == 0) {
        line->discard = true;
        return NULL;
    }
    line->text[line->used++] = (char)byte;
    return NULL;
}
