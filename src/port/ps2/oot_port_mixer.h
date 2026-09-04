#ifndef OOT_PORT_MIXER_H
#define OOT_PORT_MIXER_H

#include "ultra64.h"

#define OOT_PORT_MIXER_PROFILE_OPCODE_COUNT 32
#define OOT_PORT_MIXER_PROFILE_OPCODE_IDLE 0xFFFFFFFFU

typedef struct OotPortMixerOpcodeProfile {
    u32 sequence;
    u32 jobs;
    u32 commands;
    u32 jobTicks;
    u32 jobMaxTicks;
    u32 lastJobTicks;
    u32 lastJobCommands;
    u32 lastJobSlowOpcode;
    u32 lastJobSlowTicks;
    u32 maxJobCommands;
    u32 maxJobSlowOpcode;
    u32 maxJobSlowTicks;
    u32 currentOpcode;
    u32 currentCommandIndex;
    u32 opcodeCalls[OOT_PORT_MIXER_PROFILE_OPCODE_COUNT];
    u32 opcodeTicks[OOT_PORT_MIXER_PROFILE_OPCODE_COUNT];
    u32 opcodeMaxTicks[OOT_PORT_MIXER_PROFILE_OPCODE_COUNT];
} OotPortMixerOpcodeProfile;

void OotPortMixer_ClearBuffer(u16 dmem, s32 nbytes);
void OotPortMixer_LoadBuffer(const void* source, u16 dmemDest, u16 nbytes);
void OotPortMixer_SaveBuffer(u16 dmemSrc, void* dest, u16 nbytes);
void OotPortMixer_LoadADPCM(s32 numEntriesBytes, const s16* book);
void OotPortMixer_SetBuffer(u8 flags, u16 dmemIn, u16 dmemOut, u16 nbytes);
void OotPortMixer_Interleave(u16 dmemOut, u16 dmemLeft, u16 dmemRight, u16 count);
void OotPortMixer_Interl(u16 dmemIn, u16 dmemOut, u16 count);
void OotPortMixer_DMEMMove(u16 dmemIn, u16 dmemOut, s32 nbytes);
void OotPortMixer_SetLoop(ADPCM_STATE* state);
void OotPortMixer_ADPCMdec(u8 flags, ADPCM_STATE state);
void OotPortMixer_S8Dec(u8 flags, ADPCM_STATE state);
void OotPortMixer_Resample(u8 flags, u16 pitch, RESAMPLE_STATE state);
void OotPortMixer_ResampleZoh(u16 pitch, u16 pitchAccu);
void OotPortMixer_EnvSetup1(s32 initialReverb, s32 rampReverb, s32 rampLeft, s32 rampRight);
void OotPortMixer_EnvSetup2(s32 volLeft, s32 volRight);
void OotPortMixer_EnvMixer(u16 dmemSrc, s32 aiBufLen, s32 swapLR, s32 x0, s32 x1, s32 x2, s32 x3, u32 dmemDests,
                          u32 bits);
void OotPortMixer_Mix(s32 countQuads, s16 gain, u16 dmemIn, u16 dmemOut);
void OotPortMixer_AddMixer(s32 nbytes, u16 dmemIn, u16 dmemOut, s16 gain);
void OotPortMixer_Duplicate(s32 numCopies, u16 dmemSrc, u16 dmemDest);
void OotPortMixer_CopyBlocks(s32 numBlocks, u16 dmemSrc, u16 dmemDest, s32 blockSize);
void OotPortMixer_Filter(u8 flags, s32 countOrBuf, void* state);
void OotPortMixer_HiLoGain(s32 gain, u16 dmemIn, u16 dmemOut, s32 nbytes);
void OotPortMixer_UnkCmd3(s32 arg1, s32 arg2, s32 size);
void OotPortMixer_UnkCmd19(s32 arg1, s32 arg2, s32 size, s32 arg4);
void OotPortMixer_InitVme(void);
void OotPortMixer_ShutdownVme(void);
void OotPortMixer_ExecuteCommandList(const Acmd* cmdList, s32 cmdCount);
void OotPortMixer_ExecuteCommandListMe(const Acmd* cmdList, s32 cmdCount,
                                      volatile OotPortMixerOpcodeProfile* profile);
