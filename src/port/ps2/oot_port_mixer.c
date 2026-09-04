#include "oot_port_mixer.h"
#include "attributes.h"
#include "audio.h"
#include "oot_port_audio_commands.h"
#include <me-core-mapper/me-core-mapper.h>
#include <me-core-mapper/me-lib.h>
#include <pspkernel.h>
#include <string.h>
#include <stddef.h>

void OotPortMixer_InitVme(void) {
}

void OotPortMixer_ShutdownVme(void) {
}

void OotPortMixer_ClearBuffer(u16 dmem, s32 nbytes) {
}

void OotPortMixer_LoadBuffer(const void* source, u16 dmemDest, u16 nbytes) {
}

void OotPortMixer_SaveBuffer(u16 dmemSrc, void* dest, u16 nbytes) {
}

void OotPortMixer_LoadADPCM(s32 numEntriesBytes, const s16* book) {
}

void OotPortMixer_SetBuffer(UNUSED u8 flags, u16 dmemIn, u16 dmemOut, u16 nbytes) {
}

void OotPortMixer_Interleave(u16 dmemOut, u16 dmemLeft, u16 dmemRight, u16 count) {
}

void OotPortMixer_Interl(u16 dmemIn, u16 dmemOut, u16 count) {
}

void OotPortMixer_DMEMMove(u16 dmemIn, u16 dmemOut, s32 nbytes) {
}

void OotPortMixer_SetLoop(ADPCM_STATE* state) {
}

void OotPortMixer_ADPCMdec(u8 flags, ADPCM_STATE state) {
}

void OotPortMixer_S8Dec(u8 flags, ADPCM_STATE state) {
}

void OotPortMixer_Resample(u8 flags, u16 pitch, RESAMPLE_STATE state) {
}

void OotPortMixer_ResampleZoh(u16 pitch, u16 pitchAccu) {
}

void OotPortMixer_EnvSetup1(s32 initialReverb, s32 rampReverb, s32 rampLeft, s32 rampRight) {
}

void OotPortMixer_EnvSetup2(s32 volLeft, s32 volRight) {
}

void OotPortMixer_EnvMixer(u16 dmemSrc, s32 aiBufLen, s32 swapLR, s32 x0, s32 x1, s32 x2, s32 x3, u32 dmemDests,
                          UNUSED u32 bits) {
}

void OotPortMixer_Mix(s32 countQuads, s16 gain, u16 dmemIn, u16 dmemOut) {
}

void OotPortMixer_AddMixer(s32 nbytes, u16 dmemIn, u16 dmemOut, UNUSED s16 gain) {
}

void OotPortMixer_Duplicate(s32 numCopies, u16 dmemSrc, u16 dmemDest) {
}

void OotPortMixer_CopyBlocks(s32 numBlocks, u16 dmemSrc, u16 dmemDest, s32 blockSize) {
}

void OotPortMixer_Filter(u8 flags, s32 countOrBuf, void* state) {
}

void OotPortMixer_HiLoGain(s32 gain, u16 dmemIn, UNUSED u16 dmemOut, s32 nbytes) {
}

void OotPortMixer_UnkCmd3(s32 arg1, s32 arg2, s32 size) {
}

void OotPortMixer_UnkCmd19(UNUSED s32 arg1, UNUSED s32 arg2, UNUSED s32 size, UNUSED s32 arg4) {
}

void OotPortMixer_ExecuteCommandList(const Acmd* cmdList, s32 cmdCount) {
}

void OotPortMixer_ExecuteCommandListMe(const Acmd* cmdList, s32 cmdCount,
                                      volatile OotPortMixerOpcodeProfile* profile) {
}

void OotPortMixer_InvalidateStateCache(void) {
}
