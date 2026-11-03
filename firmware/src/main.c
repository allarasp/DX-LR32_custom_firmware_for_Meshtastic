#include "board.h"
#include "sx126x.h"

/* Bring-up diagnostics only: no RF configuration, packet TX, or Meshtastic parser. */
int main(void) {
    board_init();
    for (;;) {
        uint8_t command;
        if (!board_uart_read(&command)) { continue; }
        if (command == 'V') {
            static const char version[] = "DX-LR32 offline scaffold v0\r\n";
            for (unsigned i = 0; i < sizeof(version) - 1; ++i) {
                if (!board_uart_write((uint8_t)version[i])) { break; }
            }
        } else if (command == 'S') {
            sx126x_chip_status_t status;
            sx126x_status_t result = sx126x_get_status(0, &status);
            /* Binary reply: 'S', success=0/failure=1, mode, command status. */
            uint8_t response[4] = {'S', result == SX126X_STATUS_OK ? 0u : 1u,
                result == SX126X_STATUS_OK ? (uint8_t)status.chip_mode : 0u,
                result == SX126X_STATUS_OK ? (uint8_t)status.cmd_status : 0u};
            for (unsigned i = 0; i < sizeof(response); ++i) {
                if (!board_uart_write(response[i])) { break; }
            }
        }
    }
}
