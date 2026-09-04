#ifndef OOT_PS2_PLATFORM_H
#define OOT_PS2_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "ultra64/ultratypes.h"

typedef s32 OotPs2Handle;
typedef s64 OotPs2Offset;
typedef u32 OotPs2ThreadArgSize;
typedef s32 (*OotPs2ThreadEntry)(OotPs2ThreadArgSize args, void* argp);

typedef struct OotPs2ThreadInfo {
    void* stack;
    s32 stackSize;
    s32 currentPriority;
} OotPs2ThreadInfo;

typedef struct OotPs2PadState {
    u32 buttons;
    u8 lx;
    u8 ly;
} OotPs2PadState;

enum {
    OOT_PS2_BUTTON_SELECT   = 1u << 0,
    OOT_PS2_BUTTON_START    = 1u << 1,
    OOT_PS2_BUTTON_UP       = 1u << 2,
    OOT_PS2_BUTTON_RIGHT    = 1u << 3,
    OOT_PS2_BUTTON_DOWN     = 1u << 4,
    OOT_PS2_BUTTON_LEFT     = 1u << 5,
    OOT_PS2_BUTTON_L1       = 1u << 6,
    OOT_PS2_BUTTON_R1       = 1u << 7,
    OOT_PS2_BUTTON_TRIANGLE = 1u << 8,
    OOT_PS2_BUTTON_CIRCLE   = 1u << 9,
    OOT_PS2_BUTTON_CROSS    = 1u << 10,
    OOT_PS2_BUTTON_SQUARE   = 1u << 11,
    OOT_PS2_BUTTON_R2       = 1u << 12,
    OOT_PS2_BUTTON_L3       = 1u << 13,
    OOT_PS2_BUTTON_R3       = 1u << 14,
};

enum {
    OOT_PS2_FILE_RDONLY = 0,
    OOT_PS2_FILE_WRONLY = 1,
    OOT_PS2_FILE_RDWR = 2,
    OOT_PS2_FILE_CREAT = 0x0200,
    OOT_PS2_FILE_TRUNC = 0x0400,
};

enum {
    OOT_PS2_SEEK_SET = 0,
    OOT_PS2_SEEK_CUR = 1,
    OOT_PS2_SEEK_END = 2,
};

OotPs2Handle OotPs2Thread_Create(const char* name, OotPs2ThreadEntry entry, s32 priority, s32 stackSize);
s32 OotPs2Thread_Start(OotPs2Handle id, OotPs2ThreadArgSize args, void* argp);
s32 OotPs2Thread_Delete(OotPs2Handle id);
s32 OotPs2Thread_TerminateDelete(OotPs2Handle id);
void OotPs2Thread_ExitDelete(void);
s32 OotPs2Thread_ChangePriority(OotPs2Handle id, s32 priority);
OotPs2Handle OotPs2Thread_GetId(void);
s32 OotPs2Thread_GetInfo(OotPs2Handle id, OotPs2ThreadInfo* out);
void OotPs2Thread_Delay(u32 usec);
void OotPs2Thread_ExitGame(void);

OotPs2Handle OotPs2Sema_Create(s32 initialCount, s32 maxCount);
s32 OotPs2Sema_Wait(OotPs2Handle id);
s32 OotPs2Sema_Signal(OotPs2Handle id);
s32 OotPs2Sema_Delete(OotPs2Handle id);

u64 OotPs2Time_GetUsec(void);
u32 OotPs2Time_GetUsecLow(void);

OotPs2Handle OotPs2File_Open(const char* path, s32 flags, s32 mode);
s32 OotPs2File_Close(OotPs2Handle fd);
s32 OotPs2File_Read(OotPs2Handle fd, void* buffer, u32 size);
s32 OotPs2File_Write(OotPs2Handle fd, const void* buffer, u32 size);
OotPs2Offset OotPs2File_Seek(OotPs2Handle fd, OotPs2Offset offset, s32 whence);
s32 OotPs2File_Mkdir(const char* path, s32 mode);
s32 OotPs2File_Remove(const char* path);
s32 OotPs2File_Rename(const char* from, const char* to);

void OotPs2Platform_SetDiscBoot(s32 discBoot);
s32 OotPs2Platform_IsDiscBoot(void);

void OotPs2Trace_Init(void);
void OotPs2Trace_Log(const char* format, ...);
void OotPs2Trace_EnableScreen(void);
const char* OotPs2Trace_Path(void);

void OotPs2Cache_WritebackInvalidateAll(void);
void OotPs2Cache_WritebackRange(const void* address, size_t size);
void OotPs2Cache_InvalidateRange(void* address, size_t size);
void OotPs2Cache_InvalidateInstruction(void);

void OotPs2Pad_Init(void);
s32 OotPs2Pad_Read(OotPs2PadState* out);
void OotPs2_GetRightStick(int* x, int* y);
void OotPs2Input_SetPlayState(void* playState);

#endif
