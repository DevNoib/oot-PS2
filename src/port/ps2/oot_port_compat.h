#ifndef OOT_PORT_COMPAT_H
#define OOT_PORT_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#include "assert.h"
#include "math.h"

#if defined(TARGET_PSP)
#ifndef OOT_PSP_FAST_SQRT
#define OOT_PSP_FAST_SQRT 1
#endif

#if OOT_PSP_FAST_SQRT
static inline __attribute__((always_inline)) float OotPort_Sqrtf(float value) {
    float result;

    __asm__("sqrt.s %0, %1" : "=f"(result) : "f"(value));
    return result;
}
#define sqrtf OotPort_Sqrtf
#define sqrt(value) OotPort_Sqrtf((float)(value))
#endif
#endif

#if defined(TARGET_PSP) || defined(TARGET_PS2)
#define OOT_PORT_SYSTEM_HEAP_SIZE (4U * 1024U * 1024U)

extern unsigned char gOotPortSystemHeap[OOT_PORT_SYSTEM_HEAP_SIZE];
extern unsigned char __bss_start[];
extern unsigned char _end[];

int OotPort_IsRuntimeByteRangeSlow(uintptr_t start, uintptr_t end) __attribute__((noinline));

static inline __attribute__((always_inline)) int OotPort_IsSystemHeapRange(const void* ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;
    const uintptr_t heapStart = (uintptr_t)gOotPortSystemHeap;
    const uintptr_t heapEnd = heapStart + OOT_PORT_SYSTEM_HEAP_SIZE;

    if (ptr == NULL || size == 0 || start > UINTPTR_MAX - size) {
        return 0;
    }

    end = start + size;
    return start >= heapStart && end <= heapEnd;
}

static inline int OotPort_IsRuntimeByteRange(const void* ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;

    if (ptr == NULL || size == 0 || start > UINTPTR_MAX - size) {
        return 0;
    }

    end = start + size;
    if (start >= (uintptr_t)__bss_start && end <= (uintptr_t)_end) {
        return 1;
    }

    return OotPort_IsRuntimeByteRangeSlow(start, end);
}
#endif

#endif
