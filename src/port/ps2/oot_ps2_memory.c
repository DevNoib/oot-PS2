#include "oot_port_memory.h"

#include <stdint.h>
#include <string.h>

void* OotPort_MemcpyFast(void* dst, const void* src, size_t size) {
    void* result = dst;
    uint8_t* dst8 = dst;
    const uint8_t* src8 = src;

    if ((size < 32) || ((((uintptr_t)dst8 | (uintptr_t)src8) & 0xF) != 0)) {
        return memcpy(dst, src, size);
    }

    while (size >= 32) {
        __asm__ volatile(
            "lq $8, 0(%1)\n"
            "lq $9, 16(%1)\n"
            "sq $8, 0(%0)\n"
            "sq $9, 16(%0)\n"
            :
            : "r"(dst8), "r"(src8)
            : "$8", "$9", "memory");

        dst8 += 32;
        src8 += 32;
        size -= 32;
    }

    while (size >= 8) {
        __asm__ volatile(
            "ld $8, 0(%1)\n"
            "sd $8, 0(%0)\n"
            :
            : "r"(dst8), "r"(src8)
            : "$8", "memory");

        dst8 += 8;
        src8 += 8;
        size -= 8;
    }

    while (size != 0) {
        *dst8++ = *src8++;
        size--;
    }

    return result;
}