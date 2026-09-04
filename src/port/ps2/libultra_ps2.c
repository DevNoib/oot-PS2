#include "ultra64.h"

#include "attributes.h"
#include "oot_ps2_platform.h"

#include <stdarg.h>
#include <stdio.h>

#include "oot_port_audio_backend.h"
#include "oot_port_controls.h"
#include "versions.h"

#define OOT_PS2_MAX_THREADS 16
#define OOT_PS2_MAX_TIMERS  16

#define OOT_PS2_INVALID_HANDLE (-1)

typedef struct {
    OSThread* thread;
    OotPs2Handle ps2ThreadId;
    void (*entry)(void*);
    void* arg;
} OotPs2ThreadSlot;

typedef struct {
    OSTimer* timer;
    OSMesgQueue* mq;
    OSMesg msg;
    u64 dueUsec;
    u64 intervalUsec;
    volatile s32 active;
} OotPs2TimerSlot;

typedef struct {
    OSMesgQueue* mq;
    OSMesg msg;
} OotPs2PendingTimerMsg;

static OotPs2ThreadSlot sThreadSlots[OOT_PS2_MAX_THREADS];
static OotPs2TimerSlot sTimerSlots[OOT_PS2_MAX_TIMERS];
static OSContPad sControllerPads[MAXCONTROLLERS];
static OSContStatus sControllerStatus[MAXCONTROLLERS];
static OSMesgQueue* sPiCmdQueue;
static OSMesgQueue* sViEventQueue;
static OSMesg sViEventMsg;
static u32 sIntMask = OS_IM_ALL;
static void* sCurrentFramebuffer;
static void* sNextFramebuffer;
static OSViMode* sCurrentViMode;
static OSViContext sCurrentViContext;
static OotPs2Handle sMesgQueueLockSema = OOT_PS2_INVALID_HANDLE;

static OotPs2Handle sTimerServiceThreadId = OOT_PS2_INVALID_HANDLE;
static OotPs2Handle sTimerLockSema = OOT_PS2_INVALID_HANDLE;
static volatile s32 sTimerServiceRunning;
static volatile u32 sFastCount;
static volatile s32 sFastCountInitialized;

s32 osRomType = 0;
void* osRomBase = NULL;
#if OOT_PAL_N64
s32 osTvType = OS_TV_PAL;
#else
s32 osTvType = OS_TV_NTSC;
#endif
s32 osResetType = 0;
s32 osCicId = 0;
s32 osVersion = 0;
u32 osMemSize = 0x02000000;
s32 osAppNMIBuffer[0x10];
u64 osClockRate = OS_CLOCK_RATE;
__osHwInt __osHwIntTable[1];
OSIntMask __OSGlobalIntMask = OS_IM_ALL;
OSPiHandle* __osPiTable = NULL;
u8 __osContLastCmd = CONT_CMD_READ_BUTTON;
OSPifRam __osContPifRam;
OSPifRam __osPfsPifRam;
u8 __osMaxControllers = 1;

static s32 OotPort_IsValidUid(OotPs2Handle uid) {
    return uid > 0;
}

static u64 OotPort_GetUsec(void) {
    return OotPs2Time_GetUsec();
}

static u64 OotPort_CyclesToUsec(OSTime cycles) {
    if (cycles == 0) {
        return 0;
    }

    return (u64)OS_CYCLES_TO_USEC(cycles);
}

static void OotPort_ClearThreadSlot(OotPs2ThreadSlot* slot) {
    if (slot != NULL) {
        memset(slot, 0, sizeof(*slot));
        slot->ps2ThreadId = OOT_PS2_INVALID_HANDLE;
    }
}

static void OotPort_ClearTimerSlot(OotPs2TimerSlot* slot) {
    if (slot != NULL) {
        memset(slot, 0, sizeof(*slot));
    }
}

static OotPs2ThreadSlot* OotPort_FindThreadSlot(OSThread* thread) {
    s32 i;

    for (i = 0; i < OOT_PS2_MAX_THREADS; i++) {
        if (sThreadSlots[i].thread == thread) {
            return &sThreadSlots[i];
        }
    }

    return NULL;
}

