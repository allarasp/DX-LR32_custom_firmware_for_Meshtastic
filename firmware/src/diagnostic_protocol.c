#include <stddef.h>
#include "uart_diag.h"

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
        if (equal(line->text, "VERSION")) { return "DXLR32 UARTDIAG 0.1.1\r\n"; }
        if (equal(line->text, "STATUS")) { return "STATUS CLK=22120000 BAUD=115200 RADIO=UNINITIALIZED\r\n"; }
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
