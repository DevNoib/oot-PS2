#ifndef STDLIB_H
#define STDLIB_H

#if defined(TARGET_PSP) || defined(PLATFORM_PSP) || defined(TARGET_PS2)
#include_next <stdlib.h>
#else

typedef struct lldiv_t {
    long long quot;
    long long rem;
} lldiv_t;

typedef struct ldiv_t {
    long quot;
    long rem;
} ldiv_t;

ldiv_t ldiv(long num, long denom);
lldiv_t lldiv(long long num, long long denom);

#endif

#endif
