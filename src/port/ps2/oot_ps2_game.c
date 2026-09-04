#include "audio.h"
#include "audiomgr.h"
#include "assets/objects/gameplay_keep/gameplay_keep.h"
#include "assets/objects/object_link_boy/object_link_boy.h"
#include "assets/objects/object_link_child/object_link_child.h"
#include "assets/objects/gameplay_field_keep/gameplay_field_keep.h"
#include "assets/objects/object_bombiwa/object_bombiwa.h"
#include "assets/objects/object_horse/object_horse.h"
#include "assets/objects/object_kanban/object_kanban.h"
#include "assets/objects/object_kusa/object_kusa.h"
#include "assets/objects/object_mag/object_mag.h"
#include "assets/objects/object_owl/object_owl.h"
#include "assets/objects/object_peehat/object_peehat.h"
#include "assets/objects/object_skb/object_skb.h"
#include "assets/objects/object_spot00_objects/object_spot00_objects.h"
#include "assets/objects/object_wood02/object_wood02.h"
#include "assets/scenes/overworld/spot00/spot00_room_0.h"
#include "array_count.h"
#include "bgcheck.h"
#include "camera.h"
#include "controller.h"
#include "debug.h"
#include "dma.h"
#include "environment.h"
#include "fault.h"
#include "file_select_state.h"
#include "font.h"
#include "console_logo_state.h"
#include "gfx.h"
#include "irqmgr.h"
#include "jpeg.h"
#include "letterbox.h"
#include "libc64/malloc.h"
#include "libu64/runtime.h"
#include "main.h"
#include "map.h"
#include "map_select_state.h"
#include "message.h"
#include "one_point_cutscene.h"
#include "object.h"
#include "padmgr.h"
#include "play_state.h"
#include "player.h"
#include "prenmi_buff.h"
#include "region.h"
#include "regs.h"
#include "rumble.h"
#include "save.h"
#include "sched.h"
#include "segmented_address.h"
#include "scene.h"
#include "setup_state.h"
#include "speed_meter.h"
#include "sram.h"
#include "ss_sram.h"
#include "sys_matrix.h"
#include "sys_cfb.h"
#include "thread.h"
#include "title_setup_state.h"
#include "vi_mode.h"
#include "z_actor_dlftbls.h"
#include "z_game_dlftbls.h"
#include "z_lib.h"
#include "oot_port_asset_loader.h"
#include "oot_port_audio_backend.h"
#include "oot_port_compat.h"
#include "oot_port_memory.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_crash.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

s32 gScreenWidth = SCREEN_WIDTH;
s32 gScreenHeight = SCREEN_HEIGHT;
u32 gSystemHeapSize = 0;
Scheduler gScheduler;
PadMgr gPadMgr;
IrqMgr gIrqMgr;
PreNmiBuff* gAppNmiBufferPtr = NULL;
uintptr_t gSegments[NUM_SEGMENTS];
s32 gCurrentRegion = REGION_US;

#define OOT_PS2_NATIVE_ADDR_START 0x00100000U
#define OOT_PS2_NATIVE_ADDR_END 0x02000000U
#define OOT_PS2_SEGMENTED_COLLISION_OFFSET_MAX 0x00010000U
#define OOT_PS2_SAVE_PATH "oot-ps2-save.bin"
#define OOT_PS2_SAVE_IO_CHUNK_SIZE 0x4000

volatile OSTime gAudioThreadUpdateTimeTotalPerGfxTask = 0;
volatile OSTime gGfxTaskSentToNextReadyMinusAudioThreadUpdateTime = 0;
volatile OSTime gRSPAudioTimeTotal = 0;
volatile OSTime gRSPGfxTimeTotal = 0;
volatile OSTime gRDPTimeTotal = 0;
volatile OSTime gGraphUpdatePeriod = 0;
volatile OSTime gAudioThreadUpdateTimeStart = 0;
volatile OSTime gAudioThreadUpdateTimeAcc = 0;
volatile OSTime gRSPAudioTimeAcc = 0;
volatile OSTime gRSPGfxTimeAcc = 0;
volatile OSTime gRSPOtherTimeAcc = 0;
volatile OSTime D_8016A578 = 0;
volatile OSTime gRDPTimeAcc = 0;

