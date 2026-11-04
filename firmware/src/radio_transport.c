#include <stddef.h>
#include "radio_diag.h"
#include "sx126x_hal.h"

struct radio_link_trace radio_link;

static bool ready(void) {
    uint32_t start = radio_io_millis(), remaining = 2000000u;
    while (radio_io_busy()) {
        radio_io_idle();
        if ((uint32_t)(radio_io_millis() - start) >= 100u) {
            radio_link.error = RADIO_LINK_BUSY_TIMEOUT;
            ++radio_link.busy_timeouts;
            return false;
        }
        if (--remaining == 0) {
            radio_link.error = RADIO_LINK_CLOCK_STALL;
            return false;
        }
    }
    return true;
}

static bool delay(uint32_t ms) {
    if (radio_io_delay(ms)) { return true; }
    radio_link.error = RADIO_LINK_CLOCK_STALL;
    return false;
}

static bool safe_command(bool read, const uint8_t *cmd, uint16_t count, uint16_t size) {
#ifdef UART_RADIO_LIVE
    if(read && ((cmd[0]==0x12&&count==2&&size==2) || (cmd[0]==0x13&&count==2&&size==2) ||
       (cmd[0]==0x14&&count==2&&size==3) || (cmd[0]==0x1e&&count==3&&cmd[2]==0&&size<=255)))return true;
    if(read && cmd[0]==0x1d&&count==4&&cmd[1]==8&&cmd[2]==0xd8&&size==1)return true;
    if(!read) {
      if(size==0 && ((cmd[0]==0x82&&count==4) || (cmd[0]==0x83&&count==4) ||
        (cmd[0]==0x98&&count==3&&((cmd[1]==0x6b&&cmd[2]==0x6f)||(cmd[1]==0xd7&&cmd[2]==0xdb))) ||
        (cmd[0]==0x93&&count==2&&cmd[1]==0x20) || (cmd[0]==0x8f&&count==3) ||
        (cmd[0]==0x08&&count==9) || (cmd[0]==0x02&&count==3) ||
        (cmd[0]==0x95&&count==5&&cmd[1]==4&&cmd[2]==7&&cmd[3]==0&&cmd[4]==1) ||
        (cmd[0]==0x8e&&count==3&&(int8_t)cmd[1]>=-9&&(int8_t)cmd[1]<=22&&cmd[2]==4)))return true;
      if(cmd[0]==0x0e&&count==2&&size<=255)return true;
      if(cmd[0]==0x0d&&count==3&&cmd[1]==8&&cmd[2]==0xd8&&size==1)return true;
    }
#endif
    /* Deliberate non-RF whitelist: SetTx, RX, FS, DIO2/DIO3 and RF setup blocked. */
    if (read) {
        return (cmd[0] == 0xc0 && count == 1 && size == 1) ||
            (cmd[0] == 0x17 && count == 2 && cmd[1] == 0 && size == 2) ||
            (cmd[0] == 0x11 && count == 2 && cmd[1] == 0 && size == 1) ||
            (cmd[0] == 0x1d && count == 4 && cmd[3] == 0 && size > 0 && size <= 4);
    }
#ifdef UART_RADIO_PROTOCOL
    /* XOSC is exposed only by the controlled diagnostic, never SET_MODE. */
    if(size==0 && count==2 && cmd[0]==0x80 && cmd[1]==1) return true;
    /* Non-RF configuration only. A second band guard protects the HAL boundary.
     * PA, RX/TX/FS, image calibration, DIO2 and DIO3 remain blocked. */
    if (size == 0 && cmd[0] == 0x86 && count == 5) {
        uint32_t word = (uint32_t)cmd[1]<<24 | (uint32_t)cmd[2]<<16 |
                        (uint32_t)cmd[3]<<8 | cmd[4];
        if(word >= 454033408u && word <= 455081984u)return true; /* 433..434 MHz */
#ifdef UART_RADIO_LIVE
        return word >= 904921088u && word <= 912261120u; /* 863..870 MHz */
#else
        return false;
#endif
    }
    if (size == 0 && cmd[0] == 0x8b && count == 5)
        return cmd[1]>=5 && cmd[1]<=12 && (cmd[2]<=6 || cmd[2]==8 || cmd[2]==9 || cmd[2]==10) &&
               cmd[3]>=1 && cmd[3]<=4 && cmd[4]<=1;
    if (size == 0 && cmd[0] == 0x8c && count == 7)
        return cmd[3]<=1 && cmd[4]>0 && cmd[5]<=1 && cmd[6]<=1;
    if (cmd[0] == 0x0d && count == 3)
        return (cmd[1]==7 && cmd[2]==0x40 && size==2) ||
               (cmd[1]==7 && cmd[2]==0x36 && size==1) ||
               (cmd[1]==8 && cmd[2]==0x89 && size==1);
#endif
    return size == 0 && count == 2 &&
        ((cmd[0] == 0x80 && cmd[1] == 0) || (cmd[0] == 0x8a && cmd[1] <= 1));
}

