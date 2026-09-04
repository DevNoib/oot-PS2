#include "sched.h"

#include "array_count.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_renderer.h"

#include <string.h>

#define OOT_PS2_VI_RATE_HZ      60U
#define OOT_PS2_FRAME_BASE_USEC 16666U
#define OOT_PS2_FRAME_REMAINDER 40U

static u32 sNextGfxCompletionUsec;
static u32 sGfxPacingRemainder;
static bool sGfxPacingInitialized;

static inline s32 SchedPs2_TimeDiff(u32 a, u32 b) {
    return (s32)(a - b);
}

static u32 SchedPs2_GetUpdateRate(const OSScTask* task) {
    if ((task == NULL) || (task->framebuffer == NULL) || (task->framebuffer->updateRate <= 0)) {
        return 1U;
    }

    return (u32)task->framebuffer->updateRate;
}

static u32 SchedPs2_GetFrameUsec(u32 updateRate) {
    u32 frameUsec = OOT_PS2_FRAME_BASE_USEC * updateRate;

    sGfxPacingRemainder += OOT_PS2_FRAME_REMAINDER * updateRate;
    if (sGfxPacingRemainder >= OOT_PS2_VI_RATE_HZ) {
        const u32 extraUsec = sGfxPacingRemainder / OOT_PS2_VI_RATE_HZ;

        frameUsec += extraUsec;
        sGfxPacingRemainder -= extraUsec * OOT_PS2_VI_RATE_HZ;
    }

    return frameUsec;
}

static void SchedPs2_PacePresentedTask(u32 updateRate) {
    const u32 now = OotPs2Time_GetUsecLow();
    const u32 frameUsec = SchedPs2_GetFrameUsec(updateRate);

    if (!sGfxPacingInitialized) {
        sNextGfxCompletionUsec = now;
        sGfxPacingInitialized = true;
    }

    {
        const s32 waitUsec = SchedPs2_TimeDiff(sNextGfxCompletionUsec, now);

        if (waitUsec > 0) {
            OotPs2Thread_Delay((u32)waitUsec);
        } else if (waitUsec < 0) {

            sNextGfxCompletionUsec = now;
        }
    }

    sNextGfxCompletionUsec += frameUsec;
}

static void SchedPs2_RenderTask(const OSScTask* task) {

    OotPs2Renderer_RenderTask(&task->list);
}

void Sched_Notify(Scheduler* sc) {
    OSScTask* task;

    if (sc == NULL) {
        return;
    }

    while (osRecvMesg(&sc->cmdQueue, (OSMesg*)&task, OS_MESG_NOBLOCK) == 0) {
        if (task == NULL) {
            continue;
        }

        if (task->list.t.type == M_GFXTASK) {
            const bool presents = ((task->flags & OS_SC_SWAPBUFFER) != 0) && (task->framebuffer != NULL);

            SchedPs2_RenderTask(task);

            if (presents) {
                const u32 updateRate = SchedPs2_GetUpdateRate(task);
                
                osViSwapBuffer(task->framebuffer->swapBuffer);
                SchedPs2_PacePresentedTask(updateRate);
            }
        }

        if (task->msgQueue != NULL) {
            osSendMesg(task->msgQueue, task->msg, OS_MESG_NOBLOCK);
        }
    }
}

void Sched_Init(Scheduler* sc, void* stack, OSPri priority, u8 viModeType, UNK_TYPE arg4, IrqMgr* irqMgr) {
    if (sc == NULL) {
        return;
    }

    OotPs2Trace_Log("sched clear begin");
    memset(sc, 0, sizeof(*sc));
    OotPs2Trace_Log("sched clear done");

    osCreateMesgQueue(&sc->interruptQueue, sc->interruptMsgBuf, ARRAY_COUNT(sc->interruptMsgBuf));
    osCreateMesgQueue(&sc->cmdQueue, sc->cmdMsgBuf, ARRAY_COUNT(sc->cmdMsgBuf));

    sc->retraceCount = 1;
    sc->isFirstSwap = true;

    sNextGfxCompletionUsec = 0U;
    sGfxPacingRemainder = 0U;
    sGfxPacingInitialized = false;

    OotPs2Renderer_Init();

    (void)stack;
    (void)priority;
    (void)viModeType;
    (void)arg4;
    (void)irqMgr;
}

void Sched_FlushTaskQueue(void) {
}
