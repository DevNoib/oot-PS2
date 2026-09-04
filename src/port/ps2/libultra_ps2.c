#include "ultra64.h"
#include "attributes.h"
#include "oot_ps2_platform.h"
#include <stdarg.h>
#include <stdio.h>
#include "oot_port_audio_backend.h"
#include "oot_port_controls.h"
#include "versions.h"

void osSyncPrintf(const char* fmt, ...) {
}

void isPrintfInit(void) {
}

void __osInitialize_common(void) {
}

void __osInitialize_autodetect(void) {
}

u32 osGetMemSize(void) {
    return 0;
}

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count) {
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    return 0;
}

s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    return 0;
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    return 0;
}

void osSetEventMesg(UNUSED OSEvent e, UNUSED OSMesgQueue* mq, UNUSED OSMesg msg) {
}

void osCreateThread(OSThread* thread, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
}

void osStartThread(OSThread* thread) {
}

void osDestroyThread(OSThread* thread) {
}

void osStopThread(OSThread* thread) {
}

void osYieldThread(void) {
}

void osSetThreadPri(OSThread* thread, OSPri pri) {
}

OSPri osGetThreadPri(OSThread* thread) {
    return 0;
}

OSId osGetThreadId(OSThread* thread) {
    return 0;
}

OSThread* __osGetActiveQueue(void) {
    return NULL;
}

OSThread* __osGetCurrFaultedThread(void) {
    return NULL;
}

OSTime osGetTime(void) {
    return 0;
}

void osSetTime(UNUSED OSTime time) {
}

u32 osGetCount(void) {
    return 0;
}

s32 osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    return 0;
}

s32 osStopTimer(OSTimer* timer) {
    return 0;
}

OSIntMask osGetIntMask(void) {
    return 0;
}

OSIntMask osSetIntMask(OSIntMask mask) {
    return 0;
}

s32 __osDisableInt(void) {
    return 0;
}

void __osRestoreInt(UNUSED s32 enable) {
}

void __osSetHWIntrRoutine(UNUSED OSHWIntr intr, UNUSED s32 (*callback)(void), UNUSED void* sp) {
}

void __osGetHWIntrRoutine(UNUSED OSHWIntr intr, s32 (**callbackOut)(void), void** spOut) {
}

void __osSetSR(UNUSED u32 value) {
}

u32 __osGetSR(void) {
    return 0;
}

void __osSetFpcCsr(UNUSED u32 value) {
}

u32 __osGetFpcCsr(void) {
    return 0;
}

u32 __osGetCause(void) {
    return 0;
}

void __osSetCompare(UNUSED u32 value) {
}

void __osSetWatchLo(UNUSED u32 value) {
}

u32 __osProbeTLB(UNUSED void* addr) {
    return 0;
}

void osUnmapTLBAll(void) {
}

void osMapTLBRdb(void) {
}

void osWritebackDCache(void* vaddr, s32 nbytes) {
}

void osInvalDCache(void* vaddr, s32 nbytes) {
}

void osWritebackDCacheAll(void) {
}

void osInvalICache(UNUSED void* vaddr, UNUSED s32 nbytes) {
}

u32 osVirtualToPhysical(void* vaddr) {
    return 0;
}

void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQueue, OSMesg* cmdBuf, s32 cmdMsgCnt) {
}

OSMesgQueue* osPiGetCmdQueue(void) {
    return NULL;
}

OSPiHandle* osCartRomInit(void) {
    return NULL;
}

OSPiHandle* osDriveRomInit(void) {
    return NULL;
}

s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* mb, s32 direction) {
    return 0;
}

s32 osPiStartDma(OSIoMesg* mb, s32 priority, s32 direction, uintptr_t devAddr, void* vAddr, size_t nbytes,
                 OSMesgQueue* mq) {
    return 0;
}

void osCreateViManager(UNUSED OSPri pri) {
}

void osViSetMode(OSViMode* mode) {
}

void osViBlack(UNUSED u8 active) {
}

void osViSetSpecialFeatures(UNUSED u32 func) {
}

void osViSwapBuffer(void* frameBufPtr) {
}

void* osViGetCurrentFramebuffer(void) {
    return NULL;
}

void* osViGetNextFramebuffer(void) {
    return NULL;
}

void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
}

void osViSetYScale(f32 scale) {
}

void osViSetXScale(f32 value) {
}

void osViExtendVStart(UNUSED u32 value) {
}

OSViContext* __osViGetCurrentContext(void) {
    return NULL;
}

s32 osContInit(OSMesgQueue* mq, u8* ctlBitfield, OSContStatus* status) {
    return 0;
}

s32 osContStartQuery(UNUSED OSMesgQueue* mq) {
    return 0;
}

void osContGetQuery(OSContStatus* data) {
}

s32 osContSetCh(UNUSED u8 ch) {
    return 0;
}

s32 osContStartReadData(UNUSED OSMesgQueue* mq) {
    return 0;
}

void osContGetReadData(OSContPad* contData) {
}

s32 osPfsIsPlug(UNUSED OSMesgQueue* mq, u8* pattern) {
    return 0;
}

s32 osPfsInitPak(UNUSED OSMesgQueue* queue, UNUSED OSPfs* pfs, UNUSED s32 channel) {
    return 0;
}

s32 osMotorInit(UNUSED OSMesgQueue* ctrlrqueue, UNUSED OSPfs* pfs, UNUSED s32 channel) {
    return 0;
}

s32 __osMotorAccess(UNUSED OSPfs* pfs, UNUSED s32 vibrate) {
    return 0;
}

s32 osAfterPreNMI(void) {
    return 0;
}

void osSpTaskLoad(UNUSED OSTask* intp) {
}

void osSpTaskStartGo(UNUSED OSTask* tp) {
}

void osSpTaskYield(void) {
}

u32 osSpTaskYielded(UNUSED OSTask* task) {
    return 0;
}

u32 osDpGetStatus(void) {
    return 0;
}

void osDpSetStatus(UNUSED u32 status) {
}

s32 osAiSetFrequency(u32 frequency) {
    return 0;
}

u32 osAiGetLength(void) {
    return 0;
}