static OotPs2ThreadSlot* OotPort_FindThreadSlotByPlatformId(OotPs2Handle ps2ThreadId) {
    s32 i;

    if (!OotPort_IsValidUid(ps2ThreadId)) {
        return NULL;
    }

    for (i = 0; i < OOT_PS2_MAX_THREADS; i++) {
        if (sThreadSlots[i].ps2ThreadId == ps2ThreadId) {
            return &sThreadSlots[i];
        }
    }

    return NULL;
}

static OotPs2ThreadSlot* OotPort_AllocThreadSlot(OSThread* thread) {
    s32 i;

    for (i = 0; i < OOT_PS2_MAX_THREADS; i++) {
        if (sThreadSlots[i].thread == NULL) {
            OotPort_ClearThreadSlot(&sThreadSlots[i]);
            sThreadSlots[i].thread = thread;
            return &sThreadSlots[i];
        }
    }

    return NULL;
}

static int OotPort_ThreadTrampoline(OotPs2ThreadArgSize args, void* argp) {
    OSThread* thread = (OSThread*)argp;
    OotPs2ThreadSlot* slot = OotPort_FindThreadSlot(thread);

    (void)args;

    if (slot != NULL && slot->entry != NULL) {
        thread->state = OS_STATE_RUNNING;
        slot->entry(slot->arg);
        thread->state = OS_STATE_STOPPED;
        OotPort_ClearThreadSlot(slot);
    }

    OotPs2Thread_ExitDelete();
    return 0;
}

static OotPs2TimerSlot* OotPort_FindTimerSlot(OSTimer* timer) {
    s32 i;

    if (timer == NULL) {
        return NULL;
    }

    for (i = 0; i < OOT_PS2_MAX_TIMERS; i++) {
        if (sTimerSlots[i].timer == timer) {
            return &sTimerSlots[i];
        }
    }

    return NULL;
}

static OotPs2TimerSlot* OotPort_AllocTimerSlot(OSTimer* timer) {
    s32 i;

    for (i = 0; i < OOT_PS2_MAX_TIMERS; i++) {
        if (sTimerSlots[i].timer == NULL) {
            OotPort_ClearTimerSlot(&sTimerSlots[i]);
            sTimerSlots[i].timer = timer;
            return &sTimerSlots[i];
        }
    }

    return NULL;
}

static s32 OotPort_EnsureTimerLock(void) {
    OotPs2Handle sema;

    if (OotPort_IsValidUid(sTimerLockSema)) {
        return 0;
    }

    sema = OotPs2Sema_Create(1, 1);
    if (!OotPort_IsValidUid(sema)) {
        sTimerLockSema = OOT_PS2_INVALID_HANDLE;
        return -1;
    }

    sTimerLockSema = sema;
    return 0;
}

static void OotPort_LockTimers(void) {
    if (OotPort_IsValidUid(sTimerLockSema)) {
        OotPs2Sema_Wait(sTimerLockSema);
    }
}

static void OotPort_UnlockTimers(void) {
    if (OotPort_IsValidUid(sTimerLockSema)) {
        OotPs2Sema_Signal(sTimerLockSema);
    }
}

static int OotPort_TimerServiceThread(OotPs2ThreadArgSize args, void* argp) {
    (void)args;
    (void)argp;

    while (sTimerServiceRunning) {
        OotPs2PendingTimerMsg pending[OOT_PS2_MAX_TIMERS];
        s32 pendingCount = 0;
        s32 anyActive = false;
        u64 now = OotPort_GetUsec();
        u32 sleepUsec = 1000;
        s32 i;

        OotPort_LockTimers();

        for (i = 0; i < OOT_PS2_MAX_TIMERS; i++) {
            OotPs2TimerSlot* slot = &sTimerSlots[i];

            if (!slot->active || slot->timer == NULL) {
                continue;
            }

            anyActive = true;

            if (slot->dueUsec <= now) {
                if (slot->mq != NULL && pendingCount < OOT_PS2_MAX_TIMERS) {
                    pending[pendingCount].mq = slot->mq;
                    pending[pendingCount].msg = slot->msg;
                    pendingCount++;
                }

                if (slot->intervalUsec != 0) {
                    

                    slot->dueUsec = now + slot->intervalUsec;
                } else {
                    OotPort_ClearTimerSlot(slot);
                    continue;
                }
            }
        }

        for (i = 0; i < OOT_PS2_MAX_TIMERS; i++) {
            OotPs2TimerSlot* slot = &sTimerSlots[i];

            if (slot->active && slot->timer != NULL) {
                u64 delta = (slot->dueUsec > now) ? (slot->dueUsec - now) : 0;

                anyActive = true;
                if (delta < sleepUsec) {
                    sleepUsec = (u32)delta;
                }
            }
        }

        OotPort_UnlockTimers();

        for (i = 0; i < pendingCount; i++) {
            osSendMesg(pending[i].mq, pending[i].msg, OS_MESG_NOBLOCK);
        }

        if (!anyActive) {
            sleepUsec = 1000;
        } else {
            if (sleepUsec < 100) {
                sleepUsec = 100;
            }
            if (sleepUsec > 1000) {
                sleepUsec = 1000;
            }
        }

        OotPs2Thread_Delay(sleepUsec);
    }

    sTimerServiceThreadId = OOT_PS2_INVALID_HANDLE;
    OotPs2Thread_ExitDelete();
    return 0;
}