void OotPortMixer_InvalidateStateCache(void);

#if defined(OOT_PORT_MIXER_INLINE)
#undef aSegment
#undef aClearBuffer
#undef aSetBuffer
#undef aLoadBuffer
#undef aSaveBuffer
#undef aDMEMMove
#undef aMix
#undef aEnvMixer
#undef aResample
#undef aInterleave
#undef aInterl
#undef aSetVolume
#undef aSetVolume32
#undef aSetLoop
#undef aLoadADPCM
#undef aADPCMdec
#undef aEnvSetup1
#undef aEnvSetup2
#undef aFilter
#undef aDuplicate
#undef aAddMixer
#undef aResampleZoh
#undef aS8Dec
#undef aHiLoGain
#undef aUnkCmd3
#undef aUnkCmd19
#undef aPoleFilter
#undef aPan

#define OOT_PORT_MIXER_EVAL(pkt) ((void)(pkt))

#define aSegment(pkt, s, b) OOT_PORT_MIXER_EVAL(pkt)
#define aClearBuffer(pkt, d, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_ClearBuffer(d, c))
#define aLoadBuffer(pkt, s, d, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_LoadBuffer(s, d, c))
#define aSaveBuffer(pkt, s, d, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_SaveBuffer(s, d, c))
#define aLoadADPCM(pkt, c, d) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_LoadADPCM(c, d))
#define aSetBuffer(pkt, f, i, o, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_SetBuffer(f, i, o, c))
#define aInterleave(pkt, o, l, r, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_Interleave(o, l, r, c))
#define aInterl(pkt, i, o, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_Interl(i, o, c))
#define aDMEMMove(pkt, i, o, c) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_DMEMMove(i, o, c))
#define aSetLoop(pkt, a) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_SetLoop((ADPCM_STATE*)(a)))
#define aADPCMdec(pkt, f, s) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_ADPCMdec(f, s))
#define aS8Dec(pkt, f, s) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_S8Dec(f, s))
#define aResample(pkt, f, p, s) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_Resample(f, p, s))
#define aResampleZoh(pkt, p, a) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_ResampleZoh(p, a))
#define aEnvSetup1(pkt, a, b, c, d) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_EnvSetup1(a, b, c, d))
#define aEnvSetup2(pkt, l, r) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_EnvSetup2(l, r))
#define aEnvMixer(pkt, d, c, s, x0, x1, x2, x3, m, bits) \
    (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_EnvMixer(d, c, s, x0, x1, x2, x3, m, bits))
#define aMix(pkt, f, g, i, o) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_Mix(f, g, i, o))
#define aAddMixer(pkt, c, i, o, g) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_AddMixer(c, i, o, g))
#define aDuplicate(pkt, n, s, d) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_Duplicate(n, s, d))
#define aFilter(pkt, f, c, a) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_Filter(f, c, a))
#define aHiLoGain(pkt, gain, count, dmem, a4) \
    (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_HiLoGain(gain, dmem, a4, count))
#define aUnkCmd3(pkt, a1, a2, a3) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_UnkCmd3(a1, a2, a3))
#define aUnkCmd19(pkt, a1, a2, a3, a4) (OOT_PORT_MIXER_EVAL(pkt), OotPortMixer_UnkCmd19(a1, a2, a3, a4))
#define aSetVolume(pkt, f, v, t, r) OOT_PORT_MIXER_EVAL(pkt)
#define aSetVolume32(pkt, f, v, tr) OOT_PORT_MIXER_EVAL(pkt)
#define aPoleFilter(pkt, f, g, s) OOT_PORT_MIXER_EVAL(pkt)
#define aPan(pkt, f, d, s) OOT_PORT_MIXER_EVAL(pkt)
#endif

#endif