u32 gDmaMgrVerbose = 0;
size_t gDmaMgrDmaBuffSize = DMAMGR_DEFAULT_BUFSIZE;
float qNaN0x10000 = __builtin_nanf("");
Mtx D_01000000 = gdSPDefMtx(1.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, 1.0f);

#define OOT_PS2_ENTRANCE_FIELD(continueBgm, displayTitleCard, endTransType, startTransType)                  \
    (((continueBgm) ? ENTRANCE_INFO_CONTINUE_BGM_FLAG : 0) |                                                 \
     ((displayTitleCard) ? ENTRANCE_INFO_DISPLAY_TITLE_CARD_FLAG : 0) |                                      \
     (((endTransType) << ENTRANCE_INFO_END_TRANS_TYPE_SHIFT) & ENTRANCE_INFO_END_TRANS_TYPE_MASK) |          \
     (((startTransType) << ENTRANCE_INFO_START_TRANS_TYPE_SHIFT) & ENTRANCE_INFO_START_TRANS_TYPE_MASK))

#define DEFINE_ENTRANCE(_0, sceneId, spawn, continueBgm, displayTitleCard, endTransType, startTransType) \
    { sceneId, spawn,                                                                                    \
      OOT_PS2_ENTRANCE_FIELD(continueBgm, displayTitleCard, endTransType, startTransType) },

EntranceInfo gEntranceTable[ENTR_MAX] = {
#include "tables/entrance_table.h"
};

#undef DEFINE_ENTRANCE

GameStateOverlay gGameStateOverlayTable[GAMESTATE_ID_MAX] = {
    [GAMESTATE_SETUP] = { NULL, ROM_FILE_UNSET, NULL, NULL, NULL, Setup_Init, Setup_Destroy, NULL, NULL, 0,
                          sizeof(SetupState) },
    [GAMESTATE_MAP_SELECT] = { NULL, ROM_FILE_UNSET, NULL, NULL, NULL, MapSelect_Init, NULL, NULL, NULL, 0,
                               sizeof(GameState) },
    [GAMESTATE_CONSOLE_LOGO] = { NULL, ROM_FILE_UNSET, NULL, NULL, NULL, ConsoleLogo_Init, ConsoleLogo_Destroy, NULL,
                                 NULL, 0, sizeof(ConsoleLogoState) },
    [GAMESTATE_PLAY] = { NULL, ROM_FILE_UNSET, NULL, NULL, NULL, Play_Init, Play_Destroy, NULL, NULL, 0,
                         sizeof(PlayState) },
    [GAMESTATE_TITLE_SETUP] = { NULL, ROM_FILE_UNSET, NULL, NULL, NULL, TitleSetup_Init, TitleSetup_Destroy, NULL,
                                NULL, 0, sizeof(TitleSetupState) },
    [GAMESTATE_FILE_SELECT] = { NULL, ROM_FILE_UNSET, NULL, NULL, NULL, FileSelect_Init, NULL, NULL, NULL, 0,
                                sizeof(GameState) },
};

static RegEditor sRegEditor;
RegEditor* gRegEditor = &sRegEditor;

static PreNmiBuff sPs2PreNmiBuffer;
u8 gOotPortSystemHeap[OOT_PORT_SYSTEM_HEAP_SIZE] __attribute__((aligned(64)));
static u8 sPs2Sram[SRAM_SIZE] __attribute__((aligned(64)));
static u16 sPs2Framebuffers[2][SCREEN_HEIGHT][SCREEN_WIDTH] __attribute__((aligned(64)));
u16 D_0E000000[SCREEN_HEIGHT * SCREEN_WIDTH] __attribute__((aligned(64)));
u16 D_0F000000[SCREEN_HEIGHT * SCREEN_WIDTH] __attribute__((aligned(64)));
static uintptr_t sSysCfbFbPtr[2];
static uintptr_t sSysCfbEnd;
static OotPs2Handle sPs2StackThreadId = -1;
static uintptr_t sPs2StackStart;
static uintptr_t sPs2StackEnd;
static uintptr_t sPs2StackAltStart;
static uintptr_t sPs2StackAltEnd;