static s32 OotPort_EnsureTimerService(void) {
    OotPs2Handle threadId;
    s32 ret;

    if (OotPort_EnsureTimerLock() < 0) {
        return -1;
    }

    if (OotPort_IsValidUid(sTimerServiceThreadId)) {
        return 0;
    }

    sTimerServiceRunning = true;

    threadId = OotPs2Thread_Create("oot-timers", OotPort_TimerServiceThread, 0x18, 0x2000);
    if (!OotPort_IsValidUid(threadId)) {
        sTimerServiceRunning = false;
        sTimerServiceThreadId = OOT_PS2_INVALID_HANDLE;
        return -1;
    }

    ret = OotPs2Thread_Start(threadId, 0, NULL);
    if (ret < 0) {
        OotPs2Thread_Delete(threadId);
        sTimerServiceRunning = false;
        sTimerServiceThreadId = OOT_PS2_INVALID_HANDLE;
        return -1;
    }

    sTimerServiceThreadId = threadId;
    return 0;
}

static void OotPort_StopTimerService(void) {
    OotPs2Handle threadId = sTimerServiceThreadId;
    OotPs2Handle sema = sTimerLockSema;

    sTimerServiceRunning = false;
    sTimerServiceThreadId = OOT_PS2_INVALID_HANDLE;

    if (OotPort_IsValidUid(threadId) && threadId != OotPs2Thread_GetId()) {
        OotPs2Thread_TerminateDelete(threadId);
    }

    if (OotPort_IsValidUid(sema)) {
        OotPs2Sema_Delete(sema);
    }

    sTimerLockSema = OOT_PS2_INVALID_HANDLE;
}

static s32 OotPort_EnsureMesgQueueLock(void) {
    OotPs2Handle sema;

    if (OotPort_IsValidUid(sMesgQueueLockSema)) {
        return 0;
    }

    sema = OotPs2Sema_Create(1, 1);
    if (!OotPort_IsValidUid(sema)) {
        sMesgQueueLockSema = OOT_PS2_INVALID_HANDLE;
        return -1;
    }

    sMesgQueueLockSema = sema;
    return 0;
}

static void OotPort_StopMesgQueueLock(void) {
    OotPs2Handle sema = sMesgQueueLockSema;

    sMesgQueueLockSema = OOT_PS2_INVALID_HANDLE;

    if (OotPort_IsValidUid(sema)) {
        OotPs2Sema_Delete(sema);
    }
}

static void OotPort_LockMesgQueue(UNUSED OSMesgQueue* mq) {
    if (OotPort_IsValidUid(sMesgQueueLockSema)) {
        OotPs2Sema_Wait(sMesgQueueLockSema);
    }
}

static void OotPort_UnlockMesgQueue(UNUSED OSMesgQueue* mq) {
    if (OotPort_IsValidUid(sMesgQueueLockSema)) {
        OotPs2Sema_Signal(sMesgQueueLockSema);
    }
}

