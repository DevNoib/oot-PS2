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


#define OOT_PS2_MAX_PLATFORM_THREADS 24
#define OOT_PS2_MIN_THREAD_STACK 16384

typedef struct OotPs2ThreadSlot {
    s32 used;
    s32 id;
    OotPs2ThreadEntry entry;
    void* stack;
    s32 stackSize;
    OotPs2ThreadArgSize startArgs;
    void* startArg;
} OotPs2ThreadSlot;

static OotPs2ThreadSlot sThreadSlots[OOT_PS2_MAX_PLATFORM_THREADS];
static s32 sPadReady;
static s32 sPadAnalogConfigured;
static u8 sPadBuffer[256] __attribute__((aligned(64)));
static struct padButtonStatus sPadStatus __attribute__((aligned(64)));
static volatile s32 sPadRightX;
static volatile s32 sPadRightY;
static u32 sOotPs2TraceSequence;
static char sOotPs2TraceMessage[384] __attribute__((aligned(64)));
static char sOotPs2TraceLine[448] __attribute__((aligned(64)));
static s32 sOotPs2TraceScreenReady;
static s32 sOotPs2DiscBootEarly;

void OotPs2Platform_SetDiscBoot(s32 discBoot) {
    sOotPs2DiscBootEarly = discBoot != 0;
}

s32 OotPs2Platform_IsDiscBoot(void) {
    return sOotPs2DiscBootEarly;
}

void OotPs2Trace_Init(void) {
}

void OotPs2Trace_EnableScreen(void) {
}

void OotPs2Trace_Log(const char* format, ...) {
    (void)format;
}

const char* OotPs2Trace_Path(void) {
    return "off";
}

static OotPs2ThreadSlot* OotPs2Thread_FindSlot(s32 id) {
    s32 i;

    for (i = 0; i < OOT_PS2_MAX_PLATFORM_THREADS; i++) {
        if (sThreadSlots[i].used && sThreadSlots[i].id == id) {
            return &sThreadSlots[i];
        }
    }
    return NULL;
}

static OotPs2ThreadSlot* OotPs2Thread_AllocSlot(void) {
    s32 i;

    for (i = 0; i < OOT_PS2_MAX_PLATFORM_THREADS; i++) {
        if (!sThreadSlots[i].used) {
            memset(&sThreadSlots[i], 0, sizeof(sThreadSlots[i]));
            sThreadSlots[i].used = true;
            return &sThreadSlots[i];
        }
    }
    return NULL;
}

static void OotPs2Thread_Trampoline(void* arg) {
    OotPs2ThreadSlot* slot = (OotPs2ThreadSlot*)arg;

    if (slot != NULL && slot->entry != NULL) {
        slot->entry(slot->startArgs, slot->startArg);
    }
    ExitDeleteThread();
}

OotPs2Handle OotPs2Thread_Create(const char* name, OotPs2ThreadEntry entry, s32 priority, s32 stackSize) {
    OotPs2ThreadSlot* slot = OotPs2Thread_AllocSlot();
    ee_thread_t thread;

    (void)name;
    if (slot == NULL || entry == NULL) {
        return -1;
    }

    slot->entry = entry;
    slot->stackSize = stackSize < OOT_PS2_MIN_THREAD_STACK ? OOT_PS2_MIN_THREAD_STACK : stackSize;
    slot->stack = memalign(64, slot->stackSize);
    if (slot->stack == NULL) {
        slot->used = false;
        return -1;
    }

    memset(&thread, 0, sizeof(thread));
    thread.func = OotPs2Thread_Trampoline;
    thread.stack = slot->stack;
    thread.stack_size = slot->stackSize;
    thread.initial_priority = priority < 1 ? 64 : (priority > 127 ? 127 : priority);
    thread.gp_reg = &_gp;

    slot->id = CreateThread(&thread);
    if (slot->id < 0) {
        free(slot->stack);
        memset(slot, 0, sizeof(*slot));
        return -1;
    }
    return slot->id;
}

s32 OotPs2Thread_Start(OotPs2Handle id, OotPs2ThreadArgSize args, void* argp) {
    OotPs2ThreadSlot* slot = OotPs2Thread_FindSlot(id);

    if (slot == NULL) {
        return -1;
    }
    slot->startArgs = args;
    slot->startArg = argp;
    return StartThread(id, slot);
}

s32 OotPs2Thread_Delete(OotPs2Handle id) {
    OotPs2ThreadSlot* slot = OotPs2Thread_FindSlot(id);
    s32 result = DeleteThread(id);

    if (slot != NULL) {
        free(slot->stack);
        memset(slot, 0, sizeof(*slot));
    }
    return result;
}

s32 OotPs2Thread_TerminateDelete(OotPs2Handle id) {
    TerminateThread(id);
    return OotPs2Thread_Delete(id);
}

