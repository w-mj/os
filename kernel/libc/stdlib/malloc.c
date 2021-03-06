#include <memory/kmalloc.h>

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

void *malloc(size_t size) {
    return kmalloc(size);
}

void *calloc(size_t num, size_t size) {
    return malloc(num * size);
}


void *realloc(void *addr, size_t size) {
    return krealloc(addr, size);
}

void free(void *addr) {
    if (addr == NULL)
        return;
    kfree(addr);
}

void abort() {
    while (1);
}