void osSyncPrintf(const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void isPrintfInit(void) {
}

void __osInitialize_common(void) {
    s32 i;

    OotPs2Trace_Log("os common stop timer service begin");
    OotPort_StopTimerService();
    OotPs2Trace_Log("os common stop timer service done");
    OotPs2Trace_Log("os common stop queue lock begin");
    OotPort_StopMesgQueueLock();
    OotPs2Trace_Log("os common stop queue lock done");

    OotPs2Trace_Log("os common clear runtime state begin");
    memset(sThreadSlots, 0, sizeof(sThreadSlots));
    memset(sTimerSlots, 0, sizeof(sTimerSlots));
    memset(sControllerPads, 0, sizeof(sControllerPads));
    memset(sControllerStatus, 0, sizeof(sControllerStatus));
    memset(&sCurrentViContext, 0, sizeof(sCurrentViContext));
    OotPs2Trace_Log("os common clear runtime state done");

    for (i = 0; i < OOT_PS2_MAX_THREADS; i++) {
        sThreadSlots[i].ps2ThreadId = OOT_PS2_INVALID_HANDLE;
    }
    OotPs2Trace_Log("os common thread slots initialized count=%d", OOT_PS2_MAX_THREADS);

    osMemSize = 0x02000000;
    osClockRate = OS_CLOCK_RATE;
    OotPs2Trace_Log("os common globals mem=%u clock=%llu", (unsigned)osMemSize, (unsigned long long)osClockRate);
    OotPs2Trace_Log("os common pad init begin");
    OotPs2Pad_Init();
    OotPs2Trace_Log("os common pad init done");
    OotPs2Trace_Log("os common queue lock ensure begin");
    OotPort_EnsureMesgQueueLock();
    OotPs2Trace_Log("os common queue lock ensure done sema=%d", (int)sMesgQueueLockSema);
}

void __osInitialize_autodetect(void) {
}

u32 osGetMemSize(void) {
    return osMemSize;
}

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count) {
    if (mq == NULL) {
        return;
    }

    

    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msgBuf;

    if (count <= 0 || msgBuf == NULL) {
        return;
    }

    OotPort_EnsureMesgQueueLock();
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    if (mq == NULL || mq->msg == NULL || mq->msgCount <= 0) {
        return -1;
    }

    while (true) {
        OotPort_LockMesgQueue(mq);
        if (!MQ_IS_FULL(mq)) {
            mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
            mq->validCount++;
            OotPort_UnlockMesgQueue(mq);
            return 0;
        }

        OotPort_UnlockMesgQueue(mq);
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }

        OotPs2Thread_Delay(1000);
    }
}

s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    if (mq == NULL || mq->msg == NULL || mq->msgCount <= 0) {
        return -1;
    }

    while (true) {
        OotPort_LockMesgQueue(mq);
        if (!MQ_IS_FULL(mq)) {
            mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
            mq->msg[mq->first] = msg;
            mq->validCount++;
            OotPort_UnlockMesgQueue(mq);
            return 0;
        }

        OotPort_UnlockMesgQueue(mq);
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }

        OotPs2Thread_Delay(1000);
    }
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    if (mq == NULL || mq->msg == NULL || mq->msgCount <= 0) {
        return -1;
    }

    while (true) {
        OotPort_LockMesgQueue(mq);
        if (!MQ_IS_EMPTY(mq)) {
            if (msg != NULL) {
                *msg = mq->msg[mq->first];
            }

            mq->first = (mq->first + 1) % mq->msgCount;
            mq->validCount--;
            OotPort_UnlockMesgQueue(mq);
            return 0;
        }

        OotPort_UnlockMesgQueue(mq);
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }

        OotPs2Thread_Delay(1000);
    }
}

void osSetEventMesg(UNUSED OSEvent e, UNUSED OSMesgQueue* mq, UNUSED OSMesg msg) {
}

void osCreateThread(OSThread* thread, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    OotPs2ThreadSlot* slot;
    OotPs2Handle threadId;
    char name[32];

    if (thread == NULL) {
        return;
    }

    slot = OotPort_FindThreadSlot(thread);
    if (slot != NULL && OotPort_IsValidUid(slot->ps2ThreadId)) {
        OotPs2Thread_TerminateDelete(slot->ps2ThreadId);
        OotPort_ClearThreadSlot(slot);
    }

    memset(thread, 0, sizeof(*thread));
    thread->id = id;
    thread->priority = pri;
    thread->state = OS_STATE_STOPPED;
    thread->context.sp = (u32)(uintptr_t)sp;

    slot = OotPort_AllocThreadSlot(thread);
    if (slot == NULL) {
        return;
    }

    snprintf(name, sizeof(name), "oot-thr-%ld", (long)id);

    slot->entry = entry;
    slot->arg = arg;
    slot->ps2ThreadId = OOT_PS2_INVALID_HANDLE;

    threadId = OotPs2Thread_Create(name, OotPort_ThreadTrampoline, 0x20 + (OS_PRIORITY_APPMAX - pri), 0x20000);
    if (!OotPort_IsValidUid(threadId)) {
        OotPort_ClearThreadSlot(slot);
        return;
    }

    slot->ps2ThreadId = threadId;
}

