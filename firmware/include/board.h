#ifndef BOARD_H
#define BOARD_H
#include <stdbool.h>
#include <stdint.h>

/* DX-LR32 map; AFs checked against datasheet table 4.
 * RESET/BUSY corrected using surviving factory code and real SPI readback. */
#define RADIO_NSS_PIN 2u       /* PA2: GPIO, active low */
#define RADIO_SCK_PIN 1u       /* PA1: AF3 */
#define RADIO_MOSI_PIN 3u      /* PD3: AF3 */
#define RADIO_MISO_PIN 2u      /* PD2: AF3 */
#define RADIO_RESET_PIN 4u     /* PB4: GPIO, active low; factory table 0x7e6c */
#define RADIO_BUSY_PIN 5u      /* PB5: GPIO input; factory table 0x7e7c */
#define RADIO_DIO1_PIN 4u      /* PD4: GPIO input */
#define UART_TX_PIN 5u         /* PD5: AF7 UART0_TX */
#define UART_RX_PIN 6u         /* PD6: AF7 UART0_RX */
#define AUX_PIN 3u             /* PA3: reserved, input for now */
#define M0_PIN 5u              /* PC5: input */
#define M1_PIN 6u              /* PC6: input */
#define BOARD_UART_BAUD 9600u

void board_init(void);
bool board_uart_read(uint8_t *byte);
bool board_uart_write(uint8_t byte);
uint32_t board_millis(void);
void board_delay_ms(uint32_t ms);
bool board_radio_busy(void);
bool board_radio_dio1(void);
void board_radio_select(bool active);
void board_radio_reset(bool asserted);
bool board_spi_transfer(uint8_t tx, uint8_t *rx);
#endif