static const char* OotPort_GetSavePath(const char* filename, char* buffer, size_t bufferSize) {
    if (OotPort_IsDiscBoot()) {
        OotPs2File_Mkdir("mc0:/OOTPS2", 0777);
        if (snprintf(buffer, bufferSize, "mc0:/OOTPS2/%s", filename) >= (s32)bufferSize) {
            return filename;
        }
        return buffer;
    }
    return OotPort_ResolveRootPath(filename, buffer, bufferSize);
}

static void OotPort_LoadSram(void) {
    char pathBuffer[384];
    const char* path = OotPort_GetSavePath(OOT_PS2_SAVE_PATH, pathBuffer, sizeof(pathBuffer));
    OotPs2Handle fd = OotPs2File_Open(path, OOT_PS2_FILE_RDONLY, 0);
    u8* out = sPs2Sram;
    size_t remaining = sizeof(sPs2Sram);

    if (fd < 0) {
        return;
    }

    while (remaining != 0) {
        int chunk = remaining > OOT_PS2_SAVE_IO_CHUNK_SIZE ? OOT_PS2_SAVE_IO_CHUNK_SIZE : (int)remaining;
        int read = OotPs2File_Read(fd, out, chunk);

        if (read < 0) {
            memset(sPs2Sram, 0, sizeof(sPs2Sram));
            OotPs2File_Close(fd);
            return;
        }
        if (read == 0) {
            break;
        }

        out += read;
        remaining -= read;
    }

    OotPs2File_Close(fd);
    if (remaining != 0) {
        memset(out, 0, remaining);
    }
}

static s32 OotPort_FlushSram(void) {
    char pathBuffer[384];
    const char* path = OotPort_GetSavePath(OOT_PS2_SAVE_PATH, pathBuffer, sizeof(pathBuffer));
    OotPs2Handle fd = OotPs2File_Open(path, OOT_PS2_FILE_WRONLY | OOT_PS2_FILE_CREAT | OOT_PS2_FILE_TRUNC, 0777);
    const u8* in = sPs2Sram;
    size_t remaining = sizeof(sPs2Sram);

    if (fd < 0) {
        printf("oot-ps2 save open failed path=%s err=%d\n", path, (int)fd);
        return false;
    }

    while (remaining != 0) {
        int chunk = remaining > OOT_PS2_SAVE_IO_CHUNK_SIZE ? OOT_PS2_SAVE_IO_CHUNK_SIZE : (int)remaining;
        int written = OotPs2File_Write(fd, in, chunk);

        if (written <= 0) {
            printf("oot-ps2 save write failed path=%s written=%d\n", path, written);
            OotPs2File_Close(fd);
            return false;
        }

        in += written;
        remaining -= written;
    }

    OotPs2File_Close(fd);
    return true;
}

extern u8 __bss_start[];
extern u8 _end[];

static int OotPort_IsContainedRange(const void* ptr, size_t size, uintptr_t rangeStart, uintptr_t rangeEnd) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;

    if ((ptr == NULL) || (size == 0) || (start > UINTPTR_MAX - size)) {
        return false;
    }

    end = start + size;
    return (start >= rangeStart) && (end <= rangeEnd);
}

