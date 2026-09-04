#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <kernel.h>
#include <gsKit.h>
#include <gsInline.h>
#include <dmaKit.h>
#include "gfx_rendering_api.h"
#include "gfx_cc.h"
#include "gfx_window_manager_api.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_lag_diag.h"
#include "oot_ps2_home_font.inc"

float identity_matrix[4][4];
struct GfxRenderingAPI gfx_ps2_rapi = { 0 };

void gfx_ps2_set_texture_blend_reverse(bool enabled) {
}

void gfx_ps2_set_texture_blend_precolor(bool enabled) {
}

void gfx_ps2_set_din_fire_tint(bool enabled) {
}

void gfx_ps2_set_two_texture_blend_active(bool enabled) {
}

void gfx_ps2_set_two_texture_env_prim_tint(bool enabled) {
}

void gfx_ps2_set_skip_content_hash(bool enabled) {
}

void gfx_ps2_set_fps_overlay_enabled(bool enabled) {
}

bool gfx_ps2_get_fps_overlay_enabled(void) {
    return false;
}

void gfx_ps2_set_prerender_room_state(bool active, u32 roomKey) {
}

void gfx_ps2_invalidate_register_cache(void) {
}

int gfx_vram_space_available(void) {
    return 0;
}

int texman_vram_space_available(unsigned int size) {
    return 0;
}

int texman_texture_slot_available(void) {
    return 0;
}

void texman_clear(void) {
}

void texman_upload(int width,int height,unsigned int type,const void*buffer) {
}

void gfx_ps2_set_prerender_depth_only(bool enabled) {
}

void gfx_scegu_draw_triangles_2d(float *raw,size_t unused,size_t count) {
}

void gfx_ps2_set_boot_progress(int code, const char* label) {
}

void gfx_ps2_render_boot_progress(int code, const char* label) {
}

void gfx_ps2_render_menu(const char*title,const char*const*lines,int lineCount,int selectedIndex,
                         const char*statusMessage,int firstRow,int totalRows) {
}
