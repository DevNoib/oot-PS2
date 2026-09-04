#include <stdarg.h>
#include <stdio.h>
#include <tamtypes.h>
#include <kernel.h>
#include <ee_regs.h>

extern const u8 msx[];

static int sTraceX;
static int sTraceY;

struct TraceCharSetup {
    u64 dd0[4];
    u32 dw0[1];
    u16 x;
    u16 y;
    u64 dd1[1];
    u32 dw1[2];
    u64 dd2[5];
};

static const struct TraceCharSetup sTraceCharTemplate = {
    {0x1000000000000004ULL, 0xE, 0xA000000000000ULL, 0x50},
    {0}, 100, 100,
    {0x51},
    {8, 8},
    {0x52, 0, 0x53, 0x800000000008010ULL, 0}
};

static int TraceDmaWait(void) {
    u32 timeout = 4000000;
    while ((*R_EE_D2_CHCR & 0x100) != 0 && timeout-- != 0) {
        __asm__ volatile("nop");
    }
    if ((*R_EE_D2_CHCR & 0x100) != 0) {
        *R_EE_D2_CHCR = 0;
        *R_EE_D2_QWC = 0;
        *R_EE_D2_MADR = 0;
        *R_EE_D2_TADR = 0;
        __asm__ volatile("sync.p; nop;");
        return 0;
    }
    return 1;
}

static int TraceDmaSend(const void* addr, int qwc) {
    if (!TraceDmaWait()) {
        return 0;
    }
    *R_EE_D2_QWC = (u32)qwc;
    *R_EE_D2_MADR = (u32)addr;
    *R_EE_D2_CHCR = 0x101;
    return TraceDmaWait();
}

static void TracePutChar(int x, int y, int ch) {
    static struct TraceCharSetup setup __attribute__((aligned(16)));
    static u32 pixels[64] __attribute__((aligned(16)));
    const u8* font;
    int i;
    int j;

    *(struct TraceCharSetup*)UNCACHED_SEG(&setup) = sTraceCharTemplate;
    ((struct TraceCharSetup*)UNCACHED_SEG(&setup))->x = (u16)x;
    ((struct TraceCharSetup*)UNCACHED_SEG(&setup))->y = (u16)y;
    if (!TraceDmaSend(&setup, 6)) {
        return;
    }

    if (ch < 0 || ch > 255) {
        ch = '?';
    }
    font = &msx[ch * 8];
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            ((u32*)UNCACHED_SEG(pixels))[i * 8 + j] = (font[i] & (128 >> j)) ? 0x00FFFFFF : 0x00000000;
        }
    }
    TraceDmaSend(pixels, 16);
}

void OotPs2ScreenTrace_Reset(void) {
    sTraceX = 0;
    sTraceY = 0;
}

void OotPs2ScreenTrace_Print(const char* text) {
    int i;

    if (text == NULL) {
        return;
    }
    *R_EE_GS_PMODE = 0xff62;
    *R_EE_GS_DISPFB2 = 0x1400;
    *R_EE_GS_DISPLAY2 = 0x001bf9ff0983227cULL;
    __asm__ volatile("sync.p; nop;");

    for (i = 0; text[i] != '\0'; i++) {
        int c = (unsigned char)text[i];
        if (c == '\n') {
            sTraceX = 0;
            sTraceY++;
            if (sTraceY >= 40) {
                sTraceY = 0;
            }
            continue;
        }
        TracePutChar(sTraceX * 7, sTraceY * 8, c);
        sTraceX++;
        if (sTraceX >= 80) {
            sTraceX = 0;
            sTraceY++;
            if (sTraceY >= 40) {
                sTraceY = 0;
            }
        }
    }
}
