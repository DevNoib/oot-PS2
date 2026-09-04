#include "oot_ps2_platform.h"
#include <delaythread.h>
#include <ee_regs.h>
#include <stdarg.h>
#include <fcntl.h>
#include <kernel.h>
#include <libpad.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <timer.h>
#include <unistd.h>

void OotPs2Platform_SetDiscBoot(s32 discBoot) {
}

s32 OotPs2Platform_IsDiscBoot(void) {
    return 0;
}

void OotPs2Trace_Init(void) {
}

void OotPs2Trace_EnableScreen(void) {
}

void OotPs2Trace_Log(const char* format, ...) {
}

const char* OotPs2Trace_Path(void) {
    return NULL;
}

OotPs2Handle OotPs2Thread_Create(const char* name, OotPs2ThreadEntry entry, s32 priority, s32 stackSize) {
    return 0;
}

s32 OotPs2Thread_Start(OotPs2Handle id, OotPs2ThreadArgSize args, void* argp) {
    return 0;
}

s32 OotPs2Thread_Delete(OotPs2Handle id) {
    return 0;
}

s32 OotPs2Thread_TerminateDelete(OotPs2Handle id) {
    return 0;
}

void OotPs2Thread_ExitDelete(void) {
}

s32 OotPs2Thread_ChangePriority(OotPs2Handle id, s32 priority) {
    return 0;
}

OotPs2Handle OotPs2Thread_GetId(void) {
    return 0;
}

s32 OotPs2Thread_GetInfo(OotPs2Handle id, OotPs2ThreadInfo* out) {
    return 0;
}

void OotPs2Thread_Delay(u32 usec) {
}

void OotPs2Thread_ExitGame(void) {
}

OotPs2Handle OotPs2Sema_Create(s32 initialCount, s32 maxCount) {
    return 0;
}

s32 OotPs2Sema_Wait(OotPs2Handle id) {
    return 0;
}

s32 OotPs2Sema_Signal(OotPs2Handle id) {
    return 0;
}

s32 OotPs2Sema_Delete(OotPs2Handle id) {
    return 0;
}

u64 OotPs2Time_GetUsec(void) {
    return 0;
}

u32 OotPs2Time_GetUsecLow(void) {
    return 0;
}

OotPs2Handle OotPs2File_Open(const char* path, s32 flags, s32 mode) {
    return 0;
}

s32 OotPs2File_Close(OotPs2Handle fd) {
    return 0;
}

s32 OotPs2File_Read(OotPs2Handle fd, void* buffer, u32 size) {
    return 0;
}

s32 OotPs2File_Write(OotPs2Handle fd, const void* buffer, u32 size) {
    return 0;
}

OotPs2Offset OotPs2File_Seek(OotPs2Handle fd, OotPs2Offset offset, s32 whence) {
    return 0;
}

s32 OotPs2File_Mkdir(const char* path, s32 mode) {
    return 0;
}

s32 OotPs2File_Remove(const char* path) {
    return 0;
}

s32 OotPs2File_Rename(const char* from, const char* to) {
    return 0;
}

void OotPs2Cache_WritebackInvalidateAll(void) {
}

void OotPs2Cache_WritebackRange(const void* address, size_t size) {
}

void OotPs2Cache_InvalidateRange(void* address, size_t size) {
}

void OotPs2Cache_InvalidateInstruction(void) {
}

void OotPs2Pad_Init(void) {
}

s32 OotPs2Pad_Read(OotPs2PadState* out) {
    return 0;
}

void OotPs2_GetRightStick(int* x, int* y) {
}
