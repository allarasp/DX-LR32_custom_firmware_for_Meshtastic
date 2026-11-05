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
int memcmp(const void *a,const void *b,size_t count) {
    const unsigned char *left=a,*right=b;
    for(size_t i=0;i<count;++i)if(left[i]!=right[i])return (int)left[i]-(int)right[i];
    return 0;
}
