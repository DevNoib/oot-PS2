#ifndef OOT_PORT_AUDIO_COMMANDS_H
#define OOT_PORT_AUDIO_COMMANDS_H

#include "ultra64.h"

#define OOT_PORT_A_COPYBLOCKS          16
#define OOT_PORT_A_REVERB_DOWNSAMPLE  24
#define OOT_PORT_A_REVERB_SAVE        26
#define OOT_PORT_A_REVERB_LOAD        27
#define OOT_PORT_A_LOAD_SAMPLE_CACHED 28
#define OOT_PORT_A_LOAD_ADPCM_CACHED  29
#define OOT_PORT_AUDIO_SAMPLE_CACHE_PAGE_SIZE 1024

typedef struct {
    s16* leftRingBuf;
    s16* rightRingBuf;
    s32 startPos;
    s16 lengthA;
    s16 lengthB;
    u16 bufSizePerChan;
    u8 downsampleRate;
    u8 pad;
    u32 cacheEpoch;
} OotPortAudioReverbDownsampleCmd;

void OotPortAudioSynth_InvalidateMeCaches(void);

#define aOotPortAudioReverbDownsample(pkt, dmemLeft, desc)                           \
    {                                                                               \
        Acmd* _a = (Acmd*)(pkt);                                                    \
        _a->words.w0 = _SHIFTL(OOT_PORT_A_REVERB_DOWNSAMPLE, 24, 8) |               \
                       _SHIFTL((dmemLeft), 0, 16);                                  \
        _a->words.w1 = (uintptr_t)(desc);                                           \
    }

#define aOotPortAudioReverbSave(pkt, dmemLeft, desc)                                 \
    {                                                                               \
        Acmd* _a = (Acmd*)(pkt);                                                    \
        _a->words.w0 = _SHIFTL(OOT_PORT_A_REVERB_SAVE, 24, 8) |                      \
                       _SHIFTL((dmemLeft), 0, 16);                                  \
        _a->words.w1 = (uintptr_t)(desc);                                           \
    }

#define aOotPortAudioReverbLoad(pkt, dmemLeft, desc)                                 \
    {                                                                               \
        Acmd* _a = (Acmd*)(pkt);                                                    \
        _a->words.w0 = _SHIFTL(OOT_PORT_A_REVERB_LOAD, 24, 8) |                      \
                       _SHIFTL((dmemLeft), 0, 16);                                  \
        _a->words.w1 = (uintptr_t)(desc);                                           \
    }

#define aOotPortAudioLoadSampleCached(pkt, source, dmemDest, nbytes)                  \
    {                                                                               \
        Acmd* _a = (Acmd*)(pkt);                                                    \
        _a->words.w0 = _SHIFTL(OOT_PORT_A_LOAD_SAMPLE_CACHED, 24, 8) |              \
                       _SHIFTL(((nbytes) >> 4), 16, 8) | _SHIFTL((dmemDest), 0, 16); \
        _a->words.w1 = (uintptr_t)(source);                                         \
    }

#define aOotPortAudioLoadAdpcmCached(pkt, nbytes, book)                              \
    {                                                                               \
        Acmd* _a = (Acmd*)(pkt);                                                    \
        _a->words.w0 = _SHIFTL(OOT_PORT_A_LOAD_ADPCM_CACHED, 24, 8) |               \
                       _SHIFTL((nbytes), 0, 24);                                    \
        _a->words.w1 = (uintptr_t)(book);                                           \
    }

#endif
