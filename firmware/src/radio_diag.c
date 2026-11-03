#include <stddef.h>
#include "radio_diag.h"
#include "sx126x.h"

static char response[224];
static unsigned used;
static bool initialized;
static uint8_t raw_status = 0xff;
static sx126x_errors_mask_t device_errors = 0xffff;
static const char *failure;

static void text(const char *s) {
    while (*s && used < sizeof(response)-1) { response[used++] = *s++; }
    response[used] = 0;
}
static void hex(uint32_t value, unsigned digits) {
    static const char alphabet[] = "0123456789ABCDEF";
    for (unsigned i=digits; i>0; --i) {
        char pair[2] = {alphabet[(value >> ((i-1)*4)) & 15u],0};
        text(pair);
    }
}
static bool hal_ok(sx126x_status_t status) {
    if (status == SX126X_STATUS_OK) { return true; }
    static const char *const names[] = {"HAL_ERROR","BUSY_TIMEOUT","SPI_TIMEOUT_OR_FAULT",
        "CLOCK_STALL","BAD_ARGUMENT","COMMAND_BLOCKED"};
    unsigned n = (unsigned)radio_link.error;
    failure = n < sizeof(names)/sizeof(names[0]) ? names[n] : "HAL_ERROR";
    return false;
}

static bool status(void) {
    sx126x_chip_status_t state;
    raw_status = 0xff;
    if (!hal_ok(sx126x_get_status(NULL,&state))) { return false; }
    raw_status = radio_link.rx[1];
    /* A completed SPI shift is insufficient: reject floating/stuck/invalid data. */
    if ((raw_status & 0x81u) || state.chip_mode != SX126X_CHIP_MODE_STBY_RC) {
        failure = "INVALID_STATUS_OR_NOT_STBY_RC";
        return false;
    }
    if (state.cmd_status == SX126X_CMD_STATUS_CMD_TIMEOUT ||
        state.cmd_status == SX126X_CMD_STATUS_CMD_PROCESS_ERROR ||
        state.cmd_status == SX126X_CMD_STATUS_CMD_EXEC_FAILURE) {
        failure = "RADIO_COMMAND_ERROR";
        return false;
    }
    return true;
}
static bool errors(void) {
    device_errors = 0xffff;
    if (!hal_ok(sx126x_get_device_errors(NULL,&device_errors))) { return false; }
    if (device_errors & ~0x017fu) { failure = "INVALID_ERROR_RESERVED_BITS"; return false; }
    return true;
}

static const char *report(const char *name, bool success) {
    used = 0;
    text(name);
    text(success ? (device_errors ? " WARN" : " OK") : " ERROR");
    if (!success) { text(" reason="); text(failure ? failure : "UNKNOWN"); }
    text(" raw=0x"); hex(raw_status,2);
    text(" mode="); text((raw_status >> 4 & 7) == 2 ? "STBY_RC" : "UNVERIFIED");
    text(" errors=0x"); hex(device_errors,4);
    text(" busy="); text(radio_io_busy() ? "1" : "0");
    text(" dio1="); text(radio_io_dio1() ? "1" : "0");
    text(" tx=");
    for (unsigned i=0; i<radio_link.length && i<sizeof(radio_link.tx); ++i) { hex(radio_link.tx[i],2); }
    text(" rx=");
    for (unsigned i=0; i<radio_link.length && i<sizeof(radio_link.rx); ++i) { hex(radio_link.rx[i],2); }
    if (!success) { text(" spi_sr=0x"); hex(radio_io_spi_flags(),2); }
    return response;
}

static bool reset_radio(void) {
    return hal_ok(sx126x_reset(NULL)) &&
        hal_ok(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC)) && status() && errors();
}

const char *radio_diag_init(void) {
    initialized = false;
    failure = NULL;
    radio_io_init();
    bool success = reset_radio();
    initialized = success && device_errors == 0;
    report("RADIO_INIT",success);
    text(" reset_busy_seen="); text(radio_link.reset_busy_high ? "1" : "0");
    text("\r\n");
    return response;
}

const char *radio_diag_summary(void) {
    return initialized ? "STATUS CLK=22120000 BAUD=115200 RADIO=READY_RC\r\n" :
                         "STATUS CLK=22120000 BAUD=115200 RADIO=ERROR\r\n";
}

const char *radio_diag_execute(enum radio_diag_command command) {
    failure = NULL;
    bool success = false;
    const char *name = "RADIO_TEST";
    sx126x_pkt_type_t original = SX126X_PKT_TYPE_GFSK;
    sx126x_pkt_type_t gfsk = (sx126x_pkt_type_t)255;
    sx126x_pkt_type_t lora = (sx126x_pkt_type_t)255;
    sx126x_pkt_type_t restored = (sx126x_pkt_type_t)255;
    if (command == RADIO_RESET) {
        name = "RADIO_RESET";
        success = reset_radio();
    } else if (command == RADIO_STATUS) {
        name = "RADIO_STATUS";
        /* Read errors first so the final trace is the exact GetStatus frame. */
        success = errors() && status();
    } else if (command == RADIO_ERRORS) {
        name = "RADIO_ERRORS";
        success = status() && errors();
    } else if (command == RADIO_TEST) {
        /* Reversible packet-engine configuration in STBY_RC only. No RF clock,
         * frequency, PA, RX, TX, RF-switch or TCXO commands are issued. */
        success = status() && hal_ok(sx126x_get_pkt_type(NULL,&original));
        if (success && original != SX126X_PKT_TYPE_GFSK && original != SX126X_PKT_TYPE_LORA) {
            failure = "INVALID_PACKET_TYPE"; success = false;
        }
        if (success) {
            success = hal_ok(sx126x_set_pkt_type(NULL,SX126X_PKT_TYPE_GFSK)) &&
                hal_ok(sx126x_get_pkt_type(NULL,&gfsk)) && gfsk == SX126X_PKT_TYPE_GFSK &&
                hal_ok(sx126x_set_pkt_type(NULL,SX126X_PKT_TYPE_LORA)) &&
                hal_ok(sx126x_get_pkt_type(NULL,&lora)) && lora == SX126X_PKT_TYPE_LORA;
            /* Always attempt restoration after a state-changing test. Retain
             * the first failure instead of hiding it with a later success. */
            const char *first_failure = failure;
            bool restore_ok = hal_ok(sx126x_set_pkt_type(NULL,original)) &&
                hal_ok(sx126x_get_pkt_type(NULL,&restored)) && restored == original;
            if (first_failure) { failure = first_failure; }
            success = success && restore_ok;
            if (!success && !failure) { failure = "PACKET_TYPE_READBACK_MISMATCH"; }
        }
        if (success) { success = status() && errors(); }
    } else {
        failure = "UNKNOWN_COMMAND";
    }
    initialized = success && device_errors == 0;
    report(name,success);
    if (command == RADIO_TEST) {
        text(" gfsk=0x"); hex((uint8_t)gfsk,2);
        text(" lora=0x"); hex((uint8_t)lora,2);
        text(" restored=0x"); hex((uint8_t)restored,2);
    }
    if (command == RADIO_RESET) {
        text(" reset_busy_seen="); text(radio_link.reset_busy_high ? "1" : "0");
    }
    text("\r\n");
    return response;
}
