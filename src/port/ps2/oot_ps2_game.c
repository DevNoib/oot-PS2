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
#include <ee_debug.h>
#include <kernel.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tables/entrance_table.h"

u8 gOotPortSystemHeap[OOT_PORT_SYSTEM_HEAP_SIZE] __attribute__((aligned(64)));

int OotPort_IsRuntimeByteRangeSlow(uintptr_t start, uintptr_t end) {
    return 0;
}

void* SegmentedToVirtualCompat(uintptr_t addr) {
    return NULL;
}

void* SegmentedToVirtualExplicit(uintptr_t addr) {
    return NULL;
}

void Regs_Init(void) {
}

void SysCfb_Init(UNUSED s32 n64dd) {
}

void SysCfb_Reset(void) {
}

void* SysCfb_GetFbPtr(s32 idx) {
    return NULL;
}

void* SysCfb_GetFbEnd(void) {
    return NULL;
}

void DmaMgr_Init(void) {
}

s32 DmaMgr_RequestAsync(DmaRequest* req, void* ram, uintptr_t vrom, size_t size, u32 unk5, OSMesgQueue* queue, OSMesg msg) {
    return 0;
}

s32 DmaMgr_RequestSync(void* ram, uintptr_t vrom, size_t size) {
    return 0;
}

s32 DmaMgr_DmaRomToRam(uintptr_t rom, void* ram, size_t size) {
    return 0;
}

void DmaMgr_DmaFromDriveRom(void* ram, uintptr_t rom, size_t size) {
}

s32 DmaMgr_AudioDmaHandler(UNUSED OSPiHandle* pihandle, OSIoMesg* mb, UNUSED s32 direction) {
    return 0;
}

void OotPs2Input_SetPlayState(void* playState) {
}

void PadMgr_RequestPadData(PadMgr* padMgr, Input* inputs, s32 gameRequest) {
}

void PadMgr_RumbleStop(UNUSED PadMgr* padMgr) {
}

void PadMgr_RumbleReset(PadMgr* padMgr) {
}

void PadMgr_RumbleSetSingle(PadMgr* padMgr, u32 port, u32 rumble) {
}

void PadMgr_RumbleSet(PadMgr* padMgr, u8* enable) {
}

void SpeedMeter_Init(SpeedMeter* this) {
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
}

void LogUtils_HungupThread(const char* name, int line) {
}

void LogUtils_ResetHungup(void) {
}

void OotPs2Crash_SetStage(const char* stage) {
}

void OotPs2Crash_SetSceneFrame(s32 sceneId, u32 frame) {
}

void Fault_Init(void) {
}

void Fault_AddClient(FaultClient* client, void* callback, void* arg0, void* arg1) {
}

void Fault_RemoveClient(FaultClient* client) {
}

NORETURN void oot_port_assert(const char* assertion, const char* file, int line) {
    for (;;) {
    }
}

s32 Fault_Printf(const char* fmt, ...) {
    return 0;
}

void Fault_SetFontColor(UNUSED u16 color) {
}

void Fault_SetCursor(UNUSED s32 x, UNUSED s32 y) {
}

void Fault_SetCharPad(UNUSED s8 padW, UNUSED s8 padH) {
}

NORETURN void Fault_AddHungupAndCrashImpl(const char* exp1, const char* exp2) {
    for (;;) {
    }
}

NORETURN void Fault_AddHungupAndCrash(const char* file, int line) {
    for (;;) {
    }
}

void OotPortGame_Init(void) {
}