void osStartThread(OSThread* thread) {
    OotPs2ThreadSlot* slot;
    s32 ret;

    if (thread == NULL) {
        return;
    }

    slot = OotPort_FindThreadSlot(thread);
    if (slot == NULL || !OotPort_IsValidUid(slot->ps2ThreadId)) {
        return;
    }

    if (thread->state == OS_STATE_RUNNING || thread->state == OS_STATE_RUNNABLE) {
        return;
    }

    thread->state = OS_STATE_RUNNABLE;

    ret = OotPs2Thread_Start(slot->ps2ThreadId, sizeof(thread), thread);
    if (ret < 0) {
        thread->state = OS_STATE_STOPPED;
    }
}

void osDestroyThread(OSThread* thread) {
    OotPs2ThreadSlot* slot;
    OotPs2Handle threadId;

    if (thread == NULL) {
        threadId = OotPs2Thread_GetId();
        slot = OotPort_FindThreadSlotByPlatformId(threadId);
        if (slot != NULL) {
            if (slot->thread != NULL) {
                slot->thread->state = OS_STATE_STOPPED;
            }
            OotPort_ClearThreadSlot(slot);
        }

        OotPs2Thread_ExitDelete();
        return;
    }

    slot = OotPort_FindThreadSlot(thread);
    if (slot == NULL) {
        thread->state = OS_STATE_STOPPED;
        return;
    }

    threadId = slot->ps2ThreadId;

    OotPort_ClearThreadSlot(slot);
    thread->state = OS_STATE_STOPPED;

    if (OotPort_IsValidUid(threadId)) {
        if (threadId == OotPs2Thread_GetId()) {
            OotPs2Thread_ExitDelete();
        } else {
            OotPs2Thread_TerminateDelete(threadId);
        }
    }
}

void osStopThread(OSThread* thread) {
    osDestroyThread(thread);
}

void osYieldThread(void) {
    OotPs2Thread_Delay(0);
}

void osSetThreadPri(OSThread* thread, OSPri pri) {
    OotPs2ThreadSlot* slot = OotPort_FindThreadSlot(thread);

    if (thread != NULL) {
        thread->priority = pri;
    }

    if (slot != NULL && OotPort_IsValidUid(slot->ps2ThreadId)) {
        OotPs2Thread_ChangePriority(slot->ps2ThreadId, 0x20 + (OS_PRIORITY_APPMAX - pri));
    }
}

OSPri osGetThreadPri(OSThread* thread) {
    if (thread == NULL) {
        return 0;
    }

    return thread->priority;
}

OSId osGetThreadId(OSThread* thread) {
    if (thread == NULL) {
        return OotPs2Thread_GetId();
    }

    return thread->id;
}

OSThread* __osGetActiveQueue(void) {
    return NULL;
}

OSThread* __osGetCurrFaultedThread(void) {
    return NULL;
}

OSTime osGetTime(void) {
    return OotPs2Time_GetUsec();
}

void osSetTime(UNUSED OSTime time) {
}

u32 osGetCount(void) {
    if (!sFastCountInitialized) {
        sFastCount = OotPs2Time_GetUsecLow();
        sFastCountInitialized = true;
    }

    

    

    sFastCount += 0x9E3779B9U;
    return sFastCount;
}

s32 osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    OotPs2TimerSlot* slot;
    u64 now;

    if (timer == NULL) {
        return -1;
    }

    if (OotPort_EnsureTimerService() < 0) {
        return -1;
    }

    now = OotPort_GetUsec();

    OotPort_LockTimers();

    slot = OotPort_FindTimerSlot(timer);
    if (slot == NULL) {
        slot = OotPort_AllocTimerSlot(timer);
    }

    if (slot == NULL) {
        OotPort_UnlockTimers();
        return -1;
    }

    timer->value = countdown;
    timer->interval = interval;
    timer->mq = mq;
    timer->msg = msg;

    slot->timer = timer;
    slot->mq = mq;
    slot->msg = msg;
    slot->dueUsec = now + OotPort_CyclesToUsec(countdown);
    slot->intervalUsec = OotPort_CyclesToUsec(interval);
    slot->active = true;

    OotPort_UnlockTimers();
    return 0;
}

