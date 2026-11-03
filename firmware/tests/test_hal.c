#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "sx126x_hal.h"

static uint32_t clock_ms;
static bool busy, selected;
static unsigned count, selections, releases, fail_at;
static unsigned reset_asserts, reset_releases;
static uint8_t sent[16];
uint32_t board_millis(void) { return clock_ms++; }
bool board_radio_busy(void) { return busy; }
void board_radio_select(bool active) {
    selected = active;
    if (active) { ++selections; } else { ++releases; }
}
bool board_spi_transfer(uint8_t tx, uint8_t *rx) {
    assert(selected && count < sizeof(sent));
    sent[count++] = tx;
    *rx = (uint8_t)(0xa0u + count);
    return count != fail_at;
}
void board_radio_reset(bool asserted) {
    if (asserted) { ++reset_asserts; }
    else { ++reset_releases; }
}
void board_delay_ms(uint32_t ms) { clock_ms += ms; }
static void clear(void) {
    clock_ms = 0; busy = false; selected = false;
    count = selections = releases = fail_at = 0;
    reset_asserts = reset_releases = 0;
    memset(sent, 0, sizeof(sent));
}

int main(void) {
    const uint8_t cmd[] = {0x1d, 0x07, 0x40, 0x00};
    const uint8_t payload[] = {0x11, 0x22};
    uint8_t rx[2];
    clear();
    assert(sx126x_hal_write(NULL, cmd, sizeof(cmd), payload, 2) == SX126X_HAL_STATUS_OK);
    assert(count == 6 && selections == 1 && releases == 1 && !selected);
    assert(!memcmp(sent, cmd, 4) && !memcmp(sent + 4, payload, 2));
    clear();
    assert(sx126x_hal_read(NULL, cmd, sizeof(cmd), rx, 2) == SX126X_HAL_STATUS_OK);
    assert(count == 6 && sent[4] == 0 && sent[5] == 0 && rx[0] == 0xa5 && rx[1] == 0xa6);
    assert(releases == 1 && !selected);
    for (unsigned i = 1; i <= 6; ++i) {
        clear(); fail_at = i;
        assert(sx126x_hal_read(NULL, cmd, sizeof(cmd), rx, 2) == SX126X_HAL_STATUS_ERROR);
        assert(count == i && releases == 1 && !selected);
    }
    clear(); busy = true; clock_ms = UINT32_MAX - 50u;
    assert(sx126x_hal_read(NULL, cmd, sizeof(cmd), rx, 2) == SX126X_HAL_STATUS_ERROR);
    assert(count == 0 && selections == 0 && clock_ms < 100u);
    clear();
    assert(sx126x_hal_read(NULL, cmd, sizeof(cmd), NULL, 1) == SX126X_HAL_STATUS_ERROR);
    assert(sx126x_hal_write(NULL, NULL, 1, NULL, 0) == SX126X_HAL_STATUS_ERROR);
    assert(selections == 0);
    clear(); busy = true;
    assert(sx126x_hal_wakeup(NULL) == SX126X_HAL_STATUS_ERROR);
    assert(selections == 1 && releases == 1 && !selected);
    clear();
    assert(sx126x_hal_wakeup(NULL) == SX126X_HAL_STATUS_OK);
    assert(selections == 1 && releases == 1 && !selected && clock_ms >= 1);
    clear();
    assert(sx126x_hal_reset(NULL) == SX126X_HAL_STATUS_OK);
    assert(clock_ms == 12); /* mock delays + ready() time snapshot */
    assert(reset_asserts == 1 && reset_releases == 1);
    clear(); busy = true;
    assert(sx126x_hal_reset(NULL) == SX126X_HAL_STATUS_ERROR);
    assert(reset_asserts == 1 && reset_releases == 1);
    puts("HAL tests passed: framing, read dummy bytes, SPI failures, BUSY timeout/wrap, invalid arguments, wake cleanup");
}
