#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "ultra64.h"
#include "ultra64/gs2dex.h"
#include <pspfpu.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include "pspmath.h"
#include "gfx_fast3d.h"
#include "oot_ps2_lag_diag.h"
#include "gfx_cc.h"
#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"
#include "gfx_screen_config.h"
#include "buffers.h"
#include "oot_port_macros.h"
#include "oot_port_asset_loader.h"
#include "oot_port_compat.h"
#include "oot_port_gfx_ext.h"
#include "oot_port_memory.h"
#include "oot_psp_performance.h"
#include "segmented_address.h"
#include "sys_matrix.h"
#include <pspthreadman.h>
#include <time.h>

struct GfxDimensions gfx_current_dimensions;

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *game_name, bool start_in_fullscreen) {
}

struct GfxRenderingAPI *gfx_get_current_rendering_api(void) {
    return NULL;
}

void gfx_start_frame(void) {
}

void gfx_run(Gfx *commands) {
}

void gfx_end_frame(void) {
}

void gfx_set_dimensions(uint32_t width, uint32_t height) {
}

void gfx_set_ps2_widescreen(bool enabled) {
}

bool gfx_get_ps2_widescreen(void) {
    return false;
}

void gfx_render_callback_frame(void (*draw_callback)(void *arg), void *arg) {
}

void gfx_invalidate_render_state(void) {
}
