#include "oot_port_audio_backend.h"
#include "audio.h"
#include "dma.h"
#include "oot_port_mixer.h"
#include "oot_ps2_platform.h"
#include <audsrv.h>
#include <ps2_audio_driver.h>
#include <kernel.h>
#include <delaythread.h>
#include <timer.h>
#include <stdio.h>
#include <string.h>

s32 OotPortAudioBackend_Init(void) {
    return 0;
}

s32 OotPortAudioBackend_Queue(const void* buf, u32 size) {
    return 0;
}

s32 OotPortAudioBackend_SetFrequency(u32 frequency) {
    return 0;
}

u32 OotPortAudioBackend_GetLength(void) {
    return 0;
}

s32 OotPortAudioBackend_NeedsRefillUrgently(void) {
    return 0;
}

s32 OotPortAudioBackend_NeedsRefillDuringIo(void) {
    return 0;
}

void OotPortAudioBackend_SubmitCommands(const Acmd* cmdList, s32 cmdCount) {
}

void OotPortAudioBackend_SubmitCommandsAndQueue(const Acmd* cmdList, s32 cmdCount, const void* buf, u32 size) {
}

void OotPortAudioBackend_SubmitSynthesis(Acmd* cmdList, s16* aiBuffer, s32 aiFrames) {
}

void OotPortAudioBackend_ExecuteCommands(const Acmd* cmdList, s32 cmdCount) {
}

void OotPortAudio_Init(void) {
}

void OotPortAudio_Update(void) {
}