static void OotPort_UpdateCurrentThreadStackRange(void) {
    OotPs2Handle threadId = OotPs2Thread_GetId();
    OotPs2ThreadInfo threadInfo;
    uintptr_t stack;
    uintptr_t stackSize;

    if (threadId == sPs2StackThreadId) {
        return;
    }

    sPs2StackThreadId = threadId;
    sPs2StackStart = 0;
    sPs2StackEnd = 0;
    sPs2StackAltStart = 0;
    sPs2StackAltEnd = 0;

    memset(&threadInfo, 0, sizeof(threadInfo));
    if (OotPs2Thread_GetInfo(threadId, &threadInfo) < 0) {
        return;
    }

    stack = (uintptr_t)threadInfo.stack;
    stackSize = (uintptr_t)threadInfo.stackSize;
    if ((stack == 0) || (stackSize == 0)) {
        return;
    }

    if (stack <= (UINTPTR_MAX - stackSize)) {
        sPs2StackStart = stack;
        sPs2StackEnd = stack + stackSize;
    }
    if (stack >= stackSize) {
        sPs2StackAltStart = stack - stackSize;
        sPs2StackAltEnd = stack;
    }
}

extern s32 OotPs2Asset_IsMutableTextureRange(uintptr_t start, uintptr_t end);

int OotPort_IsRuntimeByteRangeSlow(uintptr_t start, uintptr_t end) {
    OotPort_UpdateCurrentThreadStackRange();

    return ((start >= sPs2StackStart) && (end <= sPs2StackEnd)) ||
           ((start >= sPs2StackAltStart) && (end <= sPs2StackAltEnd)) ||
           OotPs2Asset_IsMutableTextureRange(start, end);
}

static s32 OotPort_IsNativeUserRange(uintptr_t addr, size_t size) {
    if (size == 0) {
        return true;
    }

    if ((addr < OOT_PS2_NATIVE_ADDR_START) || (addr >= OOT_PS2_NATIVE_ADDR_END)) {
        return false;
    }

    return size <= (OOT_PS2_NATIVE_ADDR_END - addr);
}

static uintptr_t OotPort_StripKernelAlias(uintptr_t addr) {
    uintptr_t stripped;

    if ((addr >= 0x80000000U) && (addr < 0xA0000000U)) {
        stripped = addr - 0x80000000U;
        if (OotPort_IsNativeUserRange(stripped, 1)) {
            return stripped;
        }
    }

    if ((addr >= 0xA0000000U) && (addr < 0xC0000000U)) {
        stripped = addr - 0xA0000000U;
        if (OotPort_IsNativeUserRange(stripped, 1)) {
            return stripped;
        }
    }

    return addr;
}

static s32 OotPort_IsStaticStorageRange(const void* ptr, size_t size) {
    if (OotPort_IsContainedRange(ptr, size, (uintptr_t)gOotPortSystemHeap,
                                (uintptr_t)gOotPortSystemHeap + sizeof(gOotPortSystemHeap))) {
        return true;
    }

    if (OotPort_IsContainedRange(ptr, size, (uintptr_t)sPs2Sram,
                                (uintptr_t)sPs2Sram + sizeof(sPs2Sram))) {
        return true;
    }

    if (OotPort_IsContainedRange(ptr, size, (uintptr_t)sPs2Framebuffers,
                                (uintptr_t)sPs2Framebuffers + sizeof(sPs2Framebuffers))) {
        return true;
    }

    if (OotPort_IsContainedRange(ptr, size, (uintptr_t)D_0E000000,
                                (uintptr_t)D_0E000000 + sizeof(D_0E000000))) {
        return true;
    }

    if (OotPort_IsContainedRange(ptr, size, (uintptr_t)D_0F000000,
                                (uintptr_t)D_0F000000 + sizeof(D_0F000000))) {
        return true;
    }

    if (OotPort_IsContainedRange(ptr, size, (uintptr_t)&sPs2PreNmiBuffer,
                                (uintptr_t)&sPs2PreNmiBuffer + sizeof(sPs2PreNmiBuffer))) {
        return true;
    }

    return false;
}

static s32 OotPort_IsKnownNativePointer(uintptr_t addr, size_t size) {
    u32 loadedFlags;

    addr = OotPort_StripKernelAlias(addr);

    if (!OotPort_IsNativeUserRange(addr, size)) {
        return false;
    }

    if (addr < (uintptr_t)__bss_start) {
        return true;
    }

    if (OotPort_IsStaticStorageRange((const void*)addr, size)) {
        return true;
    }

    if (OotPort_IsRuntimeByteRange((void*)addr, size)) {
        return true;
    }

    if (OotPort_GetLoadedExternalAssetRangeFlags((void*)addr, size, &loadedFlags)) {
        return true;
    }

    return false;
}