s32 osStopTimer(OSTimer* timer) {
    OotPs2TimerSlot* slot;

    if (timer == NULL) {
        return -1;
    }

    if (OotPort_EnsureTimerLock() < 0) {
        return -1;
    }

    OotPort_LockTimers();

    slot = OotPort_FindTimerSlot(timer);
    if (slot == NULL) {
        OotPort_UnlockTimers();
        return -1;
    }

    OotPort_ClearTimerSlot(slot);

    OotPort_UnlockTimers();
    return 0;
}

OSIntMask osGetIntMask(void) {
    return sIntMask;
}

OSIntMask osSetIntMask(OSIntMask mask) {
    OSIntMask prev = sIntMask;

    sIntMask = mask;
    return prev;
}

s32 __osDisableInt(void) {
    return 0;
}

void __osRestoreInt(UNUSED s32 enable) {
}

void __osSetHWIntrRoutine(UNUSED OSHWIntr intr, UNUSED s32 (*callback)(void), UNUSED void* sp) {
}

void __osGetHWIntrRoutine(UNUSED OSHWIntr intr, s32 (**callbackOut)(void), void** spOut) {
    if (callbackOut != NULL) {
        *callbackOut = NULL;
    }
    if (spOut != NULL) {
        *spOut = NULL;
    }
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
    if (nbytes > 0) {
        OotPs2Cache_WritebackRange(vaddr, (size_t)nbytes);
    }
}

void osInvalDCache(void* vaddr, s32 nbytes) {
    if (nbytes > 0) {
        OotPs2Cache_InvalidateRange(vaddr, (size_t)nbytes);
    }
}

void osWritebackDCacheAll(void) {
    OotPs2Cache_WritebackInvalidateAll();
}

void osInvalICache(UNUSED void* vaddr, UNUSED s32 nbytes) {
    OotPs2Cache_InvalidateInstruction();
}

u32 osVirtualToPhysical(void* vaddr) {
    return (u32)(uintptr_t)vaddr;
}

void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQueue, OSMesg* cmdBuf, s32 cmdMsgCnt) {
    (void)pri;
    sPiCmdQueue = cmdQueue;
    osCreateMesgQueue(cmdQueue, cmdBuf, cmdMsgCnt);
}

OSMesgQueue* osPiGetCmdQueue(void) {
    return sPiCmdQueue;
}

static OSPiHandle sCartHandle;

OSPiHandle* osCartRomInit(void) {
    memset(&sCartHandle, 0, sizeof(sCartHandle));
    sCartHandle.type = DEVICE_TYPE_CART;
    return &sCartHandle;
}

OSPiHandle* osDriveRomInit(void) {
    return &sCartHandle;
}

s32 osEPiStartDma(OSPiHandle* handle, OSIoMesg* mb, s32 direction) {
    (void)handle;
    (void)direction;

    if (mb != NULL && mb->dramAddr != NULL && mb->devAddr != 0 && mb->size != 0) {
        memcpy(mb->dramAddr, (void*)(uintptr_t)mb->devAddr, mb->size);
    }

    if (mb != NULL && mb->hdr.retQueue != NULL) {
        osSendMesg(mb->hdr.retQueue, mb, OS_MESG_NOBLOCK);
    }

    return 0;
}

s32 osPiStartDma(OSIoMesg* mb, s32 priority, s32 direction, uintptr_t devAddr, void* vAddr, size_t nbytes,
                 OSMesgQueue* mq) {
    (void)priority;
    (void)direction;

    if (vAddr != NULL && devAddr != 0 && nbytes != 0) {
        memcpy(vAddr, (const void*)devAddr, nbytes);
    }

    if (mq != NULL) {
        osSendMesg(mq, mb, OS_MESG_NOBLOCK);
    }

    return 0;
}

void osCreateViManager(UNUSED OSPri pri) {
}

