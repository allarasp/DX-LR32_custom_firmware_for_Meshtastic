#ifndef RADIO_PROTOCOL_H
#define RADIO_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#define RP_VERSION 1u
#define RP_FW_MAJOR 0u
#ifdef UART_RADIO_LIVE
#define RP_FW_MINOR 5u
#define RP_FW_PATCH 0u
#else
#define RP_FW_MINOR 3u
#define RP_FW_PATCH 1u
#endif
#define RP_MAX_PAYLOAD 272u
#define RP_BODY_MAX (7u + RP_MAX_PAYLOAD + 2u)
enum rp_status { RP_OK, RP_VERSION_ERROR, RP_FRAME_ERROR, RP_CRC_ERROR, RP_TIMEOUT,
    RP_UNKNOWN, RP_LENGTH, RP_RANGE, RP_COMBINATION, RP_UNSUPPORTED, RP_RADIO_ERROR,
    RP_NOT_READY, RP_READBACK, RP_DISABLED, RP_OVERFLOW, RP_DUPLICATE };
enum rp_command { RP_GET_VERSION=1, RP_GET_CAPABILITIES, RP_GET_STATUS,
    RP_SET_FREQUENCY, RP_SET_LORA, RP_SET_PACKET, RP_SET_POWER, RP_APPLY,
    RP_START_RX, RP_STOP_RX, RP_SEND, RP_GET_ERRORS, RP_RESET, RP_GET_CONFIG, RP_SET_MODE, RP_GET_LINK_STATS, RP_GET_READBACK, RP_TEST_XOSC, RP_GET_RF_SWITCH, RP_PING, RP_ARM_TX, RP_GET_RADIO_STATS, RP_GET_TRANSPORT_STATS };
typedef void (*rp_write_fn)(uint8_t byte);
typedef uint16_t (*rp_handler_fn)(uint8_t command, const uint8_t *data, uint16_t length, uint8_t *reply);
struct rp_parser {
    uint8_t body[RP_BODY_MAX];
    uint16_t used;
    bool collecting, escaped, discard;
    uint8_t fault;
    uint32_t started, last, frames, errors, timeouts;
    rp_write_fn write;
    rp_handler_fn handler;
    bool (*can_emit)(uint8_t kind,uint16_t worst_size);
    uint32_t tx_drops;
};
uint16_t rp_crc_byte(uint16_t crc, uint8_t byte);
void rp_init(struct rp_parser *p, rp_write_fn write, rp_handler_fn handler);
void rp_feed(struct rp_parser *p, uint8_t byte, uint32_t now);
void rp_poll(struct rp_parser *p, uint32_t now);
void rp_abort(struct rp_parser *p);
bool rp_emit(struct rp_parser *p, uint8_t kind, uint8_t command, uint16_t seq, const uint8_t *data, uint16_t length);
static inline uint16_t rp_u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static inline uint32_t rp_u32(const uint8_t *p) { return (uint32_t)rp_u16(p) | (uint32_t)rp_u16(p+2)<<16; }
static inline void rp_put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void rp_put32(uint8_t *p,uint32_t v) { rp_put16(p,(uint16_t)v); rp_put16(p+2,(uint16_t)(v>>16)); }
#endif