void OotPs2Thread_ExitDelete(void) {
    ExitDeleteThread();
}

s32 OotPs2Thread_ChangePriority(OotPs2Handle id, s32 priority) {
    return ChangeThreadPriority(id, priority);
}

OotPs2Handle OotPs2Thread_GetId(void) {
    return GetThreadId();
}

s32 OotPs2Thread_GetInfo(OotPs2Handle id, OotPs2ThreadInfo* out) {
    ee_thread_status_t status;
    s32 result;

    if (out == NULL) {
        return -1;
    }
    result = ReferThreadStatus(id, &status);
    if (result >= 0) {
        out->stack = status.stack;
        out->stackSize = status.stack_size;
        out->currentPriority = status.current_priority;
    }
    return result;
}

void OotPs2Thread_Delay(u32 usec) {
    DelayThread(usec);
}

void OotPs2Thread_ExitGame(void) {
    SleepThread();
}

OotPs2Handle OotPs2Sema_Create(s32 initialCount, s32 maxCount) {
    ee_sema_t sema;

    memset(&sema, 0, sizeof(sema));
    sema.init_count = initialCount;
    sema.max_count = maxCount;
    return CreateSema(&sema);
}

s32 OotPs2Sema_Wait(OotPs2Handle id) {
    return WaitSema(id);
}

s32 OotPs2Sema_Signal(OotPs2Handle id) {
    return SignalSema(id);
}

s32 OotPs2Sema_Delete(OotPs2Handle id) {
    return DeleteSema(id);
}

u64 OotPs2Time_GetUsec(void) {
    u32 seconds = 0;
    u32 usec = 0;

    TimerBusClock2USec(GetTimerSystemTime(), &seconds, &usec);
    return (u64)seconds * 1000000ULL + usec;
}

u32 OotPs2Time_GetUsecLow(void) {
    return (u32)OotPs2Time_GetUsec();
}

static s32 OotPs2File_ConvertFlags(s32 flags) {
    s32 out = O_RDONLY;

    if ((flags & 3) == OOT_PS2_FILE_WRONLY) {
        out = O_WRONLY;
    } else if ((flags & 3) == OOT_PS2_FILE_RDWR) {
        out = O_RDWR;
    }
    if (flags & OOT_PS2_FILE_CREAT) {
        out |= O_CREAT;
    }
    if (flags & OOT_PS2_FILE_TRUNC) {
        out |= O_TRUNC;
    }
    return out;
}

OotPs2Handle OotPs2File_Open(const char* path, s32 flags, s32 mode) {
    return open(path, OotPs2File_ConvertFlags(flags), mode);
}

s32 OotPs2File_Close(OotPs2Handle fd) {
    return close(fd);
}

s32 OotPs2File_Read(OotPs2Handle fd, void* buffer, u32 size) {
    return read(fd, buffer, size);
}

s32 OotPs2File_Write(OotPs2Handle fd, const void* buffer, u32 size) {
    return write(fd, buffer, size);
}

OotPs2Offset OotPs2File_Seek(OotPs2Handle fd, OotPs2Offset offset, s32 whence) {
    return lseek(fd, offset, whence);
}

s32 OotPs2File_Mkdir(const char* path, s32 mode) {
    return mkdir(path, mode);
}

s32 OotPs2File_Remove(const char* path) {
    return remove(path);
}

s32 OotPs2File_Rename(const char* from, const char* to) {
    return rename(from, to);
}

void OotPs2Cache_WritebackInvalidateAll(void) {
    FlushCache(WRITEBACK_DCACHE);
    FlushCache(INVALIDATE_DCACHE);
}

void OotPs2Cache_WritebackRange(const void* address, size_t size) {
    if (address != NULL && size != 0) {
        SyncDCache((void*)address, (u8*)address + size);
    }
}

void OotPs2Cache_InvalidateRange(void* address, size_t size) {
    if (address != NULL && size != 0) {
        InvalidDCache(address, (u8*)address + size);
    }
}

void OotPs2Cache_InvalidateInstruction(void) {
    FlushCache(INVALIDATE_ICACHE);
}

void OotPs2Pad_Init(void) {
    if (!sPadReady) {
        if (padInit(0) >= 0 && padPortOpen(0, 0, sPadBuffer) > 0) {
            sPadReady = true;
        }
        return;
    }

    if (!sPadAnalogConfigured) {
        s32 state = padGetState(0, 0);

        if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
            s32 id = padInfoMode(0, 0, PAD_MODECURID, 0);
            s32 ext = padInfoMode(0, 0, PAD_MODECUREXID, 0);

            if (ext > 0) {
                id = ext;
            }
            if (id == PAD_TYPE_DUALSHOCK || id == PAD_TYPE_ANALOG) {
                sPadAnalogConfigured = true;
            } else if (id == PAD_TYPE_DIGITAL) {
                padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
            }
        }
    }
}