static s32 OotPort_AddressLooksSegmented(uintptr_t addr, u32 segment) {
    uintptr_t offset;

    addr = OotPort_StripKernelAlias(addr);
    offset = SEGMENT_OFFSET(addr);

    if ((segment == 0) || (segment >= NUM_SEGMENTS)) {
        return false;
    }

    if ((addr & 0xF0000000U) != 0) {
        return false;
    }

    if (OotPort_IsKnownNativePointer(addr, 1)) {
        return false;
    }

    if ((addr < OOT_PS2_NATIVE_ADDR_START) || (addr >= OOT_PS2_NATIVE_ADDR_END)) {
        return true;
    }

    if (offset < OOT_PS2_SEGMENTED_COLLISION_OFFSET_MAX) {
        return true;
    }

    return true;
}

static void* OotPort_TranslateSegmentedAddress(uintptr_t addr, u32 segment) {
    uintptr_t base = OotPort_StripKernelAlias(gSegments[segment]);
    uintptr_t offset = SEGMENT_OFFSET(addr);

    return (void*)(base + offset);
}

static void OotPort_LogUnmappedSegment(uintptr_t addr, u32 segment) {
    static s32 sUnmappedSegmentLogCount = 0;

    addr = OotPort_StripKernelAlias(addr);

    if (sUnmappedSegmentLogCount < 16) {
        printf("oot-ps2 cpu unmapped segment addr=%08lx segment=%lu offset=%06lx\n", (unsigned long)addr,
               (unsigned long)segment, (unsigned long)SEGMENT_OFFSET(addr));
    } else if (sUnmappedSegmentLogCount == 16) {
        printf("oot-ps2 cpu unmapped segment logs suppressed\n");
    }

    sUnmappedSegmentLogCount++;
}

void* SegmentedToVirtualCompat(uintptr_t addr) {
    u32 segment;
    s32 looksSegmented;

    if (addr == 0) {
        return NULL;
    }

    addr = OotPort_StripKernelAlias(addr);
    segment = SEGMENT_NUMBER(addr);
    looksSegmented = OotPort_AddressLooksSegmented(addr, segment);

    if (looksSegmented) {
        if (gSegments[segment] != 0) {
            return OotPort_TranslateSegmentedAddress(addr, segment);
        }

        OotPort_LogUnmappedSegment(addr, segment);
        return NULL;
    }

    return (void*)addr;
}

void* SegmentedToVirtualExplicit(uintptr_t addr) {
    u32 segment;

    if (addr == 0) {
        return NULL;
    }

    addr = OotPort_StripKernelAlias(addr);
    segment = SEGMENT_NUMBER(addr);

    if ((segment == 0) || (segment >= NUM_SEGMENTS) || ((addr & 0xF0000000U) != 0)) {
        return (void*)addr;
    }

    if (gSegments[segment] != 0) {
        return OotPort_TranslateSegmentedAddress(addr, segment);
    }

    OotPort_LogUnmappedSegment(addr, segment);
    return NULL;
}

void Regs_Init(void) {
    memset(&sRegEditor, 0, sizeof(sRegEditor));
    R_UPDATE_RATE = 1;
    R_PAUSE_BG_PRERENDER_STATE = PAUSE_BG_PRERENDER_OFF;
    R_GRAPH_TASKSET00_FLAGS = 0;
    R_ENABLE_FB_FILTER = 0;
}

void SysCfb_Init(UNUSED s32 n64dd) {
    memset(sPs2Framebuffers, 0, sizeof(sPs2Framebuffers));
    sSysCfbFbPtr[0] = (uintptr_t)&sPs2Framebuffers[0][0][0];
    sSysCfbFbPtr[1] = (uintptr_t)&sPs2Framebuffers[1][0][0];
    sSysCfbEnd = (uintptr_t)&sPs2Framebuffers[ARRAY_COUNT(sPs2Framebuffers)];
}

