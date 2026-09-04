#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <gsInline.h>
#include <gif_registers.h>
#include <kernel.h>
#include <timer.h>
#include "gfx_window_manager_api.h"
#include "../oot_ps2_platform.h"
#include "../oot_ps2_lag_diag.h"

GSGLOBAL* gs_global;
struct GfxWindowManagerAPI gfx_ps2_wapi = { 0 };

int __wrap_DIntr(void) {
    return 0;
}

int __wrap_EIntr(void) {
    return 0;
}

u64 __wrap_GsPutIMR(u64 imr) {
    return 0;
}

u32 __wrap_gsKit_texture_size(int width, int height, int psm) {
    return 0;
}

u32 __wrap_gsKit_vram_alloc(GSGLOBAL* gsGlobal, u32 size, u8 type) {
    return 0;
}

void __wrap_gsKit_TexManager_init(GSGLOBAL* gsGlobal) {
}

void __wrap_gsKit_clear(GSGLOBAL* gsGlobal, u64 color) {
}

void __wrap_gsKit_queue_exec(GSGLOBAL* gsGlobal) {
}

void __wrap_gsKit_finish(void) {
}

void __wrap_gsKit_sync_flip(GSGLOBAL* gsGlobal) {
}

void __wrap_gsKit_queue_reset(GSQUEUE* queue) {
}

void __wrap_SetGsCrt(s16 interlace, s16 mode, s16 field) {
}

void __wrap_dmaKit_send_ucab(u16 channel, void* data, u32 size) {
}

void __wrap_dmaKit_wait_fast(void) {
}

void gfx_ps2_prepare_crash_screen(void) {
}

void gfx_ps2_capture_pause_background_current(void) {
}

void gfx_ps2_request_pause_background(void) {
}

void gfx_ps2_set_pause_background_active(bool active) {
}

bool gfx_ps2_pause_background_active(void) {
    return false;
}

void gfx_ps2_restore_pause_background(void) {
}
