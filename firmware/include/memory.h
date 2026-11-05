#ifndef FW_MEMORY_H
#define FW_MEMORY_H
#include <stddef.h>
void *memcpy(void *restrict dst,const void *restrict src,size_t count);
int memcmp(const void *a,const void *b,size_t count);
#endif