void SysCfb_Reset(void) {
    sSysCfbFbPtr[0] = 0;
    sSysCfbFbPtr[1] = 0;
    sSysCfbEnd = 0;
}

void* SysCfb_GetFbPtr(s32 idx) {
    if (idx < 0 || idx >= ARRAY_COUNT(sSysCfbFbPtr)) {
        return NULL;
    }
    return (void*)sSysCfbFbPtr[idx];
}

void* SysCfb_GetFbEnd(void) {
    return (void*)sSysCfbEnd;
}

void DmaMgr_Init(void) {
}

static s32 OotPort_IsNativeRange(uintptr_t addr, size_t size) {
    if (size == 0) {
        return true;
    }

    if ((addr < OOT_PS2_NATIVE_ADDR_START) || (addr >= OOT_PS2_NATIVE_ADDR_END)) {
        return false;
    }

    return size <= (OOT_PS2_NATIVE_ADDR_END - addr);
}

static s32 OotPort_DmaReadInternal(void* ram, uintptr_t vrom, size_t size, s32 isAudioRead) {
    s32 status;

    if (isAudioRead) {
        status = (OotPortAudioBackend_NeedsRefillUrgently() || OotPortAudioBackend_NeedsRefillDuringIo())
                     ? OotPort_AssetReadAudioUrgent(ram, vrom, size)
                     : OotPort_AssetReadAudio(ram, vrom, size);
    } else {
        status = OotPort_AssetRead(ram, vrom, size);
    }

    if (status == OOT_PORT_ASSET_READ_OK) {
        return 0;
    }

    if (status == OOT_PORT_ASSET_READ_FAILED) {
        Fault_AddHungupAndCrash(__FILE__, __LINE__);
    }

    if (!OotPort_IsNativeRange(vrom, size)) {
        uintptr_t normalizedVrom = OotPort_NormalizeVrom(vrom);

        printf("oot-ps2 dma invalid fallback ram=%p vrom=%08lx normalized=%08lx size=%lu status=%ld\n", ram,
               (unsigned long)vrom, (unsigned long)normalizedVrom, (unsigned long)size, (long)status);
        Fault_AddHungupAndCrash(__FILE__, __LINE__);
    }

    if (isAudioRead) {
        memcpy(ram, (const void*)vrom, size);
    } else {
        OotPort_MemcpyFast(ram, (const void*)vrom, size);
    }
    return 0;
}

static s32 OotPort_DmaRead(void* ram, uintptr_t vrom, size_t size) {
    return OotPort_DmaReadInternal(ram, vrom, size, false);
}

s32 DmaMgr_RequestAsync(DmaRequest* req, void* ram, uintptr_t vrom, size_t size, u32 unk5, OSMesgQueue* queue, OSMesg msg) {
    (void)unk5;
    OotPort_DmaRead(ram, vrom, size);

    if (req != NULL) {
        req->dramAddr = ram;
        req->vromAddr = vrom;
        req->size = size;
        req->notifyQueue = queue;
        req->notifyMsg = msg;
    }

    if (queue != NULL) {
        osSendMesg(queue, msg, OS_MESG_NOBLOCK);
    }

    return 0;
}

s32 DmaMgr_RequestSync(void* ram, uintptr_t vrom, size_t size) {
    return OotPort_DmaRead(ram, vrom, size);
}

s32 DmaMgr_DmaRomToRam(uintptr_t rom, void* ram, size_t size) {
    return OotPort_DmaRead(ram, rom, size);
}

void DmaMgr_DmaFromDriveRom(void* ram, uintptr_t rom, size_t size) {
    OotPort_DmaRead(ram, rom, size);
}

s32 DmaMgr_AudioDmaHandler(UNUSED OSPiHandle* pihandle, OSIoMesg* mb, UNUSED s32 direction) {
    if (mb == NULL) {
        return -1;
    }

    OotPort_DmaReadInternal(mb->dramAddr, mb->devAddr, mb->size, true);
    if (mb->hdr.retQueue != NULL) {
        osSendMesg(mb->hdr.retQueue, mb, OS_MESG_NOBLOCK);
    }
    return 0;
}

