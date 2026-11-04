#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "radio_diag.h"
#include "uart_diag.h"
#include "sx126x_hal.h"

static uint32_t now;
static bool busy, frozen, spi_fault, delay_fault, selected, reset_low, stuck_type;
static unsigned index_in_frame, shifts;
static uint8_t opcode, parameter, packet_type, status_byte = 0x24;
void radio_io_init(void) {}
uint32_t radio_io_millis(void) { return now; }
void radio_io_idle(void) { if (!frozen) ++now; }
bool radio_io_delay(uint32_t ms) { now += ms; return !delay_fault; }
bool radio_io_busy(void) { return busy; }
bool radio_io_dio1(void) { return false; }
void radio_io_select(bool active) {
    if (!active && selected && opcode == 0x8a && index_in_frame == 2) packet_type = parameter;
    selected = active;
    if (active) index_in_frame = 0;
}
void radio_io_reset(bool asserted) { reset_low = asserted; if (asserted) packet_type = 0; }
uint32_t radio_io_spi_flags(void) { return 0; }
bool radio_io_spi(uint8_t tx, uint8_t *rx) {
    assert(selected);
    ++shifts;
    if (spi_fault) return false;
    unsigned i = index_in_frame++;
    if (i == 0) opcode = tx;
    if (i == 1) parameter = tx;
    *rx = 0;
    if (i == 1) *rx = status_byte;
    if (opcode == 0x11 && i == 2) *rx = stuck_type ? 0 : packet_type;
    return true;
}
static const char *send(const char *s) {
    struct diagnostic_line line = {0};
    const char *reply = NULL;
    while (*s) { const char *r = diag_line_feed(&line,(uint8_t)*s++); if (r) reply = r; }
    assert(reply);
    return reply;
}
int main(void) {
    assert(strstr(radio_diag_init(),"RADIO_INIT OK"));
    assert(!selected && !reset_low);
    assert(strstr(send("PING\n"),"PONG"));
    assert(strstr(send("VERSION\n"),"RADIODIAG 0.2.1"));
    assert(strstr(send("STATUS\n"),"READY_RC"));
    assert(strstr(send("RADIO_STATUS\n"),"tx=C000 rx=0024"));
    assert(strstr(send("RADIO_ERRORS\n"),"tx=17000000 rx=00240000"));
    assert(strstr(send("RADIO_TEST\n"),"gfsk=0x00 lora=0x01 restored=0x00"));
    assert(packet_type == 0);
    packet_type = 1;
    assert(strstr(send("RADIO_TEST\n"),"restored=0x01"));
    assert(packet_type == 1);
    stuck_type = true;
    assert(strstr(send("RADIO_TEST\n"),"PACKET_TYPE_READBACK_MISMATCH"));
    stuck_type = false;
    status_byte = 0;
    assert(strstr(send("RADIO_STATUS\n"),"INVALID_STATUS"));
    status_byte = 0xff;
    assert(strstr(send("RADIO_STATUS\n"),"INVALID_STATUS"));
    status_byte = 0x24;
    busy = true;
    assert(strstr(send("RADIO_STATUS\n"),"BUSY_TIMEOUT"));
    frozen = true;
    assert(strstr(send("RADIO_STATUS\n"),"CLOCK_STALL"));
    frozen = busy = false;
    spi_fault = true;
    assert(strstr(send("RADIO_STATUS\n"),"SPI_TIMEOUT_OR_FAULT"));
    assert(!selected);
    spi_fault = false;
    delay_fault = true;
    assert(strstr(send("RADIO_RESET\n"),"CLOCK_STALL"));
    assert(!selected && !reset_low);
    delay_fault = false;
    unsigned before = shifts;
    const uint8_t forbidden[][4] = {{0x83,0,0,0},{0x9d,1,0,0},{0x97,0,0,0},{0x82,0,0,0}};
    for (unsigned i=0; i<sizeof(forbidden)/sizeof(forbidden[0]); ++i) {
        assert(sx126x_hal_write(NULL,forbidden[i],4,NULL,0) == SX126X_HAL_STATUS_ERROR);
        assert(radio_link.error == RADIO_LINK_COMMAND_BLOCKED);
    }
    assert(shifts == before);
    assert(strstr(send("RADIO_RESET\n"),"RADIO_RESET OK"));
    puts("Radio diagnostics passed: Semtech framing, reversible state changes, invalid responses, deadlines, cleanup and RF command rejection (host mocks)");
}
