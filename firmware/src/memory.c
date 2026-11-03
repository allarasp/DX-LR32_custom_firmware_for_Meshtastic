/* Freestanding support for upstream radio routines; no heap or C runtime. */
#include <stddef.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t count) {
    unsigned char *out = dst;
    const unsigned char *in = src;
    for (size_t i = 0; i < count; ++i) { out[i] = in[i]; }
    return dst;
}
void *memset(void *dst, int value, size_t count) {
    unsigned char *out = dst;
    for (size_t i = 0; i < count; ++i) { out[i] = (unsigned char)value; }
    return dst;
}