void PadMgr_RequestPadData(PadMgr* padMgr, Input* inputs, UNUSED s32 gameRequest) {
    OSContPad pads[MAXCONTROLLERS];
    Input* input = &inputs[0];

    osContStartReadData(NULL);
    osContGetReadData(pads);

    input->prev = padMgr->inputs[0].cur;
    input->cur = pads[0];
    input->press.button = (input->cur.button ^ input->prev.button) & input->cur.button;
    input->press.stick_x = input->cur.stick_x - input->prev.stick_x;
    input->press.stick_y = input->cur.stick_y - input->prev.stick_y;
    input->rel.button = (input->cur.button ^ input->prev.button) & input->prev.button;
    input->rel.stick_x = input->cur.stick_x;
    input->rel.stick_y = input->cur.stick_y;

    memset(&inputs[1], 0, sizeof(Input) * (MAXCONTROLLERS - 1));
    padMgr->inputs[0] = *input;
    padMgr->validCtrlrsMask = 1;
    padMgr->ctrlrIsConnected[0] = true;
    padMgr->nControllers = 1;
}

void PadMgr_RumbleStop(UNUSED PadMgr* padMgr) {
}

void PadMgr_RumbleReset(PadMgr* padMgr) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(padMgr->rumbleEnable); i++) {
        padMgr->rumbleEnable[i] = 0;
        padMgr->rumbleTimer[i] = 0;
    }
}

void PadMgr_RumbleSetSingle(PadMgr* padMgr, u32 port, u32 rumble) {
    if (port < ARRAY_COUNT(padMgr->rumbleEnable)) {
        padMgr->rumbleEnable[port] = rumble != 0;
    }
}

void PadMgr_RumbleSet(PadMgr* padMgr, u8* enable) {
    s32 i;

    if (enable != NULL) {
        for (i = 0; i < ARRAY_COUNT(padMgr->rumbleEnable); i++) {
            padMgr->rumbleEnable[i] = enable[i];
        }
    }
}

void SpeedMeter_Init(SpeedMeter* this) {
    memset(this, 0, sizeof(*this));
}

void SpeedMeter_Destroy(UNUSED SpeedMeter* this) {
}

void SpeedMeter_DrawTimeEntries(UNUSED SpeedMeter* this, UNUSED GraphicsContext* gfxCtx) {
}

void SpeedMeter_DrawAllocEntries(UNUSED SpeedMeter* meter, UNUSED GraphicsContext* gfxCtx, UNUSED GameState* state) {
}

void Debug_DrawText(UNUSED GraphicsContext* gfxCtx) {
}

void SsSram_ReadWrite(s32 addr, void* dramAddr, size_t size, s32 direction) {
    uintptr_t base = OS_K1_TO_PHYSICAL(0xA8000000);
    uintptr_t offset;

    if (dramAddr == NULL) {
        return;
    }

    if ((uintptr_t)addr < base) {
        return;
    }

    offset = (uintptr_t)addr - base;
    if (offset >= sizeof(sPs2Sram)) {
        return;
    }

    if (size > (sizeof(sPs2Sram) - offset)) {
        size = sizeof(sPs2Sram) - offset;
    }

    if (direction == OS_READ) {
        memcpy(dramAddr, &sPs2Sram[offset], size);
    } else {
        memcpy(&sPs2Sram[offset], dramAddr, size);
        OotPort_FlushSram();
    }
}

void LogUtils_HungupThread(const char* name, int line) {
    Fault_AddHungupAndCrash(name, line);
}

void LogUtils_ResetHungup(void) {
}

static const char* sOotPs2CrashStage = "BOOT";
static s32 sOotPs2CrashSceneId = -1;
static u32 sOotPs2CrashFrame;

void OotPs2Crash_SetStage(const char* stage) {
    if (stage != NULL) {
        sOotPs2CrashStage = stage;
    }
}