static bool byte(uint8_t tx, uint8_t *rx) {
    if (!radio_io_spi(tx, rx)) {
        radio_link.error = RADIO_LINK_SPI_FAULT;
        ++radio_link.spi_failures;
        return false;
    }
    unsigned n = radio_link.length++;
    if (n < sizeof(radio_link.tx)) { radio_link.tx[n] = tx; radio_link.rx[n] = *rx; }
    return true;
}

static sx126x_hal_status_t transfer(bool read, const uint8_t *cmd, uint16_t count,
                                    const uint8_t *tx, uint8_t *rx, uint16_t size) {
    radio_link.error = RADIO_LINK_OK;
    radio_link.length = 0;
    if (!cmd || !count || (size && ((read && !rx) || (!read && !tx)))) {
        radio_link.error = RADIO_LINK_BAD_ARGUMENT;
        return SX126X_HAL_STATUS_ERROR;
    }
    if (!safe_command(read, cmd, count, size)) {
        radio_link.error = RADIO_LINK_COMMAND_BLOCKED;
        return SX126X_HAL_STATUS_ERROR;
    }
    if (!ready()) { return SX126X_HAL_STATUS_ERROR; }
    radio_io_select(true);
    uint8_t discard;
    bool ok = true;
    for (uint16_t i=0; ok && i<count; ++i) { ok = byte(cmd[i], &discard); }
    for (uint16_t i=0; ok && i<size; ++i) { ok = byte(read ? 0 : tx[i], read ? &rx[i] : &discard); }
    radio_io_select(false);
    /* >600 ns BUSY assertion latency after NSS rises. 2 tick transitions
     * guarantee at least 1 ms before sampling, despite tick quantization. */
    if (ok) { ok = delay(2) && ready(); }
    if (ok) { ++radio_link.transactions; }
    return ok ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *cmd,
                                    uint16_t count, const uint8_t *data, uint16_t size) {
    (void)context;
    return transfer(false,cmd,count,data,NULL,size);
}
sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *cmd,
                                   uint16_t count, uint8_t *data, uint16_t size) {
    (void)context;
    return transfer(true,cmd,count,NULL,data,size);
}
sx126x_hal_status_t sx126x_hal_reset(const void *context) {
    (void)context;
    radio_link.error = RADIO_LINK_OK;
    radio_link.length = 0;
    radio_io_select(false);
    radio_io_reset(true);
    bool ok = delay(2); /* >=1 ms physical pulse; Semtech typical minimum 100 us */
    radio_io_reset(false); /* release reset even if the timer failed */
    radio_link.reset_busy_high = radio_io_busy();
    if (ok) { ok = delay(10) && ready(); }
    if (ok) { ++radio_link.resets; }
    return ok ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}
sx126x_hal_status_t sx126x_hal_wakeup(const void *context) {
    (void)context;
    radio_link.error = RADIO_LINK_OK;
    radio_io_select(true);
    bool ok = delay(2) && ready();
    radio_io_select(false);
    return ok ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}
