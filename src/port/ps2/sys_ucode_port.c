#include "sys_ucode.h"

static u64 sPortDummyUCodeBoot[1] __attribute__((aligned(16)));
static u64 sPortDummyUCodeText[1] __attribute__((aligned(16)));
static u64 sPortDummyUCodeData[1] __attribute__((aligned(16)));

u64 gspS2DEX2d_fifoTextStart[1] __attribute__((aligned(16)));
u64 gspS2DEX2d_fifoTextEnd[1] __attribute__((aligned(16)));
u64 gspS2DEX2d_fifoDataStart[1] __attribute__((aligned(16)));
u64 gspS2DEX2d_fifoDataEnd[1] __attribute__((aligned(16)));

u64* SysUcode_GetUCodeBoot(void) {
    return sPortDummyUCodeBoot;
}

size_t SysUcode_GetUCodeBootSize(void) {
    return sizeof(sPortDummyUCodeBoot);
}

u64* SysUcode_GetUCode(void) {
    return sPortDummyUCodeText;
}

u64* SysUcode_GetUCodeData(void) {
    return sPortDummyUCodeData;
}
