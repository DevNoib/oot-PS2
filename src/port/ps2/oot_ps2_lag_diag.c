#include "oot_ps2_lag_diag.h"
#include <stdio.h>
#include <string.h>

OotPs2LagDiagFrame gOotPs2LagDiagFrame;

void OotPs2LagDiag_BeginFrame(void) {
}

void OotPs2LagDiag_RecordActorUpdate(int category, int actorId, uint32_t usec) {
}

void OotPs2LagDiag_RecordActorDraw(int category, int actorId, uint32_t usec) {
}

void OotPs2LagDiag_RecordDmaWait(uint32_t usec) {
}

void OotPs2LagDiag_RecordQueueExec(uint32_t usec) {
}

void OotPs2LagDiag_RecordGfx(uint32_t dlUsec, uint32_t submitUsec, uint32_t commands,
                             uint32_t inputTris, uint32_t outputTris, uint32_t flushes,
                             uint32_t drawCalls, uint32_t uploads, uint32_t texChanges,
                             uint32_t texSameFlushes, uint32_t maxBatch) {
}

void OotPs2LagDiag_RecordTextureHash(uint32_t usec, uint32_t bytes) {
}

void OotPs2LagDiag_RecordTextureImport(uint32_t usec, int uploaded) {
}

void OotPs2LagDiag_RecordTextureCacheClear(uint32_t usec) {
}

void OotPs2LagDiag_RecordPrecombine(uint32_t usec, int kind) {
}

void OotPs2LagDiag_RecordRapi(uint64_t drawTicks, uint64_t texTicks, uint64_t untexTicks,
                              uint64_t exactTicks, uint64_t fogTicks, uint64_t uploadTicks,
                              uint64_t bindTicks, uint32_t drawCalls, uint32_t exactCalls,
                              uint32_t fogCalls, uint32_t uploadCalls, uint32_t bindCalls,
                              uint32_t bindTransfers) {
}

void OotPs2LagDiag_RecordScheduler(uint32_t renderUsec, uint32_t rateSleepUsec,
                                   uint32_t presentSleepUsec, uint32_t endSleepUsec,
                                   uint32_t deadlineMissUsec, uint32_t presentCount,
                                   uint32_t mode) {
}

void OotPs2LagDiag_Report(int sceneId, int roomId, int updateRate,
                          uint32_t graphTotalUsec, uint32_t gameStateUsec,
                          uint32_t graphTaskUsec, uint32_t schedUsec,
                          uint32_t prevTaskWaitUsec, uint32_t audioUsec) {
}
