#include "audiomgr.h"
#include "array_count.h"
#include "attributes.h"
#include "sfx.h"

void AudioMgr_Init(UNUSED AudioMgr* audioMgr, UNUSED void* stack, UNUSED OSPri pri, UNUSED OSId id,
                   UNUSED Scheduler* sched, UNUSED IrqMgr* irqMgr) {
}

void AudioMgr_WaitForInit(UNUSED AudioMgr* audioMgr) {
}

void AudioMgr_StopAllSfx(void) {
}
