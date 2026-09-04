#ifndef OOT_PORT_AUDIO_BACKEND_H
#define OOT_PORT_AUDIO_BACKEND_H

#include "ultra64.h"

#ifndef OOT_PSP_AUDIO_DIAGNOSTICS
#define OOT_PSP_AUDIO_DIAGNOSTICS 0
#endif

typedef enum OotPortAudioProducerState {
    OOT_PSP_AUDIO_PRODUCER_STATE_STOPPED,
    OOT_PSP_AUDIO_PRODUCER_STATE_STARTING,
    OOT_PSP_AUDIO_PRODUCER_STATE_PRIMING,
    OOT_PSP_AUDIO_PRODUCER_STATE_TIMER_WAIT,
    OOT_PSP_AUDIO_PRODUCER_STATE_UPDATE,
    OOT_PSP_AUDIO_PRODUCER_STATE_CATCHUP,
    OOT_PSP_AUDIO_PRODUCER_STATE_IO_BACKOFF,
    OOT_PSP_AUDIO_PRODUCER_STATE_RING_FULL,
    OOT_PSP_AUDIO_PRODUCER_STATE_WAIT_ME,
    OOT_PSP_AUDIO_PRODUCER_STATE_PREPARE,
    OOT_PSP_AUDIO_PRODUCER_STATE_SEQUENCE,
    OOT_PSP_AUDIO_PRODUCER_STATE_SUBMIT,
} OotPortAudioProducerState;

#if defined(OOTDEBUG)
typedef struct OotPortAudioProfileCounters {
    u32 updates;
    u32 waitUsec;
    u32 prepareUsec;
    u32 synthUsec;
    u32 submitUsec;
    u32 sequenceUsec;
    u32 commandBuildUsec;
    u32 abiCommands;
    u32 sampleDmas;
    u32 meSubmits;
    u32 cpuMixes;
    u32 meFailures;
    u32 meActive;
    u32 meState;
    u32 meProgress;
} OotPortAudioProfileCounters;
#endif

s32 OotPortAudioBackend_Init(void);
#if defined(TARGET_PSP)
s32 OotPortAudioBackend_BootMe(void);
#endif
s32 OotPortAudioBackend_Queue(const void* buf, u32 size);
s32 OotPortAudioBackend_SetFrequency(u32 frequency);
u32 OotPortAudioBackend_GetLength(void);
s32 OotPortAudioBackend_NeedsRefillUrgently(void);
s32 OotPortAudioBackend_NeedsRefillDuringIo(void);
#if defined(OOTDEBUG)
void OotPortAudioBackend_GetThreadRunClocks(u64* producerClocks, u64* outputClocks);
void OotPortAudioBackend_GetProfileCounters(OotPortAudioProfileCounters* counters);
#endif
#if defined(OOTDEBUG) || OOT_PSP_AUDIO_DIAGNOSTICS
void OotPortAudioBackend_RecordUpdateProfile(u32 waitUsec, u32 prepareUsec, u32 synthUsec, u32 submitUsec,
                                            u32 abiCommands, u32 sampleDmas);
void OotPortAudioBackend_RecordSynthesisProfile(u32 sequenceUsec, u32 commandBuildUsec);
#endif
#if OOT_PSP_AUDIO_DIAGNOSTICS
void OotPortAudioBackend_SetDiagnosticProducerState(OotPortAudioProducerState state);
#else
#define OotPortAudioBackend_SetDiagnosticProducerState(state) ((void)0)
#endif
void OotPortAudioBackend_SubmitCommands(const Acmd* cmdList, s32 cmdCount);
void OotPortAudioBackend_SubmitCommandsAndQueue(const Acmd* cmdList, s32 cmdCount, const void* buf, u32 size);
void OotPortAudioBackend_SubmitSynthesis(Acmd* cmdList, s16* aiBuffer, s32 aiFrames);
#if defined(TARGET_PSP)
void OotPortAudioBackend_WaitForCommands(void);
#endif
void OotPortAudioBackend_ExecuteCommands(const Acmd* cmdList, s32 cmdCount);

void OotPortAudio_Init(void);
void OotPortAudio_Update(void);

#endif
