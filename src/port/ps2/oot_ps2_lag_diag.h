#ifndef OOT_PS2_LAG_DIAG_H
#define OOT_PS2_LAG_DIAG_H

#include <stdint.h>

#ifndef OOT_PS2_SPIKE_DIAG
#define OOT_PS2_SPIKE_DIAG 0
#endif

#define OOT_PS2_LAG_ACTOR_CATEGORIES 12

typedef struct OotPs2LagDiagFrame {
    uint32_t playUpdateUsec;
    uint32_t playDrawUsec;
    uint32_t objectsUsec;
    uint32_t roomProcessUsec;
    uint32_t collisionAtUsec;
    uint32_t collisionOcUsec;
    uint32_t collisionDamageUsec;
    uint32_t collisionClearUsec;
    uint32_t actorUpdateUsec;
    uint32_t cutsceneUsec;
    uint32_t effectsUpdateUsec;
    uint32_t cameraUsec;
    uint32_t environmentUsec;
    uint32_t sceneRoomDrawUsec;
    uint32_t actorDrawUsec;
    uint32_t overlayDrawUsec;
    uint32_t dynaContextUsec;
    uint32_t dynaTransformsUsec;

    uint32_t actorUpdateCatUsec[OOT_PS2_LAG_ACTOR_CATEGORIES];
    uint16_t actorUpdateCatCount[OOT_PS2_LAG_ACTOR_CATEGORIES];
    uint32_t actorDrawCatUsec[OOT_PS2_LAG_ACTOR_CATEGORIES];
    uint16_t actorDrawCatCount[OOT_PS2_LAG_ACTOR_CATEGORIES];
    int16_t hotUpdateActorId;
    int8_t hotUpdateCategory;
    uint32_t hotUpdateUsec;
    int16_t hotDrawActorId;
    int8_t hotDrawCategory;
    uint32_t hotDrawUsec;

    uint32_t gfxDlUsec;
    uint32_t gfxSubmitUsec;
    uint32_t gfxCommands;
    uint32_t gfxInputTris;
    uint32_t gfxOutputTris;
    uint32_t gfxFlushes;
    uint32_t gfxDrawCalls;
    uint32_t gfxUploads;
    uint32_t gfxTextureChanges;
    uint32_t gfxTextureSameFlushes;
    uint32_t gfxMaxBatch;
    uint32_t gfxHashUsec;
    uint32_t gfxHashCalls;
    uint32_t gfxHashBytes;
    uint32_t gfxImportUsec;
    uint32_t gfxImportCalls;
    uint32_t gfxImportMaxUsec;
    uint32_t gfxImportUploads;
    uint32_t gfxCacheClearUsec;
    uint32_t gfxCacheClearCalls;
    uint32_t gfxPrecombineUsec;
    uint32_t gfxPrecombineCalls;
    uint32_t gfxPrecombineMaxUsec;
    uint32_t gfxTwoI4Usec;
    uint32_t gfxTwoI4Calls;
    uint32_t gfxFlameUsec;
    uint32_t gfxFlameCalls;

    uint32_t rapiDrawUsec;
    uint32_t rapiTexUsec;
    uint32_t rapiUntexUsec;
    uint32_t rapiExactUsec;
    uint32_t rapiFogUsec;
    uint32_t rapiUploadUsec;
    uint32_t rapiBindUsec;
    uint32_t rapiDrawCalls;
    uint32_t rapiExactCalls;
    uint32_t rapiFogCalls;
    uint32_t rapiUploadCalls;
    uint32_t rapiBindCalls;
    uint32_t rapiBindTransfers;

    uint32_t gsWaitUsec;
    uint32_t gsWaitMaxUsec;
    uint32_t gsWaitCount;
    uint32_t dmaWaitUsec;
    uint32_t dmaWaitMaxUsec;
    uint32_t dmaWaitCount;
    uint32_t queueExecUsec;
    uint32_t queueExecMaxUsec;
    uint32_t queueExecCount;

    uint32_t schedRenderUsec;
    uint32_t schedRateSleepUsec;
    uint32_t schedPresentSleepUsec;
    uint32_t schedEndSleepUsec;
    uint32_t schedDeadlineMissUsec;
    uint32_t schedPresentCount;
    uint32_t schedMode;
} OotPs2LagDiagFrame;

extern OotPs2LagDiagFrame gOotPs2LagDiagFrame;

void OotPs2LagDiag_BeginFrame(void);
void OotPs2LagDiag_RecordActorUpdate(int category, int actorId, uint32_t usec);
void OotPs2LagDiag_RecordActorDraw(int category, int actorId, uint32_t usec);
void OotPs2LagDiag_RecordGsWait(uint32_t usec);
void OotPs2LagDiag_RecordDmaWait(uint32_t usec);
void OotPs2LagDiag_RecordQueueExec(uint32_t usec);
void OotPs2LagDiag_RecordGfx(uint32_t dlUsec, uint32_t submitUsec, uint32_t commands,
                             uint32_t inputTris, uint32_t outputTris, uint32_t flushes,
                             uint32_t drawCalls, uint32_t uploads, uint32_t texChanges,
                             uint32_t texSameFlushes, uint32_t maxBatch);
void OotPs2LagDiag_RecordTextureHash(uint32_t usec, uint32_t bytes);
void OotPs2LagDiag_RecordTextureImport(uint32_t usec, int uploaded);
void OotPs2LagDiag_RecordTextureCacheClear(uint32_t usec);
void OotPs2LagDiag_RecordPrecombine(uint32_t usec, int kind);
void OotPs2LagDiag_RecordRapi(uint64_t drawTicks, uint64_t texTicks, uint64_t untexTicks,
                              uint64_t exactTicks, uint64_t fogTicks, uint64_t uploadTicks,
                              uint64_t bindTicks, uint32_t drawCalls, uint32_t exactCalls,
                              uint32_t fogCalls, uint32_t uploadCalls, uint32_t bindCalls,
                              uint32_t bindTransfers);
void OotPs2LagDiag_RecordScheduler(uint32_t renderUsec, uint32_t rateSleepUsec,
                                   uint32_t presentSleepUsec, uint32_t endSleepUsec,
                                   uint32_t deadlineMissUsec, uint32_t presentCount,
                                   uint32_t mode);
void OotPs2LagDiag_Report(int sceneId, int roomId, int updateRate,
                          uint32_t graphTotalUsec, uint32_t gameStateUsec,
                          uint32_t graphTaskUsec, uint32_t schedUsec,
                          uint32_t prevTaskWaitUsec, uint32_t audioUsec);

#endif