void osViSetMode(OSViMode* mode) {
    sCurrentViMode = mode;
    sCurrentViContext.modep = mode;
}

void osViBlack(UNUSED u8 active) {
}

void osViSetSpecialFeatures(UNUSED u32 func) {
}

void osViSwapBuffer(void* frameBufPtr) {
    sCurrentFramebuffer = frameBufPtr;
    sNextFramebuffer = frameBufPtr;
    sCurrentViContext.framep = frameBufPtr;
}

void* osViGetCurrentFramebuffer(void) {
    return sCurrentFramebuffer;
}

void* osViGetNextFramebuffer(void) {
    return sNextFramebuffer;
}

void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    sViEventQueue = mq;
    sViEventMsg = msg;
    sCurrentViContext.mq = mq;
    sCurrentViContext.msg = msg;
    sCurrentViContext.retraceCount = retraceCount;
}

void osViSetYScale(f32 scale) {
    sCurrentViContext.y.factor = scale;
}

void osViSetXScale(f32 value) {
    sCurrentViContext.x.factor = value;
}

void osViExtendVStart(UNUSED u32 value) {
}

OSViContext* __osViGetCurrentContext(void) {
    return &sCurrentViContext;
}

s32 osContInit(OSMesgQueue* mq, u8* ctlBitfield, OSContStatus* status) {
    s32 i;

    (void)mq;

    for (i = 0; i < MAXCONTROLLERS; i++) {
        sControllerStatus[i].type = 0;
        sControllerStatus[i].status = 0;
        sControllerStatus[i].errno = CONT_ERR_NO_CONTROLLER;
    }

    sControllerStatus[0].type = CONT_TYPE_NORMAL;
    sControllerStatus[0].status = 0;
    sControllerStatus[0].errno = 0;

    if (ctlBitfield != NULL) {
        *ctlBitfield = 1;
    }
    if (status != NULL) {
        memcpy(status, sControllerStatus, sizeof(sControllerStatus));
    }

    return 0;
}

s32 osContStartQuery(UNUSED OSMesgQueue* mq) {
    return 0;
}

void osContGetQuery(OSContStatus* data) {
    if (data != NULL) {
        memcpy(data, sControllerStatus, sizeof(sControllerStatus));
    }
}

s32 osContSetCh(UNUSED u8 ch) {
    return 0;
}

s32 osContStartReadData(UNUSED OSMesgQueue* mq) {
    OotPs2PadState pad;

    memset(sControllerPads, 0, sizeof(sControllerPads));
    OotPs2Pad_Read(&pad);

    sControllerPads[0].button = OotPortControls_MapButtons(pad.buttons);
    sControllerPads[0].stick_x = OotPortControls_MapStick(pad.lx);
    sControllerPads[0].stick_y = -OotPortControls_MapStick(pad.ly);
    sControllerPads[0].errno = 0;

    return 0;
}

void osContGetReadData(OSContPad* contData) {
    if (contData != NULL) {
        memcpy(contData, sControllerPads, sizeof(sControllerPads));
    }
}

s32 osPfsIsPlug(UNUSED OSMesgQueue* mq, u8* pattern) {
    if (pattern != NULL) {
        *pattern = 0;
    }
    return PFS_ERR_NOPACK;
}

s32 osPfsInitPak(UNUSED OSMesgQueue* queue, UNUSED OSPfs* pfs, UNUSED s32 channel) {
    return PFS_ERR_NOPACK;
}

s32 osMotorInit(UNUSED OSMesgQueue* ctrlrqueue, UNUSED OSPfs* pfs, UNUSED s32 channel) {
    return PFS_ERR_DEVICE;
}

s32 __osMotorAccess(UNUSED OSPfs* pfs, UNUSED s32 vibrate) {
    return PFS_ERR_DEVICE;
}

s32 osAfterPreNMI(void) {
    return 0;
}

void osSpTaskLoad(UNUSED OSTask* intp) {
}

void osSpTaskStartGo(UNUSED OSTask* tp) {
    if (sViEventQueue != NULL) {
        osSendMesg(sViEventQueue, sViEventMsg, OS_MESG_NOBLOCK);
    }
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
    return OotPortAudioBackend_SetFrequency(frequency);
}

u32 osAiGetLength(void) {
    return OotPortAudioBackend_GetLength();
}
