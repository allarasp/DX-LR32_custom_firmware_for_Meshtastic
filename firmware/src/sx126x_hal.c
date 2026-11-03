#include <stddef.h>
#include "board.h"
#include "sx126x_hal.h"

static bool ready(void) {
    uint32_t start = board_millis();
    while (board_radio_busy()) {
        if ((uint32_t)(board_millis() - start) >= 100u) { return false; }
    }
    return true;
}

static sx126x_hal_status_t transfer(const uint8_t *cmd, uint16_t cmd_len,
                                    const uint8_t *tx, uint8_t *rx, uint16_t len) {
    if (cmd == NULL || cmd_len == 0 || (len && tx == NULL && rx == NULL)) {
        return SX126X_HAL_STATUS_ERROR;
    }
    if (!ready()) { return SX126X_HAL_STATUS_ERROR; }
    board_radio_select(true);
    uint8_t discard;
    bool ok = true;
    for (uint16_t i = 0; ok && i < cmd_len; ++i) {
        ok = board_spi_transfer(cmd[i], &discard);
    }
    for (uint16_t i = 0; ok && i < len; ++i) {
        ok = board_spi_transfer(tx ? tx[i] : SX126X_NOP, rx ? &rx[i] : &discard);
    }
    board_radio_select(false);
    return ok ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *cmd,
                                    uint16_t cmd_len, const uint8_t *data, uint16_t len) {
    (void)context;
    return transfer(cmd, cmd_len, data, NULL, len);
}
sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *cmd,
                                   uint16_t cmd_len, uint8_t *data, uint16_t len) {
    (void)context;
    return transfer(cmd, cmd_len, NULL, data, len);
}
sx126x_hal_status_t sx126x_hal_reset(const void *context) {
    (void)context;
    board_radio_reset(true);
    board_delay_ms(1);
    board_radio_reset(false);
    board_delay_ms(10);
    return ready() ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}
sx126x_hal_status_t sx126x_hal_wakeup(const void *context) {
    (void)context;
    board_radio_select(true);
    /* Allow wake-up initialization before checking BUSY; not called by main. */
    board_delay_ms(1);
    bool ok = ready();
    board_radio_select(false);
    return ok ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}
