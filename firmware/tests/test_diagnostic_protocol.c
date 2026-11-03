#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "uart_diag.h"

static const char *send(struct diagnostic_line *line, const char *text) {
    const char *reply = NULL;
    while (*text) {
        const char *next = diag_line_feed(line, (uint8_t)*text++);
        if (next) { assert(!reply); reply = next; }
    }
    return reply;
}
int main(void) {
    struct diagnostic_line line = {0};
    assert(!strcmp(send(&line,"PING\r\n"),"PONG\r\n"));
    assert(!strcmp(send(&line,"VERSION\n"),"DXLR32 UARTDIAG 0.1.1\r\n"));
    assert(strstr(send(&line,"STATUS\r"),"BAUD=115200"));
    assert(send(&line,"\r\n") == NULL);
    assert(!strcmp(send(&line,"bad\n"),"ERR UNKNOWN COMMAND\r\n"));
    assert(send(&line,"PI") == NULL);
    assert(!strcmp(send(&line,"NG\n"),"PONG\r\n"));
    assert(!strcmp(send(&line,"PINGxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"),"ERR LINE TOO LONG\r\n"));
    assert(!strcmp(send(&line,"PING\n"),"PONG\r\n"));
    assert(diag_line_feed(&line, 0) == NULL);
    assert(!strcmp(send(&line,"PING\n"),"ERR LINE TOO LONG\r\n"));
    puts("Diagnostic protocol tests passed (host only, no UART hardware)");
}
