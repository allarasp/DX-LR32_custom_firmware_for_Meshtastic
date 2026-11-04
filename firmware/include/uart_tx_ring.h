#ifndef UART_TX_RING_H
#define UART_TX_RING_H
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
/* Main produces; UART ISR consumes. Each publishes its index after the byte. */
struct uart_tx_ring {uint8_t bytes[1024];volatile uint16_t head,tail;};
static inline unsigned uart_tx_free(const struct uart_tx_ring *q) {
    return (q->tail-q->head-1u)&1023u;
}
static inline bool uart_tx_push(struct uart_tx_ring *q,uint8_t b) {
    uint16_t next=(q->head+1u)&1023u;
    if(next==q->tail)return false;
    q->bytes[q->head]=b;atomic_signal_fence(memory_order_release);q->head=next;return true;
}
static inline bool uart_tx_pop(struct uart_tx_ring *q,uint8_t *b) {
    if(q->tail==q->head)return false;
    atomic_signal_fence(memory_order_acquire);
    *b=q->bytes[q->tail];atomic_signal_fence(memory_order_release);q->tail=(q->tail+1u)&1023u;return true;
}
#endif