void OotPs2Crash_SetSceneFrame(s32 sceneId, u32 frame) {
    sOotPs2CrashSceneId = sceneId;
    sOotPs2CrashFrame = frame;
}

void Fault_Init(void) {
}

void Fault_AddClient(FaultClient* client, void* callback, void* arg0, void* arg1) {
    if (client != NULL) {
        client->callback = callback;
        client->arg0 = arg0;
        client->arg1 = arg1;
    }
}

void Fault_RemoveClient(FaultClient* client) {
    if (client != NULL) {
        client->callback = NULL;
        client->arg0 = NULL;
        client->arg1 = NULL;
    }
}

static void OotPort_FaultWrite(const char* msg) {
    osSyncPrintf("%s", msg);
    OotPs2File_Write(1, msg, strlen(msg));
}

NORETURN void oot_port_assert(const char* assertion, const char* file, int line) {
    char msg[256];

    snprintf(msg, sizeof(msg), "oot-ps2 assert failed: %s (%s:%d)\n", assertion != NULL ? assertion : "(null)",
             file != NULL ? file : "(null)", line);
    OotPort_FaultWrite(msg);
    Fault_AddHungupAndCrash(file, line);
}

s32 Fault_Printf(const char* fmt, ...) {
    char buf[512];
    va_list args;
    s32 written;

    va_start(args, fmt);
    written = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (written < 0) {
        return written;
    }

    osSyncPrintf("%s", buf);
    return written;
}

void Fault_SetFontColor(UNUSED u16 color) {
}

void Fault_SetCursor(UNUSED s32 x, UNUSED s32 y) {
}

void Fault_SetCharPad(UNUSED s8 padW, UNUSED s8 padH) {
}

NORETURN void Fault_AddHungupAndCrashImpl(const char* exp1, const char* exp2) {
    char msg[320];

    snprintf(msg, sizeof(msg), "oot-ps2 fault: %s %s stage=%s scene=%d frame=%u\n",
             exp1 != NULL ? exp1 : "(null)", exp2 != NULL ? exp2 : "",
             sOotPs2CrashStage != NULL ? sOotPs2CrashStage : "(null)", sOotPs2CrashSceneId,
             (unsigned)sOotPs2CrashFrame);
    OotPort_FaultWrite(msg);
    while (true) {
        OotPs2Thread_Delay(1000000);
    }
}

NORETURN void Fault_AddHungupAndCrash(const char* file, int line) {
    char msg[320];

    snprintf(msg, sizeof(msg), "oot-ps2 fault at %s:%d stage=%s scene=%d frame=%u\n",
             file != NULL ? file : "(null)", line,
             sOotPs2CrashStage != NULL ? sOotPs2CrashStage : "(null)", sOotPs2CrashSceneId,
             (unsigned)sOotPs2CrashFrame);
    OotPort_FaultWrite(msg);
    while (true) {
        OotPs2Thread_Delay(1000000);
    }
}

void OotPortGame_Init(void) {
    memset(&gScheduler, 0, sizeof(gScheduler));
    memset(&gPadMgr, 0, sizeof(gPadMgr));
    memset(&gIrqMgr, 0, sizeof(gIrqMgr));
    memset(gSegments, 0, sizeof(gSegments));

    gCurrentRegion = REGION_US;
    SaveContext_Init();
    OotPort_LoadSram();
    gAppNmiBufferPtr = &sPs2PreNmiBuffer;
    PreNmiBuff_Init(gAppNmiBufferPtr);

    SysCfb_Init(0);
    osViSwapBuffer(SysCfb_GetFbPtr(0));

    gSystemHeapSize = sizeof(gOotPortSystemHeap);
    Runtime_Init(gOotPortSystemHeap, gSystemHeapSize);
    Regs_Init();
    Letterbox_Init();

    gPadMgr.validCtrlrsMask = 1;
    gPadMgr.ctrlrIsConnected[0] = true;
    gPadMgr.nControllers = 1;

    Sched_Init(&gScheduler, NULL, THREAD_PRI_SCHED, 0, 0, &gIrqMgr);
}