s32 OotPs2Pad_Read(OotPs2PadState* out) {
    u32 buttons;
    s32 state;

    if (out == NULL) {
        return -1;
    }

    OotPs2Pad_Init();
    memset(out, 0, sizeof(*out));
    out->lx = 128;
    out->ly = 128;
    sPadRightX = 0;
    sPadRightY = 0;

    if (!sPadReady) {
        return 0;
    }
    state = padGetState(0, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
        return 0;
    }

    memset(&sPadStatus, 0, sizeof(sPadStatus));
    sPadStatus.btns = 0xffffu;
    sPadStatus.ljoy_h = sPadStatus.ljoy_v = 128;
    sPadStatus.rjoy_h = sPadStatus.rjoy_v = 128;
    if (padRead(0, 0, &sPadStatus) == 0) {
        return 0;
    }
    if (sPadStatus.btns == 0 && sPadStatus.ljoy_h == 0 && sPadStatus.ljoy_v == 0 &&
        sPadStatus.rjoy_h == 0 && sPadStatus.rjoy_v == 0) {
        return 0;
    }

    buttons = 0xffffu ^ sPadStatus.btns;
    if (buttons & PAD_SELECT) out->buttons |= OOT_PS2_BUTTON_SELECT;
    if (buttons & PAD_START) out->buttons |= OOT_PS2_BUTTON_START;
    if (buttons & PAD_UP) out->buttons |= OOT_PS2_BUTTON_UP;
    if (buttons & PAD_RIGHT) out->buttons |= OOT_PS2_BUTTON_RIGHT;
    if (buttons & PAD_DOWN) out->buttons |= OOT_PS2_BUTTON_DOWN;
    if (buttons & PAD_LEFT) out->buttons |= OOT_PS2_BUTTON_LEFT;
    if (buttons & PAD_L1) out->buttons |= OOT_PS2_BUTTON_L1;
    if (buttons & PAD_R1) out->buttons |= OOT_PS2_BUTTON_R1;
    if (buttons & PAD_TRIANGLE) out->buttons |= OOT_PS2_BUTTON_TRIANGLE;
    if (buttons & PAD_CIRCLE) out->buttons |= OOT_PS2_BUTTON_CIRCLE;
    if (buttons & PAD_CROSS) out->buttons |= OOT_PS2_BUTTON_CROSS;
    if (buttons & PAD_SQUARE) out->buttons |= OOT_PS2_BUTTON_SQUARE;
    if (buttons & PAD_R2) out->buttons |= OOT_PS2_BUTTON_R2;
    if (buttons & PAD_L3) out->buttons |= OOT_PS2_BUTTON_L3;
    if (buttons & PAD_R3) out->buttons |= OOT_PS2_BUTTON_R3;

    if ((sPadStatus.mode & 0xf0) == 0x70) {
        s32 lx = (s32)sPadStatus.ljoy_h - 128;
        s32 ly = (s32)sPadStatus.ljoy_v - 128;
        const s32 leftDeadzone = 48;
        s32 rx;
        s32 ry;
        const s32 rightDeadzone = 24;
        s32 ax = lx < 0 ? -lx : lx;
        s32 ay = ly < 0 ? -ly : ly;
        s32 approxMag = (ax > ay ? ax : ay) + ((ax > ay ? ay : ax) >> 1);

        if (approxMag <= leftDeadzone) {
            lx = 0;
            ly = 0;
        } else {
            s32 cappedMag = approxMag > 127 ? 127 : approxMag;
            s32 scaleNum = (cappedMag - leftDeadzone) * 127;
            s32 scaleDen = (127 - leftDeadzone) * cappedMag;
            if (scaleDen > 0) {
                lx = (lx * scaleNum) / scaleDen;
                ly = (ly * scaleNum) / scaleDen;
            }
        }
        if (lx < -127) lx = -127;
        if (lx > 127) lx = 127;
        if (ly < -127) ly = -127;
        if (ly > 127) ly = 127;
        out->lx = (u8)(128 + lx);
        out->ly = (u8)(128 + ly);

        rx = (s32)sPadStatus.rjoy_h - 128;
        ry = 128 - (s32)sPadStatus.rjoy_v;
        if (rx > -rightDeadzone && rx < rightDeadzone) rx = 0;
        if (ry > -rightDeadzone && ry < rightDeadzone) ry = 0;
        if (rx < -80) rx = -80;
        if (rx > 80) rx = 80;
        if (ry < -80) ry = -80;
        if (ry > 80) ry = 80;
        sPadRightX = rx;
        sPadRightY = ry;
    }

    return 1;
}

void OotPs2_GetRightStick(int* x, int* y) {
    if (x != NULL) {
        *x = sPadRightX;
    }
    if (y != NULL) {
        *y = sPadRightY;
    }
}
