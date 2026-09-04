#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#if !defined(_LANGUAGE_C)
#define _LANGUAGE_C
#endif
#include "ultra64.h"
#include "ultra64/gs2dex.h"

#include "gfx_fast3d.h"
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
#include "segmented_address.h"
#include "sys_matrix.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define INFO_MSG(x) printf("%s %s\n", __FILE__ ":" TOSTRING(__LINE__), x)
#define _UNUSED(x) (void)(x)

#define SUPPORT_CHECK(x) assert(x)

#if !defined(OOT_PS2_PERF_BENCH)
#define OOT_PS2_PERF_BENCH 0
#endif

#if !defined(OOT_PS2_DEEP_PROFILE)
#define OOT_PS2_DEEP_PROFILE 0
#endif

#define GFX_DL_HANDLER

#if !defined(OOT_PS2_GFX_DIAGNOSTICS)
#define OOT_PS2_GFX_DIAGNOSTICS 0
#endif

#define SCALE_5_8(VAL_) (((VAL_) * 0xFF) / 0x1F)
#define SCALE_8_5(VAL_) ((((VAL_) + 4) * 0x1F) / 0xFF)
#define SCALE_4_8(VAL_) ((VAL_) * 0x11)
#define SCALE_8_4(VAL_) ((VAL_) / 0x11)
#define SCALE_3_8(VAL_) ((VAL_) * 0x24)
#define SCALE_8_3(VAL_) ((VAL_) / 0x24)

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define HALF_SCREEN_WIDTH (SCREEN_WIDTH / 2)
#define HALF_SCREEN_HEIGHT (SCREEN_HEIGHT / 2)

#define RATIO_X (gfx_current_dimensions.width / (2.0f * HALF_SCREEN_WIDTH))
#define RATIO_Y (gfx_current_dimensions.height / (2.0f * HALF_SCREEN_HEIGHT))

#define MAX_BUFFERED (1024)
#define MAX_LIGHTS 7
#define MAX_VERTICES 64
#define GFX_TLUT_SIZE_BYTES 512
#define GFX_CI4_TLUT_SIZE_BYTES 32
#define MODELVIEW_STACK_SIZE 11

#define PS2_NATIVE_ADDR_START 0x00100000U
#define PS2_NATIVE_ADDR_END 0x02000000U
#define PS2_SEGMENTED_COLLISION_OFFSET_MAX 0x00010000U
#define PS2_ASSET_SYMBOL_GIDENTITYMTX 0x0E000001U

#define PS2_TEXFMT_5650		(0)
#define PS2_TEXFMT_5551		(1)
#define PS2_TEXFMT_4444		(2)
#define PS2_TEXFMT_8888		(3)
#define PS2_TEXFMT_T4		(4)
#define PS2_TEXFMT_T8		(5)
#define PS2_TEXFMT_T16		(6)
#define PS2_TEXFMT_T32		(7)

#define PS2_TEXFMT_8888_GS_NATIVE (8)
extern void gfx_ps2_draw_triangles_2d(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
extern float identity_matrix[4][4];
extern Mtx D_01000000;
extern void gfx_ps2_set_texture_blend_reverse(bool enabled);
extern void gfx_ps2_set_texture_blend_precolor(bool enabled);
extern void gfx_ps2_set_din_fire_tint(bool enabled);
extern void gfx_ps2_set_two_texture_blend_active(bool enabled);
extern void gfx_ps2_set_two_texture_env_prim_tint(bool enabled);
extern void gfx_ps2_set_skip_content_hash(bool enabled);

struct GfxCmdSnapshot {
    uintptr_t addr;
    uint32_t w0;
    uint32_t w1;
};

struct RGBA {
    uint8_t r, g, b, a;
} __attribute__((packed, aligned(4)));

struct XYWidthHeight {
    uint16_t x, y, width, height;
} __attribute__((packed, aligned(4)));

struct LoadedVertex {
    float x, y, z, w;
    float _x, _y, _z, _w;
    float u, v;
    struct RGBA color;
    uint8_t clip_rej;
    uint8_t fog_alpha;
    uint16_t padding;

    float screen_x, screen_y, screen_z;
    uint32_t screen_serial;
} __attribute__((packed, aligned(16)));

typedef struct VertexColor {

	short u, v;
	struct RGBA color;
	unsigned short x, y, z;
} VertexColor __attribute__((aligned(16)));

struct TextureHashmapNode {
    struct TextureHashmapNode *next;

    const uint8_t *texture_addr;
    uint8_t fmt, siz;
    uint16_t width, height;
    uint32_t row_stride_bytes;
    uint8_t source_nibble_offset;
    uint32_t source_key;
    uint32_t dynamic_content_key;

    uint32_t runtime_content_key;
    uint32_t runtime_content_check_frame;
    const uint8_t* palette_addr;
    uint32_t palette_key;
    uint32_t last_used_frame;
    uint16_t upload_width, upload_height;

    uint32_t texture_id;
    uint8_t cms, cmt;
    uint8_t masks, maskt;
    uint8_t mirror_s, mirror_t;
    bool linear_filter;
} __attribute__((packed, aligned(4)));
static struct {
    struct TextureHashmapNode *hashmap[1024];
    struct TextureHashmapNode pool[768];
    uint32_t pool_pos;
} gfx_texture_cache;
static uint32_t sTextureCacheFrameSerial;
typedef struct Ps2TextureLoadStamp {
    uint32_t frameSerial;
    const uint8_t* addr;
    uint32_t sizeBytes;
    uint32_t sourceSizeBytes;
    uint32_t rowStrideBytes;
    uint8_t fmt;
    uint8_t siz;
    uint8_t sourceNibbleOffset;
} Ps2TextureLoadStamp;
static Ps2TextureLoadStamp sPs2TextureLoadStamp[2];
typedef struct Ps2PaletteLoadStamp {
    uint32_t frameSerial;
    const uint8_t* addr;
    uint32_t highIndex;
} Ps2PaletteLoadStamp;
static Ps2PaletteLoadStamp sPs2PaletteLoadStamp;

static inline bool gfx_ps2_same_frame_texture_load(int slot, const uint8_t* addr, uint32_t sizeBytes,
                                                    uint32_t sourceSizeBytes, uint32_t rowStrideBytes,
                                                    uint8_t sourceNibbleOffset, uint8_t fmt, uint8_t siz) {
    Ps2TextureLoadStamp* stamp = &sPs2TextureLoadStamp[slot];
    const bool same = (stamp->frameSerial == sTextureCacheFrameSerial) &&
                      (stamp->addr == addr) &&
                      (stamp->sizeBytes == sizeBytes) &&
                      (stamp->sourceSizeBytes == sourceSizeBytes) &&
                      (stamp->rowStrideBytes == rowStrideBytes) &&
                      (stamp->fmt == fmt) &&
                      (stamp->siz == siz) &&
                      (stamp->sourceNibbleOffset == sourceNibbleOffset);

    stamp->frameSerial = sTextureCacheFrameSerial;
    stamp->addr = addr;
    stamp->sizeBytes = sizeBytes;
    stamp->sourceSizeBytes = sourceSizeBytes;
    stamp->rowStrideBytes = rowStrideBytes;
    stamp->fmt = fmt;
    stamp->siz = siz;
    stamp->sourceNibbleOffset = sourceNibbleOffset;
    return same;
}

#define OOT_PS2_PRERENDER_DEPTH_BEGIN 0x505332D0U
#define OOT_PS2_PRERENDER_DEPTH_END   0x505332D1U
static bool sPs2ForceDepthOnly;
extern void gfx_ps2_set_prerender_depth_only(bool enabled);
extern void gfx_ps2_capture_pause_background_current(void);

struct ColorCombiner {
    uint32_t cc_id;
    struct ShaderProgram *prg;
    uint8_t used_textures[2];
    int8_t active_texture;
    uint8_t vertex_color_source[2];
    bool texture_blend;
    bool uses_texture_alpha;
} __attribute__((packed, aligned(4)));

#define COLOR_COMBINER_CACHE_SET_COUNT 32
#define COLOR_COMBINER_CACHE_WAYS 4
typedef struct ColorCombinerCacheEntry {
    struct ColorCombiner combiner;
    bool valid;
} ColorCombinerCacheEntry;
static ColorCombinerCacheEntry color_combiner_cache[COLOR_COMBINER_CACHE_SET_COUNT][COLOR_COMBINER_CACHE_WAYS];
static uint8_t color_combiner_cache_next_way[COLOR_COMBINER_CACHE_SET_COUNT];

struct TriPipelineState {
    struct ColorCombiner *comb;
    bool use_alpha;
    bool use_fog;
    bool used_textures[2];
    bool use_texture;
    bool two_texture_blend;
    bool two_texture_blend_uses_prim_lod;
    bool two_texture_alpha_blend;
    bool two_texture_env_prim_tint;
    bool texture_blend_reverse;
    bool texture_blend_precolor;
    bool din_fire_tint;
    bool fog_uses_texture_alpha;
    bool flame_texture_atlas;
    bool texture_tint_colors_corrected;
    bool two_texture_uncompensated_alpha;
    struct RGBA texture_tint_env_color;
    bool color_mul_env;
    bool color_mul_prim;
    bool alpha_mul_env;
    float tex_u_scale[2], tex_v_scale[2];
    float tex_u_bias[2], tex_v_bias[2];
    float tex_u_shift_scale[2], tex_v_shift_scale[2];
    float tex_u_nominal_span[2], tex_v_nominal_span[2];
    bool tex_u_scale_to_primitive[2], tex_v_scale_to_primitive[2];
} __attribute__((aligned(4)));

typedef struct TextureTileState {
    uint8_t fmt;
    uint8_t siz;
    uint8_t cms, cmt;
    uint8_t masks, maskt;
    uint8_t shifts, shiftt;
    uint16_t uls, ult, lrs, lrt;
    uint32_t line_size_bytes;
} TextureTileState;

static struct RSP {
    float modelview_matrix_stack[MODELVIEW_STACK_SIZE][4][4]__attribute__((aligned(16)));

    float P_matrix[4][4] __attribute__((aligned(16)));
    uint8_t modelview_matrix_stack_size;

    Light_t current_lights[MAX_LIGHTS + 1];
    float current_lights_coeffs[MAX_LIGHTS][3];
    float current_lookat_coeffs[2][3];
    uint8_t current_num_lights;
    bool lights_changed;

    uint32_t geometry_mode;
    uint32_t rdp_half_1;
    int16_t fog_mul, fog_offset;

    struct {

        uint16_t s, t;
    } texture_scaling_factor;

    void *segments[NUM_SEGMENTS];
    struct GfxCmdSnapshot segment_cmd[NUM_SEGMENTS];

    struct VertexColor loaded_vertices_2D[4];
    struct LoadedVertex loaded_vertices[MAX_VERTICES];
} rsp  __attribute__((aligned(16)));

static struct RDP {
    const uint8_t *palette;
    uint32_t palette_key;
    struct {
        const uint8_t *addr;
        uint8_t fmt;
        uint8_t siz;
        uint32_t width;
        uint8_t tile_number;
        struct GfxCmdSnapshot image_cmd;
    } texture_to_load;
    struct {
        const uint8_t *addr;
        uint32_t size_bytes;
        uint32_t source_size_bytes;
        uint32_t row_stride_bytes;

        uint32_t load_row_bytes;
        uint8_t source_nibble_offset;
        struct GfxCmdSnapshot image_cmd;
        struct GfxCmdSnapshot load_cmd;
    } loaded_texture[2];
    TextureTileState texture_tile[2];
    bool textures_changed[2];

    uint32_t other_mode_l, other_mode_h;
    uint32_t combine_mode;
    bool combine_color_mul_env;
    bool combine_color_mul_prim;
    bool combine_two_texture_blend;
    bool combine_two_texture_blend_uses_prim_lod;
    bool combine_two_texture_alpha_blend;
    bool combine_alpha_mul_env;
    bool combine_two_intensity_env_prim_precombine;
    bool combine_flame_texture_atlas;
    bool combine_texture_tint_uses_prim_lod;
    bool combine_texture_tint_uses_env_alpha;
    bool combine_texture_blend_reverse;
    bool combine_din_fire_tint;

    struct RGBA env_color, prim_color, fog_color, fill_color;
    uint8_t prim_lod_frac;
    uint16_t prim_depth;
    struct XYWidthHeight viewport, scissor;
    bool viewport_or_scissor_changed;
    void *z_buf_address;
    void *color_image_address;
} rdp  __attribute__((aligned(4)));

static struct RenderingState {
    struct XYWidthHeight viewport, scissor;
    struct ShaderProgram *shader_program;
    struct ColorCombiner *color_combiner;
    uint32_t color_combiner_id;
    bool color_combiner_valid;
    struct TextureHashmapNode *textures[2];
    bool depth_test;
    bool depth_mask;
    bool decal_mode;
    bool alpha_blend;
    bool texture_env_color_valid;
    uint32_t bound_texture_id;
    int8_t bound_texture_tile;
    struct RGBA texture_env_color;
    bool tri_pipeline_dirty;
    bool backend_state_dirty;
    struct TriPipelineState tri_pipeline;
} rendering_state __attribute__((aligned(16)));

struct GfxDimensions gfx_current_dimensions __attribute__((aligned(4)));

static inline TextureTileState* gfx_get_texture_tile(int tile) {
    return &rdp.texture_tile[tile != 0];
}

static inline bool gfx_get_render_tile_slot(uint8_t tile, int* slot) {
    if (tile == G_TX_RENDERTILE) {
        *slot = 0;
        return true;
    }

    if (tile == 1) {
        *slot = 1;
        return true;
    }

    return false;
}

static bool dropped_frame;
static const struct RGBA white_color = {0xff, 0xff, 0xff, 0xff};
static OotPortHudAnchor sHudAnchor;
static bool sHudViewportFullscreen = true;
static float sNdcAspectScale = 1.0f;
static float sWidescreenMarginPixels;
static float sHudAnchorOffsetPixels;
static float sHudAnchorOffsetNdc;
static bool sPs2Widescreen;
static float sUploadedProjection[4][4] __attribute__((aligned(16)));
static bool sUploadedProjectionValid;
static float sPs2MvpMatrix[4][4] __attribute__((aligned(16)));
static bool sPs2MvpDirty = true;

static uint32_t sPs2ScreenTransformSerial = 1U;
static float sPs2ScreenHalfWidth;
static float sPs2ScreenHalfHeight;
static float sPs2ScreenCenterX;
static float sPs2ScreenCenterY;
static float sPs2ScreenZOffset;

static uint8_t sInvalidTextureBuf[256] __attribute__((aligned(16))) = { 0xff, 0xff, 0xff, 0xff };
static uint8_t sInvalidPaletteBuf[512] __attribute__((aligned(16))) = { 0xff, 0xff };
static struct GfxCmdSnapshot sCurrentCmd;
#define GFX_VALIDATED_DL_CURSOR_CACHE_SIZE 1024
#define GFX_VALIDATED_READ_CACHE_SIZE 1024
#define GFX_SEGMENT_ADDRESS_CACHE_SIZE 1024
typedef struct GfxValidatedReadCacheEntry {
    uintptr_t addr;
    size_t size;
} GfxValidatedReadCacheEntry;
typedef struct GfxSegmentAddressCacheEntry {
    uintptr_t raw;
    uintptr_t rspBase;
    uintptr_t globalBase;
    void* resolved;
} GfxSegmentAddressCacheEntry;
static uintptr_t sValidatedDlCursorCache[GFX_VALIDATED_DL_CURSOR_CACHE_SIZE];
static GfxValidatedReadCacheEntry sValidatedReadCache[GFX_VALIDATED_READ_CACHE_SIZE];
static GfxSegmentAddressCacheEntry sSegmentAddressCache[GFX_SEGMENT_ADDRESS_CACHE_SIZE];
#if OOT_PS2_GFX_DIAGNOSTICS
#define GFX_CAPTURE_CMD(dst, src) ((dst) = (src))
#else
#define GFX_CAPTURE_CMD(dst, src) ((void)0)
#endif
extern u8 __bss_start[];

static inline bool gfx_addr_is_native(uintptr_t addr) {
    return (addr >= PS2_NATIVE_ADDR_START) && (addr < PS2_NATIVE_ADDR_END);
}

static inline bool gfx_native_range_contains(uintptr_t value, size_t size) {
    uintptr_t end;

    if (size == 0) {
        return false;
    }

    end = value + size;
    if (end < value) {
        return false;
    }

    return (value >= PS2_NATIVE_ADDR_START) && (end <= PS2_NATIVE_ADDR_END);
}

static inline uintptr_t gfx_bswap32(uintptr_t value) {
    uint32_t v = (uint32_t)value;

    return ((v & 0x000000FFU) << 24) | ((v & 0x0000FF00U) << 8) | ((v & 0x00FF0000U) >> 8) |
           ((v & 0xFF000000U) >> 24);
}

static inline bool gfx_normalize_native_range(uintptr_t value, size_t size, uintptr_t* normalized) {
    uintptr_t candidate;

    if (gfx_native_range_contains(value, size)) {
        *normalized = value;
        return true;
    }

    candidate = value & 0x0FFFFFFFU;
    if ((candidate != value) && gfx_native_range_contains(candidate, size)) {
        *normalized = candidate;
        return true;
    }

    if (value >= 0x00010000U) {
        candidate = gfx_bswap32(value);
        if (gfx_native_range_contains(candidate, size)) {
            *normalized = candidate;
            return true;
        }

        candidate &= 0x0FFFFFFFU;
        if (gfx_native_range_contains(candidate, size)) {
            *normalized = candidate;
            return true;
        }
    }

    return false;
}

static inline bool gfx_normalize_native_addr(uintptr_t value, uintptr_t* normalized) {
    uintptr_t candidate;

    if (gfx_addr_is_native(value)) {
        *normalized = value;
        return true;
    }

    candidate = value & 0x0FFFFFFFU;
    if ((candidate != value) && gfx_addr_is_native(candidate)) {
        *normalized = candidate;
        return true;
    }

    if (value >= 0x00010000U) {
        candidate = gfx_bswap32(value);
        if (gfx_addr_is_native(candidate)) {
            *normalized = candidate;
            return true;
        }

        candidate &= 0x0FFFFFFFU;
        if (gfx_addr_is_native(candidate)) {
            *normalized = candidate;
            return true;
        }
    }

    return false;
}

static bool gfx_range_contains(uintptr_t value, size_t size, uintptr_t rangeStart, uintptr_t rangeEnd) {
    uintptr_t end;

    if (size == 0) {
        return true;
    }

    end = value + size;
    if (end < value) {
        return false;
    }

    return (value >= rangeStart) && (end <= rangeEnd);
}

static bool gfx_is_graph_pool_range(uintptr_t value, size_t size) {
    for (size_t i = 0; i < ARRAY_COUNT(gGfxPools); i++) {
        uintptr_t poolStart = (uintptr_t)&gGfxPools[i];
        uintptr_t poolEnd = poolStart + sizeof(gGfxPools[i]);

        if (gfx_range_contains(value, size, poolStart, poolEnd)) {
            return true;
        }
    }

    return false;
}

static bool gfx_is_static_prx_range(uintptr_t value, size_t size) {
    return gfx_range_contains(value, size, PS2_NATIVE_ADDR_START, (uintptr_t)__bss_start);
}

static bool gfx_is_loaded_external_range(uintptr_t value, size_t size) {
    u32 flags;

    return OotPort_GetLoadedExternalAssetRangeFlags((const void*)value, size, &flags);
}

static bool gfx_is_valid_native_dl_range(uintptr_t value, size_t size) {
    if (!gfx_native_range_contains(value, size)) {
        return false;
    }

    if (gfx_is_static_prx_range(value, size)) {
        return true;
    }

    if (gfx_is_graph_pool_range(value, size)) {
        return true;
    }

    if (OotPort_IsSystemHeapRange((const void*)value, size)) {
        return true;
    }

    if (gfx_is_loaded_external_range(value, size)) {
        return true;
    }

    return false;
}

static bool gfx_is_valid_native_read_range(uintptr_t value, size_t size) {
    if (!gfx_native_range_contains(value, size)) {
        return false;
    }

    if (gfx_is_static_prx_range(value, size) || gfx_is_graph_pool_range(value, size) ||
        OotPort_IsSystemHeapRange((const void*)value, size)) {
        return true;
    }

    if (OotPort_IsRuntimeByteRange((const void*)value, size)) {
        return true;
    }

    return gfx_is_loaded_external_range(value, size);
}

static inline bool gfx_is_valid_native_read_range_cached(uintptr_t value, size_t size) {
    const size_t cacheIndex = ((value >> 4) ^ size) & (GFX_VALIDATED_READ_CACHE_SIZE - 1);

    if ((sValidatedReadCache[cacheIndex].addr == value) &&
        (sValidatedReadCache[cacheIndex].size == size)) {
        return true;
    }
    if (!gfx_is_valid_native_read_range(value, size)) {
        return false;
    }
    sValidatedReadCache[cacheIndex].addr = value;
    sValidatedReadCache[cacheIndex].size = size;
    return true;
}

static void gfx_log_bad_data_source(const char* context, const void* addr, size_t sizeBytes) {
    static s32 sBadDataSourceLogCount = 0;
    uintptr_t value = (uintptr_t)addr;
    uint8_t segment = value >> 24;
    const struct GfxCmdSnapshot emptyCmd = { 0 };
    const struct GfxCmdSnapshot* segmentCmd = &emptyCmd;

    if (segment < NUM_SEGMENTS) {
        segmentCmd = &rsp.segment_cmd[segment];
    }

    if (sBadDataSourceLogCount < 32) {
        printf("oot-port gfx bad data source context=%s addr=%08lx size=%lu cur=%08lx/%08lx/%08lx "
               "seg=%u segbase=%08lx segcmd=%08lx/%08lx/%08lx "
               "seg1=%08lx seg2=%08lx seg3=%08lx seg4=%08lx seg6=%08lx seg8=%08lx seg9=%08lx sega=%08lx "
               "segc=%08lx segd=%08lx segacmd=%08lx/%08lx/%08lx\n",
               context, (unsigned long)value, (unsigned long)sizeBytes, (unsigned long)sCurrentCmd.addr,
               (unsigned long)sCurrentCmd.w0, (unsigned long)sCurrentCmd.w1, segment,
               (segment < NUM_SEGMENTS) ? (unsigned long)(uintptr_t)rsp.segments[segment] : 0,
               (unsigned long)segmentCmd->addr, (unsigned long)segmentCmd->w0, (unsigned long)segmentCmd->w1,
               (unsigned long)(uintptr_t)rsp.segments[1], (unsigned long)(uintptr_t)rsp.segments[2],
               (unsigned long)(uintptr_t)rsp.segments[3], (unsigned long)(uintptr_t)rsp.segments[4],
               (unsigned long)(uintptr_t)rsp.segments[6], (unsigned long)(uintptr_t)rsp.segments[8],
               (unsigned long)(uintptr_t)rsp.segments[9], (unsigned long)(uintptr_t)rsp.segments[10],
               (unsigned long)(uintptr_t)rsp.segments[12], (unsigned long)(uintptr_t)rsp.segments[13],
               (unsigned long)rsp.segment_cmd[10].addr, (unsigned long)rsp.segment_cmd[10].w0,
               (unsigned long)rsp.segment_cmd[10].w1);
    } else if (sBadDataSourceLogCount == 32) {
        printf("oot-port gfx bad data source logs suppressed\n");
    }

    sBadDataSourceLogCount++;
}

static void gfx_log_bad_texture_source(int tile, const char* context, const uint8_t* addr, uint32_t sizeBytes) {
    static s32 sBadTextureSourceLogCount = 0;

    if (sBadTextureSourceLogCount < 16) {
        const struct GfxCmdSnapshot emptyCmd = { 0 };
        const struct GfxCmdSnapshot* imageCmd = &rdp.texture_to_load.image_cmd;
        const struct GfxCmdSnapshot* loadCmd = &emptyCmd;

        if (tile >= 0 && tile < 2) {
            imageCmd = &rdp.loaded_texture[tile].image_cmd;
            loadCmd = &rdp.loaded_texture[tile].load_cmd;
        }

        printf("oot-port gfx bad texture source context=%s tile=%d addr=%08lx size=%lu cur=%08lx/%08lx/%08lx "
               "setimg=%08lx/%08lx/%08lx load=%08lx/%08lx/%08lx tex=%08lx fmt=%u siz=%u width=%lu "
               "loadslot=%u seg1=%08lx seg4=%08lx seg8=%08lx seg9=%08lx sega=%08lx segd=%08lx "
               "seg8cmd=%08lx/%08lx/%08lx seg9cmd=%08lx/%08lx/%08lx segacmd=%08lx/%08lx/%08lx\n",
               context, tile, (unsigned long)(uintptr_t)addr, (unsigned long)sizeBytes,
               (unsigned long)sCurrentCmd.addr, (unsigned long)sCurrentCmd.w0, (unsigned long)sCurrentCmd.w1,
               (unsigned long)imageCmd->addr, (unsigned long)imageCmd->w0, (unsigned long)imageCmd->w1,
               (unsigned long)loadCmd->addr, (unsigned long)loadCmd->w0, (unsigned long)loadCmd->w1,
               (unsigned long)(uintptr_t)rdp.texture_to_load.addr, rdp.texture_to_load.fmt, rdp.texture_to_load.siz,
               (unsigned long)rdp.texture_to_load.width, rdp.texture_to_load.tile_number,
               (unsigned long)(uintptr_t)rsp.segments[1], (unsigned long)(uintptr_t)rsp.segments[4],
               (unsigned long)(uintptr_t)rsp.segments[8], (unsigned long)(uintptr_t)rsp.segments[9],
               (unsigned long)(uintptr_t)rsp.segments[10], (unsigned long)(uintptr_t)rsp.segments[13],
               (unsigned long)rsp.segment_cmd[8].addr, (unsigned long)rsp.segment_cmd[8].w0,
               (unsigned long)rsp.segment_cmd[8].w1, (unsigned long)rsp.segment_cmd[9].addr,
               (unsigned long)rsp.segment_cmd[9].w0, (unsigned long)rsp.segment_cmd[9].w1,
               (unsigned long)rsp.segment_cmd[10].addr, (unsigned long)rsp.segment_cmd[10].w0,
               (unsigned long)rsp.segment_cmd[10].w1);
    } else if (sBadTextureSourceLogCount == 16) {
        printf("oot-port gfx bad texture source logs suppressed\n");
    }

    sBadTextureSourceLogCount++;
}

static bool gfx_normalize_texture_source(const uint8_t** addr, uint32_t sizeBytes) {
    uintptr_t normalized;

    if (gfx_normalize_native_range((uintptr_t)*addr, sizeBytes, &normalized) &&
        gfx_is_valid_native_read_range(normalized, sizeBytes)) {
        *addr = (const uint8_t*)normalized;
        return true;
    }

    return false;
}

static uint32_t gfx_texture_palette_key(const uint8_t* addr, uint32_t sizeBytes) {
    uint32_t key;
    const uint8_t* bytes = addr;

    if (sizeBytes == GFX_TLUT_SIZE_BYTES) {
        const uint32_t halfSize = sizeBytes / 2;
        const uint32_t firstSerial = OotPort_GetExternalAssetRangeSerial(addr, halfSize);
        const uint32_t secondSerial = OotPort_GetExternalAssetRangeSerial(addr + halfSize, halfSize);

        if ((firstSerial != 0) && (secondSerial != 0)) {
            key = 2166136261U;
            key = (key ^ firstSerial) * 16777619U;
            key = (key ^ secondSerial) * 16777619U;
            return key != 0 ? key : 1;
        }
    }

    key = OotPort_GetExternalAssetRangeSerial(addr, sizeBytes);
    if (key != 0) {
        return key;
    }

    if (!gfx_normalize_texture_source(&bytes, sizeBytes)) {
        return 0;
    }

    key = 2166136261U;

    while ((sizeBytes != 0U) && (((uintptr_t)bytes & 3U) != 0U)) {
        key = (key ^ *bytes++) * 16777619U;
        sizeBytes--;
    }
    while (sizeBytes >= 4U) {
        const uint32_t word = *(const uint32_t*)bytes;
        key = (key ^ word) * 16777619U;
        bytes += 4;
        sizeBytes -= 4U;
    }
    while (sizeBytes != 0U) {
        key = (key ^ *bytes++) * 16777619U;
        sizeBytes--;
    }

    return key != 0 ? key : 1;
}

static bool gfx_texture_load_slot(const char* context, int* slot) {
    if (rdp.texture_to_load.tile_number < 2) {
        *slot = rdp.texture_to_load.tile_number;
        return true;
    }

    gfx_log_bad_texture_source(rdp.texture_to_load.tile_number, context, rdp.texture_to_load.addr, 1);
    return false;
}

static void gfx_set_invalid_loaded_texture(int tile) {
    rdp.loaded_texture[tile].addr = sInvalidTextureBuf;
    rdp.loaded_texture[tile].size_bytes = 8;
    rdp.loaded_texture[tile].source_size_bytes = 8;
    rdp.loaded_texture[tile].row_stride_bytes = 2;
    rdp.loaded_texture[tile].load_row_bytes = 2;
    rdp.loaded_texture[tile].source_nibble_offset = 0;
}

static void gfx_set_invalid_texture_tile(int tile) {
    TextureTileState* tileState = gfx_get_texture_tile(tile);

    tileState->uls = 0;
    tileState->ult = 0;
    tileState->lrs = 0;
    tileState->lrt = 0;
    tileState->shifts = G_TX_NOLOD;
    tileState->shiftt = G_TX_NOLOD;
    tileState->line_size_bytes = 2;
}

static void gfx_validate_palette_source(const char* context) {
    if (gfx_normalize_texture_source(&rdp.palette, sizeof(sInvalidPaletteBuf))) {
        return;
    }

    gfx_log_bad_texture_source(-1, context, rdp.palette, sizeof(sInvalidPaletteBuf));
    rdp.palette = sInvalidPaletteBuf;
}

static uint32_t gfx_loaded_texture_source_size(int tile) {
    return rdp.loaded_texture[tile].source_size_bytes != 0 ? rdp.loaded_texture[tile].source_size_bytes
                                                           : rdp.loaded_texture[tile].size_bytes;
}

static bool gfx_validate_texture_source(int tile, const char* context) {
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    uint32_t sizeBytes = gfx_loaded_texture_source_size(tile);

    if (gfx_normalize_texture_source(&addr, sizeBytes)) {
        rdp.loaded_texture[tile].addr = addr;
        return true;
    }

    gfx_log_bad_texture_source(tile, context, addr, sizeBytes);
    gfx_set_invalid_loaded_texture(tile);
    gfx_set_invalid_texture_tile(tile);
    return false;
}

static uint32_t gfx_texture_source_span_size(int tile) {
    return gfx_loaded_texture_source_size(tile);
}

typedef enum GfxTextureSwapMode {
    GFX_TEXTURE_SWAP_NONE,
    GFX_TEXTURE_SWAP_DIRECT,
    GFX_TEXTURE_SWAP_MAPPED,
} GfxTextureSwapMode;

typedef struct GfxTextureSwapState {
    GfxTextureSwapMode mode;
    uintptr_t rangeStart;
    uintptr_t rangeEnd;
} GfxTextureSwapState;

static GfxTextureSwapState gfx_texture_source_swap_state(const uint8_t* addr, uint32_t sizeBytes) {
    GfxTextureSwapState state = { GFX_TEXTURE_SWAP_NONE, 0, 0 };
    u32 loadedFlags;

    if ((addr == sInvalidTextureBuf) || (addr == sInvalidPaletteBuf)) {
        return state;
    }

    if (OotPort_GetLoadedExternalAssetRangeFlags(addr, sizeBytes, &loadedFlags) ||
        OotPort_GetLoadedExternalAssetRangeFlags(addr, 1, &loadedFlags)) {
        state.mode = ((loadedFlags & OOT_PORT_EXTERNAL_ASSET_NATIVE) != 0) ? GFX_TEXTURE_SWAP_MAPPED
                                                                          : GFX_TEXTURE_SWAP_NONE;
        if (state.mode == GFX_TEXTURE_SWAP_MAPPED) {
            OotPort_GetNativeExternalTextureMappingRange(addr, &state.rangeStart, &state.rangeEnd);
        }
        return state;
    }

    state.mode = OotPort_IsRuntimeByteRange(addr, sizeBytes) ? GFX_TEXTURE_SWAP_NONE : GFX_TEXTURE_SWAP_DIRECT;
    return state;
}

static inline uint8_t gfx_read_texture_source_u8(const uint8_t* addr, uint32_t offset,
                                                 GfxTextureSwapState* swapState) {
    uintptr_t source = (uintptr_t)addr + offset;

    if (swapState->mode == GFX_TEXTURE_SWAP_MAPPED) {
        uintptr_t relative;
        uintptr_t mappedRelative;

        if ((source < swapState->rangeStart) || (source >= swapState->rangeEnd)) {
            OotPort_GetNativeExternalTextureMappingRange((const void*)source, &swapState->rangeStart,
                                                        &swapState->rangeEnd);
        }

        relative = source - swapState->rangeStart;
        mappedRelative = relative ^ 7U;
        if ((swapState->rangeStart != 0) &&
            (mappedRelative < (swapState->rangeEnd - swapState->rangeStart))) {
            source = swapState->rangeStart + mappedRelative;
        } else {
            source ^= 7U;
        }
    } else if (swapState->mode == GFX_TEXTURE_SWAP_DIRECT) {
        source ^= 7U;
    }

    return *(const uint8_t*)source;
}

static inline uint16_t gfx_read_texture_source_be16(const uint8_t* addr, uint32_t offset,
                                                    GfxTextureSwapState* swapState) {
    uint16_t hi = gfx_read_texture_source_u8(addr, offset, swapState);
    uint16_t lo = gfx_read_texture_source_u8(addr, offset + 1, swapState);

    return (hi << 8) | lo;
}

typedef struct ps2_fast_t {
  float u,v;

  float real_u,real_v;
  struct RGBA color;
  float x,y,z;
  float q;
  uint32_t fog_color;
  uint32_t fog;
} ps2_fast_t;
typedef struct ps2_uv_t {
  float u,v;
  float real_u,real_v;
  uint8_t alpha;
} ps2_uv_t;
typedef struct ps2_fog_color_t {
  struct RGBA color;
  float x,y,z;
} ps2_fog_color_t;
typedef struct ps2_fog_textured_t {
  float u,v;
  struct RGBA color;
  float x,y,z;

  float q;
} ps2_fog_textured_t;
typedef union ps2_fog_buffer_t {
  ps2_fog_color_t color[MAX_BUFFERED * 3];
  ps2_fog_textured_t textured[MAX_BUFFERED * 3];
} ps2_fog_buffer_t;
typedef char ps2_fog_color_t_size_check[(sizeof(ps2_fog_color_t) == 16) ? 1 : -1];
typedef char ps2_fog_textured_t_size_check[(sizeof(ps2_fog_textured_t) == 28) ? 1 : -1];
static ps2_fast_t buf_vbo[MAX_BUFFERED  * 3] __attribute__ ((aligned (32)));
static ps2_uv_t buf_vbo_tex1[MAX_BUFFERED * 3] __attribute__((aligned(32)));
static ps2_fog_buffer_t buf_vbo_fog __attribute__((aligned(32)));
static uint8_t buf_vbo_fog_tex1_alpha[MAX_BUFFERED * 3] __attribute__((aligned(32)));
static inline __attribute__((always_inline)) void gfx_two_texture_blend_pass_alphas(
    uint8_t surfaceAlpha, uint8_t mixAlpha, bool blendTextureAlpha,
    bool uncompensatedTextureAlpha, uint8_t* baseAlpha, uint8_t* overlayAlpha);

#if OOT_PS2_PERF_BENCH
static uint32_t sPerformanceCommandCount;
static uint32_t sPerformanceInputTriangleCount;
static uint32_t sPerformanceOutputTriangleCount;
static uint32_t sPerformanceFlushCount;
static uint32_t sPerformanceDrawCallCount;
static uint32_t sPerformanceOpaqueDrawCallCount;
static uint32_t sPerformanceOpaqueTriangleCount;
static uint32_t sPerformanceTranslucentDrawCallCount;
static uint32_t sPerformanceTranslucentTriangleCount;
static uint32_t sPerformanceMaxBatchTriangles;
static uint32_t sPerformanceTextureUploadCount;
static uint32_t sPerformanceTextureSameFlushCount;
static uint32_t sPerformanceTextureChangeCount;
#if OOT_PS2_PERF_BENCH
static uint32_t sPs2OpcodeCounts[256];
#if OOT_PS2_DEEP_PROFILE
static uint32_t sPs2DeepVtxCycles;
static uint32_t sPs2DeepTriCycles;
static uint32_t sPs2DeepFlushCycles;
static inline __attribute__((always_inline)) uint32_t gfx_ps2_read_count(void) {
    uint32_t v;
    __asm__ volatile("mfc0 %0, $9" : "=r"(v));
    return v;
}
#endif
#endif
#endif
static size_t buf_vbo_len;
static size_t buf_num_vert;
static size_t buf_vbo_num_tris;

static struct GfxWindowManagerAPI *gfx_wapi;
static struct GfxRenderingAPI *gfx_rapi;

#define GFX_FLAME_ATLAS_FRAME_WIDTH 32
#define GFX_FLAME_ATLAS_FRAME_HEIGHT 64
#define GFX_FLAME_ATLAS_COLUMNS 8
#define GFX_FLAME_ATLAS_ROWS 4
#define GFX_FLAME_ATLAS_PHASE_STEP 4
#define GFX_FLAME_ATLAS_WIDTH (GFX_FLAME_ATLAS_FRAME_WIDTH * GFX_FLAME_ATLAS_COLUMNS)
#define GFX_FLAME_ATLAS_HEIGHT (GFX_FLAME_ATLAS_FRAME_HEIGHT * GFX_FLAME_ATLAS_ROWS)
#define GFX_FLAME_ATLAS_SIZE (GFX_FLAME_ATLAS_WIDTH * GFX_FLAME_ATLAS_HEIGHT)
#define GFX_FLAME_ATLAS_CACHE_SIZE 2
typedef struct __attribute__((aligned(16))) FlameAtlasCacheEntry {

    uint32_t pixels[GFX_FLAME_ATLAS_SIZE];
    uint32_t textureId;
    const uint8_t* texture0Addr;
    const uint8_t* texture1Addr;
    uint32_t lastUsed;
    uint32_t primRgb;
    uint32_t envRgb;
    uint8_t primLodFrac;
    bool pixelsValid;
    bool textureValid;
} FlameAtlasCacheEntry;
static FlameAtlasCacheEntry sFlameAtlasCache[GFX_FLAME_ATLAS_CACHE_SIZE] __attribute__((aligned(16)));
static FlameAtlasCacheEntry* sPreparedFlameAtlas;
static uint32_t sFlameAtlasUseClock;

static uint32_t sPs2FlameColorLut[256 * 16] __attribute__((aligned(64)));

static uint8_t sPs2FlameDecodedI8[GFX_FLAME_ATLAS_FRAME_WIDTH * GFX_FLAME_ATLAS_FRAME_HEIGHT]
    __attribute__((aligned(64)));
static uint8_t sPs2FlameDecodedI4[GFX_FLAME_ATLAS_FRAME_WIDTH * 128] __attribute__((aligned(64)));

static uint16_t sPs2FlameIndexAtlas[GFX_FLAME_ATLAS_SIZE] __attribute__((aligned(64)));
static bool sPs2FlameIndexAtlasValid;
static const uint8_t* sPs2FlameDecodedI8Addr;
static const uint8_t* sPs2FlameDecodedI4Addr;
static uint32_t sPs2FlameDecodedI8Serial;
static uint32_t sPs2FlameDecodedI4Serial;

#define GFX_TWO_I4_PRECOMBINE_WIDTH 32
#define GFX_TWO_I4_PRECOMBINE_HEIGHT 32
#define GFX_TWO_I4_PRECOMBINE_PIXELS (GFX_TWO_I4_PRECOMBINE_WIDTH * GFX_TWO_I4_PRECOMBINE_HEIGHT)
#define GFX_TWO_I4_PRECOMBINE_CACHE_SIZE 128

typedef struct __attribute__((aligned(16))) TwoI4PrecombineCacheEntry {
    uint32_t pixels[GFX_TWO_I4_PRECOMBINE_PIXELS];
    uint32_t textureId;
    const uint8_t* texture0Addr;
    const uint8_t* texture1Addr;
    uint32_t primRgb;
    uint32_t envRgb;
    uint32_t lastUsedFrame;
    uint16_t importWidth0, importHeight0;
    uint16_t importWidth1, importHeight1;
    uint8_t fmt0, siz0, fmt1, siz1;
    uint8_t nibble0, nibble1;
    uint8_t sample0X[GFX_TWO_I4_PRECOMBINE_WIDTH];
    uint8_t sample0Y[GFX_TWO_I4_PRECOMBINE_HEIGHT];
    uint8_t sample1X[GFX_TWO_I4_PRECOMBINE_WIDTH];
    uint8_t sample1Y[GFX_TWO_I4_PRECOMBINE_HEIGHT];
    uint8_t envAlpha;
    uint8_t outputWidth;
    uint8_t outputHeight;
    bool textureAllocated;
    bool textureValid;
} TwoI4PrecombineCacheEntry;

static TwoI4PrecombineCacheEntry sTwoI4PrecombineCache[GFX_TWO_I4_PRECOMBINE_CACHE_SIZE]
    __attribute__((aligned(16)));

typedef enum GfxPreparedAtlasKind {
    GFX_PREPARED_ATLAS_NONE = 0,
    GFX_PREPARED_ATLAS_FLAME,
    GFX_PREPARED_ATLAS_TWO_I4,
} GfxPreparedAtlasKind;
static GfxPreparedAtlasKind sPreparedAtlasKind;
static uint32_t sPreparedAtlasTextureId;

static inline uint32_t gfx_ps2_upload_next_power_of_two(uint32_t value) {
    if (value <= 1) return 1;
    return 1U << (32 - __builtin_clz(value - 1));
}
static inline bool gfx_ps2_is_power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static inline uint32_t gfx_ps2_mirror_base_extent(uint32_t extent, bool mirror, uint32_t mask) {
    if (mirror && mask != G_TX_NOMASK && mask < 12) {
        const uint32_t period = 1U << mask;
        if (period < extent) {
            return period;
        }
    }
    return extent;
}

static inline bool gfx_ps2_axis_is_clamped(uint32_t mode, uint32_t mask) {

    return ((mode & G_TX_CLAMP) != 0U) || (mask == G_TX_NOMASK);
}

static inline uint32_t gfx_ps2_physical_upload_extent(uint32_t extent, bool mirror, uint32_t mask,
                                                       bool clamp) {
    const uint32_t base = gfx_ps2_mirror_base_extent(extent, mirror, mask);
    (void)clamp;

    return gfx_ps2_upload_next_power_of_two(base) * (mirror ? 2U : 1U);
}
static inline size_t gfx_ps2_texture_bytes_per_pixel(unsigned int type) {
    if (type == PS2_TEXFMT_T8) return 1;
    if (type == PS2_TEXFMT_5551 || type == PS2_TEXFMT_4444) return 2;
    return 4;
}
#define GFX_PS2_UPLOAD_SCRATCH_MAX (1024U * 1024U)
static uint8_t sPs2UploadScratchStorage[GFX_PS2_UPLOAD_SCRATCH_MAX] __attribute__((aligned(64)));
static uint8_t* sPs2UploadScratch = sPs2UploadScratchStorage;
static size_t sPs2UploadScratchCapacity = GFX_PS2_UPLOAD_SCRATCH_MAX;

static inline void gfx_copy_ps2_texture_texel(uint8_t* dst, const uint8_t* src, size_t bytes_per_pixel) {
    switch (bytes_per_pixel) {
        case 1:
            dst[0] = src[0];
            break;
        case 2:
            dst[0] = src[0];
            dst[1] = src[1];
            break;
        default:
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            break;
    }
}

static void gfx_fill_ps2_texture_horizontal_padding(uint8_t* row, uint32_t content_width,
                                                    uint32_t upload_width, size_t bytes_per_pixel) {
    if (content_width == 0 || content_width >= upload_width) {
        return;
    }

    const uint8_t* edge = row + (size_t)(content_width - 1U) * bytes_per_pixel;
    uint8_t* dst = row + (size_t)content_width * bytes_per_pixel;
    for (uint32_t x = content_width; x < upload_width; x++) {
        gfx_copy_ps2_texture_texel(dst, edge, bytes_per_pixel);
        dst += bytes_per_pixel;
    }
}

static void gfx_fill_ps2_texture_vertical_padding(uint8_t* buf, uint32_t content_height,
                                                  uint32_t upload_height, size_t row_bytes) {
    if (content_height == 0 || content_height >= upload_height) {
        return;
    }

    const uint8_t* edge_row = buf + (size_t)(content_height - 1U) * row_bytes;
    for (uint32_t y = content_height; y < upload_height; y++) {
        memcpy(buf + (size_t)y * row_bytes, edge_row, row_bytes);
    }
}

static inline void gfx_upload_texture(int tile, const uint8_t* buf, uint32_t width, uint32_t height,
                                      unsigned int type) {
    const TextureTileState* tileState = gfx_get_texture_tile(tile);
    const bool mirror_s = (tileState->cms & G_TX_MIRROR) != 0 && tileState->masks != G_TX_NOMASK;
    const bool mirror_t = (tileState->cmt & G_TX_MIRROR) != 0 && tileState->maskt != G_TX_NOMASK;
    const uint32_t base_width = gfx_ps2_mirror_base_extent(width, mirror_s, tileState->masks);
    const uint32_t base_height = gfx_ps2_mirror_base_extent(height, mirror_t, tileState->maskt);
    const bool clamp_s = gfx_ps2_axis_is_clamped(tileState->cms, tileState->masks);
    const bool clamp_t = gfx_ps2_axis_is_clamped(tileState->cmt, tileState->maskt);
    const uint32_t uploadWidth = gfx_ps2_physical_upload_extent(width, mirror_s, tileState->masks, clamp_s);
    const uint32_t uploadHeight = gfx_ps2_physical_upload_extent(height, mirror_t, tileState->maskt, clamp_t);
    const uint32_t pad_width = mirror_s ? (uploadWidth / 2U) : uploadWidth;
    const uint32_t pad_height = mirror_t ? (uploadHeight / 2U) : uploadHeight;

    if (!mirror_s && !mirror_t && uploadWidth == width && uploadHeight == height) {
        gfx_rapi->upload_texture(buf, width, height, type);
        return;
    }

    const size_t bpp = gfx_ps2_texture_bytes_per_pixel(type);
    const size_t rowBytes = (size_t)uploadWidth * bpp;
    const size_t need = rowBytes * uploadHeight;
    if (need > sPs2UploadScratchCapacity) {
        printf("oot-ps2 texture staging too large size=%u cap=%u\n", (unsigned)need,
               (unsigned)sPs2UploadScratchCapacity);
        return;
    }

    const size_t srcRowBytes = (size_t)width * bpp;
    for (uint32_t y = 0; y < base_height; y++) {
        uint8_t* dstBase = sPs2UploadScratch + (size_t)y * rowBytes;
        const uint8_t* src = buf + (size_t)y * srcRowBytes;
        const size_t copyRowBytes = (size_t)base_width * bpp;
        memcpy(dstBase, src, copyRowBytes);

        gfx_fill_ps2_texture_horizontal_padding(dstBase, base_width, pad_width, bpp);
        if (mirror_s) {
            for (uint32_t x = 0; x < pad_width; x++) {
                memcpy(dstBase + (size_t)(pad_width + x) * bpp,
                       dstBase + (size_t)(pad_width - 1U - x) * bpp, bpp);
            }
        }
    }

    gfx_fill_ps2_texture_vertical_padding(sPs2UploadScratch, base_height, pad_height, rowBytes);

    if (mirror_t) {
        for (uint32_t y = 0; y < pad_height; y++) {
            memcpy(sPs2UploadScratch + (size_t)(pad_height + y) * rowBytes,
                   sPs2UploadScratch + (size_t)(pad_height - 1U - y) * rowBytes, rowBytes);
        }
    }

    gfx_rapi->upload_texture(sPs2UploadScratch, uploadWidth, uploadHeight, type);
}

#include <time.h>
__attribute__((unused))
static unsigned long get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

#define Z_NEG  (0x01)
#define Z_POS  (0x02)
#define Y_NEG  (0x04)
#define Y_POS  (0x08)
#define X_NEG  (0x10)
#define X_POS  (0x20)

#define W_NEG  (0x40)
#define CLIP_TEST_FLAGS ( X_POS | X_NEG | Y_POS | Y_NEG | Z_POS | Z_NEG | W_NEG)

static inline float vec3_dot(const float *lhs, const float *rhs){
    return (lhs[0]*rhs[0]) + (lhs[1]*rhs[1]) + (lhs[2]*rhs[2]);
}

static inline float vec4_dot(const float *lhs, const float *rhs){
    return (lhs[0]*rhs[0]) + (lhs[1]*rhs[1]) + (lhs[2]*rhs[2])+ (lhs[3]*rhs[3]);
}

void gfx_clip_interpolate_vert(struct LoadedVertex* out, const struct  LoadedVertex* lhs, const struct LoadedVertex* rhs, const float factor )
{

    out->x = lhs->x + (rhs->x - lhs->x) * factor;
    out->y = lhs->y + (rhs->y - lhs->y) * factor;
    out->z = lhs->z + (rhs->z - lhs->z) * factor;

    out->w = lhs->w + (rhs->w - lhs->w) * factor;

    out->_x = lhs->_x + (rhs->_x - lhs->_x) * factor;
    out->_y = lhs->_y + (rhs->_y - lhs->_y) * factor;
    out->_z = lhs->_z + (rhs->_z - lhs->_z) * factor;
    out->_w = lhs->_w + (rhs->_w - lhs->_w) * factor;

    out->color.r = lhs->color.r + (rhs->color.r - lhs->color.r) * factor;
    out->color.g = lhs->color.g + (rhs->color.g - lhs->color.g) * factor;
    out->color.b = lhs->color.b + (rhs->color.b - lhs->color.b) * factor;
    out->color.a = lhs->color.a + (rhs->color.a - lhs->color.a) * factor;
    out->fog_alpha = lhs->fog_alpha + (rhs->fog_alpha - lhs->fog_alpha) * factor;

    out->u = lhs->u + (rhs->u - lhs->u) * factor;
    out->v = lhs->v + (rhs->v - lhs->v) * factor;
}

static const float NDCPlane[6][4] __attribute__((aligned(16))) =
{
	{  0.f,  0.f,  1.f, -1.f },
	{  1.f,  0.f,  0.f, -1.f },
	{ -1.f,  0.f,  0.f, -1.f },
	{  0.f,  1.f,  0.f, -1.f },
	{  0.f, -1.f,  0.f, -1.f },
	{  0.f,  0.f, -1.f, -1.f }
};

static uint32_t clipToHyperPlane( struct LoadedVertex *dest, const struct LoadedVertex *source, uint32_t inCount, const float plane[4] )
{
	uint32_t outCount;
	struct LoadedVertex *out;

	const struct LoadedVertex *a;
	const struct LoadedVertex *b;

	float aDotPlane;
	float bDotPlane;

	out = dest;
	outCount = 0;
	b = source;
	bDotPlane = vec4_dot(&b->_x, plane);
    size_t i;

#define EPSILON 0.00000001
	for(i = 1; i < inCount + 1; ++i)
	{
		a = &source[i%inCount];
		aDotPlane = vec4_dot(&a->_x, plane);

		if ( aDotPlane <= EPSILON )
		{

			if ( bDotPlane > EPSILON )
			{

                const float dot_projected = bDotPlane - aDotPlane;
				gfx_clip_interpolate_vert(out, b, a, bDotPlane / dot_projected );
				out += 1;
				outCount += 1;
			}

			*out = *a;
			b = out;

			out += 1;
			outCount += 1;
		}
		else
		{

			if ( bDotPlane <= EPSILON )
			{

                const float dot_projected = bDotPlane - aDotPlane;
				gfx_clip_interpolate_vert(out, b, a, bDotPlane / dot_projected );

				out += 1;
				outCount += 1;
			}
			b = a;
		}

        bDotPlane = aDotPlane;
	}

	return outCount;
}

static uint32_t ps2_clip_to_eye_plane(struct LoadedVertex* dest, const struct LoadedVertex* source,
                                      uint32_t inCount) {
    const float minW = 0.0001f;
    uint32_t outCount = 0;
    if (inCount == 0) return 0;

    const struct LoadedVertex* b = &source[inCount - 1];
    bool bInside = __builtin_isfinite(b->_w) && b->_w >= minW;
    for (uint32_t i = 0; i < inCount; ++i) {
        const struct LoadedVertex* a = &source[i];
        const bool aInside = __builtin_isfinite(a->_w) && a->_w >= minW;

        if (aInside != bInside) {
            const float denom = a->_w - b->_w;
            if (__builtin_isfinite(denom) && fabsf(denom) > 0.0000001f) {
                float t = (minW - b->_w) / denom;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                gfx_clip_interpolate_vert(&dest[outCount++], b, a, t);
                dest[outCount - 1]._w = minW;
            }
        }
        if (aInside) dest[outCount++] = *a;
        b = a;
        bInside = aInside;
    }
    return outCount;
}

static uint32_t clip_to_frustum(struct LoadedVertex* v0, struct LoadedVertex* v1, uint32_t vIn,
                                uint32_t clipFlags, struct LoadedVertex** clippedVertices)
{
    struct LoadedVertex* source = v0;
    struct LoadedVertex* dest = v1;
    struct LoadedVertex* swap;
    uint32_t vOut = vIn;

    if (clipFlags & W_NEG) {
        vOut = ps2_clip_to_eye_plane(dest, source, vOut);
        swap = source; source = dest; dest = swap;
        if (vOut == 0) goto clippingComplete;

        clipFlags |= X_POS | X_NEG | Y_POS | Y_NEG | Z_POS | Z_NEG;
    }

#define CLIP_TO_PLANE_IF_NEEDED(flag, plane)                        \
    do {                                                           \
        if (clipFlags & (flag)) {                                  \
            vOut = clipToHyperPlane(dest, source, vOut, (plane));  \
            swap = source;                                         \
            source = dest;                                         \
            dest = swap;                                           \
            if (vOut == 0) {                                       \
                goto clippingComplete;                             \
            }                                                      \
        }                                                          \
    } while (0)

    CLIP_TO_PLANE_IF_NEEDED(X_POS, NDCPlane[2]);
    CLIP_TO_PLANE_IF_NEEDED(X_NEG, NDCPlane[1]);
    CLIP_TO_PLANE_IF_NEEDED(Y_POS, NDCPlane[4]);
    CLIP_TO_PLANE_IF_NEEDED(Y_NEG, NDCPlane[3]);

    CLIP_TO_PLANE_IF_NEEDED(Z_POS, NDCPlane[5]);
    CLIP_TO_PLANE_IF_NEEDED(Z_NEG, NDCPlane[0]);

clippingComplete:
#undef CLIP_TO_PLANE_IF_NEEDED
    *clippedVertices = source;
    return vOut;
}

static struct LoadedVertex temp_a[12];
static struct LoadedVertex temp_b[12];

static struct LoadedVertex sClippedVertices[24] __attribute__((aligned(16)));
static struct LoadedVertex* sClippedVertexPtrs[24];

void gfx_clip_single_vert(struct LoadedVertex* p_p_vertices, size_t* p_num_vertices,
                          struct LoadedVertex* v_arr[3], uint32_t clipFlags)
{

    size_t clipped_vertices_num = 0;

    temp_a[ 0 ] = *v_arr[ 0 ];
    temp_a[ 1 ] = *v_arr[ 1 ];
    temp_a[ 2 ] = *v_arr[ 2 ];

    struct LoadedVertex* clippedPolygon;
    uint32_t out = clip_to_frustum(temp_a, temp_b, 3, clipFlags, &clippedPolygon);
    if( out < 3 ){
        *p_num_vertices = 0;
        return;
    }

    for( uint32_t j = 0; j <= out - 3; ++j )
    {
        p_p_vertices[clipped_vertices_num++] = clippedPolygon[0];
        p_p_vertices[clipped_vertices_num++] = clippedPolygon[j + 1];
        p_p_vertices[clipped_vertices_num++] = clippedPolygon[j + 2];
    }

	*p_num_vertices = clipped_vertices_num;
}

static void gfx_flush(void) {
#if OOT_PS2_DEEP_PROFILE
    const uint32_t ps2DeepStart = gfx_ps2_read_count();
#endif
    if (buf_vbo_len > 0) {

        const bool twoTextureBlend = rendering_state.tri_pipeline.two_texture_blend &&
                                     rendering_state.textures[0] != NULL && rendering_state.textures[1] != NULL;
        const bool useFog = rendering_state.tri_pipeline.use_fog;
        const bool fogUsesTextureAlpha = rendering_state.tri_pipeline.fog_uses_texture_alpha;
        const bool twoTextureAlphaFog =
            useFog && twoTextureBlend && rendering_state.tri_pipeline.two_texture_alpha_blend;

        if (rendering_state.tri_pipeline.use_texture && !rendering_state.tri_pipeline.flame_texture_atlas) {
            if (twoTextureBlend) {
                rendering_state.textures[0]->last_used_frame = sTextureCacheFrameSerial;
                rendering_state.textures[1]->last_used_frame = sTextureCacheFrameSerial;
            } else {
                int activeTexture = rendering_state.tri_pipeline.comb->active_texture;

                if (activeTexture < 0) {
                    activeTexture = rendering_state.tri_pipeline.used_textures[0] ? 0 : 1;
                }
                if (rendering_state.textures[activeTexture] != NULL) {
                    rendering_state.textures[activeTexture]->last_used_frame = sTextureCacheFrameSerial;
                }
            }
        }

#if defined(OOTDEBUG) || OOT_PS2_PERF_BENCH
        sPerformanceFlushCount++;
        sPerformanceDrawCallCount += (twoTextureBlend ? 2 : 1) + (useFog ? (twoTextureAlphaFog ? 2 : 1) : 0);
        if (rendering_state.alpha_blend || twoTextureBlend) {
            sPerformanceTranslucentDrawCallCount += twoTextureBlend ? 2 : 1;
            sPerformanceTranslucentTriangleCount += buf_vbo_num_tris * (twoTextureBlend ? 2 : 1);
        } else {
            sPerformanceOpaqueDrawCallCount++;
            sPerformanceOpaqueTriangleCount += buf_vbo_num_tris;
        }
        if (buf_vbo_num_tris > sPerformanceMaxBatchTriangles) {
            sPerformanceMaxBatchTriangles = buf_vbo_num_tris;
        }
#endif

        if (twoTextureBlend) {
            gfx_rapi->set_use_alpha(true);
        }
        gfx_ps2_set_texture_blend_reverse(rendering_state.tri_pipeline.texture_blend_reverse);
        gfx_ps2_set_texture_blend_precolor(rendering_state.tri_pipeline.texture_blend_precolor);
        gfx_ps2_set_din_fire_tint(rendering_state.tri_pipeline.din_fire_tint);
        gfx_ps2_set_two_texture_blend_active(twoTextureBlend);
        gfx_ps2_set_two_texture_env_prim_tint(
            twoTextureBlend && rendering_state.tri_pipeline.two_texture_env_prim_tint);
        gfx_rapi->draw_triangles((float *)buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        if (twoTextureBlend) {
            for (size_t i = 0; i < buf_num_vert; i++) {
                buf_vbo[i].u = buf_vbo_tex1[i].u;
                buf_vbo[i].v = buf_vbo_tex1[i].v;
                buf_vbo[i].real_u = buf_vbo_tex1[i].real_u;
                buf_vbo[i].real_v = buf_vbo_tex1[i].real_v;
                buf_vbo[i].color.a = buf_vbo_tex1[i].alpha;
            }

            gfx_rapi->select_texture(1, rendering_state.textures[1]->texture_id);
            gfx_rapi->draw_triangles((float *)buf_vbo, buf_vbo_len, buf_vbo_num_tris);
            gfx_rapi->set_use_alpha(rendering_state.alpha_blend);
            gfx_rapi->select_texture(0, rendering_state.textures[0]->texture_id);
            rendering_state.bound_texture_id = rendering_state.textures[0]->texture_id;
            rendering_state.bound_texture_tile = 0;
        }
        if (useFog) {
#if defined(OOTDEBUG) || OOT_PS2_PERF_BENCH
            sPerformanceTranslucentDrawCallCount += twoTextureAlphaFog ? 2 : 1;
            sPerformanceTranslucentTriangleCount += buf_vbo_num_tris * (twoTextureAlphaFog ? 2 : 1);
#endif
            float* fogBuffer = fogUsesTextureAlpha ? (float*)buf_vbo_fog.textured
                                                   : (float*)buf_vbo_fog.color;
            const size_t fogBufferLen =
                (fogUsesTextureAlpha ? sizeof(ps2_fog_textured_t) : sizeof(ps2_fog_color_t)) *
                buf_num_vert;

            if (twoTextureAlphaFog) {
                gfx_rapi->draw_fog_triangles(fogBuffer, fogBufferLen, buf_vbo_num_tris,
                                             fogUsesTextureAlpha, false);

                for (size_t i = 0; i < buf_num_vert; i++) {
                    if (fogUsesTextureAlpha) {
                        buf_vbo_fog.textured[i].u = buf_vbo_tex1[i].u;
                        buf_vbo_fog.textured[i].v = buf_vbo_tex1[i].v;
                        buf_vbo_fog.textured[i].color.a = buf_vbo_fog_tex1_alpha[i];
                    } else {
                        buf_vbo_fog.color[i].color.a = buf_vbo_fog_tex1_alpha[i];
                    }
                }
                if (fogUsesTextureAlpha) {
                    gfx_rapi->select_texture(1, rendering_state.textures[1]->texture_id);
                }
                gfx_rapi->draw_fog_triangles(fogBuffer, fogBufferLen, buf_vbo_num_tris,
                                             fogUsesTextureAlpha, true);
                if (fogUsesTextureAlpha) {
                    gfx_rapi->select_texture(0, rendering_state.textures[0]->texture_id);
                    rendering_state.bound_texture_id = rendering_state.textures[0]->texture_id;
                    rendering_state.bound_texture_tile = 0;
                }
            } else {
                gfx_rapi->draw_fog_triangles(fogBuffer, fogBufferLen, buf_vbo_num_tris,
                                             fogUsesTextureAlpha, true);
            }
        }
        buf_vbo_len = 0;
        buf_num_vert = 0;
        buf_vbo_num_tris = 0;

    }
#if OOT_PS2_DEEP_PROFILE
    sPs2DeepFlushCycles += gfx_ps2_read_count() - ps2DeepStart;
#endif
}

static struct ShaderProgram *gfx_lookup_or_create_shader_program(uint32_t shader_id) {
    struct ShaderProgram *prg = gfx_rapi->lookup_shader(shader_id);
    if (prg == NULL) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        prg = gfx_rapi->create_and_load_new_shader(shader_id);
        rendering_state.shader_program = prg;
    }
    return prg;
}

static uint8_t gfx_cc_pick_vertex_color_source(const uint8_t components[4], const uint8_t input_mapping[4],
                                               uint8_t input_count) {
    if (input_count == 0) {
        return CC_0;
    }

    if ((components[0] == CC_PRIM) && (components[1] == CC_ENV) && (components[3] == CC_ENV) &&
        ((components[2] == CC_TEXEL0) || (components[2] == CC_TEXEL0A) || (components[2] == CC_TEXEL1))) {
        return CC_PRIM;
    }

    return input_mapping[input_count - 1];
}

static void gfx_generate_cc(struct ColorCombiner *comb, uint32_t cc_id) {
    uint8_t c[2][4];
    uint32_t shader_id = (cc_id >> 24) << 24;
    uint8_t shader_input_mapping[2][4] = {{0}};
    uint8_t shader_input_count[2] = {0};
    struct CCFeatures cc_features;
    for (int i = 0; i < 4; i++) {
        c[0][i] = (cc_id >> (i * 3)) & 7;
        c[1][i] = (cc_id >> (12 + i * 3)) & 7;
    }
    for (int i = 0; i < 2; i++) {
        if (c[i][0] == c[i][1] || c[i][2] == CC_0) {
            c[i][0] = c[i][1] = c[i][2] = 0;
        }
        uint8_t input_number[8] = {0};
        int next_input_number = SHADER_INPUT_1;
        for (int j = 0; j < 4; j++) {
            int val = 0;
            switch (c[i][j]) {
                case CC_0:
                    break;
                case CC_TEXEL0:
                    val = SHADER_TEXEL0;
                    break;
                case CC_TEXEL1:
                    val = SHADER_TEXEL1;
                    break;
                case CC_TEXEL0A:
                    val = SHADER_TEXEL0A;
                    break;
                case CC_PRIM:
                case CC_SHADE:
                case CC_ENV:
                case CC_LOD:
                    if (input_number[c[i][j]] == 0) {
                        shader_input_mapping[i][next_input_number - 1] = c[i][j];
                        input_number[c[i][j]] = next_input_number++;
                    }
                    val = input_number[c[i][j]];
                    break;
            }
            shader_id |= val << (i * 12 + j * 3);
        }
        shader_input_count[i] = next_input_number - SHADER_INPUT_1;
    }
    gfx_cc_get_features(shader_id, &cc_features);
    comb->cc_id = cc_id;
    comb->prg = gfx_lookup_or_create_shader_program(shader_id);
    const bool collapse_tex1 = cc_features.used_textures[0] && cc_features.used_textures[1];

    comb->used_textures[0] = cc_features.used_textures[0];
    comb->used_textures[1] = cc_features.used_textures[1] && !collapse_tex1;
    comb->active_texture = cc_features.used_textures[0] ? 0 : (cc_features.used_textures[1] ? 1 : -1);
    comb->uses_texture_alpha = false;
    if (cc_features.opt_alpha) {
        for (int i = 0; i < 4; i++) {
            if ((cc_features.c[1][i] == SHADER_TEXEL0) ||
                (cc_features.c[1][i] == SHADER_TEXEL0A) ||
                (cc_features.c[1][i] == SHADER_TEXEL1)) {
                comb->uses_texture_alpha = true;
                break;
            }
        }
    }
    comb->vertex_color_source[0] =
        gfx_cc_pick_vertex_color_source(c[0], shader_input_mapping[0], shader_input_count[0]);
    comb->vertex_color_source[1] =
        gfx_cc_pick_vertex_color_source(c[1], shader_input_mapping[1], shader_input_count[1]);
    comb->texture_blend = cc_features.opt_texture_blend;
    if (cc_features.opt_texture_blend) {
        comb->vertex_color_source[0] = cc_features.opt_texture_blend_shade ? CC_SHADE : CC_ENV;
    }
}

static inline struct RGBA gfx_get_vertex_color(const struct ColorCombiner *comb, bool use_alpha, const struct RGBA *shade_color, float lod_w, bool allow_lod) {
    switch (comb->vertex_color_source[use_alpha ? 1 : 0]) {
        case CC_PRIM:
            return rdp.prim_color;
        case CC_SHADE:
            return *shade_color;
        case CC_ENV:
            return rdp.env_color;
        case CC_LOD:
            if (allow_lod) {
                float distance_frac = (lod_w - 3000.0f) / 3000.0f;
                if (distance_frac < 0.0f) distance_frac = 0.0f;
                if (distance_frac > 1.0f) distance_frac = 1.0f;
                const uint8_t lod = distance_frac * 255.0f;
                return (struct RGBA){lod, lod, lod, lod};
            }
            break;
    }
    return white_color;
}

static inline uint8_t gfx_color_mul_channel(uint8_t lhs, uint8_t rhs) {
    return ((uint16_t)lhs * (uint16_t)rhs + 127) / 255;
}

static inline void gfx_color_mul_env(struct RGBA* color) {
    color->r = gfx_color_mul_channel(color->r, rdp.env_color.r);
    color->g = gfx_color_mul_channel(color->g, rdp.env_color.g);
    color->b = gfx_color_mul_channel(color->b, rdp.env_color.b);
}

static inline void gfx_color_mul_prim(struct RGBA* color) {
    color->r = gfx_color_mul_channel(color->r, rdp.prim_color.r);
    color->g = gfx_color_mul_channel(color->g, rdp.prim_color.g);
    color->b = gfx_color_mul_channel(color->b, rdp.prim_color.b);
}

static inline __attribute__((always_inline)) void gfx_two_texture_blend_pass_alphas(
    uint8_t surfaceAlpha, uint8_t mixAlpha, bool blendTextureAlpha,
    bool uncompensatedTextureAlpha, uint8_t* baseAlpha, uint8_t* overlayAlpha) {
    uint32_t base;
    uint32_t overlay;
    uint32_t denominator;

    if (surfaceAlpha == 0xFF) {
        if (blendTextureAlpha) {
            *baseAlpha = 255 - mixAlpha;
            *overlayAlpha = uncompensatedTextureAlpha ? mixAlpha : (mixAlpha != 0 ? 0xFF : 0);
        } else {
            *baseAlpha = mixAlpha != 0xFF ? 0xFF : 0;
            *overlayAlpha = mixAlpha;
        }
        return;
    }

    if (blendTextureAlpha) {
        base = ((uint32_t)surfaceAlpha * (255 - mixAlpha) + 127) / 255;
        if (uncompensatedTextureAlpha) {

            overlay = ((uint32_t)surfaceAlpha * mixAlpha + 127) / 255;
            *baseAlpha = base;
            *overlayAlpha = overlay;
            return;
        }

        denominator = 255 - base;
        overlay = 0;

        if (denominator != 0) {
            overlay = ((uint32_t)surfaceAlpha * mixAlpha + denominator / 2) / denominator;
            if (overlay > 255) {
                overlay = 255;
            }
        }

        *baseAlpha = base;
        *overlayAlpha = overlay;
        return;
    }

    overlay = ((uint32_t)surfaceAlpha * mixAlpha + 127) / 255;
    denominator = 255 - overlay;
    base = 0;

    if (denominator != 0) {
        base = ((uint32_t)surfaceAlpha * (255 - mixAlpha) + denominator / 2) / denominator;
        if (base > 255) {
            base = 255;
        }
    }

    *baseAlpha = base;
    *overlayAlpha = overlay;
}

static inline struct RGBA gfx_get_vertex_rgba(const struct ColorCombiner *comb, bool use_alpha,
                                              const struct RGBA *shade_color, float lod_w, bool allow_lod) {
    struct RGBA color = gfx_get_vertex_color(comb, false, shade_color, lod_w, allow_lod);

    if (comb->texture_blend && (comb->vertex_color_source[0] == CC_ENV) &&
        (rdp.combine_texture_tint_uses_env_alpha ||
         (rdp.combine_texture_tint_uses_prim_lod && rdp.combine_two_texture_blend))) {
        color.r = rdp.prim_color.r;
        color.g = rdp.prim_color.g;
        color.b = rdp.prim_color.b;
    }

    if (use_alpha) {
        color.a = gfx_get_vertex_color(comb, true, shade_color, lod_w, allow_lod).a;
    }

    return color;
}

static struct ColorCombiner *gfx_lookup_or_create_color_combiner(uint32_t cc_id) {
    const size_t cacheIndex =
        (cc_id ^ (cc_id >> 12) ^ (cc_id >> 24)) & (COLOR_COMBINER_CACHE_SET_COUNT - 1);
    ColorCombinerCacheEntry* cacheSet = color_combiner_cache[cacheIndex];
    ColorCombinerCacheEntry* entry = NULL;

    for (size_t way = 0; way < COLOR_COMBINER_CACHE_WAYS; way++) {
        if (cacheSet[way].valid) {
            if (cacheSet[way].combiner.cc_id == cc_id) {
                return &cacheSet[way].combiner;
            }
        } else if (entry == NULL) {
            entry = &cacheSet[way];
        }
    }

    if (entry == NULL) {
        const uint8_t replacementWay = color_combiner_cache_next_way[cacheIndex];

        entry = &cacheSet[replacementWay];
        color_combiner_cache_next_way[cacheIndex] = (replacementWay + 1) & (COLOR_COMBINER_CACHE_WAYS - 1);
    }

    if (entry->valid) {
        gfx_flush();
    }
    gfx_generate_cc(&entry->combiner, cc_id);
    entry->valid = true;
    return &entry->combiner;
}

extern int gfx_vram_space_available(void);
extern void texman_clear(void);
extern void texman_upload(int width, int height, unsigned int type, const void* buffer);
extern int texman_vram_space_available(unsigned int size);
extern int texman_texture_slot_available(void);

static uint32_t gfx_texture_import_width(int tile);
static uint32_t gfx_texture_import_height(int tile);
static void gfx_texture_import_dimensions(int tile, uint32_t* width, uint32_t* height);

static uint32_t gfx_ps2_next_power_of_two(uint32_t value) {
    if (value <= 1) {
        return 1;
    }
    return 1U << (32 - __builtin_clz(value - 1));
}

static unsigned int gfx_texture_cache_upload_size_ps2(uint16_t width, uint16_t height, uint32_t fmt, uint32_t siz,
                                                       bool mirror_s, bool mirror_t, uint32_t cms, uint32_t cmt,
                                                       uint32_t masks, uint32_t maskt) {
    size_t bytesPerPixel;

    if ((fmt == G_IM_FMT_I) && (siz == G_IM_SIZ_8b)) {

        bytesPerPixel = 1;
    } else if (((fmt == G_IM_FMT_RGBA) && (siz == G_IM_SIZ_16b)) || (fmt == G_IM_FMT_CI)) {
        bytesPerPixel = 2;
    } else {
        bytesPerPixel = 4;
    }
    const uint32_t uploadWidth = gfx_ps2_physical_upload_extent(
        width, mirror_s, masks, gfx_ps2_axis_is_clamped(cms, masks));
    const uint32_t uploadHeight = gfx_ps2_physical_upload_extent(
        height, mirror_t, maskt, gfx_ps2_axis_is_clamped(cmt, maskt));
    size_t size = (size_t)uploadWidth * uploadHeight * bytesPerPixel;
    size = (size + 127U) & ~127U;
    return size > 0xFFFFFFFFU ? 0xFFFFFFFFU : (unsigned int)size;
}

static void gfx_texture_cache_clear(void) {
    gfx_flush();
    texman_clear();
    for (uint32_t i = 0; i < ARRAY_COUNTU(sFlameAtlasCache); i++) {
        sFlameAtlasCache[i].textureId = 0;
        sFlameAtlasCache[i].textureValid = false;
    }
    sPreparedFlameAtlas = NULL;
    for (uint32_t i = 0; i < GFX_TWO_I4_PRECOMBINE_CACHE_SIZE; i++) {

        sTwoI4PrecombineCache[i].textureId = 0;
        sTwoI4PrecombineCache[i].textureAllocated = false;
    }
    sPreparedAtlasKind = GFX_PREPARED_ATLAS_NONE;
    sPreparedAtlasTextureId = 0;
    rendering_state.bound_texture_id = 0;
    rendering_state.bound_texture_tile = -1;
    gfx_texture_cache.pool_pos = 0;
    memset(gfx_texture_cache.pool, 0, sizeof(gfx_texture_cache.pool));
    memset(gfx_texture_cache.hashmap, 0, sizeof(gfx_texture_cache.hashmap));
    rdp.textures_changed[0] = true;
    rdp.textures_changed[1] = true;
    memset(rendering_state.textures, 0, sizeof(rendering_state.textures));
    rendering_state.tri_pipeline_dirty = true;
    rendering_state.backend_state_dirty = true;
}

static uint32_t gfx_ps2_dynamic_texture_content_key(const uint8_t* addr, uint32_t sizeBytes) {
    const uint8_t* bytes = addr;
    uint32_t key = 2166136261U;

    if ((sizeBytes == 0) || !gfx_normalize_texture_source(&bytes, sizeBytes)) {
        return 0;
    }

    while ((sizeBytes != 0U) && (((uintptr_t)bytes & 3U) != 0U)) {
        key = (key ^ *bytes++) * 16777619U;
        sizeBytes--;
    }
    while (sizeBytes >= 4U) {
        const uint32_t word = *(const uint32_t*)bytes;
        key = (key ^ word) * 16777619U;
        bytes += 4;
        sizeBytes -= 4U;
    }
    while (sizeBytes != 0U) {
        key = (key ^ *bytes++) * 16777619U;
        sizeBytes--;
    }
    return key != 0 ? key : 1;
}

static bool gfx_texture_cache_lookup(int tile, struct TextureHashmapNode **n, const uint8_t *orig_addr,
                                     uint32_t fmt, uint32_t siz, bool allocateOnMiss, bool* uploadInPlace) {
    size_t hash = (uintptr_t)orig_addr;
    struct TextureHashmapNode **node;
    const struct TextureHashmapNode* previousNode = *n;

    *uploadInPlace = false;
    const TextureTileState* tileState = gfx_get_texture_tile(tile);
    const uint32_t source_span_size = gfx_texture_source_span_size(tile);
    const uint32_t source_key = OotPort_GetExternalAssetRangeSerial(orig_addr, source_span_size);
    const bool runtime_source = (orig_addr != sInvalidTextureBuf) &&
                                (orig_addr != sInvalidPaletteBuf) &&
                                OotPort_IsRuntimeByteRange(orig_addr, source_span_size);

    const bool immutable_dynamic =
        (((fmt == G_IM_FMT_IA) && (siz == G_IM_SIZ_4b) &&
          (source_span_size > 0U) && (source_span_size <= 1024U)) ||
         ((fmt == G_IM_FMT_RGBA) && (siz == G_IM_SIZ_32b) &&
          (source_span_size == (32U * 32U * 4U))));
    const bool dynamic_source = ((source_key == 0) && runtime_source) || immutable_dynamic;
    const bool fingerprint_runtime = ((source_key == 0) && runtime_source && !immutable_dynamic &&
                                      (source_span_size > 0U) && (source_span_size <= 32768U));
    const uint32_t dynamic_content_key = immutable_dynamic
        ? gfx_ps2_dynamic_texture_content_key(orig_addr, source_span_size) : 0;
    const bool uses_palette = fmt == G_IM_FMT_CI;
    const uint8_t* palette_addr = uses_palette ? rdp.palette : NULL;
    const uint32_t palette_key = uses_palette ? rdp.palette_key : 0;
    hash ^= (size_t)source_key * 2654435761U;
    hash ^= (size_t)dynamic_content_key * 2246822519U;
    hash ^= (size_t)palette_addr >> 4;
    const uint8_t mirror_s = (tileState->cms & G_TX_MIRROR) != 0 && tileState->masks != G_TX_NOMASK;
    const uint8_t mirror_t = (tileState->cmt & G_TX_MIRROR) != 0 && tileState->maskt != G_TX_NOMASK;
    hash ^= (size_t)mirror_s * 0x9E3779B1U;
    hash ^= (size_t)mirror_t * 0x85EBCA77U;
    uint16_t desired_upload_width = 0;
    uint16_t desired_upload_height = 0;
    uint32_t importWidth;
    uint32_t importHeight;

    gfx_texture_import_dimensions(tile, &importWidth, &importHeight);
    const uint16_t width = importWidth;
    const uint16_t height = importHeight;
    desired_upload_width = (uint16_t)gfx_ps2_physical_upload_extent(
        width, mirror_s, tileState->masks, gfx_ps2_axis_is_clamped(tileState->cms, tileState->masks));
    desired_upload_height = (uint16_t)gfx_ps2_physical_upload_extent(
        height, mirror_t, tileState->maskt, gfx_ps2_axis_is_clamped(tileState->cmt, tileState->maskt));
    hash ^= (size_t)desired_upload_width * 0x27D4EB2DU;
    hash ^= (size_t)desired_upload_height * 0x165667B1U;
    hash = (hash >> 5) & 0x3ff;
    node = &gfx_texture_cache.hashmap[hash];

    while (*node != NULL && *node - gfx_texture_cache.pool < (int)gfx_texture_cache.pool_pos) {
        if ((*node)->texture_addr == orig_addr && (*node)->fmt == fmt && (*node)->siz == siz
            && (*node)->width == width && (*node)->height == height
            && (*node)->row_stride_bytes == rdp.loaded_texture[tile].row_stride_bytes
            && (*node)->source_nibble_offset == rdp.loaded_texture[tile].source_nibble_offset
            && (*node)->source_key == source_key
            && (*node)->dynamic_content_key == dynamic_content_key
            && (*node)->palette_addr == palette_addr
            && (*node)->mirror_s == mirror_s && (*node)->mirror_t == mirror_t
            && (*node)->upload_width == desired_upload_width
            && (*node)->upload_height == desired_upload_height
        ) {
            const bool paletteChanged = uses_palette && ((*node)->palette_key != palette_key);
            bool runtimeContentChanged = false;

            if (fingerprint_runtime && ((*node)->runtime_content_check_frame != sTextureCacheFrameSerial)) {
                const uint32_t currentContentKey =
                    gfx_ps2_dynamic_texture_content_key(orig_addr, source_span_size);
                (*node)->runtime_content_check_frame = sTextureCacheFrameSerial;
                if ((*node)->runtime_content_key != currentContentKey) {
                    (*node)->runtime_content_key = currentContentKey;
                    runtimeContentChanged = true;
                }
            }

            const bool needsUpload = paletteChanged || runtimeContentChanged ||
                (dynamic_source && !immutable_dynamic && !fingerprint_runtime &&
                 ((*node)->last_used_frame != sTextureCacheFrameSerial));

            if (paletteChanged && ((*node)->last_used_frame == sTextureCacheFrameSerial)) {
                node = &(*node)->next;
                continue;
            }

            *n = *node;

            if ((previousNode == NULL) ||
                (previousNode->linear_filter != (*node)->linear_filter) ||
                (previousNode->cms != (*node)->cms) || (previousNode->cmt != (*node)->cmt) ||
                (previousNode->masks != (*node)->masks) || (previousNode->maskt != (*node)->maskt)) {
                gfx_rapi->set_sampler_parameters(tile, (*node)->linear_filter, (*node)->cms, (*node)->cmt,
                                                 (*node)->masks, (*node)->maskt);
            }

            (*node)->last_used_frame = sTextureCacheFrameSerial;
            if (needsUpload) {
                (*node)->palette_key = palette_key;
                gfx_rapi->select_texture(tile, (*node)->texture_id);
                *uploadInPlace = true;
            }
            return !needsUpload;
        }
        node = &(*node)->next;
    }

    if (!allocateOnMiss) {
        return false;
    }

    if (!texman_vram_space_available(gfx_texture_cache_upload_size_ps2(width, height, fmt, siz, mirror_s, mirror_t,
                                                 tileState->cms, tileState->cmt,
                                                 tileState->masks, tileState->maskt)) ||
        !texman_texture_slot_available()) {
        gfx_texture_cache_clear();
        node = &gfx_texture_cache.hashmap[hash];

    }
    if (gfx_texture_cache.pool_pos == sizeof(gfx_texture_cache.pool) / sizeof(struct TextureHashmapNode)) {
        gfx_texture_cache_clear();
        node = &gfx_texture_cache.hashmap[hash];

    }
    *node = &gfx_texture_cache.pool[gfx_texture_cache.pool_pos++];
    if ((*node)->texture_addr == NULL) {
        (*node)->texture_id = gfx_rapi->new_texture();
    }

    gfx_rapi->set_sampler_parameters(tile, false, 0, 0, G_TX_NOMASK, G_TX_NOMASK);
    (*node)->cms = 0;
    (*node)->cmt = 0;
    (*node)->masks = G_TX_NOMASK;
    (*node)->maskt = G_TX_NOMASK;
    (*node)->linear_filter = false;
    (*node)->source_key = source_key;
    (*node)->dynamic_content_key = dynamic_content_key;
    (*node)->runtime_content_key = fingerprint_runtime
        ? gfx_ps2_dynamic_texture_content_key(orig_addr, source_span_size) : 0U;
    (*node)->runtime_content_check_frame = fingerprint_runtime ? sTextureCacheFrameSerial : 0U;
    (*node)->palette_addr = palette_addr;
    (*node)->palette_key = palette_key;
    (*node)->last_used_frame = sTextureCacheFrameSerial;

    (*node)->upload_width = desired_upload_width;
    (*node)->upload_height = desired_upload_height;
    (*node)->mirror_s = mirror_s;
    (*node)->mirror_t = mirror_t;
    (*node)->next = NULL;
    (*node)->texture_addr = orig_addr;
    (*node)->fmt = fmt;
    (*node)->siz = siz;
    (*node)->width = width;
    (*node)->height = height;
    (*node)->row_stride_bytes = rdp.loaded_texture[tile].row_stride_bytes;
    (*node)->source_nibble_offset = rdp.loaded_texture[tile].source_nibble_offset;
    *n = *node;
    return false;
}

static uint32_t gfx_texture_tile_width(int tile) {
    const TextureTileState* tileState = gfx_get_texture_tile(tile);

    return (tileState->lrs - tileState->uls + 4) >> G_TEXTURE_IMAGE_FRAC;
}

static uint32_t gfx_texture_tile_height(int tile) {
    const TextureTileState* tileState = gfx_get_texture_tile(tile);

    return (tileState->lrt - tileState->ult + 4) >> G_TEXTURE_IMAGE_FRAC;
}

static uint32_t gfx_texture_row_bytes(uint32_t width, uint32_t siz) {
    switch (siz) {
        case G_IM_SIZ_4b:
            return (width + 1) >> 1;
        case G_IM_SIZ_8b:
            return width;
        case G_IM_SIZ_16b:
            return width << 1;
        case G_IM_SIZ_32b:
            return width << 2;
        default:
            return width;
    }
}

static uint32_t gfx_ps2_texture_width_from_row_bytes(uint32_t rowBytes, uint32_t siz) {
    switch (siz) {
        case G_IM_SIZ_4b:  return rowBytes << 1;
        case G_IM_SIZ_8b:  return rowBytes;
        case G_IM_SIZ_16b: return rowBytes >> 1;
        case G_IM_SIZ_32b: return rowBytes >> 2;
        default:            return rowBytes;
    }
}

static bool gfx_ps2_try_loaded_texture_dimensions(int tile, uint32_t* width, uint32_t* height) {
    const TextureTileState* tileState = gfx_get_texture_tile(tile);
    const uint32_t rowBytes = rdp.loaded_texture[tile].load_row_bytes != 0
                                  ? rdp.loaded_texture[tile].load_row_bytes
                                  : tileState->line_size_bytes;
    const uint32_t sizeBytes = rdp.loaded_texture[tile].size_bytes;
    uint32_t rowTexels;

    if (rowBytes == 0 || sizeBytes < rowBytes) {
        return false;
    }
    rowTexels = gfx_ps2_texture_width_from_row_bytes(rowBytes, tileState->siz);
    if (rowTexels <= rdp.loaded_texture[tile].source_nibble_offset) {
        return false;
    }
    *width = rowTexels - rdp.loaded_texture[tile].source_nibble_offset;
    *height = sizeBytes / rowBytes;
    return *width != 0 && *height != 0;
}

static void gfx_texture_import_dimensions(int tile, uint32_t* width, uint32_t* height) {
    const TextureTileState* tileState = gfx_get_texture_tile(tile);
    const uint32_t tileWidth = gfx_texture_tile_width(tile);
    const uint32_t tileHeight = gfx_texture_tile_height(tile);
    uint32_t loadedWidth = 0;
    uint32_t loadedHeight = 0;

    *width = tileWidth;
    *height = tileHeight;

    if (!gfx_ps2_try_loaded_texture_dimensions(tile, &loadedWidth, &loadedHeight)) {
        return;
    }

    if ((tileState->cms & G_TX_CLAMP) && loadedWidth < *width) {
        *width = loadedWidth;
    }
    if ((tileState->cmt & G_TX_CLAMP) && loadedHeight < *height) {
        *height = loadedHeight;
    }

    if (!(tileState->cms & G_TX_CLAMP) && tileState->masks != G_TX_NOMASK && tileState->masks < 12) {
        const uint32_t period = 1U << tileState->masks;
        if (period > *width && period <= loadedWidth) {
            *width = period;
        }
    }
    if (!(tileState->cmt & G_TX_CLAMP) && tileState->maskt != G_TX_NOMASK && tileState->maskt < 12) {
        const uint32_t period = 1U << tileState->maskt;
        if (period > *height && period <= loadedHeight) {
            *height = period;
        }
    }
}

static uint32_t gfx_texture_import_width(int tile) {
    uint32_t width;
    uint32_t height;

    gfx_texture_import_dimensions(tile, &width, &height);
    return width;
}

static uint32_t gfx_texture_import_height(int tile) {
    uint32_t width;
    uint32_t height;

    gfx_texture_import_dimensions(tile, &width, &height);
    return height;
}

static uint32_t gfx_texture_byte_offset(uint32_t texel, uint32_t siz) {
    if (siz == G_IM_SIZ_4b) {
        return texel >> 1;
    }

    return gfx_texture_row_bytes(texel, siz);
}

static const uint8_t* gfx_texture_row(int tile, uint32_t y, uint32_t fallbackRowBytes) {
    uint32_t stride = rdp.loaded_texture[tile].row_stride_bytes;

    if (stride == 0) {
        stride = fallbackRowBytes;
    }

    return rdp.loaded_texture[tile].addr + (size_t)y * stride;
}

#define GFX_PS2_TEXTURE_IMPORT_SCRATCH_SIZE (512U * 1024U)
static uint8_t sPs2TextureImportScratch[GFX_PS2_TEXTURE_IMPORT_SCRATCH_SIZE] __attribute__((aligned(64)));

static void* gfx_ps2_texture_import_scratch(size_t size) {
    if (size == 0 || size > sizeof(sPs2TextureImportScratch)) {
        return NULL;
    }
    return sPs2TextureImportScratch;
}

static bool gfx_ps2_validate_texture_import_dimensions(int tile) {
    const uint32_t width = gfx_texture_import_width(tile);
    const uint32_t height = gfx_texture_import_height(tile);

    if (width == 0 || height == 0 ||
        (uint64_t)width * (uint64_t)height * 4ULL > GFX_PS2_TEXTURE_IMPORT_SCRATCH_SIZE) {
        gfx_log_bad_texture_source(tile, "import-texture-dimensions", rdp.loaded_texture[tile].addr,
                                   gfx_texture_source_span_size(tile));
        return false;
    }
    return true;
}

static inline uint16_t gfx_rgba16_to_gu5551(uint16_t color) {
    return ((color & 0x0001U) << 15) | ((color & 0x003EU) << 9) |
           ((color & 0x07C0U) >> 1) | ((color & 0xF800U) >> 11);
}

static void import_texture_rgba16(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint16_t* rgba16_buf = (uint16_t*)gfx_ps2_texture_import_scratch((size_t)width * height * sizeof(uint16_t));
    uint32_t rowBytes = gfx_texture_row_bytes(width, G_IM_SIZ_16b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint16_t col16 = gfx_read_texture_source_be16(row, 2 * x, &swapState);
            rgba16_buf[i] = gfx_rgba16_to_gu5551(col16);
        }
    }

    gfx_upload_texture(tile, (const uint8_t *)rgba16_buf, width, height, PS2_TEXFMT_5551);
}

static void import_texture_rgba32(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint8_t* rgba32_buf = (uint8_t*)gfx_ps2_texture_import_scratch((size_t)width * height * 4U);
    uint32_t rowBytes = gfx_texture_row_bytes(width, G_IM_SIZ_32b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    if ((swapState.mode == GFX_TEXTURE_SWAP_NONE) &&
        (rdp.loaded_texture[tile].row_stride_bytes == 0 || rdp.loaded_texture[tile].row_stride_bytes == rowBytes)) {
        gfx_upload_texture(tile, rdp.loaded_texture[tile].addr, width, height, PS2_TEXFMT_8888);
        return;
    }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < rowBytes; x++) {
            rgba32_buf[y * rowBytes + x] = gfx_read_texture_source_u8(row, x, &swapState);
        }
    }

    gfx_upload_texture(tile, rgba32_buf, width, height, PS2_TEXFMT_8888);
}

static uint8_t gfx_texture_read_4b(int tile, uint32_t x, const uint8_t* row,
                                   GfxTextureSwapState* swapState) {
    uint32_t texel = rdp.loaded_texture[tile].source_nibble_offset + x;
    uint8_t byte = gfx_read_texture_source_u8(row, texel >> 1, swapState);

    return (byte >> (4 - (texel & 1) * 4)) & 0xf;
}

static void import_texture_ia4(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint8_t* rgba32_buf = (uint8_t*)gfx_ps2_texture_import_scratch((size_t)width * height * 4U);
    uint32_t rowBytes = gfx_texture_row_bytes(width + rdp.loaded_texture[tile].source_nibble_offset, G_IM_SIZ_4b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint8_t part = gfx_texture_read_4b(tile, x, row, &swapState);
            uint8_t intensity = part >> 1;
            uint8_t alpha = part & 1;
            uint8_t r = intensity;
            uint8_t g = intensity;
            uint8_t b = intensity;
            rgba32_buf[4*i + 0] = SCALE_3_8(r);
            rgba32_buf[4*i + 1] = SCALE_3_8(g);
            rgba32_buf[4*i + 2] = SCALE_3_8(b);
            rgba32_buf[4*i + 3] = alpha ? 255 : 0;
        }
    }

    gfx_upload_texture(tile, rgba32_buf, width, height, PS2_TEXFMT_8888);
}

static void import_texture_ia8(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint16_t* rgba16_buf = (uint16_t*)gfx_ps2_texture_import_scratch((size_t)width * height * sizeof(uint16_t));
    uint32_t rowBytes = gfx_texture_row_bytes(width, G_IM_SIZ_8b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint8_t texel = gfx_read_texture_source_u8(row, x, &swapState);
            uint16_t intensity = texel >> 4;
            uint16_t alpha = texel & 0xF;

            rgba16_buf[i] = (alpha << 12) | (intensity << 8) | (intensity << 4) | intensity;
        }
    }

    gfx_upload_texture(tile, (const uint8_t*)rgba16_buf, width, height, PS2_TEXFMT_4444);
}

static void import_texture_ia16(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint8_t* rgba32_buf = (uint8_t*)gfx_ps2_texture_import_scratch((size_t)width * height * 4U);
    uint32_t rowBytes = gfx_texture_row_bytes(width, G_IM_SIZ_16b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint8_t intensity = gfx_read_texture_source_u8(row, 2 * x, &swapState);
            uint8_t alpha = gfx_read_texture_source_u8(row, 2 * x + 1, &swapState);
            uint8_t r = intensity;
            uint8_t g = intensity;
            uint8_t b = intensity;
            rgba32_buf[4*i + 0] = r;
            rgba32_buf[4*i + 1] = g;
            rgba32_buf[4*i + 2] = b;
            rgba32_buf[4*i + 3] = alpha;
        }
    }

    gfx_upload_texture(tile, rgba32_buf, width, height, PS2_TEXFMT_8888);
}

static void import_texture_i4(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint16_t* rgba16_buf = (uint16_t*)gfx_ps2_texture_import_scratch((size_t)width * height * sizeof(uint16_t));
    uint32_t rowBytes = gfx_texture_row_bytes(width + rdp.loaded_texture[tile].source_nibble_offset, G_IM_SIZ_4b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint16_t intensity = gfx_texture_read_4b(tile, x, row, &swapState);

            rgba16_buf[i] = (intensity << 12) | (intensity << 8) | (intensity << 4) | intensity;
        }
    }

    gfx_upload_texture(tile, (const uint8_t*)rgba16_buf, width, height, PS2_TEXFMT_4444);
}

static void import_texture_i8(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint8_t* i8_buf = (uint8_t*)gfx_ps2_texture_import_scratch((size_t)width * height);
    uint32_t rowBytes = gfx_texture_row_bytes(width, G_IM_SIZ_8b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    if ((swapState.mode == GFX_TEXTURE_SWAP_NONE) &&
        (rdp.loaded_texture[tile].row_stride_bytes == 0 || rdp.loaded_texture[tile].row_stride_bytes == rowBytes)) {
        gfx_upload_texture(tile, rdp.loaded_texture[tile].addr, width, height, PS2_TEXFMT_T8);
        return;
    }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            i8_buf[y * width + x] = gfx_read_texture_source_u8(row, x, &swapState);
        }
    }

    gfx_upload_texture(tile, i8_buf, width, height, PS2_TEXFMT_T8);
}

static void import_texture_ci4(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint16_t* rgba16_buf = (uint16_t*)gfx_ps2_texture_import_scratch((size_t)width * height * sizeof(uint16_t));
    uint32_t rowBytes = gfx_texture_row_bytes(width + rdp.loaded_texture[tile].source_nibble_offset, G_IM_SIZ_4b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    GfxTextureSwapState paletteSwapState = gfx_texture_source_swap_state(rdp.palette, GFX_CI4_TLUT_SIZE_BYTES);

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint8_t idx = gfx_texture_read_4b(tile, x, row, &swapState);
            uint16_t col16 = gfx_read_texture_source_be16(rdp.palette, idx * 2, &paletteSwapState);
            rgba16_buf[i] = gfx_rgba16_to_gu5551(col16);
        }
    }

    gfx_upload_texture(tile, (const uint8_t*)rgba16_buf, width, height, PS2_TEXFMT_5551);
}

static void import_texture_ci8(int tile) {
    uint32_t width = gfx_texture_import_width(tile);
    uint32_t height = gfx_texture_import_height(tile);
    uint16_t* rgba16_buf = (uint16_t*)gfx_ps2_texture_import_scratch((size_t)width * height * sizeof(uint16_t));
    uint32_t rowBytes = gfx_texture_row_bytes(width, G_IM_SIZ_8b);
    GfxTextureSwapState swapState =
        gfx_texture_source_swap_state(rdp.loaded_texture[tile].addr, gfx_texture_source_span_size(tile));

    GfxTextureSwapState paletteSwapState = gfx_texture_source_swap_state(rdp.palette, GFX_TLUT_SIZE_BYTES);

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(tile, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            uint32_t i = y * width + x;
            uint8_t idx = gfx_read_texture_source_u8(row, x, &swapState);
            uint16_t col16 = gfx_read_texture_source_be16(rdp.palette, idx * 2, &paletteSwapState);
            rgba16_buf[i] = gfx_rgba16_to_gu5551(col16);
        }
    }

    gfx_upload_texture(tile, (const uint8_t*)rgba16_buf, width, height, PS2_TEXFMT_5551);
}

static void import_texture(int tile) {
    const TextureTileState* tileState = gfx_get_texture_tile(tile);
    uint8_t fmt = tileState->fmt;
    uint8_t siz = tileState->siz;
    bool uploadInPlace;

    gfx_validate_texture_source(tile, "import_texture-ps2");
    if (!gfx_ps2_validate_texture_import_dimensions(tile)) {
        return;
    }
    if (fmt == G_IM_FMT_CI) {
        gfx_validate_palette_source("import_texture-palette-ps2");
    }

    if (gfx_texture_cache_lookup(tile, &rendering_state.textures[tile], rdp.loaded_texture[tile].addr,
                                 fmt, siz, false, &uploadInPlace)) {
        return;
    }

    if (!uploadInPlace) {
        if (gfx_texture_cache_lookup(tile, &rendering_state.textures[tile], rdp.loaded_texture[tile].addr,
                                     fmt, siz, true, &uploadInPlace)) {
            return;
        }
    }

    if (fmt == G_IM_FMT_RGBA) {
        if (siz == G_IM_SIZ_16b) {
            import_texture_rgba16(tile);
        } else if (siz == G_IM_SIZ_32b) {
            import_texture_rgba32(tile);
        } else {
            abort();
        }
    } else if (fmt == G_IM_FMT_IA) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ia4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ia8(tile);
        } else if (siz == G_IM_SIZ_16b) {
            import_texture_ia16(tile);
        } else {
            abort();
        }
    } else if (fmt == G_IM_FMT_CI) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ci4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ci8(tile);
        } else {
            abort();
        }
    } else if (fmt == G_IM_FMT_I) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_i4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_i8(tile);
        } else {
            abort();
        }
    } else {
        abort();
    }

}

static bool gfx_intensity_tint_texture_dimensions(int tile, uint32_t* width, uint32_t* height) {
    if (!gfx_ps2_try_loaded_texture_dimensions(tile, width, height)) {
        gfx_texture_import_dimensions(tile, width, height);
    }
    if ((*width == 0) || (*height == 0)) {
        return false;
    }
    const TextureTileState* texture = gfx_get_texture_tile(tile);
    const uint32_t textureWidth = *width + rdp.loaded_texture[tile].source_nibble_offset;
    const uint32_t rowBytes = gfx_texture_row_bytes(textureWidth, texture->siz);
    const uint32_t stride = rdp.loaded_texture[tile].row_stride_bytes != 0
                                ? rdp.loaded_texture[tile].row_stride_bytes
                                : rowBytes;
    const uint64_t span = (uint64_t)(*height - 1U) * stride + rowBytes;
    return rowBytes != 0 && span <= gfx_loaded_texture_source_size(tile);
}

static bool gfx_intensity_tint_texture_is_supported(int tile) {
    const TextureTileState* texture = gfx_get_texture_tile(tile);
    uint32_t width;
    uint32_t height;

    if ((texture->fmt != G_IM_FMT_I) ||
        ((texture->siz != G_IM_SIZ_4b) && (texture->siz != G_IM_SIZ_8b))) {
        return false;
    }

    return gfx_intensity_tint_texture_dimensions(tile, &width, &height);
}

static bool gfx_two_intensity_tint_textures_are_supported(void) {
    return gfx_intensity_tint_texture_is_supported(0) && gfx_intensity_tint_texture_is_supported(1);
}

#define GFX_INTENSITY_TINT_CACHE_SIZE 16
#define GFX_INTENSITY_TINT_CACHE_SINGLE_PRIM_LOD 0
#define GFX_INTENSITY_TINT_CACHE_TWO_PRIM_LOD 1
#define GFX_INTENSITY_TINT_CACHE_SINGLE_ENV_ALPHA 2

typedef struct GfxIntensityTintCacheEntry {
    const uint8_t* addr;
    uint32_t sourceSerial;
    uint32_t sourceSpan;
    uint32_t rowStride;
    uint32_t sizeBytes;
    uint32_t width;
    uint32_t height;
    struct RGBA inputPrimColor;
    struct RGBA inputEnvColor;
    struct RGBA outputPrimColor;
    struct RGBA outputEnvColor;
    uint8_t sourceNibbleOffset;
    uint8_t siz;
    uint8_t tintFactor;
    uint8_t mode;
    bool valid;
} GfxIntensityTintCacheEntry;

static GfxIntensityTintCacheEntry sIntensityTintCache[GFX_INTENSITY_TINT_CACHE_SIZE];
static uint8_t sIntensityTintCacheNext;

static bool gfx_intensity_tint_cache_lookup(uint8_t mode, uint8_t tintFactor, int tile, uint32_t width,
                                            uint32_t height, struct RGBA* primColor, struct RGBA* envColor) {
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    const uint32_t sourceSpan = gfx_texture_source_span_size(tile);
    const uint32_t sourceSerial = OotPort_GetExternalAssetRangeSerial(addr, sourceSpan);

    if ((sourceSerial == 0) && OotPort_IsRuntimeByteRange(addr, sourceSpan)) {
        return false;
    }

    for (uint32_t i = 0; i < GFX_INTENSITY_TINT_CACHE_SIZE; i++) {
        const GfxIntensityTintCacheEntry* entry = &sIntensityTintCache[i];

        if (entry->valid && (entry->mode == mode) && (entry->addr == addr) &&
            (entry->sourceSerial == sourceSerial) && (entry->sourceSpan == sourceSpan) &&
            (entry->rowStride == rdp.loaded_texture[tile].row_stride_bytes) &&
            (entry->sizeBytes == rdp.loaded_texture[tile].size_bytes) && (entry->width == width) &&
            (entry->height == height) &&
            (entry->sourceNibbleOffset == rdp.loaded_texture[tile].source_nibble_offset) &&
            (entry->siz == gfx_get_texture_tile(tile)->siz) && (entry->tintFactor == tintFactor) &&
            (entry->inputPrimColor.r == rdp.prim_color.r) &&
            (entry->inputPrimColor.g == rdp.prim_color.g) &&
            (entry->inputPrimColor.b == rdp.prim_color.b) &&
            (entry->inputEnvColor.r == rdp.env_color.r) &&
            (entry->inputEnvColor.g == rdp.env_color.g) &&
            (entry->inputEnvColor.b == rdp.env_color.b)) {
            *primColor = entry->outputPrimColor;
            *envColor = entry->outputEnvColor;
            return true;
        }
    }

    return false;
}

static void gfx_intensity_tint_cache_store(uint8_t mode, uint8_t tintFactor, int tile, uint32_t width,
                                           uint32_t height, const struct RGBA* primColor,
                                           const struct RGBA* envColor) {
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    const uint32_t sourceSpan = gfx_texture_source_span_size(tile);
    const uint32_t sourceSerial = OotPort_GetExternalAssetRangeSerial(addr, sourceSpan);
    GfxIntensityTintCacheEntry* entry;

    if ((sourceSerial == 0) && OotPort_IsRuntimeByteRange(addr, sourceSpan)) {
        return;
    }

    entry = &sIntensityTintCache[sIntensityTintCacheNext];
    sIntensityTintCacheNext = (sIntensityTintCacheNext + 1) % GFX_INTENSITY_TINT_CACHE_SIZE;
    entry->addr = addr;
    entry->sourceSerial = sourceSerial;
    entry->sourceSpan = sourceSpan;
    entry->rowStride = rdp.loaded_texture[tile].row_stride_bytes;
    entry->sizeBytes = rdp.loaded_texture[tile].size_bytes;
    entry->width = width;
    entry->height = height;
    entry->inputPrimColor = rdp.prim_color;
    entry->inputEnvColor = rdp.env_color;
    entry->outputPrimColor = *primColor;
    entry->outputEnvColor = *envColor;
    entry->sourceNibbleOffset = rdp.loaded_texture[tile].source_nibble_offset;
    entry->siz = gfx_get_texture_tile(tile)->siz;
    entry->tintFactor = tintFactor;
    entry->mode = mode;
    entry->valid = true;
}

static int32_t gfx_div_round_nearest_s64(int64_t numerator, int64_t denominator) {
    return numerator >= 0 ? (numerator + denominator / 2) / denominator
                          : -((-numerator + denominator / 2) / denominator);
}

static bool gfx_get_single_texture_tint_colors(uint8_t tintFactor, uint8_t cacheMode,
                                               struct RGBA* primColor, struct RGBA* envColor) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    uint64_t intensitySum = 0;
    uint64_t intensitySquaredSum = 0;
    int64_t colorSum[3] = { 0 };
    int64_t intensityColorSum[3] = { 0 };
    uint32_t width;
    uint32_t height;
    uint32_t texelCount;
    uint32_t rowBytes;
    GfxTextureSwapState swapState;

    if (!gfx_intensity_tint_texture_is_supported(0) ||
        !gfx_validate_texture_source(0, "single-texture-tint-texture0") ||
        !gfx_intensity_tint_texture_dimensions(0, &width, &height) ||
        (height > (UINT32_MAX / width))) {
        return false;
    }

    if (gfx_intensity_tint_cache_lookup(cacheMode, tintFactor, 0, width, height, primColor, envColor)) {
        return true;
    }

    texelCount = width * height;
    rowBytes = gfx_texture_row_bytes(width + rdp.loaded_texture[0].source_nibble_offset, texture0->siz);
    swapState = gfx_texture_source_swap_state(rdp.loaded_texture[0].addr, gfx_texture_source_span_size(0));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(0, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            const int32_t intensity = texture0->siz == G_IM_SIZ_4b
                                          ? gfx_texture_read_4b(0, x, row, &swapState) * 0x11
                                          : gfx_read_texture_source_u8(row, x, &swapState);

            intensitySum += intensity;
            intensitySquaredSum += intensity * intensity;
            for (uint32_t channel = 0; channel < 3; channel++) {
                const int32_t prim = ((const uint8_t*)&rdp.prim_color)[channel];
                const int32_t env = ((const uint8_t*)&rdp.env_color)[channel];
                int32_t combinedNumerator =
                    intensity * 256 + (intensity - prim) * tintFactor;

                combinedNumerator = combinedNumerator < 0 ? 0 :
                                    (combinedNumerator > 255 * 256 ? 255 * 256 : combinedNumerator);
                const int32_t combined = (combinedNumerator + 128) / 256;
                const int32_t color =
                    env + gfx_div_round_nearest_s64((int64_t)(prim - env) * combined, 255);

                colorSum[channel] += color;
                intensityColorSum[channel] += intensity * color;
            }
        }
    }

    const int64_t denominator =
        (int64_t)texelCount * intensitySquaredSum - (int64_t)intensitySum * intensitySum;
    if (denominator == 0) {
        return false;
    }

    *primColor = rdp.prim_color;
    *envColor = rdp.env_color;

    for (uint32_t channel = 0; channel < 3; channel++) {
        const int64_t slopeNumerator =
            (int64_t)texelCount * intensityColorSum[channel] -
            (int64_t)intensitySum * colorSum[channel];
        const int64_t envNumerator =
            colorSum[channel] * (int64_t)intensitySquaredSum -
            (int64_t)intensitySum * intensityColorSum[channel];
        int32_t correctedEnv = gfx_div_round_nearest_s64(envNumerator, denominator);
        int32_t correctedPrim =
            gfx_div_round_nearest_s64(envNumerator + 255 * slopeNumerator, denominator);

        correctedPrim = correctedPrim < 0 ? 0 : (correctedPrim > 255 ? 255 : correctedPrim);
        correctedEnv = correctedEnv < 0 ? 0 : (correctedEnv > 255 ? 255 : correctedEnv);
        ((uint8_t*)primColor)[channel] = correctedPrim;
        ((uint8_t*)envColor)[channel] = correctedEnv;
    }

    gfx_intensity_tint_cache_store(cacheMode, tintFactor, 0, width, height, primColor, envColor);

    return true;
}

static bool gfx_get_two_texture_prim_lod_tint_colors(struct RGBA* primColor, struct RGBA* envColor) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    uint32_t width;
    uint32_t height;
    uint32_t rowBytes;
    uint64_t intensitySum = 0;
    uint32_t texelCount;
    uint8_t meanIntensity;
    GfxTextureSwapState swapState;

    if (!gfx_two_intensity_tint_textures_are_supported() ||
        !gfx_validate_texture_source(0, "two-prim-lod-tint-texture0")) {
        return false;
    }

    if (!gfx_intensity_tint_texture_dimensions(0, &width, &height) ||
        (height > (UINT32_MAX / width))) {
        return false;
    }

    if (gfx_intensity_tint_cache_lookup(GFX_INTENSITY_TINT_CACHE_TWO_PRIM_LOD, rdp.prim_lod_frac, 0,
                                        width, height, primColor, envColor)) {
        return true;
    }

    texelCount = width * height;
    rowBytes = gfx_texture_row_bytes(width + rdp.loaded_texture[0].source_nibble_offset, texture0->siz);
    swapState = gfx_texture_source_swap_state(rdp.loaded_texture[0].addr, gfx_texture_source_span_size(0));

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = gfx_texture_row(0, y, rowBytes);

        for (uint32_t x = 0; x < width; x++) {
            if (texture0->siz == G_IM_SIZ_4b) {
                intensitySum += gfx_texture_read_4b(0, x, row, &swapState) * 0x11U;
            } else {
                intensitySum += gfx_read_texture_source_u8(row, x, &swapState);
            }
        }
    }

    meanIntensity = (intensitySum + texelCount / 2) / texelCount;
    *primColor = rdp.prim_color;
    *envColor = rdp.env_color;

    for (uint32_t channel = 0; channel < 3; channel++) {
        const int32_t prim = ((const uint8_t*)&rdp.prim_color)[channel];
        const int32_t env = ((const uint8_t*)&rdp.env_color)[channel];
        const int64_t numerator =
            (int64_t)(prim - env) * (meanIntensity - prim) * rdp.prim_lod_frac;
        const int32_t denominator = 255 * 256;
        const int32_t correction = numerator >= 0 ? (numerator + denominator / 2) / denominator
                                                   : -((-numerator + denominator / 2) / denominator);
        int32_t correctedPrim = prim + correction;
        int32_t correctedEnv = env + correction;

        correctedPrim = correctedPrim < 0 ? 0 : (correctedPrim > 255 ? 255 : correctedPrim);
        correctedEnv = correctedEnv < 0 ? 0 : (correctedEnv > 255 ? 255 : correctedEnv);
        ((uint8_t*)primColor)[channel] = correctedPrim;
        ((uint8_t*)envColor)[channel] = correctedEnv;
    }

    gfx_intensity_tint_cache_store(GFX_INTENSITY_TINT_CACHE_TWO_PRIM_LOD, rdp.prim_lod_frac, 0, width,
                                   height, primColor, envColor);

    return true;
}

static bool gfx_flame_atlas_material_is_supported(void) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    const TextureTileState* texture1 = gfx_get_texture_tile(1);
    return (texture0->fmt == G_IM_FMT_I) && (texture0->siz == G_IM_SIZ_8b) &&
           (texture1->fmt == G_IM_FMT_I) && (texture1->siz == G_IM_SIZ_4b) &&
           (gfx_texture_import_width(0) == GFX_FLAME_ATLAS_FRAME_WIDTH) &&
           (gfx_texture_import_height(0) == GFX_FLAME_ATLAS_FRAME_HEIGHT) &&
           (gfx_texture_import_width(1) == GFX_FLAME_ATLAS_FRAME_WIDTH) &&
           (gfx_texture_import_height(1) == (GFX_FLAME_ATLAS_PHASE_STEP * GFX_FLAME_ATLAS_COLUMNS * GFX_FLAME_ATLAS_ROWS));
}
static inline uint8_t gfx_ps2_clamp_u8_i32(int32_t v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline int32_t gfx_ps2_div255_round(int32_t v) {

    if (v >= 0) {
        const uint32_t x = (uint32_t)v + 128U;
        return (int32_t)((x + (x >> 8)) >> 8);
    }
    {
        const uint32_t x = (uint32_t)(-v) + 128U;
        return -(int32_t)((x + (x >> 8)) >> 8);
    }
}

static void gfx_build_flame_atlas(FlameAtlasCacheEntry* entry) {
    const uint32_t row0 = gfx_texture_row_bytes(GFX_FLAME_ATLAS_FRAME_WIDTH, G_IM_SIZ_8b);
    const uint32_t row1 = gfx_texture_row_bytes(GFX_FLAME_ATLAS_FRAME_WIDTH, G_IM_SIZ_4b);
    const uint32_t span0 = gfx_texture_source_span_size(0);
    const uint32_t span1 = gfx_texture_source_span_size(1);
    const uint8_t* addr0 = rdp.loaded_texture[0].addr;
    const uint8_t* addr1 = rdp.loaded_texture[1].addr;
    const uint32_t serial0 = OotPort_GetExternalAssetRangeSerial(addr0, span0);
    const uint32_t serial1 = OotPort_GetExternalAssetRangeSerial(addr1, span1);
    const bool runtime0 = (serial0 == 0U) && OotPort_IsRuntimeByteRange(addr0, span0);
    const bool runtime1 = (serial1 == 0U) && OotPort_IsRuntimeByteRange(addr1, span1);

    const uint32_t sourceKey0 = serial0 != 0U ? serial0 :
        (runtime0 ? gfx_ps2_dynamic_texture_content_key(addr0, span0) : 1U);
    const uint32_t sourceKey1 = serial1 != 0U ? serial1 :
        (runtime1 ? gfx_ps2_dynamic_texture_content_key(addr1, span1) : 1U);
    const int32_t pr = (int32_t)(entry->primRgb & 0xffU);
    const int32_t pg = (int32_t)((entry->primRgb >> 8) & 0xffU);
    const int32_t pb = (int32_t)((entry->primRgb >> 16) & 0xffU);
    const int32_t er = (int32_t)(entry->envRgb & 0xffU);
    const int32_t eg = (int32_t)((entry->envRgb >> 8) & 0xffU);
    const int32_t eb = (int32_t)((entry->envRgb >> 16) & 0xffU);
    const int32_t lod = entry->primLodFrac;
    bool sourceChanged = false;

    if (sPs2FlameDecodedI8Addr != addr0 || sPs2FlameDecodedI8Serial != sourceKey0) {
        sourceChanged = true;
        GfxTextureSwapState swap0 = gfx_texture_source_swap_state(addr0, span0);
        for (uint32_t y = 0; y < GFX_FLAME_ATLAS_FRAME_HEIGHT; y++) {
            const uint8_t* src = gfx_texture_row(0, y, row0);
            uint8_t* dst = &sPs2FlameDecodedI8[y * GFX_FLAME_ATLAS_FRAME_WIDTH];
            if (swap0.mode == GFX_TEXTURE_SWAP_NONE) {
                memcpy(dst, src, GFX_FLAME_ATLAS_FRAME_WIDTH);
            } else {
                for (uint32_t x = 0; x < GFX_FLAME_ATLAS_FRAME_WIDTH; x++) {
                    dst[x] = gfx_read_texture_source_u8(src, x, &swap0);
                }
            }
        }
        sPs2FlameDecodedI8Addr = addr0;
        sPs2FlameDecodedI8Serial = sourceKey0;
    }

    if (sPs2FlameDecodedI4Addr != addr1 || sPs2FlameDecodedI4Serial != sourceKey1) {
        sourceChanged = true;
        GfxTextureSwapState swap1 = gfx_texture_source_swap_state(addr1, span1);
        for (uint32_t y = 0; y < 128U; y++) {
            const uint8_t* src = gfx_texture_row(1, y, row1);
            uint8_t* dst = &sPs2FlameDecodedI4[y * GFX_FLAME_ATLAS_FRAME_WIDTH];
            if (swap1.mode == GFX_TEXTURE_SWAP_NONE) {
                const uint32_t nibbleBase = rdp.loaded_texture[1].source_nibble_offset;
                for (uint32_t x = 0; x < GFX_FLAME_ATLAS_FRAME_WIDTH; x++) {
                    const uint32_t texel = nibbleBase + x;
                    const uint8_t packed = src[texel >> 1];
                    dst[x] = (packed >> (4 - (texel & 1U) * 4U)) & 0x0fU;
                }
            } else {
                for (uint32_t x = 0; x < GFX_FLAME_ATLAS_FRAME_WIDTH; x++) {
                    dst[x] = gfx_texture_read_4b(1, x, src, &swap1);
                }
            }
        }
        sPs2FlameDecodedI4Addr = addr1;
        sPs2FlameDecodedI4Serial = sourceKey1;
    }

    for (uint32_t t0 = 0; t0 < 256U; t0++) {
        for (uint32_t noise = 0; noise < 16U; noise++) {
            const int32_t t1 = (int32_t)(noise * 0x11U);
            int32_t cr = ((int32_t)t0 * 256 + (t1 - pr) * lod + 128) >> 8;
            int32_t cg = ((int32_t)t0 * 256 + (t1 - pg) * lod + 128) >> 8;
            int32_t cb = ((int32_t)t0 * 256 + (t1 - pb) * lod + 128) >> 8;
            cr = gfx_ps2_clamp_u8_i32(cr);
            cg = gfx_ps2_clamp_u8_i32(cg);
            cb = gfx_ps2_clamp_u8_i32(cb);
            const uint8_t r = gfx_ps2_clamp_u8_i32(er + gfx_ps2_div255_round((pr - er) * cr));
            const uint8_t g = gfx_ps2_clamp_u8_i32(eg + gfx_ps2_div255_round((pg - eg) * cg));
            const uint8_t bl = gfx_ps2_clamp_u8_i32(eb + gfx_ps2_div255_round((pb - eb) * cb));
            const int32_t ca = ((int32_t)t0 * 256 + (t1 - 255) * lod + 128) >> 8;
            const uint8_t alpha = gfx_ps2_clamp_u8_i32(ca);

            const uint8_t gsAlpha = (uint8_t)((alpha + 1U) >> 1);
            sPs2FlameColorLut[(t0 << 4) | noise] =
                (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | ((uint32_t)gsAlpha << 24);
        }
    }

    if (sourceChanged || !sPs2FlameIndexAtlasValid) {

        for (uint32_t phase = 0; phase < GFX_FLAME_ATLAS_COLUMNS * GFX_FLAME_ATLAS_ROWS; phase++) {
            const uint32_t ax = (phase % GFX_FLAME_ATLAS_COLUMNS) * GFX_FLAME_ATLAS_FRAME_WIDTH;
            const uint32_t ay = (phase / GFX_FLAME_ATLAS_COLUMNS) * GFX_FLAME_ATLAS_FRAME_HEIGHT;
            const uint32_t off = phase * GFX_FLAME_ATLAS_PHASE_STEP;
            for (uint32_t y = 0; y < GFX_FLAME_ATLAS_FRAME_HEIGHT; y++) {
                const uint8_t* a = &sPs2FlameDecodedI8[y * GFX_FLAME_ATLAS_FRAME_WIDTH];
                const uint8_t* b = &sPs2FlameDecodedI4[((y - off) & 0x7fU) * GFX_FLAME_ATLAS_FRAME_WIDTH];
                const uint32_t base = (ay + y) * GFX_FLAME_ATLAS_WIDTH + ax;
                uint16_t* idx = &sPs2FlameIndexAtlas[base];
                uint32_t* dst = &entry->pixels[base];
                for (uint32_t x = 0; x < GFX_FLAME_ATLAS_FRAME_WIDTH; x++) {
                    const uint16_t key = (uint16_t)(((uint32_t)a[x] << 4) | b[x]);
                    idx[x] = key;
                    dst[x] = sPs2FlameColorLut[key];
                }
            }
        }
        sPs2FlameIndexAtlasValid = true;
    } else {
        const uint16_t* idx = sPs2FlameIndexAtlas;
        uint32_t* dst = entry->pixels;
        for (uint32_t i = 0; i < GFX_FLAME_ATLAS_SIZE; i++) {
            dst[i] = sPs2FlameColorLut[idx[i]];
        }
    }
}

static bool gfx_prepare_flame_atlas(void) {
    FlameAtlasCacheEntry* entry = NULL;
    FlameAtlasCacheEntry* victim = &sFlameAtlasCache[0];
    for (uint32_t i = 0; i < GFX_FLAME_ATLAS_CACHE_SIZE; i++) {
        FlameAtlasCacheEntry* c = &sFlameAtlasCache[i];
        const uint32_t primRgb = (uint32_t)rdp.prim_color.r | ((uint32_t)rdp.prim_color.g << 8) |
                                 ((uint32_t)rdp.prim_color.b << 16);
        const uint32_t envRgb = (uint32_t)rdp.env_color.r | ((uint32_t)rdp.env_color.g << 8) |
                                ((uint32_t)rdp.env_color.b << 16);
        if (c->pixelsValid && c->texture0Addr == rdp.loaded_texture[0].addr &&
            c->texture1Addr == rdp.loaded_texture[1].addr && c->primLodFrac == rdp.prim_lod_frac &&
            c->primRgb == primRgb && c->envRgb == envRgb) { entry = c; break; }
        if (!c->pixelsValid || (victim->pixelsValid && c->lastUsed < victim->lastUsed)) victim = c;
    }
    if (entry && entry->textureValid) { entry->lastUsed = ++sFlameAtlasUseClock; sPreparedFlameAtlas = entry; return true; }
    if (!gfx_flame_atlas_material_is_supported() ||
        !gfx_validate_texture_source(0, "ps2-flame0") || !gfx_validate_texture_source(1, "ps2-flame1")) return false;
    if (!entry) entry = victim;
    gfx_flush();
    if (entry->textureId == 0) {

        if (!texman_vram_space_available(GFX_FLAME_ATLAS_SIZE * 4U) || !texman_texture_slot_available()) gfx_texture_cache_clear();
        entry->textureId = gfx_rapi->new_texture();
    } else gfx_rapi->select_texture(0, entry->textureId);
    const uint32_t primRgb = (uint32_t)rdp.prim_color.r | ((uint32_t)rdp.prim_color.g << 8) |
                             ((uint32_t)rdp.prim_color.b << 16);
    const uint32_t envRgb = (uint32_t)rdp.env_color.r | ((uint32_t)rdp.env_color.g << 8) |
                            ((uint32_t)rdp.env_color.b << 16);
    if (!entry->pixelsValid || entry->texture0Addr != rdp.loaded_texture[0].addr ||
        entry->texture1Addr != rdp.loaded_texture[1].addr || entry->primLodFrac != rdp.prim_lod_frac ||
        entry->primRgb != primRgb || entry->envRgb != envRgb) {
        entry->texture0Addr = rdp.loaded_texture[0].addr;
        entry->texture1Addr = rdp.loaded_texture[1].addr;
        entry->primLodFrac = rdp.prim_lod_frac;
        entry->primRgb = primRgb;
        entry->envRgb = envRgb;
        gfx_build_flame_atlas(entry);
        entry->pixelsValid = true;
    }
    gfx_rapi->upload_texture((const uint8_t*)entry->pixels, GFX_FLAME_ATLAS_WIDTH,
                             GFX_FLAME_ATLAS_HEIGHT, PS2_TEXFMT_8888_GS_NATIVE);
    entry->textureValid = true; entry->lastUsed = ++sFlameAtlasUseClock; sPreparedFlameAtlas = entry; return true;
}

static bool gfx_two_i4_precombine_material_is_supported(void) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    const TextureTileState* texture1 = gfx_get_texture_tile(1);
    const uint32_t width0 = gfx_texture_tile_width(0);
    const uint32_t height0 = gfx_texture_tile_height(0);
    const uint32_t width1 = gfx_texture_tile_width(1);
    const uint32_t height1 = gfx_texture_tile_height(1);
    const bool texture0Intensity =
        (texture0->fmt == G_IM_FMT_I) &&
        ((texture0->siz == G_IM_SIZ_4b) || (texture0->siz == G_IM_SIZ_8b));
    const bool texture1Intensity =
        (texture1->fmt == G_IM_FMT_I) &&
        ((texture1->siz == G_IM_SIZ_4b) || (texture1->siz == G_IM_SIZ_8b));

    if (!texture0Intensity || !texture1Intensity || width0 == 0 || height0 == 0 ||
        width1 == 0 || height1 == 0 || width0 > GFX_TWO_I4_PRECOMBINE_WIDTH ||
        width1 > GFX_TWO_I4_PRECOMBINE_WIDTH || height1 > GFX_TWO_I4_PRECOMBINE_HEIGHT ||
        height0 > 128) {
        return false;
    }

    return gfx_texture_import_width(0) >= width0 && gfx_texture_import_height(0) >= height0 &&
           gfx_texture_import_width(1) >= width1 && gfx_texture_import_height(1) >= height1;
}

static inline float gfx_texture_shift_scale(uint8_t shift);

static inline int32_t gfx_ps2_div256_round_signed(int32_t value) {
    return value >= 0 ? (value + 128) >> 8 : -(((-value) + 128) >> 8);
}

static int32_t gfx_two_i4_address_coord(int32_t coord, uint32_t extent, uint8_t cm, uint8_t mask) {
    if (extent == 0) {
        return 0;
    }
    if ((cm & G_TX_CLAMP) != 0) {
        if (coord < 0) return 0;
        if ((uint32_t)coord >= extent) return (int32_t)extent - 1;
        return coord;
    }

    uint32_t period = (mask != G_TX_NOMASK && mask < 12) ? (1U << mask) : extent;
    if (period == 0) period = extent;
    int32_t wrapped = coord % (int32_t)period;
    if (wrapped < 0) wrapped += (int32_t)period;

    if ((cm & G_TX_MIRROR) != 0) {
        const int32_t mirrorPeriod = (int32_t)period * 2;
        int32_t m = coord % mirrorPeriod;
        if (m < 0) m += mirrorPeriod;
        if (m >= (int32_t)period) m = mirrorPeriod - 1 - m;
        wrapped = m;
    }
    if ((uint32_t)wrapped >= extent) wrapped %= (int32_t)extent;
    return wrapped;
}

static uint8_t gfx_two_i4_read_texel_255(int tile, int32_t x, int32_t y,
                                         uint32_t width, uint32_t height,
                                         GfxTextureSwapState* swapState) {
    const TextureTileState* texture = gfx_get_texture_tile(tile);
    x = gfx_two_i4_address_coord(x, width, texture->cms, texture->masks);
    y = gfx_two_i4_address_coord(y, height, texture->cmt, texture->maskt);
    const uint32_t rowBytes = gfx_texture_row_bytes(width + rdp.loaded_texture[tile].source_nibble_offset,
                                                    texture->siz);
    const uint8_t* row = gfx_texture_row(tile, (uint32_t)y, rowBytes);
    if (texture->siz == G_IM_SIZ_4b) {
        return (uint8_t)(gfx_texture_read_4b(tile, (uint32_t)x, row, swapState) * 17U);
    }
    return gfx_read_texture_source_u8(row, (uint32_t)x, swapState);
}

static void gfx_two_i4_build_sample_maps(uint8_t sample0X[GFX_TWO_I4_PRECOMBINE_WIDTH],
                                         uint8_t sample0Y[GFX_TWO_I4_PRECOMBINE_HEIGHT],
                                         uint8_t sample1X[GFX_TWO_I4_PRECOMBINE_WIDTH],
                                         uint8_t sample1Y[GFX_TWO_I4_PRECOMBINE_HEIGHT],
                                         uint32_t outputWidth, uint32_t outputHeight) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    const TextureTileState* texture1 = gfx_get_texture_tile(1);
    const uint32_t width0 = gfx_texture_import_width(0);
    const uint32_t height0 = gfx_texture_import_height(0);
    const uint32_t width1 = gfx_texture_import_width(1);
    const uint32_t height1 = gfx_texture_import_height(1);
    const float shift0S = gfx_texture_shift_scale(texture0->shifts);
    const float shift0T = gfx_texture_shift_scale(texture0->shiftt);
    const float shift1S = gfx_texture_shift_scale(texture1->shifts);
    const float shift1T = gfx_texture_shift_scale(texture1->shiftt);
    const float scroll0S = (float)texture0->uls * (1.0f / 4.0f);
    const float scroll0T = (float)texture0->ult * (1.0f / 4.0f);
    const float scroll1S = (float)texture1->uls * (1.0f / 4.0f);
    const float scroll1T = (float)texture1->ult * (1.0f / 4.0f);

    for (uint32_t x = 0; x < outputWidth; x++) {
        const float incomingS = ((float)x + scroll1S) / shift1S;
        const int32_t x0 = (int32_t)floorf(incomingS * shift0S - scroll0S + 0.5f);
        sample0X[x] = (uint8_t)gfx_two_i4_address_coord(x0, width0, texture0->cms, texture0->masks);
        sample1X[x] = (uint8_t)gfx_two_i4_address_coord((int32_t)x, width1, texture1->cms, texture1->masks);
    }
    for (uint32_t y = 0; y < outputHeight; y++) {
        const float incomingT = ((float)y + scroll1T) / shift1T;
        const int32_t y0 = (int32_t)floorf(incomingT * shift0T - scroll0T + 0.5f);
        sample0Y[y] = (uint8_t)gfx_two_i4_address_coord(y0, height0, texture0->cmt, texture0->maskt);
        sample1Y[y] = (uint8_t)gfx_two_i4_address_coord((int32_t)y, height1, texture1->cmt, texture1->maskt);
    }
}
static bool gfx_two_i4_precombine_key_matches(const TwoI4PrecombineCacheEntry* entry,
                                               uint32_t primRgb, uint32_t envRgb,
                                               const uint8_t* sample0X, const uint8_t* sample0Y,
                                               const uint8_t* sample1X, const uint8_t* sample1Y,
                                               uint32_t outputWidth, uint32_t outputHeight) {
    return entry->textureValid &&
           entry->texture0Addr == rdp.loaded_texture[0].addr &&
           entry->texture1Addr == rdp.loaded_texture[1].addr &&
           entry->primRgb == primRgb && entry->envRgb == envRgb &&
           entry->envAlpha == rdp.env_color.a &&
           entry->outputWidth == outputWidth && entry->outputHeight == outputHeight &&
           entry->importWidth0 == gfx_texture_import_width(0) &&
           entry->importHeight0 == gfx_texture_import_height(0) &&
           entry->importWidth1 == gfx_texture_import_width(1) &&
           entry->importHeight1 == gfx_texture_import_height(1) &&
           entry->fmt0 == gfx_get_texture_tile(0)->fmt && entry->siz0 == gfx_get_texture_tile(0)->siz &&
           entry->fmt1 == gfx_get_texture_tile(1)->fmt && entry->siz1 == gfx_get_texture_tile(1)->siz &&
           entry->nibble0 == rdp.loaded_texture[0].source_nibble_offset &&
           entry->nibble1 == rdp.loaded_texture[1].source_nibble_offset &&
           memcmp(entry->sample0X, sample0X, outputWidth) == 0 &&
           memcmp(entry->sample1X, sample1X, outputWidth) == 0 &&
           memcmp(entry->sample0Y, sample0Y, outputHeight) == 0 &&
           memcmp(entry->sample1Y, sample1Y, outputHeight) == 0;
}

static void gfx_build_two_i4_precombine_pixels(TwoI4PrecombineCacheEntry* entry) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    const TextureTileState* texture1 = gfx_get_texture_tile(1);
    const uint32_t width0 = gfx_texture_import_width(0);
    const uint32_t height0 = gfx_texture_import_height(0);
    const uint32_t width1 = gfx_texture_import_width(1);
    const uint32_t height1 = gfx_texture_import_height(1);
    const uint8_t* sample0X = entry->sample0X;
    const uint8_t* sample0Y = entry->sample0Y;
    const uint8_t* sample1X = entry->sample1X;
    const uint8_t* sample1Y = entry->sample1Y;
    const int32_t pr = (int32_t)(entry->primRgb & 0xffU);
    const int32_t pg = (int32_t)((entry->primRgb >> 8) & 0xffU);
    const int32_t pb = (int32_t)((entry->primRgb >> 16) & 0xffU);
    const int32_t er = (int32_t)(entry->envRgb & 0xffU);
    const int32_t eg = (int32_t)((entry->envRgb >> 8) & 0xffU);
    const int32_t eb = (int32_t)((entry->envRgb >> 16) & 0xffU);
    const int32_t envA = entry->envAlpha;
    GfxTextureSwapState swap0 =
        gfx_texture_source_swap_state(rdp.loaded_texture[0].addr, gfx_texture_source_span_size(0));
    GfxTextureSwapState swap1 =
        gfx_texture_source_swap_state(rdp.loaded_texture[1].addr, gfx_texture_source_span_size(1));

    const uint32_t outputWidth = entry->outputWidth;
    const uint32_t outputHeight = entry->outputHeight;
    for (uint32_t y = 0; y < outputHeight; y++) {
        for (uint32_t x = 0; x < outputWidth; x++) {

            const uint8_t t0 = gfx_two_i4_read_texel_255(0, sample0X[x], sample0Y[y], width0, height0, &swap0);
            const uint8_t t1 = gfx_two_i4_read_texel_255(1, sample1X[x], sample1Y[y], width1, height1, &swap1);

            const int32_t rgbMix = gfx_ps2_clamp_u8_i32((int32_t)t1 +
                gfx_ps2_div256_round_signed((int32_t)t0 * envA));
            const int32_t alphaMix = gfx_ps2_clamp_u8_i32((int32_t)t1 +
                gfx_ps2_div256_round_signed(((int32_t)t0 - 255) * envA));
            const uint8_t r = gfx_ps2_clamp_u8_i32(er + gfx_ps2_div255_round((pr - er) * rgbMix));
            const uint8_t g = gfx_ps2_clamp_u8_i32(eg + gfx_ps2_div255_round((pg - eg) * rgbMix));
            const uint8_t b = gfx_ps2_clamp_u8_i32(eb + gfx_ps2_div255_round((pb - eb) * rgbMix));
            entry->pixels[y * outputWidth + x] =
                (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)alphaMix << 24);
        }
    }
}

static bool gfx_prepare_two_i4_precombine(void) {
    const uint32_t primRgb = (uint32_t)rdp.prim_color.r | ((uint32_t)rdp.prim_color.g << 8) |
                             ((uint32_t)rdp.prim_color.b << 16);
    const uint32_t envRgb = (uint32_t)rdp.env_color.r | ((uint32_t)rdp.env_color.g << 8) |
                            ((uint32_t)rdp.env_color.b << 16);
    TwoI4PrecombineCacheEntry* entry = NULL;
    TwoI4PrecombineCacheEntry* victim = NULL;
    uint32_t oldestFrame = UINT32_MAX;
    bool needsUpload = false;
    bool overwriteResident = false;
    uint8_t sample0X[GFX_TWO_I4_PRECOMBINE_WIDTH] = { 0 };
    uint8_t sample0Y[GFX_TWO_I4_PRECOMBINE_HEIGHT] = { 0 };
    uint8_t sample1X[GFX_TWO_I4_PRECOMBINE_WIDTH] = { 0 };
    uint8_t sample1Y[GFX_TWO_I4_PRECOMBINE_HEIGHT] = { 0 };
    uint32_t outputWidth;
    uint32_t outputHeight;

    if (!gfx_two_i4_precombine_material_is_supported() ||
        !gfx_validate_texture_source(0, "ps2-two-i4-0") ||
        !gfx_validate_texture_source(1, "ps2-two-i4-1")) {
        return false;
    }

    outputWidth = gfx_texture_tile_width(1);
    outputHeight = gfx_texture_tile_height(1);
    gfx_two_i4_build_sample_maps(sample0X, sample0Y, sample1X, sample1Y, outputWidth, outputHeight);

    for (uint32_t i = 0; i < GFX_TWO_I4_PRECOMBINE_CACHE_SIZE; i++) {
        TwoI4PrecombineCacheEntry* candidate = &sTwoI4PrecombineCache[i];
        if (gfx_two_i4_precombine_key_matches(candidate, primRgb, envRgb, sample0X, sample0Y,
                                               sample1X, sample1Y, outputWidth, outputHeight)) {
            entry = candidate;
            break;
        }
        if (!candidate->textureValid) {

            if (victim == NULL || victim->textureValid) victim = candidate;
        } else if (candidate->lastUsedFrame != sTextureCacheFrameSerial &&
                   candidate->lastUsedFrame < oldestFrame &&
                   (victim == NULL || victim->textureValid)) {
            oldestFrame = candidate->lastUsedFrame;
            victim = candidate;
        }
    }

    if (entry == NULL) {
        if (victim == NULL) {

            gfx_texture_cache_clear();
            victim = &sTwoI4PrecombineCache[0];
        }
        entry = victim;
        overwriteResident = entry->textureAllocated;
        entry->texture0Addr = rdp.loaded_texture[0].addr;
        entry->texture1Addr = rdp.loaded_texture[1].addr;
        entry->primRgb = primRgb;
        entry->envRgb = envRgb;
        entry->envAlpha = rdp.env_color.a;
        entry->outputWidth = (uint8_t)outputWidth;
        entry->outputHeight = (uint8_t)outputHeight;
        entry->importWidth0 = (uint16_t)gfx_texture_import_width(0);
        entry->importHeight0 = (uint16_t)gfx_texture_import_height(0);
        entry->importWidth1 = (uint16_t)gfx_texture_import_width(1);
        entry->importHeight1 = (uint16_t)gfx_texture_import_height(1);
        entry->fmt0 = gfx_get_texture_tile(0)->fmt; entry->siz0 = gfx_get_texture_tile(0)->siz;
        entry->fmt1 = gfx_get_texture_tile(1)->fmt; entry->siz1 = gfx_get_texture_tile(1)->siz;
        entry->nibble0 = rdp.loaded_texture[0].source_nibble_offset;
        entry->nibble1 = rdp.loaded_texture[1].source_nibble_offset;
        memcpy(entry->sample0X, sample0X, outputWidth);
        memcpy(entry->sample1X, sample1X, outputWidth);
        memcpy(entry->sample0Y, sample0Y, outputHeight);
        memcpy(entry->sample1Y, sample1Y, outputHeight);

        gfx_build_two_i4_precombine_pixels(entry);
        entry->textureValid = true;
        needsUpload = true;
    }

    if (!entry->textureAllocated) {
        const uint32_t bytes = (uint32_t)entry->outputWidth * (uint32_t)entry->outputHeight * 4U;
        if (!texman_vram_space_available(bytes) || !texman_texture_slot_available()) {

            gfx_texture_cache_clear();
        }
        entry->textureId = gfx_rapi->new_texture();
        entry->textureAllocated = true;
        needsUpload = true;
    } else {
        gfx_rapi->select_texture(0, entry->textureId);
    }

    if (needsUpload) {

        if (overwriteResident) {
            gfx_flush();
        }
        gfx_rapi->select_texture(0, entry->textureId);
        gfx_ps2_set_skip_content_hash(true);
        gfx_rapi->upload_texture((const uint8_t*)entry->pixels,
                                 entry->outputWidth, entry->outputHeight, PS2_TEXFMT_8888);
        gfx_ps2_set_skip_content_hash(false);
    }

    entry->lastUsedFrame = sTextureCacheFrameSerial;
    sPreparedAtlasKind = GFX_PREPARED_ATLAS_TWO_I4;
    sPreparedAtlasTextureId = entry->textureId;
    return true;
}

static inline void gfx_mark_tri_pipeline_dirty(void) {
    rendering_state.tri_pipeline_dirty = true;
}

static inline uint32_t color_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

static inline bool gfx_blend_cycle_uses_framebuffer(uint32_t other_mode_l, uint32_t m2a_shift, uint32_t m2b_shift) {
    uint32_t m2a = (other_mode_l >> m2a_shift) & 3;
    uint32_t m2b = (other_mode_l >> m2b_shift) & 3;

    return (m2a == G_BL_CLR_MEM) && ((m2b == G_BL_1MA) || (m2b == G_BL_1));
}

static inline bool gfx_blend_cycle_preserves_color(uint32_t other_mode_l, uint32_t m1a_shift,
                                                   uint32_t m1b_shift, uint32_t m2a_shift,
                                                   uint32_t m2b_shift) {
    return (((other_mode_l >> m1a_shift) & 3) == G_BL_CLR_BL) &&
           (((other_mode_l >> m1b_shift) & 3) == G_BL_0) &&
           (((other_mode_l >> m2a_shift) & 3) == G_BL_CLR_MEM) &&
           (((other_mode_l >> m2b_shift) & 3) == G_BL_1MA);
}

static inline float gfx_texture_shift_scale(uint8_t shift) {
    static const float scales[16] = {
        1.0f,       0.5f,       0.25f,      0.125f,
        0.0625f,    0.03125f,   0.015625f,  0.0078125f,
        0.00390625f, 0.001953125f, 0.0009765625f, 32.0f,
        16.0f,      8.0f,       4.0f,       2.0f,
    };

    return scales[shift & 0xF];
}

static void gfx_prepare_texture_coord_state(struct TriPipelineState* state, int coordSlot, int textureSlot,
                                            bool linear_filter) {
    const TextureTileState* tileState = gfx_get_texture_tile(textureSlot);
    const float filter_bias = linear_filter ? 16.0f : 0.0f;
    const float shift_scale_s = gfx_texture_shift_scale(tileState->shifts);
    const float shift_scale_t = gfx_texture_shift_scale(tileState->shiftt);

    state->tex_u_shift_scale[coordSlot] = shift_scale_s;
    state->tex_v_shift_scale[coordSlot] = shift_scale_t;

    state->tex_u_nominal_span[coordSlot] = 8.0f * (tileState->lrs - tileState->uls + 4);
    state->tex_v_nominal_span[coordSlot] = 8.0f * (tileState->lrt - tileState->ult + 4);

    if (state->tex_u_nominal_span[coordSlot] == 0.0f || state->tex_v_nominal_span[coordSlot] == 0.0f) {
        return;
    }

    const float inv_nominal_span_u = 1.0f / state->tex_u_nominal_span[coordSlot];
    const float inv_nominal_span_v = 1.0f / state->tex_v_nominal_span[coordSlot];

    state->tex_u_scale[coordSlot] = shift_scale_s * inv_nominal_span_u;
    state->tex_v_scale[coordSlot] = shift_scale_t * inv_nominal_span_v;
    state->tex_u_bias[coordSlot] =
        (filter_bias - tileState->uls * 8.0f) * inv_nominal_span_u;
    state->tex_v_bias[coordSlot] =
        (filter_bias - tileState->ult * 8.0f) * inv_nominal_span_v;
    state->tex_u_scale_to_primitive[coordSlot] = tileState->masks == G_TX_NOMASK;
    state->tex_v_scale_to_primitive[coordSlot] = tileState->maskt == G_TX_NOMASK;
    {
        const struct TextureHashmapNode* texture_node = rendering_state.textures[textureSlot];
        const uint32_t tile_width = (tileState->lrs - tileState->uls + 4) / 4;
        const uint32_t tile_height = (tileState->lrt - tileState->ult + 4) / 4;

        if (texture_node != NULL && tile_width != 0 && tile_height != 0) {
            if ((texture_node->upload_width != 0) && (texture_node->upload_width != tile_width)) {
                const float upload_width = texture_node->upload_width;
                const float u_factor = (float)tile_width / upload_width;

                state->tex_u_scale[coordSlot] *= u_factor;
                state->tex_u_bias[coordSlot] *= u_factor;
            }

            if ((texture_node->upload_height != 0) && (texture_node->upload_height != tile_height)) {
                const float upload_height = texture_node->upload_height;
                const float v_factor = (float)tile_height / upload_height;

                state->tex_v_scale[coordSlot] *= v_factor;
                state->tex_v_bias[coordSlot] *= v_factor;
            }

            if (linear_filter && rdp.combine_texture_tint_uses_env_alpha &&
                texture_node->upload_width == 32 && texture_node->upload_height == 64) {
                state->tex_u_scale[coordSlot] *= 31.0f / 32.0f;
                state->tex_v_scale[coordSlot] *= 63.0f / 64.0f;
            }

        }
    }
}

static void gfx_prepare_flame_atlas_coord_state(struct TriPipelineState* state) {
    const TextureTileState* texture0 = gfx_get_texture_tile(0);
    if (sPreparedAtlasKind == GFX_PREPARED_ATLAS_TWO_I4) {
        const TextureTileState* texture1 = gfx_get_texture_tile(1);
        const bool linear = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
        const float filterBias = linear ? 16.0f : 0.0f;
        const float textureSpanU = 8.0f * (texture1->lrs - texture1->uls + 4);
        const float textureSpanV = 8.0f * (texture1->lrt - texture1->ult + 4);

        if (textureSpanU == 0.0f || textureSpanV == 0.0f) return;
        state->tex_u_nominal_span[0] = textureSpanU;
        state->tex_v_nominal_span[0] = textureSpanV;
        state->tex_u_shift_scale[0] = gfx_texture_shift_scale(texture1->shifts);
        state->tex_v_shift_scale[0] = gfx_texture_shift_scale(texture1->shiftt);
        state->tex_u_scale[0] = state->tex_u_shift_scale[0] / textureSpanU;
        state->tex_v_scale[0] = state->tex_v_shift_scale[0] / textureSpanV;
        state->tex_u_bias[0] = (filterBias - texture1->uls * 8.0f) / textureSpanU;
        state->tex_v_bias[0] = (filterBias - texture1->ult * 8.0f) / textureSpanV;
        state->tex_u_scale_to_primitive[0] = false;
        state->tex_v_scale_to_primitive[0] = false;
        return;
    }
    const TextureTileState* texture1 = gfx_get_texture_tile(1);
    const float textureSpanU = 8.0f * (texture0->lrs - texture0->uls + 4);
    const float textureSpanV = 8.0f * (texture0->lrt - texture0->ult + 4);
    const uint32_t scrollPixels = (texture1->ult + 2) >> G_TEXTURE_IMAGE_FRAC;
    const uint32_t phase = ((scrollPixels + (GFX_FLAME_ATLAS_PHASE_STEP / 2)) /
                            GFX_FLAME_ATLAS_PHASE_STEP) &
                           ((GFX_FLAME_ATLAS_COLUMNS * GFX_FLAME_ATLAS_ROWS) - 1);
    const uint32_t atlasX = (phase % GFX_FLAME_ATLAS_COLUMNS) * GFX_FLAME_ATLAS_FRAME_WIDTH;
    const uint32_t atlasY = (phase / GFX_FLAME_ATLAS_COLUMNS) * GFX_FLAME_ATLAS_FRAME_HEIGHT;

    state->tex_u_nominal_span[0] = textureSpanU;
    state->tex_v_nominal_span[0] = textureSpanV;
    state->tex_u_scale[0] = (GFX_FLAME_ATLAS_FRAME_WIDTH - 1.0f) /
                            (GFX_FLAME_ATLAS_WIDTH * textureSpanU);
    state->tex_v_scale[0] = (GFX_FLAME_ATLAS_FRAME_HEIGHT - 1.0f) /
                            (GFX_FLAME_ATLAS_HEIGHT * textureSpanV);
    state->tex_u_bias[0] =
        (atlasX + 0.5f - (texture0->uls * 8.0f * (GFX_FLAME_ATLAS_FRAME_WIDTH - 1.0f) / textureSpanU)) /
        GFX_FLAME_ATLAS_WIDTH;
    state->tex_v_bias[0] =
        (atlasY + 0.5f - (texture0->ult * 8.0f * (GFX_FLAME_ATLAS_FRAME_HEIGHT - 1.0f) / textureSpanV)) /
        GFX_FLAME_ATLAS_HEIGHT;
}

static bool gfx_ps2_env_alpha_two_texture_pair_is_real(void) {
    const TextureTileState* tile0 = gfx_get_texture_tile(0);
    const TextureTileState* tile1 = gfx_get_texture_tile(1);
    const uint32_t w0 = gfx_texture_import_width(0);
    const uint32_t h0 = gfx_texture_import_height(0);
    const uint32_t w1 = gfx_texture_import_width(1);
    const uint32_t h1 = gfx_texture_import_height(1);
    const bool i0 = tile0->fmt == G_IM_FMT_I &&
                    (tile0->siz == G_IM_SIZ_4b || tile0->siz == G_IM_SIZ_8b);
    const bool i1 = tile1->fmt == G_IM_FMT_I &&
                    (tile1->siz == G_IM_SIZ_4b || tile1->siz == G_IM_SIZ_8b);

    if (rdp.loaded_texture[0].addr == NULL || rdp.loaded_texture[1].addr == NULL ||
        !i0 || !i1 || w0 == 0 || h0 == 0 || w1 == 0 || h1 == 0) {
        return false;
    }

    if (w0 <= 32 && w1 <= 32 && h0 <= 128 && h1 <= 128) {
        return true;
    }

    if (w0 == 64 && w1 == 64 && h0 == h1 && h0 <= 32 &&
        (tile0->cms & G_TX_CLAMP) && (tile0->cmt & G_TX_CLAMP) &&
        (tile1->cms & G_TX_CLAMP) && (tile1->cmt & G_TX_CLAMP) &&
        rdp.loaded_texture[0].addr != rdp.loaded_texture[1].addr) {
        return true;
    }

    return false;
}

static inline void gfx_ps2_refresh_screen_transform(void) {
    const float width = (float)rendering_state.viewport.width;
    const float height = (float)rendering_state.viewport.height;
    sPs2ScreenHalfWidth = width * 0.5f;
    sPs2ScreenHalfHeight = height * 0.5f;
    sPs2ScreenCenterX = (float)rendering_state.viewport.x + sPs2ScreenHalfWidth;
    sPs2ScreenCenterY = (float)gfx_current_dimensions.height -
                        (float)rendering_state.viewport.y - sPs2ScreenHalfHeight;
    sPs2ScreenZOffset = rendering_state.decal_mode ? 32.0f : 0.0f;
    if (++sPs2ScreenTransformSerial == 0U) {
        sPs2ScreenTransformSerial = 1U;
        for (size_t i = 0; i < MAX_VERTICES; ++i) {
            rsp.loaded_vertices[i].screen_serial = 0U;
        }
    }
}

static inline __attribute__((always_inline)) void gfx_ps2_ndc_to_screen(
    float nx, float ny, float nz, float* sx, float* sy, float* sz) {
    float z = (1.0f - nz) * 65535.0f + sPs2ScreenZOffset;
    if (z < 0.0f) z = 0.0f;
    if (z > 65535.0f) z = 65535.0f;
    *sx = nx * sPs2ScreenHalfWidth + sPs2ScreenCenterX;
    *sy = ny * -sPs2ScreenHalfHeight + sPs2ScreenCenterY;
    *sz = (float)(int)z;
}

static void gfx_prepare_tri_pipeline_state(void) {
    if (!rendering_state.tri_pipeline_dirty) {
        return;
    }

    if ((buf_vbo_len > 0) &&
        (rdp.combine_two_intensity_env_prim_precombine || rdp.combine_flame_texture_atlas)) {
        gfx_flush();
    }

    bool two_i4_precombine = false;
    bool flame_texture_atlas = false;

    sPreparedAtlasKind = GFX_PREPARED_ATLAS_NONE;
    sPreparedAtlasTextureId = 0;
    if (rdp.combine_two_intensity_env_prim_precombine && gfx_prepare_two_i4_precombine()) {
        two_i4_precombine = true;
        flame_texture_atlas = true;
    } else if (rdp.combine_flame_texture_atlas && gfx_prepare_flame_atlas()) {
        sPreparedAtlasKind = GFX_PREPARED_ATLAS_FLAME;
        sPreparedAtlasTextureId = sPreparedFlameAtlas->textureId;
        flame_texture_atlas = true;
    }
    bool prepared_two_texture_blend = rdp.combine_two_texture_blend && !two_i4_precombine;
    if (prepared_two_texture_blend && rdp.combine_texture_tint_uses_env_alpha &&
        !gfx_ps2_env_alpha_two_texture_pair_is_real()) {
        prepared_two_texture_blend = false;
    }
    const bool prepared_two_texture_blend_uses_prim_lod =
        prepared_two_texture_blend && rdp.combine_two_texture_blend_uses_prim_lod;
    const bool prepared_two_texture_alpha_blend =
        prepared_two_texture_blend && rdp.combine_two_texture_alpha_blend;
    const bool prepared_two_texture_env_prim_tint =
        prepared_two_texture_blend && rdp.combine_texture_tint_uses_prim_lod;

    if (flame_texture_atlas != rendering_state.tri_pipeline.flame_texture_atlas) {
        gfx_flush();
    }

    if (prepared_two_texture_blend != rendering_state.tri_pipeline.two_texture_blend) {
        gfx_flush();
    }
    if ((prepared_two_texture_blend_uses_prim_lod !=
         rendering_state.tri_pipeline.two_texture_blend_uses_prim_lod) ||
        (prepared_two_texture_alpha_blend !=
         rendering_state.tri_pipeline.two_texture_alpha_blend)) {
        gfx_flush();
    }
    if (prepared_two_texture_env_prim_tint !=
        rendering_state.tri_pipeline.two_texture_env_prim_tint) {
        gfx_flush();
    }
    const bool backend_state_dirty = rendering_state.backend_state_dirty;
    bool depth_test = (rsp.geometry_mode & G_ZBUFFER) == G_ZBUFFER;
    if (backend_state_dirty || depth_test != rendering_state.depth_test) {
        gfx_flush();
        gfx_rapi->set_depth_test(depth_test);
        rendering_state.depth_test = depth_test;
    }

    bool z_upd = (rdp.other_mode_l & Z_UPD) == Z_UPD;
    if (backend_state_dirty || z_upd != rendering_state.depth_mask) {
        gfx_flush();
        gfx_rapi->set_depth_mask(z_upd);
        rendering_state.depth_mask = z_upd;
    }

    bool zmode_decal = (rdp.other_mode_l & ZMODE_DEC) == ZMODE_DEC;
    if (backend_state_dirty || zmode_decal != rendering_state.decal_mode) {
        gfx_flush();
        gfx_rapi->set_zmode_decal(zmode_decal);
        rendering_state.decal_mode = zmode_decal;
        gfx_ps2_refresh_screen_transform();
    }

    if (backend_state_dirty || rdp.viewport_or_scissor_changed) {
        if (backend_state_dirty || memcmp(&rdp.viewport, &rendering_state.viewport, sizeof(rdp.viewport)) != 0) {
            gfx_flush();
            gfx_rapi->set_viewport(rdp.viewport.x, rdp.viewport.y, rdp.viewport.width, rdp.viewport.height);
            rendering_state.viewport = rdp.viewport;
            gfx_ps2_refresh_screen_transform();
        }
        if (backend_state_dirty || memcmp(&rdp.scissor, &rendering_state.scissor, sizeof(rdp.scissor)) != 0) {
            gfx_flush();
            gfx_rapi->set_scissor(rdp.scissor.x, rdp.scissor.y, rdp.scissor.width, rdp.scissor.height);
            rendering_state.scissor = rdp.scissor;
        }
        rdp.viewport_or_scissor_changed = false;
    }

    uint32_t cc_id = rdp.combine_mode;
    if (two_i4_precombine) {

        const uint32_t reducedRgb =
            color_comb(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0);
        const uint32_t reducedAlpha =
            color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        cc_id = reducedRgb | (reducedAlpha << 12);
    }

    uint32_t alpha_compare = rdp.other_mode_l & (3 << G_MDSFT_ALPHACOMPARE);
    bool alpha_blend = (rdp.other_mode_l & FORCE_BL) &&
                       (gfx_blend_cycle_uses_framebuffer(rdp.other_mode_l, 22, 18) ||
                        gfx_blend_cycle_uses_framebuffer(rdp.other_mode_l, 20, 16));
    bool use_fog = (rdp.other_mode_l >> 30) == G_BL_CLR_FOG;
    bool texture_edge = (rdp.other_mode_l & CVG_X_ALPHA) == CVG_X_ALPHA;
    bool use_noise = alpha_compare == G_AC_DITHER;
    bool alpha_threshold = alpha_compare == G_AC_THRESHOLD;
    bool depth_only = z_upd && (rdp.other_mode_l & FORCE_BL) &&
                      gfx_blend_cycle_preserves_color(rdp.other_mode_l, 30, 26, 22, 18) &&
                      gfx_blend_cycle_preserves_color(rdp.other_mode_l, 28, 24, 20, 16);
    bool use_alpha = alpha_blend || texture_edge || (alpha_compare != G_AC_NONE);

    if (use_alpha) cc_id |= SHADER_OPT_ALPHA;
    if (use_fog) cc_id |= SHADER_OPT_FOG;
    if (texture_edge) cc_id |= SHADER_OPT_TEXTURE_EDGE;
    if (use_noise) cc_id |= SHADER_OPT_NOISE;
    if (alpha_threshold) cc_id |= SHADER_OPT_ALPHA_THRESHOLD;
    if (sPs2ForceDepthOnly) {
        depth_only = true;
    }
    if (depth_only) cc_id |= SHADER_OPT_DEPTH_ONLY;

    if (!use_alpha) {
        cc_id &= ~0xfff000;
    }

    if (backend_state_dirty || !rendering_state.color_combiner_valid || rendering_state.color_combiner_id != cc_id) {
        rendering_state.color_combiner = gfx_lookup_or_create_color_combiner(cc_id);
        rendering_state.color_combiner_id = cc_id;
        rendering_state.color_combiner_valid = true;
    }

    struct ColorCombiner *comb = rendering_state.color_combiner;
    struct ShaderProgram *prg = comb->prg;
    struct RGBA textureTintPrimColor = rdp.prim_color;
    struct RGBA textureTintEnvColor = rdp.env_color;
    bool textureTintColorsCorrected = false;
    bool twoTextureUncompensatedAlpha = false;
    twoTextureUncompensatedAlpha =
        rdp.combine_two_texture_blend && rdp.combine_two_texture_alpha_blend &&
        gfx_two_intensity_tint_textures_are_supported();
    if (rdp.combine_texture_tint_uses_prim_lod) {
        if (rdp.combine_two_texture_blend &&
            gfx_get_two_texture_prim_lod_tint_colors(&textureTintPrimColor, &textureTintEnvColor)) {
            textureTintColorsCorrected = true;
        } else if (!rdp.combine_two_texture_blend) {
            textureTintColorsCorrected = gfx_get_single_texture_tint_colors(
                rdp.prim_lod_frac, GFX_INTENSITY_TINT_CACHE_SINGLE_PRIM_LOD, &textureTintPrimColor,
                &textureTintEnvColor);
        }
    } else if (rdp.combine_texture_tint_uses_env_alpha && !rdp.combine_two_texture_blend) {
        textureTintColorsCorrected = gfx_get_single_texture_tint_colors(
            rdp.env_color.a, GFX_INTENSITY_TINT_CACHE_SINGLE_ENV_ALPHA, &textureTintPrimColor,
            &textureTintEnvColor);
    }
    if (backend_state_dirty || prg != rendering_state.shader_program) {
        gfx_flush();
        gfx_rapi->unload_shader(backend_state_dirty ? NULL : rendering_state.shader_program);
        gfx_rapi->load_shader(prg);
        rendering_state.shader_program = prg;
    }
    if (backend_state_dirty || alpha_blend != rendering_state.alpha_blend) {
        gfx_flush();
        gfx_rapi->set_use_alpha(alpha_blend);
        rendering_state.alpha_blend = alpha_blend;
    }
    struct RGBA backendTextureEnvColor = textureTintPrimColor;
    if (prepared_two_texture_env_prim_tint) {
        backendTextureEnvColor = rdp.env_color;
    }
    if (comb->texture_blend &&
        (backend_state_dirty || !rendering_state.texture_env_color_valid ||
         (rendering_state.texture_env_color.r != backendTextureEnvColor.r) ||
         (rendering_state.texture_env_color.g != backendTextureEnvColor.g) ||
         (rendering_state.texture_env_color.b != backendTextureEnvColor.b))) {
        gfx_flush();
        gfx_rapi->set_texture_env_color(backendTextureEnvColor.r, backendTextureEnvColor.g,
                                        backendTextureEnvColor.b, 0xFF);
        rendering_state.texture_env_color = backendTextureEnvColor;
        rendering_state.texture_env_color.a = 0xFF;
        rendering_state.texture_env_color_valid = true;
    }

    const bool used_textures[2] = {
        comb->used_textures[0],
        comb->used_textures[1] || prepared_two_texture_blend,
    };
    const bool linear_filter = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
    const int active_texture = two_i4_precombine ? 0 : comb->active_texture;

    for (int i = 0; i < 2; i++) {
        if (used_textures[i] && !flame_texture_atlas) {
            const TextureTileState* tileState = gfx_get_texture_tile(i);

            if (rendering_state.textures[i] == NULL) {
                rdp.textures_changed[i] = true;
            }

            if (rdp.textures_changed[i]) {
#if OOT_PS2_PERF_BENCH
                struct TextureHashmapNode* ps2PrevTexture = rendering_state.textures[i];
                const uint32_t ps2PrevUploads = sPerformanceTextureUploadCount;
                sPerformanceTextureChangeCount++;
#endif
                gfx_flush();
                import_texture(i);
#if OOT_PS2_PERF_BENCH
                if (ps2PrevTexture == rendering_state.textures[i] && ps2PrevUploads == sPerformanceTextureUploadCount) {
                    sPerformanceTextureSameFlushCount++;
                }
#endif
                rdp.textures_changed[i] = false;
            }
            if (backend_state_dirty ||
                linear_filter != rendering_state.textures[i]->linear_filter ||
                tileState->cms != rendering_state.textures[i]->cms ||
                tileState->cmt != rendering_state.textures[i]->cmt ||
                tileState->masks != rendering_state.textures[i]->masks ||
                tileState->maskt != rendering_state.textures[i]->maskt) {
                gfx_flush();
                gfx_rapi->set_sampler_parameters(i, linear_filter, tileState->cms, tileState->cmt,
                                                 tileState->masks, tileState->maskt);
                rendering_state.textures[i]->linear_filter = linear_filter;
                rendering_state.textures[i]->cms = tileState->cms;
                rendering_state.textures[i]->cmt = tileState->cmt;
                rendering_state.textures[i]->masks = tileState->masks;
                rendering_state.textures[i]->maskt = tileState->maskt;
            }
        }
    }

    if (flame_texture_atlas) {
        const uint32_t atlasTextureId = sPreparedAtlasTextureId;

        if (backend_state_dirty || (rendering_state.bound_texture_id != atlasTextureId) ||
            (rendering_state.bound_texture_tile != 0)) {
            gfx_flush();
            if (sPreparedAtlasKind == GFX_PREPARED_ATLAS_TWO_I4) {
                const TextureTileState* maskTile = gfx_get_texture_tile(1);
                gfx_rapi->set_sampler_parameters(0, linear_filter, maskTile->cms, maskTile->cmt,
                                                 maskTile->masks, maskTile->maskt);
            } else
            {
                gfx_rapi->set_sampler_parameters(0, linear_filter, G_TX_CLAMP, G_TX_CLAMP,
                                                 G_TX_NOMASK, G_TX_NOMASK);
            }
            gfx_rapi->select_texture(0, atlasTextureId);
            rendering_state.bound_texture_id = atlasTextureId;
            rendering_state.bound_texture_tile = 0;
        }
    } else if ((active_texture >= 0) && (rendering_state.textures[active_texture] != NULL)) {
        const uint32_t textureId = rendering_state.textures[active_texture]->texture_id;

        if (backend_state_dirty || (rendering_state.bound_texture_id != textureId) ||
            (rendering_state.bound_texture_tile != active_texture)) {
            gfx_flush();
            gfx_rapi->select_texture(active_texture, textureId);
            rendering_state.bound_texture_id = textureId;
            rendering_state.bound_texture_tile = active_texture;
        }
    }

    const bool textureBlendPrecolor = comb->texture_blend &&
                                      !rdp.combine_texture_blend_reverse &&
                                      !rdp.combine_texture_tint_uses_prim_lod &&
                                      !rdp.combine_texture_tint_uses_env_alpha &&
                                      !rdp.combine_two_texture_blend &&
                                      !rdp.combine_din_fire_tint;

    if (textureBlendPrecolor != rendering_state.tri_pipeline.texture_blend_precolor) {
        gfx_flush();
    }

    struct TriPipelineState *state = &rendering_state.tri_pipeline;
    state->comb = comb;
    state->use_alpha = use_alpha;
    state->use_fog = use_fog;
    state->used_textures[0] = used_textures[0];
    state->used_textures[1] = used_textures[1];
    state->use_texture = used_textures[0] || used_textures[1];
    state->two_texture_blend = prepared_two_texture_blend;
    state->two_texture_blend_uses_prim_lod = prepared_two_texture_blend_uses_prim_lod;
    state->two_texture_alpha_blend = prepared_two_texture_alpha_blend;
    state->two_texture_env_prim_tint = prepared_two_texture_env_prim_tint;
    state->texture_blend_reverse = rdp.combine_texture_blend_reverse;
    state->texture_blend_precolor = textureBlendPrecolor;
    state->din_fire_tint = rdp.combine_din_fire_tint;
    state->fog_uses_texture_alpha = use_fog && comb->uses_texture_alpha;
    state->flame_texture_atlas = flame_texture_atlas;
    state->texture_tint_colors_corrected = textureTintColorsCorrected;
    state->two_texture_uncompensated_alpha = twoTextureUncompensatedAlpha;
    state->texture_tint_env_color = textureTintEnvColor;
    state->color_mul_env = !two_i4_precombine && rdp.combine_color_mul_env;
    state->color_mul_prim = !two_i4_precombine && rdp.combine_color_mul_prim;
    state->alpha_mul_env = !two_i4_precombine && rdp.combine_alpha_mul_env;
    for (int i = 0; i < 2; i++) {
        state->tex_u_scale[i] = 0.0f;
        state->tex_v_scale[i] = 0.0f;
        state->tex_u_bias[i] = 0.0f;
        state->tex_v_bias[i] = 0.0f;
        state->tex_u_shift_scale[i] = 1.0f;
        state->tex_v_shift_scale[i] = 1.0f;
        state->tex_u_nominal_span[i] = 0.0f;
        state->tex_v_nominal_span[i] = 0.0f;
        state->tex_u_scale_to_primitive[i] = false;
        state->tex_v_scale_to_primitive[i] = false;
    }
    if (state->use_texture) {
        int base_texture = state->two_texture_blend ? 0 : active_texture;

        if (base_texture < 0) {
            base_texture = used_textures[0] ? 0 : 1;
        }

        if (state->flame_texture_atlas) {
            gfx_prepare_flame_atlas_coord_state(state);
        } else {
            gfx_prepare_texture_coord_state(state, 0, base_texture, linear_filter);
        }
        if (state->two_texture_blend && !state->flame_texture_atlas) {
            gfx_prepare_texture_coord_state(state, 1, 1, linear_filter);
        }
    }

    rendering_state.tri_pipeline_dirty = false;
    rendering_state.backend_state_dirty = false;
}

static inline float dot(const float a[3], const float b[3])
{
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

static uint8_t gfx_clamp_num_lights(uint32_t num_lights) {
    const uint32_t max_lights_with_ambient = MAX_LIGHTS + 1;

    return (num_lights > max_lights_with_ambient) ? max_lights_with_ambient : num_lights;
}

static void calculate_normal_dir(const Light_t *light, float coeffs[3]) {
    float light_dir[3] = {
        light->dir[0] / 127.0f,
        light->dir[1] / 127.0f,
        light->dir[2] / 127.0f
    };
    const float (*matrix)[4] = rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1];
    float dot;

    coeffs[0] = light_dir[0] * matrix[0][0] + light_dir[1] * matrix[0][1] + light_dir[2] * matrix[0][2];
    coeffs[1] = light_dir[0] * matrix[1][0] + light_dir[1] * matrix[1][1] + light_dir[2] * matrix[1][2];
    coeffs[2] = light_dir[0] * matrix[2][0] + light_dir[1] * matrix[2][1] + light_dir[2] * matrix[2][2];

    dot = coeffs[0] * coeffs[0] + coeffs[1] * coeffs[1] + coeffs[2] * coeffs[2];
    if (dot > 0.00001f) {
        const float scale = 1.0f / sqrtf(dot);
        coeffs[0] *= scale;
        coeffs[1] *= scale;
        coeffs[2] *= scale;
    }
}

static void gfx_matrix_mul(float res[4][4], const float a[4][4], const float b[4][4]) {

    __asm__ volatile(
        "lqc2 $vf1, 0(%[b])\n"
        "lqc2 $vf2, 16(%[b])\n"
        "lqc2 $vf3, 32(%[b])\n"
        "lqc2 $vf4, 48(%[b])\n"
        "lqc2 $vf5, 0(%[a])\n"
        "vmulax $ACC, $vf1, $vf5\n"
        "vmadday $ACC, $vf2, $vf5\n"
        "vmaddaz $ACC, $vf3, $vf5\n"
        "vmaddw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 0(%[res])\n"
        "lqc2 $vf5, 16(%[a])\n"
        "vmulax $ACC, $vf1, $vf5\n"
        "vmadday $ACC, $vf2, $vf5\n"
        "vmaddaz $ACC, $vf3, $vf5\n"
        "vmaddw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 16(%[res])\n"
        "lqc2 $vf5, 32(%[a])\n"
        "vmulax $ACC, $vf1, $vf5\n"
        "vmadday $ACC, $vf2, $vf5\n"
        "vmaddaz $ACC, $vf3, $vf5\n"
        "vmaddw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 32(%[res])\n"
        "lqc2 $vf5, 48(%[a])\n"
        "vmulax $ACC, $vf1, $vf5\n"
        "vmadday $ACC, $vf2, $vf5\n"
        "vmaddaz $ACC, $vf3, $vf5\n"
        "vmaddw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 48(%[res])\n"
        :
        : [res] "r"(res), [a] "r"(a), [b] "r"(b)
        : "memory");
}

static inline void gfx_transform_vec4(float out[4], const float matrix[4][4], const float in[4]) {

    __asm__ volatile(
        "lqc2 $vf1, 0(%[matrix])\n"
        "lqc2 $vf2, 16(%[matrix])\n"
        "lqc2 $vf3, 32(%[matrix])\n"
        "lqc2 $vf4, 48(%[matrix])\n"
        "lqc2 $vf5, 0(%[in])\n"
        "vmulax $ACC, $vf1, $vf5\n"
        "vmadday $ACC, $vf2, $vf5\n"
        "vmaddaz $ACC, $vf3, $vf5\n"
        "vmaddw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 0(%[out])\n"
        :
        : [out] "r"(out), [matrix] "r"(matrix), [in] "r"(in)
        : "memory");
}

static inline __attribute__((always_inline)) void gfx_ps2_vu0_load_resident_mvp(const float matrix[4][4]) {
    __asm__ volatile(
        "lqc2 $vf1, 0(%[matrix])\n"
        "lqc2 $vf2, 16(%[matrix])\n"
        "lqc2 $vf3, 32(%[matrix])\n"
        "lqc2 $vf4, 48(%[matrix])\n"
        :
        : [matrix] "r"(matrix)
        : "memory");
}

static inline __attribute__((always_inline)) void gfx_ps2_vu0_transform_vec4_resident(float out[4],
                                                                                       const float in[4]) {
    __asm__ volatile(
        "lqc2 $vf5, 0(%[in])\n"
        "vmulax $ACC, $vf1, $vf5\n"
        "vmadday $ACC, $vf2, $vf5\n"
        "vmaddaz $ACC, $vf3, $vf5\n"
        "vmaddw $vf6, $vf4, $vf5\n"
        "sqc2 $vf6, 0(%[out])\n"
        :
        : [out] "r"(out), [in] "r"(in)
        : "memory");
}

static inline void gfx_upload_projection_matrix(const float matrix[4][4]) {
    if (sUploadedProjectionValid && memcmp(sUploadedProjection, matrix, sizeof(sUploadedProjection)) == 0) {
        return;
    }
    memcpy(sUploadedProjection, matrix, sizeof(sUploadedProjection));
    sUploadedProjectionValid = true;
}

static bool gfx_hud_anchor_enabled(void);

static inline __attribute__((always_inline)) void gfx_apply_projection_matrix(void) {
    float projection[4][4];

    if (!gfx_hud_anchor_enabled() || !sHudViewportFullscreen) {
        gfx_upload_projection_matrix(rsp.P_matrix);
        return;
    }

    memcpy(projection, rsp.P_matrix, sizeof(projection));
    {
        for (size_t i = 0; i < 4; i++) {
            projection[i][0] = (projection[i][0] * sNdcAspectScale) +
                               (projection[i][3] * sHudAnchorOffsetNdc);
        }
    }

    gfx_upload_projection_matrix(projection);
}

static void gfx_apply_modelview_matrix(void) {

    rsp.lights_changed = true;
}

static GFX_DL_HANDLER void gfx_sp_matrix(uint8_t parameters, const int32_t *addr) {
    float matrix[4][4] __attribute__((aligned(16)));

    if (addr == NULL) {
        gfx_log_bad_data_source("matrix", addr, sizeof(Mtx));
        return;
    }
#if !defined(GBI_FLOATS)

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t int_part = addr[i * 2 + j / 2];
            uint32_t frac_part = addr[8 + i * 2 + j / 2];
            matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
            matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
        }
    }
#else

    memcpy(matrix, addr, sizeof(matrix));
#endif

    if (parameters & G_MTX_PROJECTION) {

        gfx_flush();
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.P_matrix, matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(rsp.P_matrix, matrix, rsp.P_matrix);
        }
        gfx_apply_projection_matrix();
    } else {
        if ((parameters & G_MTX_PUSH) && rsp.modelview_matrix_stack_size < MODELVIEW_STACK_SIZE) {
            ++rsp.modelview_matrix_stack_size;
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 2], sizeof(matrix));
        }
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, sizeof(matrix));
        } else {
            gfx_matrix_mul(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
        }
        gfx_apply_modelview_matrix();
    }
    sPs2MvpDirty = true;
}

static void gfx_sp_pop_matrix(uint32_t count) {
    if (count == 0) {
        return;
    }

    while (count--) {
        if (rsp.modelview_matrix_stack_size > 1) {
            --rsp.modelview_matrix_stack_size;
        }
    }
    gfx_apply_modelview_matrix();
    sPs2MvpDirty = true;
}

static float gfx_adjust_x_for_aspect_ratio(float x) {
    return x * sNdcAspectScale;
}

static float gfx_hud_anchor_direction(void) {
    if (sHudAnchor == OOT_PORT_HUD_ANCHOR_LEFT) {
        return -1.0f;
    }
    if (sHudAnchor == OOT_PORT_HUD_ANCHOR_RIGHT) {
        return 1.0f;
    }
    return 0.0f;
}

static bool gfx_hud_anchor_enabled(void) {
    return sHudAnchor != OOT_PORT_HUD_ANCHOR_NONE;
}

static void gfx_update_screen_metrics(void) {
    const float width = (float)gfx_current_dimensions.width;
    const float height = gfx_current_dimensions.height != 0 ? (float)gfx_current_dimensions.height : 1.0f;
    float margin;

    if (sPs2Widescreen) {
        gfx_current_dimensions.aspect_ratio = 16.0f / 9.0f;
        sNdcAspectScale = (4.0f / 3.0f) / (16.0f / 9.0f);
        margin = width * (1.0f - sNdcAspectScale) * 0.5f;
    } else
    {
        gfx_current_dimensions.aspect_ratio = width / height;
        sNdcAspectScale = width != 0.0f ? (height * (4.0f / 3.0f)) / width : 1.0f;
        margin = (width - (height * (4.0f / 3.0f))) * 0.5f;
    }

    if (margin < 0.0f) {
        margin = 0.0f;
    }
    sWidescreenMarginPixels = margin;
    sHudAnchorOffsetPixels = gfx_hud_anchor_direction() * sWidescreenMarginPixels;
    sHudAnchorOffsetNdc = width != 0.0f ? 2.0f * sHudAnchorOffsetPixels / width : 0.0f;
}

static float gfx_widescreen_margin_pixels(void) {
    return sWidescreenMarginPixels;
}

static float gfx_hud_anchor_offset_pixels(void) {
    return sHudAnchorOffsetPixels;
}

static GFX_DL_HANDLER void gfx_apply_unmasked_texture_axis(const struct LoadedVertex *const vertices[],
                                                           size_t n_vertices, bool use_u, float nominal_span,
                                                           float shift_scale, float *scale, float *bias) {
    float min_coord;
    float max_coord;
    float span;

    if (n_vertices == 0 || nominal_span <= 0.0f) {
        return;
    }

    min_coord = (use_u ? vertices[0]->u : vertices[0]->v) * shift_scale;
    max_coord = min_coord;

    for (size_t i = 1; i < n_vertices; i++) {
        const float coord = (use_u ? vertices[i]->u : vertices[i]->v) * shift_scale;

        if (coord < min_coord) {
            min_coord = coord;
        }
        if (coord > max_coord) {
            max_coord = coord;
        }
    }

    span = max_coord - min_coord;
    if (span <= nominal_span + 1.0f) {
        return;
    }

    *scale = shift_scale / (span + 1.0f);
    *bias = -min_coord / (span + 1.0f);
}

struct ShaderProgram {
    bool enabled;
    uint32_t shader_id;
    struct CCFeatures cc;
    int mix;
    bool texture_used[2];
    int texture_ord[2];
    int num_inputs;
};

static bool gfx_sp_vertex(size_t n_vertices, size_t dest_index, const Vtx *vertices) {
#if OOT_PS2_DEEP_PROFILE
    const uint32_t ps2DeepStart = gfx_ps2_read_count();
#endif
    float temp_vec[4] __attribute__((aligned(16)));
    float model_vec[4] __attribute__((aligned(16)));
    float proj_vec[4] __attribute__((aligned(16)));
    const float hudAnchorOffsetNdc = sHudViewportFullscreen ? sHudAnchorOffsetNdc : 0.0f;
    const uint32_t geometryMode = rsp.geometry_mode;
    const uint8_t directionalLightCount = rsp.current_num_lights - 1;
    if ((n_vertices == 0) || (dest_index >= MAX_VERTICES) ||
        (n_vertices > (MAX_VERTICES - dest_index)) || (vertices == NULL)) {
        if (n_vertices != 0) {
            gfx_log_bad_data_source("ps2-vertex-range", vertices, n_vertices * sizeof(Vtx));
        }
        return n_vertices == 0;
    }
    if ((geometryMode & G_LIGHTING) && rsp.lights_changed) {
        static const Light_t lookat_x = {{0, 0, 0}, 0, {0, 0, 0}, 0, {127, 0, 0}, 0};
        static const Light_t lookat_y = {{0, 0, 0}, 0, {0, 0, 0}, 0, {0, 127, 0}, 0};

        for (int i = 0; i < directionalLightCount; i++) {
            calculate_normal_dir(&rsp.current_lights[i], rsp.current_lights_coeffs[i]);
        }
        calculate_normal_dir(&lookat_x, rsp.current_lookat_coeffs[0]);
        calculate_normal_dir(&lookat_y, rsp.current_lookat_coeffs[1]);
        rsp.lights_changed = false;
    }

    if (sPs2MvpDirty) {
        gfx_matrix_mul(sPs2MvpMatrix,
                       rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1],
                       rsp.P_matrix);
        sPs2MvpDirty = false;
    }
    gfx_ps2_vu0_load_resident_mvp(sPs2MvpMatrix);

    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        const Vtx_t *v = &vertices[i].v;
        const Vtx_tn *vn = &vertices[i].n;
        struct LoadedVertex *d = &rsp.loaded_vertices[dest_index];

        temp_vec[0] = v->ob[0];
        temp_vec[1] = v->ob[1];
        temp_vec[2] = v->ob[2];
        temp_vec[3] = 1.0f;

        gfx_ps2_vu0_transform_vec4_resident(proj_vec, temp_vec);

        float w = proj_vec[3];
        float x = gfx_adjust_x_for_aspect_ratio(proj_vec[0]);
        const float y = proj_vec[1];
        const float z = proj_vec[2];

        if (hudAnchorOffsetNdc != 0.0f) {
            x += hudAnchorOffsetNdc * w;
        }

        short U = v->tc[0] * rsp.texture_scaling_factor.s >> 16;
        short V = v->tc[1] * rsp.texture_scaling_factor.t >> 16;

        if (geometryMode & G_LIGHTING) {
            unsigned int r = rsp.current_lights[directionalLightCount].col[0];
            unsigned int g = rsp.current_lights[directionalLightCount].col[1];
            unsigned int b = rsp.current_lights[directionalLightCount].col[2];

            for (int i = 0; i < directionalLightCount; i++) {
                float intensity = 0;
                intensity += vn->n[0] * rsp.current_lights_coeffs[i][0];
                intensity += vn->n[1] * rsp.current_lights_coeffs[i][1];
                intensity += vn->n[2] * rsp.current_lights_coeffs[i][2];
                intensity *= (1.0f / 127.0f);
                if (intensity > 0.0f) {
                    r += intensity * rsp.current_lights[i].col[0];
                    g += intensity * rsp.current_lights[i].col[1];
                    b += intensity * rsp.current_lights[i].col[2];
                }
            }

            d->color.r = r > 255 ? 255 : r;
            d->color.g = g > 255 ? 255 : g;
            d->color.b = b > 255 ? 255 : b;

            if (geometryMode & G_TEXTURE_GEN) {
                float dotx = 0, doty = 0;
                dotx += vn->n[0] * rsp.current_lookat_coeffs[0][0];
                dotx += vn->n[1] * rsp.current_lookat_coeffs[0][1];
                dotx += vn->n[2] * rsp.current_lookat_coeffs[0][2];
                doty += vn->n[0] * rsp.current_lookat_coeffs[1][0];
                doty += vn->n[1] * rsp.current_lookat_coeffs[1][1];
                doty += vn->n[2] * rsp.current_lookat_coeffs[1][2];

                U = (int32_t)((dotx * (1.0f / 127.0f) + 1.0f) * 0.25f * rsp.texture_scaling_factor.s);
                V = (int32_t)((doty * (1.0f / 127.0f) + 1.0f) * 0.25f * rsp.texture_scaling_factor.t);
            }
        } else {
            d->color.r = v->cn[0];
            d->color.g = v->cn[1];
            d->color.b = v->cn[2];
        }

        d->u = U;
        d->v = V;

        uint32_t clipRej = 0;
        if (x < -w) clipRej |= X_POS;
        if (x > w) clipRej |= X_NEG;
        if (y < -w) clipRej |= Y_POS;
        if (y > w) clipRej |= Y_NEG;
        if (z < -w) clipRej |= Z_POS;
        if (z > w) clipRej |= Z_NEG;
        if (!__builtin_isfinite(w) || w < 0.0001f) clipRej |= W_NEG;
        d->clip_rej = clipRej;

        d->x = (fabsf(w) > 0.000001f) ? (1.0f / w) : 1.0f;
        d->y = x * d->x;
        d->z = y * d->x;
        d->w = (z + w) * 0.5f * d->x;
        d->screen_serial = 0U;

        d->_x = x;
        d->_y = y;
        d->_z = z;
        d->_w = w;
        if ((geometryMode & G_FOG) && w > 0.0f) {
            float fogAlpha = (z * d->x) * rsp.fog_mul + rsp.fog_offset;
            if (fogAlpha < 0.0f) fogAlpha = 0.0f;
            if (fogAlpha > 255.0f) fogAlpha = 255.0f;
            d->fog_alpha = (uint8_t)fogAlpha;
        } else {
            d->fog_alpha = 0;
        }

        d->color.a = v->cn[3];
    }

#if OOT_PS2_DEEP_PROFILE
    sPs2DeepVtxCycles += gfx_ps2_read_count() - ps2DeepStart;
#endif
    return true;
}

static bool gfx_sp_cull_display_list(uint32_t firstVertex, uint32_t lastVertex) {
    uint8_t commonClipPlanes = CLIP_TEST_FLAGS;

    if ((firstVertex >= MAX_VERTICES) || (lastVertex >= MAX_VERTICES) ||
        (firstVertex > lastVertex)) {
        return false;
    }

    for (uint32_t i = firstVertex; i <= lastVertex; i++) {
        commonClipPlanes &= rsp.loaded_vertices[i].clip_rej;
    }

    return commonClipPlanes != 0;
}

#if defined(F3DEX_GBI_2)
static __attribute__((noinline, no_instrument_function)) void *seg_addr_cached_slow(uintptr_t w1);

static inline __attribute__((always_inline, no_instrument_function)) void *seg_addr(uintptr_t w1) {
    uint8_t segment;
    uintptr_t base;
    uintptr_t translated;

    if (__builtin_expect((w1 == (uintptr_t)&D_01000000) ||
                         (w1 == PS2_ASSET_SYMBOL_GIDENTITYMTX), 0)) {
        return seg_addr_cached_slow(w1);
    }

    if (__builtin_expect((w1 >= PS2_NATIVE_ADDR_START) && (w1 < 0x01000000U), 1)) {
        return (void*)w1;
    }

    segment = (uint8_t)(w1 >> 24);
    if (__builtin_expect((segment > 1) && (segment < NUM_SEGMENTS), 1)) {
        base = (uintptr_t)rsp.segments[segment];
        if (base == 0) {
            base = (uintptr_t)gSegments[segment];
        }

        if (gfx_addr_is_native(base)) {
            translated = base + (w1 & 0x00FFFFFFU);
            if (__builtin_expect(gfx_addr_is_native(translated), 1)) {
                return (void*)translated;
            }
        }
    }

    return seg_addr_cached_slow(w1);
}

static bool gfx_decode_vertex_cmd_f3dex2(uint32_t w0, uint32_t* n, uint32_t* destIndex) {
    const uint32_t count = (w0 >> 12) & 0xFF;
    const uint32_t end = (w0 >> 1) & 0x7F;

    if (((w0 & 0x00F00F01U) != 0) || (count == 0) || (count > MAX_VERTICES) || (end > MAX_VERTICES) ||
        (end < count)) {
        return false;
    }

    *n = count;
    *destIndex = end - count;
    return true;
}

static GFX_DL_HANDLER bool gfx_sp_vertex_f3dex2(uint32_t w0, uintptr_t rawAddr) {
    uint32_t n;
    uint32_t destIndex;

    if (!gfx_decode_vertex_cmd_f3dex2(w0, &n, &destIndex)) {
        n = (w0 >> 12) & 0xFF;
        gfx_log_bad_data_source("vertex-cmd", (const void*)rawAddr, n * sizeof(Vtx));
        return false;
    }

    return gfx_sp_vertex(n, destIndex, seg_addr(rawAddr));
}
#endif

static inline __attribute__((always_inline)) float gfx_triangle_cross_homogeneous_vu0(
    const struct LoadedVertex* v1, const struct LoadedVertex* v2, const struct LoadedVertex* v3) {
    union { uint32_t bits; float value; } cross;
    uint64_t packed;

    __asm__ volatile(
        "lqc2 $vf1, 16(%[v1])\n"
        "lqc2 $vf2, 16(%[v2])\n"
        "lqc2 $vf3, 16(%[v3])\n"
        "vmulw.xy $vf4, $vf1, $vf2\n"
        "vmulw.xy $vf5, $vf2, $vf1\n"
        "vsub.xy $vf4, $vf4, $vf5\n"
        "vmulw.xy $vf5, $vf3, $vf2\n"
        "vmulw.xy $vf6, $vf2, $vf3\n"
        "vsub.xy $vf5, $vf5, $vf6\n"
        "vmuly.x $vf6, $vf4, $vf5\n"
        "vmulx.y $vf6, $vf4, $vf5\n"
        "vsuby.x $vf6, $vf6, $vf6\n"
        "qmfc2 %[packed], $vf6\n"
        : [packed] "=r"(packed)
        : [v1] "r"(v1), [v2] "r"(v2), [v3] "r"(v3)
        : "memory");
    cross.bits = (uint32_t)packed;
    return cross.value;
}

#if defined(F3DEX_GBI_2) || defined(F3DEX_GBI) || defined(F3DLP_GBI)
#define GFX_TRI_INDEX_DIVISOR 2
#else
#define GFX_TRI_INDEX_DIVISOR 10
#endif

static void gfx_sp_triangles_run(uint32_t packed0, uint32_t packed1, uint8_t triangleCount,
                                 bool* ps2RunPipelinePrepared, uint32_t ps2RunCullMode) {
#if OOT_PS2_DEEP_PROFILE
    const uint32_t ps2DeepStart = gfx_ps2_read_count();
#endif

#if OOT_PS2_PERF_BENCH
    sPerformanceInputTriangleCount += triangleCount;
#endif
    for (uint8_t triangle = 0; triangle < triangleCount; triangle++) {
        const uint32_t packed = triangle == 0 ? packed0 : packed1;
        const uint8_t vtx1_idx = ((packed >> 16) & 0xFF) / GFX_TRI_INDEX_DIVISOR;
        const uint8_t vtx2_idx = ((packed >> 8) & 0xFF) / GFX_TRI_INDEX_DIVISOR;
        const uint8_t vtx3_idx = (packed & 0xFF) / GFX_TRI_INDEX_DIVISOR;
        struct LoadedVertex *v1 = &rsp.loaded_vertices[vtx1_idx];
        struct LoadedVertex *v2 = &rsp.loaded_vertices[vtx2_idx];
        struct LoadedVertex *v3 = &rsp.loaded_vertices[vtx3_idx];
        struct LoadedVertex *v_arr[3] = {v1, v2, v3};
        const uint32_t clip_flags = v1->clip_rej | v2->clip_rej | v3->clip_rej;

        if (v1->clip_rej & v2->clip_rej & v3->clip_rej) {

            continue;
        }
        const uint32_t cull_mode = ps2RunPipelinePrepared != NULL ? ps2RunCullMode : (rsp.geometry_mode & G_CULL_BOTH);
        if (cull_mode != 0) {

            if (cull_mode == G_CULL_BOTH) continue;
            if (clip_flags == 0) {

                const float dx1 = v1->y - v2->y;
                const float dy1 = v1->z - v2->z;
                const float dx2 = v3->y - v2->y;
                const float dy2 = v3->z - v2->z;
                const float cross = dx1 * dy2 - dy1 * dx2;
                if (!__builtin_isfinite(cross) ||
                    ((cull_mode == G_CULL_FRONT) && (cross <= 0.0f)) ||
                    ((cull_mode == G_CULL_BACK) && (cross >= 0.0f))) continue;
            } else {
                const float eps = 0.000001f;

                const bool safeW = __builtin_isfinite(v1->_w) && __builtin_isfinite(v2->_w) &&
                                   __builtin_isfinite(v3->_w) && v1->_w > eps &&
                                   v2->_w > eps && v3->_w > eps;
                if (safeW) {
                    const float invW1 = 1.0f / v1->_w;
                    const float invW2 = 1.0f / v2->_w;
                    const float invW3 = 1.0f / v3->_w;
                    const float dx1 = v1->_x * invW1 - v2->_x * invW2;
                    const float dy1 = v1->_y * invW1 - v2->_y * invW2;
                    const float dx2 = v3->_x * invW3 - v2->_x * invW2;
                    const float dy2 = v3->_y * invW3 - v2->_y * invW2;
                    float cross = dx1 * dy2 - dy1 * dx2;
                    if ((v1->_w < 0.0f) ^ (v2->_w < 0.0f) ^ (v3->_w < 0.0f)) cross = -cross;
                    if (!__builtin_isfinite(cross) ||
                        ((cull_mode == G_CULL_FRONT) && (cross <= 0.0f)) ||
                        ((cull_mode == G_CULL_BACK) && (cross >= 0.0f))) continue;
                }
            }
        }

    struct LoadedVertex **clipped_vertices = v_arr;
    size_t clipped_vertices_num = 3;

    if (clip_flags & CLIP_TEST_FLAGS) {
        gfx_clip_single_vert(sClippedVertices, &clipped_vertices_num, v_arr, clip_flags);

        if (!clipped_vertices_num) {

            continue;
        }
        size_t i;
        for (i = 0; i < clipped_vertices_num; i++) {
            sClippedVertexPtrs[i] = &sClippedVertices[i];
        }
        clipped_vertices = sClippedVertexPtrs;
    }

    if ((clip_flags & CLIP_TEST_FLAGS) != 0) {
        const float eps = 0.000001f;
        const bool deferCull = (cull_mode != 0) &&
                               (!__builtin_isfinite(v1->_w) || !__builtin_isfinite(v2->_w) ||
                                !__builtin_isfinite(v3->_w) || v1->_w <= eps ||
                                v2->_w <= eps || v3->_w <= eps);
        size_t kept = 0;

        for (size_t triBase = 0; triBase + 2 < clipped_vertices_num; triBase += 3) {
            struct LoadedVertex* a = clipped_vertices[triBase + 0];
            struct LoadedVertex* b = clipped_vertices[triBase + 1];
            struct LoadedVertex* c = clipped_vertices[triBase + 2];
            const struct LoadedVertex* tv[3] = { a, b, c };
            bool valid = true;

            for (size_t k = 0; k < 3; k++) {
                const struct LoadedVertex* cv = tv[k];
                if (!__builtin_isfinite(cv->_x) || !__builtin_isfinite(cv->_y) ||
                    !__builtin_isfinite(cv->_z) || !__builtin_isfinite(cv->_w) || cv->_w <= eps) {
                    valid = false;
                    break;
                }
            }
            if (!valid) continue;

            if (deferCull) {
                const float dx1 = a->_x * b->_w - b->_x * a->_w;
                const float dy1 = a->_y * b->_w - b->_y * a->_w;
                const float dx2 = c->_x * b->_w - b->_x * c->_w;
                const float dy2 = c->_y * b->_w - b->_y * c->_w;
                const float cross = dx1 * dy2 - dy1 * dx2;
                if (!__builtin_isfinite(cross) ||
                    ((cull_mode == G_CULL_FRONT) && (cross <= 0.0f)) ||
                    ((cull_mode == G_CULL_BACK) && (cross >= 0.0f)) ||
                    (cull_mode == G_CULL_BOTH)) continue;
            }
            clipped_vertices[kept++] = a;
            clipped_vertices[kept++] = b;
            clipped_vertices[kept++] = c;
        }
        clipped_vertices_num = kept;
        if (clipped_vertices_num == 0) continue;
    }

    if (ps2RunPipelinePrepared != NULL) {
        if (!*ps2RunPipelinePrepared) {
            gfx_prepare_tri_pipeline_state();
            *ps2RunPipelinePrepared = true;
        }
    } else {
        gfx_prepare_tri_pipeline_state();
    }
    const struct TriPipelineState *state = &rendering_state.tri_pipeline;
    struct ColorCombiner *comb = state->comb;
    const bool use_alpha = state->use_alpha;
    const bool use_texture = state->use_texture;
    const float* tex_u_scale = state->tex_u_scale;
    const float* tex_v_scale = state->tex_v_scale;
    const float* tex_u_bias = state->tex_u_bias;
    const float* tex_v_bias = state->tex_v_bias;
    float adjusted_tex_u_scale[2];
    float adjusted_tex_v_scale[2];
    float adjusted_tex_u_bias[2];
    float adjusted_tex_v_bias[2];
    const uint32_t shader_program_id = rendering_state.shader_program->shader_id;

    const uint8_t ps2RgbSource = comb->vertex_color_source[0];
    const uint8_t ps2AlphaSource = comb->vertex_color_source[1];
    const bool ps2TextureTintException = comb->texture_blend && (ps2RgbSource == CC_ENV) &&
        (rdp.combine_texture_tint_uses_env_alpha ||
         (rdp.combine_texture_tint_uses_prim_lod && rdp.combine_two_texture_blend));
    const bool ps2SimpleShadeColor = !ps2TextureTintException &&
        (ps2RgbSource == CC_SHADE) && (!use_alpha || ps2AlphaSource == CC_SHADE);
    const bool ps2SimpleFixedRgb = !ps2TextureTintException &&
        (ps2RgbSource == CC_PRIM || ps2RgbSource == CC_ENV || ps2RgbSource == CC_0);
    struct RGBA ps2FixedRgb = white_color;
    if (ps2RgbSource == CC_PRIM) ps2FixedRgb = rdp.prim_color;
    else if (ps2RgbSource == CC_ENV) ps2FixedRgb = rdp.env_color;

    if (use_texture) {
        const struct LoadedVertex *uv_vertices[3] = {v1, v2, v3};
        const int coord_count = state->two_texture_blend ? 2 : 1;
        const bool adjust_tex_coords =
            state->tex_u_scale_to_primitive[0] || state->tex_v_scale_to_primitive[0] ||
            (state->two_texture_blend &&
             (state->tex_u_scale_to_primitive[1] || state->tex_v_scale_to_primitive[1]));

        if (adjust_tex_coords) {
            memcpy(adjusted_tex_u_scale, state->tex_u_scale, sizeof(adjusted_tex_u_scale));
            memcpy(adjusted_tex_v_scale, state->tex_v_scale, sizeof(adjusted_tex_v_scale));
            memcpy(adjusted_tex_u_bias, state->tex_u_bias, sizeof(adjusted_tex_u_bias));
            memcpy(adjusted_tex_v_bias, state->tex_v_bias, sizeof(adjusted_tex_v_bias));
            tex_u_scale = adjusted_tex_u_scale;
            tex_v_scale = adjusted_tex_v_scale;
            tex_u_bias = adjusted_tex_u_bias;
            tex_v_bias = adjusted_tex_v_bias;

            for (int coord = 0; coord < coord_count; coord++) {
                if (state->tex_u_scale_to_primitive[coord]) {
                    gfx_apply_unmasked_texture_axis(uv_vertices, 3, true, state->tex_u_nominal_span[coord],
                                                    state->tex_u_shift_scale[coord], &adjusted_tex_u_scale[coord],
                                                    &adjusted_tex_u_bias[coord]);
                }
                if (state->tex_v_scale_to_primitive[coord]) {
                    gfx_apply_unmasked_texture_axis(uv_vertices, 3, false, state->tex_v_nominal_span[coord],
                                                    state->tex_v_shift_scale[coord], &adjusted_tex_v_scale[coord],
                                                    &adjusted_tex_v_bias[coord]);
                }
            }
        }
    }

    const size_t new_tri_count = clipped_vertices_num / 3;

    if (new_tri_count == 0) {
        continue;
    }

#if OOT_PS2_PERF_BENCH
    sPerformanceOutputTriangleCount += new_tri_count;
#endif

    if ((buf_num_vert + clipped_vertices_num) > (sizeof(buf_vbo) / sizeof(buf_vbo[0]))) {
        gfx_flush();
    }

    if ((buf_vbo_num_tris + new_tri_count) > MAX_BUFFERED) {
        gfx_flush();
    }

    size_t i;
    for (i = 0; i < clipped_vertices_num; i++) {
        const struct LoadedVertex *vertex = clipped_vertices[i];
        ps2_fast_t *out = &buf_vbo[buf_num_vert];

        const bool ps2CachedProjection = (clip_flags == 0 && __builtin_isfinite(vertex->x));
        const float invW = ps2CachedProjection ? vertex->x :
                           ((fabsf(vertex->_w) > 0.000001f) ? (1.0f / vertex->_w) : 1.0f);
        if (ps2CachedProjection) {
            struct LoadedVertex* mutableVertex = (struct LoadedVertex*)vertex;
            if (__builtin_expect(mutableVertex->screen_serial != sPs2ScreenTransformSerial, 0)) {
                gfx_ps2_ndc_to_screen(vertex->y, vertex->z, vertex->w,
                                      &mutableVertex->screen_x, &mutableVertex->screen_y,
                                      &mutableVertex->screen_z);
                mutableVertex->screen_serial = sPs2ScreenTransformSerial;
            }
            out->x = mutableVertex->screen_x;
            out->y = mutableVertex->screen_y;
            out->z = mutableVertex->screen_z;
        } else {
            const float ndcX = vertex->_x * invW;
            const float ndcY = vertex->_y * invW;
            const float ndcZ = (vertex->_z + vertex->_w) * 0.5f * invW;
            gfx_ps2_ndc_to_screen(ndcX, ndcY, ndcZ, &out->x, &out->y, &out->z);
        }
        out->q = invW;

        if (use_texture) {
            out->real_u = vertex->u * tex_u_scale[0] + tex_u_bias[0];
            out->real_v = vertex->v * tex_v_scale[0] + tex_v_bias[0];
            out->u = out->real_u * invW;
            out->v = out->real_v * invW;
            if (state->two_texture_blend) {
                buf_vbo_tex1[buf_num_vert].real_u = vertex->u * tex_u_scale[1] + tex_u_bias[1];
                buf_vbo_tex1[buf_num_vert].real_v = vertex->v * tex_v_scale[1] + tex_v_bias[1];
                buf_vbo_tex1[buf_num_vert].u = buf_vbo_tex1[buf_num_vert].real_u * invW;
                buf_vbo_tex1[buf_num_vert].v = buf_vbo_tex1[buf_num_vert].real_v * invW;
            }
        } else {
            out->u = 0.0f;
            out->v = 0.0f;
            out->real_u = 0.0f;
            out->real_v = 0.0f;
        }

        if (__builtin_expect(ps2SimpleShadeColor, 1)) {
            out->color = vertex->color;
        } else if (ps2SimpleFixedRgb && ps2AlphaSource != CC_LOD) {
            out->color = ps2FixedRgb;
            if (use_alpha) {
                if (ps2AlphaSource == CC_SHADE) out->color.a = vertex->color.a;
                else if (ps2AlphaSource == CC_PRIM) out->color.a = rdp.prim_color.a;
                else if (ps2AlphaSource == CC_ENV) out->color.a = rdp.env_color.a;
                else out->color.a = 0xFF;
            }
        } else {
            out->color = gfx_get_vertex_rgba(comb, use_alpha, &vertex->color, vertex->_w, true);
        }
        if (state->texture_tint_colors_corrected) {
            out->color.r = state->texture_tint_env_color.r;
            out->color.g = state->texture_tint_env_color.g;
            out->color.b = state->texture_tint_env_color.b;
        }
        if (state->color_mul_env) {
            gfx_color_mul_env(&out->color);
        }
        if (state->color_mul_prim) {
            gfx_color_mul_prim(&out->color);
        }
        if (state->alpha_mul_env) {
            out->color.a = gfx_color_mul_channel(out->color.a, rdp.env_color.a);
        }

        out->fog_color = (u32)rdp.fog_color.r | ((u32)rdp.fog_color.g << 8) | ((u32)rdp.fog_color.b << 16);
        out->fog = state->use_fog ? (0x100U | (u32)(255U - vertex->fog_alpha)) : 0U;

        const uint8_t surfaceAlpha = use_alpha ? out->color.a : 0xFF;

        if (state->two_texture_blend) {
            uint8_t baseAlpha;
            uint8_t overlayAlpha;
            const uint8_t mixAlpha = state->two_texture_blend_uses_prim_lod
                                         ? rdp.prim_lod_frac
                                         : rdp.env_color.a;

            gfx_two_texture_blend_pass_alphas(out->color.a, mixAlpha, state->two_texture_alpha_blend,
                                              state->two_texture_uncompensated_alpha, &baseAlpha,
                                              &overlayAlpha);
            out->color.a = baseAlpha;
            buf_vbo_tex1[buf_num_vert].alpha = overlayAlpha;
        }
        if (state->use_fog) {
            struct RGBA fogColor = rdp.fog_color;

            fogColor.a = gfx_color_mul_channel(vertex->fog_alpha, surfaceAlpha);
            if (state->two_texture_blend && state->two_texture_alpha_blend) {
                uint8_t baseAlpha;
                uint8_t overlayAlpha;
                const uint8_t mixAlpha = state->two_texture_blend_uses_prim_lod
                                             ? rdp.prim_lod_frac
                                             : rdp.env_color.a;

                gfx_two_texture_blend_pass_alphas(fogColor.a, mixAlpha, true,
                                                  state->two_texture_uncompensated_alpha, &baseAlpha,
                                                  &overlayAlpha);
                fogColor.a = baseAlpha;
                buf_vbo_fog_tex1_alpha[buf_num_vert] = overlayAlpha;
            }
            if (state->fog_uses_texture_alpha) {
                ps2_fog_textured_t* fogOut = &buf_vbo_fog.textured[buf_num_vert];

                fogOut->u = out->u;
                fogOut->v = out->v;
                fogOut->color = fogColor;
                fogOut->x = out->x;
                fogOut->y = out->y;
                fogOut->z = out->z;
                fogOut->q = out->q;
            } else {
                ps2_fog_color_t* fogOut = &buf_vbo_fog.color[buf_num_vert];

                fogOut->color = fogColor;
                fogOut->x = out->x;
                fogOut->y = out->y;
                fogOut->z = out->z;
            }
        }
        if (shader_program_id == 0x01A00045) {

        }
        buf_num_vert++;
        buf_vbo_len += sizeof(ps2_fast_t);
    }
    buf_vbo_num_tris += new_tri_count;
    if (buf_vbo_num_tris >= MAX_BUFFERED) {
        gfx_flush();
    }
    }
#if OOT_PS2_DEEP_PROFILE
    sPs2DeepTriCycles += gfx_ps2_read_count() - ps2DeepStart;
#endif
}

static inline void gfx_sp_triangles(uint32_t packed0, uint32_t packed1, uint8_t triangleCount) {
    gfx_sp_triangles_run(packed0, packed1, triangleCount, NULL, 0);
}

#undef GFX_TRI_INDEX_DIVISOR

static inline void gfx_normalize_2d_repeat_axis(short* first, short* second, uint8_t cm, uint8_t mask,
                                                uint16_t period, bool repeatNomask) {
    int32_t minimum;
    int32_t maximum;
    int32_t offset;

    if ((cm & G_TX_CLAMP) || (!repeatNomask && mask == G_TX_NOMASK) || period == 0) {
        return;
    }

    minimum = *first < *second ? *first : *second;
    if (minimum >= 0) {
        return;
    }

    maximum = *first > *second ? *first : *second;
    offset = ((-minimum + period - 1) / period) * period;
    if (maximum + offset > INT16_MAX) {
        return;
    }

    *first += offset;
    *second += offset;
}

static inline void gfx_normalize_2d_repeat_uvs(VertexColor vertices[2], const TextureTileState* tileState,
                                                const struct TextureHashmapNode* texture, bool repeatNomask) {
    if (texture == NULL) {
        return;
    }

    gfx_normalize_2d_repeat_axis(&vertices[0].u, &vertices[1].u, tileState->cms, tileState->masks,
                                 texture->upload_width, repeatNomask);
    gfx_normalize_2d_repeat_axis(&vertices[0].v, &vertices[1].v, tileState->cmt, tileState->maskt,
                                 texture->upload_height, repeatNomask);
}

static inline uint8_t gfx_2d_physical_repeat_mask(uint8_t cm, uint8_t mask, bool repeatNomask) {
    return repeatNomask && !(cm & G_TX_CLAMP) && mask == G_TX_NOMASK ? 1 : mask;
}

static void gfx_sp_tri1_2d(uint8_t vtx1_idx, uint8_t vtx2_idx, UNUSED uint8_t vtx3_idx) {

    gfx_flush();

    struct VertexColor *v1 = &rsp.loaded_vertices_2D[vtx1_idx];
    struct VertexColor *v2 = &rsp.loaded_vertices_2D[vtx2_idx];
    struct VertexColor *v_arr[2] = {v1, v2};

    gfx_prepare_tri_pipeline_state();
    const struct TriPipelineState *state = &rendering_state.tri_pipeline;
    struct ColorCombiner *comb = state->comb;
    const bool use_alpha = state->use_alpha;
    const bool use_texture = state->use_texture;
    const int texture_tile = state->two_texture_blend ? 0 : (comb->active_texture >= 0 ? comb->active_texture : 0);
    const TextureTileState* tileState = gfx_get_texture_tile(texture_tile);
    const bool twoTextureBlend =
        state->two_texture_blend && rendering_state.textures[0] != NULL && rendering_state.textures[1] != NULL;
    bool baseSamplerOverridden = false;
    uint8_t secondPassAlpha[2] = { 0 };

    VertexColor tri_buf[2] = {{0}};
    int tri_num_vert = 0;

    for (int i = 0; i < 2; i++) {
        tri_buf[tri_num_vert].x = v_arr[i]->x;
        tri_buf[tri_num_vert].y = v_arr[i]->y;

        tri_buf[tri_num_vert].z = ((rdp.other_mode_l & G_ZS_PRIM) == G_ZS_PRIM)
                                      ? (uint16_t)(0xffffU - rdp.prim_depth)
                                      : 0;

        if (use_texture) {
            int32_t u = ((v_arr[i]->u * gfx_texture_shift_scale(tileState->shifts)) - tileState->uls * 8) / 32;
            int32_t v = ((v_arr[i]->v * gfx_texture_shift_scale(tileState->shiftt)) - tileState->ult * 8) / 32;
            const int active_texture = comb->active_texture;
            const struct TextureHashmapNode *texture_node =
                active_texture >= 0 ? rendering_state.textures[active_texture] : NULL;

            _UNUSED(texture_node);

            tri_buf[tri_num_vert].u = u;
            tri_buf[tri_num_vert].v = v;
        } else {
            tri_buf[tri_num_vert].u = 0;
            tri_buf[tri_num_vert].v = 0;
        }

        tri_buf[tri_num_vert].color = gfx_get_vertex_rgba(comb, use_alpha, &v_arr[i]->color, 0.0f, false);
        if (state->texture_tint_colors_corrected) {
            tri_buf[tri_num_vert].color.r = state->texture_tint_env_color.r;
            tri_buf[tri_num_vert].color.g = state->texture_tint_env_color.g;
            tri_buf[tri_num_vert].color.b = state->texture_tint_env_color.b;
        }
        if (state->color_mul_env) {
            gfx_color_mul_env(&tri_buf[tri_num_vert].color);
        }
        if (state->color_mul_prim) {
            gfx_color_mul_prim(&tri_buf[tri_num_vert].color);
        }
        if (state->alpha_mul_env) {
            tri_buf[tri_num_vert].color.a =
                gfx_color_mul_channel(tri_buf[tri_num_vert].color.a, rdp.env_color.a);
        }
        if (twoTextureBlend) {
            uint8_t baseAlpha;
            const uint8_t mixAlpha = state->two_texture_blend_uses_prim_lod
                                         ? rdp.prim_lod_frac
                                         : rdp.env_color.a;

            gfx_two_texture_blend_pass_alphas(tri_buf[tri_num_vert].color.a, mixAlpha,
                                              state->two_texture_alpha_blend,
                                              state->two_texture_uncompensated_alpha, &baseAlpha,
                                              &secondPassAlpha[tri_num_vert]);
            tri_buf[tri_num_vert].color.a = baseAlpha;
        }
        tri_num_vert++;
    }

    if (twoTextureBlend && use_texture && rendering_state.textures[texture_tile] != NULL) {
        const uint8_t physicalMasks =
            gfx_2d_physical_repeat_mask(tileState->cms, tileState->masks, true);
        const uint8_t physicalMaskt =
            gfx_2d_physical_repeat_mask(tileState->cmt, tileState->maskt, true);

        gfx_normalize_2d_repeat_uvs(tri_buf, tileState, rendering_state.textures[texture_tile], true);
        baseSamplerOverridden = physicalMasks != tileState->masks || physicalMaskt != tileState->maskt;
        if (baseSamplerOverridden) {
            gfx_rapi->set_sampler_parameters(texture_tile,
                                             rendering_state.textures[texture_tile]->linear_filter,
                                             tileState->cms, tileState->cmt, physicalMasks, physicalMaskt);
        }
    }

    if (use_texture && !state->flame_texture_atlas && rendering_state.textures[texture_tile] != NULL) {
        rendering_state.textures[texture_tile]->last_used_frame = sTextureCacheFrameSerial;
        if (twoTextureBlend) {
            rendering_state.textures[1]->last_used_frame = sTextureCacheFrameSerial;
            gfx_rapi->set_use_alpha(true);
        }
    }
    gfx_ps2_set_texture_blend_reverse(state->texture_blend_reverse);

    gfx_ps2_set_texture_blend_precolor(state->texture_blend_precolor);
    gfx_ps2_set_din_fire_tint(false);
    gfx_ps2_set_two_texture_blend_active(twoTextureBlend);
    gfx_ps2_set_two_texture_env_prim_tint(twoTextureBlend && state->two_texture_env_prim_tint);
    gfx_ps2_draw_triangles_2d((float*)&tri_buf[0],0,1);
    if (twoTextureBlend) {
        const TextureTileState* tile1State = gfx_get_texture_tile(1);
        const float shiftScaleS = gfx_texture_shift_scale(tile1State->shifts);
        const float shiftScaleT = gfx_texture_shift_scale(tile1State->shiftt);
        const uint8_t physicalMasks =
            gfx_2d_physical_repeat_mask(tile1State->cms, tile1State->masks, true);
        const uint8_t physicalMaskt =
            gfx_2d_physical_repeat_mask(tile1State->cmt, tile1State->maskt, true);
        const bool secondSamplerOverridden =
            physicalMasks != tile1State->masks || physicalMaskt != tile1State->maskt;

        for (int i = 0; i < 2; i++) {
            tri_buf[i].u = ((v_arr[i]->u * shiftScaleS) - tile1State->uls * 8) / 32;
            tri_buf[i].v = ((v_arr[i]->v * shiftScaleT) - tile1State->ult * 8) / 32;
            tri_buf[i].color.a = secondPassAlpha[i];
        }

        gfx_normalize_2d_repeat_uvs(tri_buf, tile1State, rendering_state.textures[1], true);

        if (secondSamplerOverridden) {
            gfx_rapi->set_sampler_parameters(1, rendering_state.textures[1]->linear_filter,
                                             tile1State->cms, tile1State->cmt,
                                             physicalMasks, physicalMaskt);
        }
        gfx_rapi->select_texture(1, rendering_state.textures[1]->texture_id);
        gfx_ps2_set_texture_blend_reverse(state->texture_blend_reverse);
        gfx_ps2_set_texture_blend_precolor(state->texture_blend_precolor);
        gfx_ps2_set_two_texture_blend_active(true);
        gfx_ps2_draw_triangles_2d((float*)&tri_buf[0], 0, 1);
        gfx_rapi->set_use_alpha(rendering_state.alpha_blend);
        if (secondSamplerOverridden) {
            gfx_rapi->set_sampler_parameters(1, rendering_state.textures[1]->linear_filter,
                                             tile1State->cms, tile1State->cmt,
                                             tile1State->masks, tile1State->maskt);
        }
        if (baseSamplerOverridden) {
            gfx_rapi->set_sampler_parameters(0, rendering_state.textures[0]->linear_filter,
                                             tileState->cms, tileState->cmt,
                                             tileState->masks, tileState->maskt);
        }
        gfx_rapi->select_texture(0, rendering_state.textures[0]->texture_id);
        rendering_state.bound_texture_id = rendering_state.textures[0]->texture_id;
        rendering_state.bound_texture_tile = 0;
    }
}

static void gfx_sp_geometry_mode(uint32_t clear, uint32_t set) {
    uint32_t geometryMode = (rsp.geometry_mode & ~clear) | set;

    if (geometryMode == rsp.geometry_mode) {
        return;
    }
    rsp.geometry_mode = geometryMode;
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_calc_and_set_viewport(const Vp_t *viewport) {

    if (viewport == NULL) {
        gfx_log_bad_data_source("viewport", viewport, sizeof(Vp_t));
        rdp.viewport.x = 0;
        rdp.viewport.y = 0;
        rdp.viewport.width = gfx_current_dimensions.width;
        rdp.viewport.height = gfx_current_dimensions.height;
        sHudViewportFullscreen = true;
        rdp.viewport_or_scissor_changed = true;
        gfx_mark_tri_pipeline_dirty();
        return;
    }

    float width = 2.0f * viewport->vscale[0] / 4.0f;
    float height = 2.0f * viewport->vscale[1] / 4.0f;
    float x = (viewport->vtrans[0] / 4.0f) - width / 2.0f;
    float y = SCREEN_HEIGHT - ((viewport->vtrans[1] / 4.0f) + height / 2.0f);

    sHudViewportFullscreen =
        (x <= 0.0f) && (y <= 0.0f) && (width >= SCREEN_WIDTH) && (height >= SCREEN_HEIGHT);

    if (gfx_hud_anchor_enabled() && !sHudViewportFullscreen) {
        width *= RATIO_Y;
        x = (x * RATIO_Y) + gfx_widescreen_margin_pixels() + gfx_hud_anchor_offset_pixels();
    } else {
        width *= RATIO_X;
        x *= RATIO_X;
    }
    height *= RATIO_Y;
    y *= RATIO_Y;

    rdp.viewport.x = x;
    rdp.viewport.y = y;
    rdp.viewport.width = width;
    rdp.viewport.height = height;

    rdp.viewport_or_scissor_changed = true;
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_sp_movemem(uint8_t index, uint8_t offset, const void* data) {
    switch (index) {
        case G_MV_VIEWPORT:
            gfx_calc_and_set_viewport((const Vp_t *) data);
            break;
#if 0
        case G_MV_LOOKATY:
        case G_MV_LOOKATX:
            memcpy(rsp.current_lookat + (index - G_MV_LOOKATY) / 2, data, sizeof(Light_t));

            break;
#endif
#if defined(F3DEX_GBI_2)
        case G_MV_LIGHT: {
            int lightidx = offset / 24 - 2;
            if (lightidx >= 0 && lightidx <= MAX_LIGHTS) {

                if (data == NULL) {
                    gfx_log_bad_data_source("light", data, sizeof(Light_t));
                    break;
                }

                memcpy(rsp.current_lights + lightidx, data, sizeof(Light_t));
            }
            break;
        }
#else
        case G_MV_L0:
        case G_MV_L1:
        case G_MV_L2:
        {

            if (data == NULL) {
                gfx_log_bad_data_source("light", data, sizeof(Light_t));
                break;
            }
        }

            memcpy(rsp.current_lights + (index - G_MV_L0) / 2, data, sizeof(Light_t));
            break;
#endif
    }
}

static void gfx_sp_moveword(uint8_t index, uint16_t offset, uint32_t data) {
    switch (index) {
        case G_MW_NUMLIGHT:
#if defined(F3DEX_GBI_2)
            rsp.current_num_lights = gfx_clamp_num_lights(data / 24 + 1);
#else

            rsp.current_num_lights = gfx_clamp_num_lights((data - 0x80000000U) / 32);
#endif
            rsp.lights_changed = 1;
            break;
        case G_MW_FOG:
            rsp.fog_mul = (int16_t)(data >> 16);
            rsp.fog_offset = (int16_t)data;
            break;
        case G_MW_SEGMENT:
        {
            uint8_t segment = offset >> 2;
            uintptr_t base = data;
            uintptr_t normalized;

            if (segment < NUM_SEGMENTS) {
                if (gfx_normalize_native_addr(base, &normalized)) {
                    base = normalized;
                }
                rsp.segments[segment] = (void*)base;
                GFX_CAPTURE_CMD(rsp.segment_cmd[segment], sCurrentCmd);
            }
            break;
        }
    }
}

static void gfx_sp_texture(uint16_t sc, uint16_t tc, uint8_t level, uint8_t tile, uint8_t on) {
    _UNUSED(level);
    _UNUSED(tile);
    _UNUSED(on);

    rsp.texture_scaling_factor.s = sc;
    rsp.texture_scaling_factor.t = tc;
}

static void gfx_dp_set_scissor(uint32_t mode, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    _UNUSED(mode);

    const float activeWidth = sPs2Widescreen ? (float)gfx_current_dimensions.width
                                                : gfx_current_dimensions.height * (4.0f / 3.0f);
    const float activeX = (gfx_current_dimensions.width - activeWidth) * 0.5f;
    const float xScale = sPs2Widescreen ? RATIO_X : RATIO_Y;
    float x = activeX + (ulx / 4.0f * xScale) + gfx_hud_anchor_offset_pixels();
    float y = (SCREEN_HEIGHT - lry / 4.0f) * RATIO_Y;
    float width = (lrx - ulx) / 4.0f * xScale;
    float height = (lry - uly) / 4.0f * RATIO_Y;

    struct XYWidthHeight scissor = { x, y, width, height };

    if (memcmp(&rdp.scissor, &scissor, sizeof(scissor)) == 0) {
        return;
    }
    rdp.scissor = scissor;

    rdp.viewport_or_scissor_changed = true;
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_dp_set_texture_image(uint32_t format, uint32_t size, uint32_t width, const void* addr) {
    rdp.texture_to_load.addr = addr;
    rdp.texture_to_load.fmt = format;
    rdp.texture_to_load.siz = size;
    rdp.texture_to_load.width = width;
}

static void gfx_dp_set_tile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile, UNUSED uint32_t palette, uint32_t cmt, uint32_t maskt, uint32_t shiftt, uint32_t cms, uint32_t masks, uint32_t shifts) {
    int renderSlot;

    if (gfx_get_render_tile_slot(tile, &renderSlot)) {
        TextureTileState* tileState = gfx_get_texture_tile(renderSlot);
        const uint32_t lineSizeBytes = line * 8;

        if ((renderSlot == 0) && (tileState->fmt == fmt) && (tileState->siz == siz) &&
            (tileState->cms == cms) && (tileState->cmt == cmt) &&
            (tileState->masks == masks) && (tileState->maskt == maskt) &&
            (tileState->shifts == shifts) && (tileState->shiftt == shiftt) &&
            (tileState->line_size_bytes == lineSizeBytes)) {
            return;
        }

        const bool oldMirrorS = (tileState->cms & G_TX_MIRROR) != 0 && tileState->masks != G_TX_NOMASK;
        const bool oldMirrorT = (tileState->cmt & G_TX_MIRROR) != 0 && tileState->maskt != G_TX_NOMASK;
        const bool newMirrorS = (cms & G_TX_MIRROR) != 0 && masks != G_TX_NOMASK;
        const bool newMirrorT = (cmt & G_TX_MIRROR) != 0 && maskt != G_TX_NOMASK;
        bool descriptorChanged = (tileState->fmt != fmt) || (tileState->siz != siz) ||
                                 (tileState->cms != cms) || (tileState->cmt != cmt) ||
                                 (tileState->masks != masks) || (tileState->maskt != maskt) ||
                                 (tileState->shifts != shifts) || (tileState->shiftt != shiftt) ||
                                 (tileState->line_size_bytes != lineSizeBytes);
        bool textureImportChanged = (tileState->fmt != fmt) || (tileState->siz != siz) ||
                                    (tileState->line_size_bytes != lineSizeBytes) ||
                                    (oldMirrorS != newMirrorS) || (oldMirrorT != newMirrorT);

        if (tile == G_TX_RENDERTILE) {
            SUPPORT_CHECK(palette == 0);
        }
        tileState->fmt = fmt;
        tileState->siz = siz;
        tileState->cms = cms;
        tileState->cmt = cmt;
        tileState->masks = masks;
        tileState->maskt = maskt;
        tileState->shifts = shifts;
        tileState->shiftt = shiftt;
        tileState->line_size_bytes = lineSizeBytes;
        if (renderSlot == 1 && tmem == 0 && rdp.loaded_texture[0].addr != NULL) {
            const bool sourceChanged = rdp.textures_changed[0] ||
                                       (rdp.loaded_texture[1].addr != rdp.loaded_texture[0].addr) ||
                                       (rdp.loaded_texture[1].size_bytes != rdp.loaded_texture[0].size_bytes) ||
                                       (rdp.loaded_texture[1].source_size_bytes != rdp.loaded_texture[0].source_size_bytes) ||
                                       (rdp.loaded_texture[1].row_stride_bytes != rdp.loaded_texture[0].row_stride_bytes) ||
                                       (rdp.loaded_texture[1].source_nibble_offset != rdp.loaded_texture[0].source_nibble_offset);

            descriptorChanged = descriptorChanged || sourceChanged;
            textureImportChanged = textureImportChanged || sourceChanged;

            rdp.loaded_texture[1] = rdp.loaded_texture[0];
        }
        if (descriptorChanged) {

            if (textureImportChanged) {
                rdp.textures_changed[renderSlot] = true;
            }
            gfx_mark_tri_pipeline_dirty();
        }
    }

    if (tile == G_TX_LOADTILE) {

        rdp.texture_to_load.tile_number = tmem != 0;
    }
}

static void gfx_dp_set_tile_size(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    int renderSlot;

    if (gfx_get_render_tile_slot(tile, &renderSlot)) {
        TextureTileState* tileState = gfx_get_texture_tile(renderSlot);

        if ((tileState->uls == uls) && (tileState->ult == ult) &&
            (tileState->lrs == lrs) && (tileState->lrt == lrt)) {
            return;
        }

        const uint16_t oldSpanS = tileState->lrs - tileState->uls;
        const uint16_t oldSpanT = tileState->lrt - tileState->ult;
        const bool zeroWidthScroll = (((uint32_t)lrs + 4U) & 0x0FFFU) == ((uint32_t)uls & 0x0FFFU);
        const bool zeroHeightScroll = (((uint32_t)lrt + 4U) & 0x0FFFU) == ((uint32_t)ult & 0x0FFFU);

        if (zeroWidthScroll && oldSpanS < 0x0FFCU) {
            lrs = uls + oldSpanS;
        }
        if (zeroHeightScroll && oldSpanT < 0x0FFCU) {
            lrt = ult + oldSpanT;
        }
        const uint16_t newSpanS = lrs - uls;
        const uint16_t newSpanT = lrt - ult;

        tileState->uls = uls;
        tileState->ult = ult;
        tileState->lrs = lrs;
        tileState->lrt = lrt;

        if ((oldSpanS != newSpanS) || (oldSpanT != newSpanT)) {
            rdp.textures_changed[renderSlot] = true;
        }
        gfx_mark_tri_pipeline_dirty();
    }
}

static void gfx_dp_load_tlut(UNUSED uint8_t tile, uint32_t high_index) {

    int loadSlot;

    if (tile != G_TX_LOADTILE || rdp.texture_to_load.siz != G_IM_SIZ_16b ||
        !gfx_texture_load_slot("load-tlut-slot", &loadSlot)) {
        gfx_log_bad_texture_source(-1, "load-tlut-command", rdp.texture_to_load.addr, GFX_TLUT_SIZE_BYTES);
        return;
    }
    if ((sPs2PaletteLoadStamp.frameSerial == sTextureCacheFrameSerial) &&
        (sPs2PaletteLoadStamp.addr == rdp.texture_to_load.addr) &&
        (sPs2PaletteLoadStamp.highIndex == high_index) &&
        (rdp.palette == rdp.texture_to_load.addr)) {
        return;
    }
    sPs2PaletteLoadStamp.frameSerial = sTextureCacheFrameSerial;
    sPs2PaletteLoadStamp.addr = rdp.texture_to_load.addr;
    sPs2PaletteLoadStamp.highIndex = high_index;
    const uint32_t paletteSize = (high_index + 1) * sizeof(uint16_t);
    const uint32_t paletteKey = gfx_texture_palette_key(rdp.texture_to_load.addr, paletteSize);
    const bool paletteChanged = (rdp.palette != rdp.texture_to_load.addr) ||
                                (rdp.palette_key != paletteKey);

    rdp.palette = rdp.texture_to_load.addr;
    rdp.palette_key = paletteKey;
    if (paletteChanged) {
        rdp.textures_changed[0] = true;
        rdp.textures_changed[1] = true;
        gfx_mark_tri_pipeline_dirty();
    }
}

static uint32_t gfx_ps2_load_block_row_bytes(uint32_t dxt) {
    if (dxt == 0) {
        return 0;
    }

    const uint32_t wordsPerRow = ((1U << G_TX_DXT_FRAC) + dxt - 1U) / dxt;
    return wordsPerRow * 8U;
}

static void gfx_dp_load_block(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt) {

    if (tile == 1) return;
    int loadSlot;

    if (tile != G_TX_LOADTILE || uls != 0 || ult != 0 ||
        !gfx_texture_load_slot("load-block-slot", &loadSlot)) {
        gfx_log_bad_texture_source(-1, "load-block-command", rdp.texture_to_load.addr, 1);
        return;
    }

    uint32_t word_size_shift = 0;
    switch (rdp.texture_to_load.siz) {
        case G_IM_SIZ_4b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }
    uint32_t size_bytes;
    if (rdp.texture_to_load.siz == G_IM_SIZ_4b) {
        size_bytes = (lrs + 2) >> 1;
    } else {
        size_bytes = (lrs + 1) << word_size_shift;
    }
    const uint32_t ps2RowBytes = gfx_ps2_load_block_row_bytes(dxt);
    const bool sameFrameLoad = gfx_ps2_same_frame_texture_load(loadSlot, rdp.texture_to_load.addr, size_bytes,
                                                               size_bytes, ps2RowBytes, 0, rdp.texture_to_load.fmt, rdp.texture_to_load.siz);
    if (sameFrameLoad && loadSlot == 0) {
        return;
    }
    rdp.loaded_texture[loadSlot].size_bytes = size_bytes;
    rdp.loaded_texture[loadSlot].source_size_bytes = size_bytes;
    rdp.loaded_texture[loadSlot].load_row_bytes = ps2RowBytes;
    rdp.loaded_texture[loadSlot].row_stride_bytes = ps2RowBytes;
    rdp.loaded_texture[loadSlot].source_nibble_offset = 0;

    rdp.loaded_texture[loadSlot].addr = rdp.texture_to_load.addr;

    if (!sameFrameLoad) {
        rdp.textures_changed[loadSlot] = true;
        gfx_mark_tri_pipeline_dirty();
    }
}

static void gfx_dp_load_tile(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    if (tile == 1) return;
    int loadSlot;

    if (tile != G_TX_LOADTILE || !gfx_texture_load_slot("load-tile-slot", &loadSlot)) {
        gfx_log_bad_texture_source(-1, "load-tile-command", rdp.texture_to_load.addr, 1);
        return;
    }

    uint32_t source_uls = uls >> G_TEXTURE_IMAGE_FRAC;
    uint32_t source_ult = ult >> G_TEXTURE_IMAGE_FRAC;
    uint32_t source_lrs = lrs >> G_TEXTURE_IMAGE_FRAC;
    uint32_t source_lrt = lrt >> G_TEXTURE_IMAGE_FRAC;
    if (source_lrs < source_uls || source_lrt < source_ult) {
        gfx_log_bad_texture_source(loadSlot, "load-tile-bounds", rdp.texture_to_load.addr, 1);
        gfx_set_invalid_loaded_texture(loadSlot);
        gfx_set_invalid_texture_tile(loadSlot);
        rdp.textures_changed[loadSlot] = true;
        gfx_mark_tri_pipeline_dirty();
        return;
    }
    uint32_t width = source_lrs - source_uls + 1;
    uint32_t height = source_lrt - source_ult + 1;
    uint32_t source_width = rdp.texture_to_load.width;

    if (source_width == 0 || source_width < source_lrs + 1) {
        source_width = source_lrs + 1;
    }

    uint32_t source_stride = gfx_texture_row_bytes(source_width, rdp.texture_to_load.siz);
    uint32_t source_x_offset = gfx_texture_byte_offset(source_uls, rdp.texture_to_load.siz);
    uint32_t row_bytes = gfx_texture_row_bytes(width + (rdp.texture_to_load.siz == G_IM_SIZ_4b ? (source_uls & 1) : 0),
                                               rdp.texture_to_load.siz);
    uint32_t size_bytes = row_bytes * height;
    uint32_t source_size_bytes = height > 0 ? ((height - 1) * source_stride) + row_bytes : 0;
    const uint8_t* source_addr = rdp.texture_to_load.addr + (size_t)source_ult * source_stride + source_x_offset;
    const uint8_t sourceNibbleOffset = rdp.texture_to_load.siz == G_IM_SIZ_4b ? (source_uls & 1) : 0;
    const bool sameFrameLoad = gfx_ps2_same_frame_texture_load(loadSlot, source_addr, size_bytes, source_size_bytes,
                                                               source_stride, sourceNibbleOffset,
                                                               rdp.texture_to_load.fmt, rdp.texture_to_load.siz);

    rdp.loaded_texture[loadSlot].size_bytes = size_bytes;
    rdp.loaded_texture[loadSlot].source_size_bytes = source_size_bytes;
    rdp.loaded_texture[loadSlot].row_stride_bytes = source_stride;
    rdp.loaded_texture[loadSlot].load_row_bytes = row_bytes;
    rdp.loaded_texture[loadSlot].source_nibble_offset = sourceNibbleOffset;

    rdp.loaded_texture[loadSlot].addr = source_addr;
    gfx_get_texture_tile(loadSlot)->uls = uls;
    gfx_get_texture_tile(loadSlot)->ult = ult;
    gfx_get_texture_tile(loadSlot)->lrs = lrs;
    gfx_get_texture_tile(loadSlot)->lrt = lrt;

    if (!sameFrameLoad) {
        rdp.textures_changed[loadSlot] = true;
        gfx_mark_tri_pipeline_dirty();
    }
}

static uint8_t color_comb_component(uint32_t v) {
    switch (v) {
        case G_CCMUX_TEXEL0:
            return CC_TEXEL0;
        case G_CCMUX_TEXEL1:
            return CC_TEXEL1;
        case G_CCMUX_PRIMITIVE:
            return CC_PRIM;
        case G_CCMUX_SHADE:
            return CC_SHADE;
        case G_CCMUX_ENVIRONMENT:
            return CC_ENV;
        case G_CCMUX_TEXEL0_ALPHA:
            return CC_TEXEL0A;
        case G_CCMUX_LOD_FRACTION:
            return CC_LOD;
        default:
            return CC_0;
    }
}

static inline uint32_t color_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return color_comb_component(a) |
           (color_comb_component(b) << 3) |
           (color_comb_component(c) << 6) |
           (color_comb_component(d) << 9);
}

static bool gfx_cc_is_two_cycle_texture_tint(uint32_t a0, uint32_t b0, uint32_t c0, uint32_t d0, uint32_t a1,
                                             uint32_t b1, uint32_t c1, uint32_t d1) {
    return ((a0 == G_CCMUX_TEXEL0) || (a0 == G_CCMUX_TEXEL1)) &&
           ((b0 == G_CCMUX_PRIMITIVE) || (b0 == G_CCMUX_TEXEL0)) &&
           ((c0 == G_CCMUX_ENV_ALPHA) || (c0 == G_CCMUX_PRIM_LOD_FRAC)) && (d0 == G_CCMUX_TEXEL0) &&
           (a1 == G_CCMUX_PRIMITIVE) && (b1 == G_CCMUX_ENVIRONMENT) && (c1 == G_CCMUX_COMBINED) &&
           (d1 == G_CCMUX_ENVIRONMENT);
}

static bool gfx_cc_is_two_cycle_texture_shade_prim_tint(uint32_t a0, uint32_t b0, uint32_t c0, uint32_t d0,
                                                        uint32_t a1, uint32_t b1, uint32_t c1, uint32_t d1) {
    return (a0 == G_CCMUX_TEXEL1) && (b0 == G_CCMUX_TEXEL0) && (c0 == G_CCMUX_PRIM_LOD_FRAC) &&
           (d0 == G_CCMUX_TEXEL0) && (a1 == G_CCMUX_SHADE) && (b1 == G_CCMUX_PRIMITIVE) &&
           (c1 == G_CCMUX_COMBINED) && (d1 == G_CCMUX_PRIMITIVE);
}

static bool gfx_cc_is_two_cycle_texture_blend_mul_shade(uint32_t a0, uint32_t b0, uint32_t c0, uint32_t d0,
                                                        uint32_t a1, uint32_t b1, uint32_t c1, uint32_t d1,
                                                        uint32_t mixSource) {
    return (a0 == G_CCMUX_TEXEL1) && (b0 == G_CCMUX_TEXEL0) && (c0 == mixSource) &&
           (d0 == G_CCMUX_TEXEL0) && (a1 == G_CCMUX_COMBINED) && (b1 == (G_CCMUX_0 & 0xF)) &&
           (c1 == G_CCMUX_SHADE) && (d1 == (G_CCMUX_0 & 0x7));
}

static bool gfx_cc_is_two_cycle_texture_blend_passthrough(uint32_t a0, uint32_t b0, uint32_t c0, uint32_t d0,
                                                          uint32_t a1, uint32_t b1, uint32_t c1, uint32_t d1,
                                                          uint32_t mixSource) {
    return (a0 == G_CCMUX_TEXEL1) && (b0 == G_CCMUX_TEXEL0) && (c0 == mixSource) &&
           (d0 == G_CCMUX_TEXEL0) && (a1 == (G_CCMUX_0 & 0xF)) &&
           (b1 == (G_CCMUX_0 & 0xF)) && (c1 == G_CCMUX_0) && (d1 == G_CCMUX_COMBINED);
}

static bool gfx_cc_is_alpha_two_texture_blend(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                                               uint32_t mixSource) {
    return (a == G_ACMUX_TEXEL1) && (b == G_ACMUX_TEXEL0) && (c == mixSource) &&
           (d == G_ACMUX_TEXEL0);
}

static bool gfx_cc_is_texture_prim_env_blend(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a == G_CCMUX_PRIMITIVE) && (b == G_CCMUX_ENVIRONMENT) && (c == G_CCMUX_TEXEL0) &&
           (d == G_CCMUX_ENVIRONMENT);
}

static bool gfx_cc_is_color_mul(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t lhs, uint32_t rhs) {
    return (b == (G_CCMUX_0 & 0xF)) && (d == (G_CCMUX_0 & 0x7)) &&
           (((a == lhs) && (c == rhs)) || ((a == rhs) && (c == lhs)));
}

static bool gfx_cc_is_one(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a == G_ACMUX_0) && (b == G_ACMUX_0) && (c == G_ACMUX_0) && (d == G_ACMUX_1);
}

static bool gfx_cc_is_combined_mul_primitive(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a == G_ACMUX_COMBINED) && (b == G_ACMUX_0) && (c == G_ACMUX_PRIMITIVE) &&
           (d == G_ACMUX_0);
}

static bool gfx_cc_is_two_i4_env_prim_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0, uint32_t rgbD0,
                                      uint32_t alphaA0, uint32_t alphaB0, uint32_t alphaC0,
                                      uint32_t alphaD0, uint32_t rgbA1, uint32_t rgbB1,
                                      uint32_t rgbC1, uint32_t rgbD1, uint32_t alphaA1,
                                      uint32_t alphaB1, uint32_t alphaC1, uint32_t alphaD1) {
    return (rgbA0 == G_CCMUX_TEXEL0) && (rgbB0 == (G_CCMUX_0 & 0xFU)) &&
           (rgbC0 == G_CCMUX_ENV_ALPHA) && (rgbD0 == G_CCMUX_TEXEL1) &&
           (alphaA0 == G_ACMUX_TEXEL0) && (alphaB0 == G_ACMUX_1) &&
           (alphaC0 == G_ACMUX_ENVIRONMENT) && (alphaD0 == G_ACMUX_TEXEL1) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
           gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1);
}

static bool gfx_cc_is_combined_mul_shade(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a == G_ACMUX_COMBINED) && (b == G_ACMUX_0) && (c == G_ACMUX_SHADE) &&
           (d == G_ACMUX_0);
}

static bool gfx_cc_is_combined_mul_environment(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a == G_ACMUX_COMBINED) && (b == G_ACMUX_0) && (c == G_ACMUX_ENVIRONMENT) &&
           (d == G_ACMUX_0);
}

static bool gfx_cc_is_two_texture_prim_lod_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0,
                                                uint32_t rgbD0, uint32_t alphaA0, uint32_t alphaB0,
                                                uint32_t alphaC0, uint32_t alphaD0, uint32_t rgbA1,
                                                uint32_t rgbB1, uint32_t rgbC1, uint32_t rgbD1,
                                                uint32_t alphaA1, uint32_t alphaB1, uint32_t alphaC1,
                                                uint32_t alphaD1) {
    return (rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
           (rgbC0 == G_CCMUX_PRIM_LOD_FRAC) && (rgbD0 == G_CCMUX_TEXEL0) &&
           (alphaA0 == G_ACMUX_TEXEL1) && (alphaB0 == G_ACMUX_TEXEL0) &&
           (alphaC0 == G_ACMUX_PRIM_LOD_FRAC) && (alphaD0 == G_ACMUX_TEXEL0) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
           gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1);
}

static bool gfx_cc_is_weather_crossfade_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0,
                                              uint32_t rgbD0, uint32_t alphaA0, uint32_t alphaB0,
                                              uint32_t alphaC0, uint32_t alphaD0, uint32_t rgbA1,
                                              uint32_t rgbB1, uint32_t rgbC1, uint32_t rgbD1,
                                              uint32_t alphaA1, uint32_t alphaB1, uint32_t alphaC1,
                                              uint32_t alphaD1) {
    return (rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_TEXEL0) &&
           (rgbC0 == G_CCMUX_PRIM_LOD_FRAC) && (rgbD0 == G_CCMUX_TEXEL0) &&
           (alphaA0 == G_ACMUX_TEXEL1) && (alphaB0 == G_ACMUX_TEXEL0) &&
           ((alphaC0 == G_ACMUX_ENVIRONMENT) || (alphaC0 == G_ACMUX_PRIM_LOD_FRAC)) &&
           (alphaD0 == G_ACMUX_TEXEL0) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
           gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1);
}

static bool gfx_cc_is_two_texture_self_modulated_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0,
                                                       uint32_t rgbD0, uint32_t alphaA0, uint32_t alphaB0,
                                                       uint32_t alphaC0, uint32_t alphaD0, uint32_t rgbA1,
                                                       uint32_t rgbB1, uint32_t rgbC1, uint32_t rgbD1,
                                                       uint32_t alphaA1, uint32_t alphaB1, uint32_t alphaC1,
                                                       uint32_t alphaD1) {
    return (rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
           (rgbC0 == G_CCMUX_TEXEL0) && (rgbD0 == G_CCMUX_TEXEL0) &&
           gfx_cc_is_alpha_two_texture_blend(alphaA0, alphaB0, alphaC0, alphaD0,
                                              G_ACMUX_PRIM_LOD_FRAC) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
           gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1);
}

static bool gfx_cc_is_two_texture_lod_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0,
                                           uint32_t rgbD0, uint32_t alphaA0, uint32_t alphaB0,
                                           uint32_t alphaC0, uint32_t alphaD0, uint32_t rgbA1,
                                           uint32_t rgbB1, uint32_t rgbC1, uint32_t rgbD1,
                                           uint32_t alphaA1, uint32_t alphaB1, uint32_t alphaC1,
                                           uint32_t alphaD1) {
    return (rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_TEXEL0) &&
           (rgbC0 == G_CCMUX_LOD_FRACTION) && (rgbD0 == G_CCMUX_TEXEL0) &&
           gfx_cc_is_alpha_two_texture_blend(alphaA0, alphaB0, alphaC0, alphaD0,
                                              G_ACMUX_LOD_FRACTION) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
           gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1);
}

static bool gfx_cc_is_single_texture_prim_lod_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0,
                                                   uint32_t rgbD0, uint32_t rgbA1, uint32_t rgbB1,
                                                   uint32_t rgbC1, uint32_t rgbD1) {
    return (rgbA0 == G_CCMUX_TEXEL0) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
           (rgbC0 == G_CCMUX_PRIM_LOD_FRAC) && (rgbD0 == G_CCMUX_TEXEL0) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT);
}

static bool gfx_cc_is_standalone_heart_tint(uint32_t rgbA0, uint32_t rgbB0, uint32_t rgbC0,
                                            uint32_t rgbD0, uint32_t alphaA0, uint32_t alphaB0,
                                            uint32_t alphaC0, uint32_t alphaD0, uint32_t rgbA1,
                                            uint32_t rgbB1, uint32_t rgbC1, uint32_t rgbD1,
                                            uint32_t alphaA1, uint32_t alphaB1, uint32_t alphaC1,
                                            uint32_t alphaD1) {
    return (alphaA0 == G_ACMUX_0) && (alphaB0 == G_ACMUX_0) &&
           (alphaC0 == G_ACMUX_0) && (alphaD0 == G_ACMUX_TEXEL0) &&
           (alphaA1 == G_ACMUX_0) && (alphaB1 == G_ACMUX_0) &&
           (alphaC1 == G_ACMUX_0) && (alphaD1 == G_ACMUX_COMBINED) &&
           (rgbA0 == G_CCMUX_TEXEL0) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
           (rgbC0 == G_CCMUX_ENV_ALPHA) && (rgbD0 == G_CCMUX_TEXEL0) &&
           (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
           (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT);
}

static void gfx_dp_set_combine_mode(uint32_t rgb, uint32_t alpha, bool color_mul_env, bool color_mul_prim,
                                    bool texture_blend, bool texture_blend_shade, bool texture_blend_reverse,
                                    bool two_texture_blend,
                                    bool two_texture_blend_uses_prim_lod, bool two_texture_alpha_blend,
                                    bool alpha_mul_env, bool two_intensity_env_prim_precombine,
                                    bool flame_texture_atlas,
                                    bool texture_tint_uses_prim_lod,
                                    bool texture_tint_uses_env_alpha,
                                    bool din_fire_tint) {
    uint32_t combineMode = rgb | (alpha << 12);

    if (texture_blend) {
        combineMode |= texture_blend_shade ? SHADER_OPT_TEXTURE_BLEND_SHADE : SHADER_OPT_TEXTURE_BLEND;
    }
    if ((rdp.combine_mode == combineMode) && (rdp.combine_color_mul_env == color_mul_env) &&
        (rdp.combine_color_mul_prim == color_mul_prim) &&
        (rdp.combine_texture_blend_reverse == texture_blend_reverse) &&
        (rdp.combine_two_texture_blend == two_texture_blend) &&
        (rdp.combine_two_texture_blend_uses_prim_lod == two_texture_blend_uses_prim_lod) &&
        (rdp.combine_two_texture_alpha_blend == two_texture_alpha_blend) &&
        (rdp.combine_alpha_mul_env == alpha_mul_env) &&
        (rdp.combine_two_intensity_env_prim_precombine == two_intensity_env_prim_precombine) &&
        (rdp.combine_flame_texture_atlas == flame_texture_atlas) &&
        (rdp.combine_texture_tint_uses_prim_lod == texture_tint_uses_prim_lod) &&
        (rdp.combine_texture_tint_uses_env_alpha == texture_tint_uses_env_alpha) &&
        (rdp.combine_din_fire_tint == din_fire_tint)) {
        return;
    }
    rdp.combine_mode = combineMode;
    rdp.combine_color_mul_env = color_mul_env;
    rdp.combine_color_mul_prim = color_mul_prim;
    rdp.combine_texture_blend_reverse = texture_blend_reverse;
    rdp.combine_two_texture_blend = two_texture_blend;
    rdp.combine_two_texture_blend_uses_prim_lod = two_texture_blend_uses_prim_lod;
    rdp.combine_two_texture_alpha_blend = two_texture_alpha_blend;
    rdp.combine_alpha_mul_env = alpha_mul_env;
    rdp.combine_two_intensity_env_prim_precombine = two_intensity_env_prim_precombine;
    rdp.combine_flame_texture_atlas = flame_texture_atlas;
    rdp.combine_texture_tint_uses_prim_lod = texture_tint_uses_prim_lod;
    rdp.combine_texture_tint_uses_env_alpha = texture_tint_uses_env_alpha;
    rdp.combine_din_fire_tint = din_fire_tint;
    gfx_mark_tri_pipeline_dirty();
}

static GFX_DL_HANDLER void gfx_dp_set_combine(uint32_t w0, uint32_t w1) {
#define COMB_FIELD(word, pos, width) (((word) >> (pos)) & ((1U << (width)) - 1))
    uint32_t rgbA0 = COMB_FIELD(w0, 20, 4);
    uint32_t rgbB0 = COMB_FIELD(w1, 28, 4);
    uint32_t rgbC0 = COMB_FIELD(w0, 15, 5);
    uint32_t rgbD0 = COMB_FIELD(w1, 15, 3);
    uint32_t alphaA0 = COMB_FIELD(w0, 12, 3);
    uint32_t alphaB0 = COMB_FIELD(w1, 12, 3);
    uint32_t alphaC0 = COMB_FIELD(w0, 9, 3);
    uint32_t alphaD0 = COMB_FIELD(w1, 9, 3);
    uint32_t rgbA1 = COMB_FIELD(w0, 5, 4);
    uint32_t rgbB1 = COMB_FIELD(w1, 24, 4);
    uint32_t rgbC1 = COMB_FIELD(w0, 0, 5);
    uint32_t rgbD1 = COMB_FIELD(w1, 6, 3);
    uint32_t alphaA1 = COMB_FIELD(w1, 21, 3);
    uint32_t alphaB1 = COMB_FIELD(w1, 3, 3);
    uint32_t alphaC1 = COMB_FIELD(w1, 18, 3);
    uint32_t alphaD1 = COMB_FIELD(w1, 0, 3);
    bool colorMulTexelShade =
        gfx_cc_is_color_mul(rgbA0, rgbB0, rgbC0, rgbD0, G_CCMUX_TEXEL0, G_CCMUX_SHADE);
    bool colorMulEnv = colorMulTexelShade &&
                       gfx_cc_is_color_mul(rgbA1, rgbB1, rgbC1, rgbD1, G_CCMUX_ENVIRONMENT,
                                           G_CCMUX_COMBINED);
    bool colorMulPrim = colorMulTexelShade &&
                        gfx_cc_is_color_mul(rgbA1, rgbB1, rgbC1, rgbD1, G_CCMUX_COMBINED,
                                            G_CCMUX_PRIMITIVE);
    bool colorMulShadePrim =
        gfx_cc_is_color_mul(rgbA0, rgbB0, rgbC0, rgbD0, G_CCMUX_SHADE, G_CCMUX_PRIMITIVE);
    bool textureBlend = gfx_cc_is_texture_prim_env_blend(rgbA0, rgbB0, rgbC0, rgbD0);
    bool textureBlendShade = false;
    bool textureBlendReverse = false;
    bool twoTextureBlend = false;
    bool twoTextureBlendUsesPrimLod = false;
    bool twoTextureAlphaBlend = false;
    bool alphaMulEnv = false;
    bool twoIntensityEnvPrimPrecombine = false;
    bool flameTextureAtlas = false;
    bool dinFireTint = false;
    bool textureTintUsesPrimLod = false;
    bool singleTexturePrimLodTint = false;
    bool singleTextureEnvAlphaTint = false;
    uint32_t rgbComb = color_comb(rgbA0, rgbB0, rgbC0, rgbD0);
    uint32_t alphaComb = color_comb(alphaA0, alphaB0, alphaC0, alphaD0);

    if ((rgbA0 == G_CCMUX_ENVIRONMENT) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
        (rgbC0 == G_CCMUX_TEXEL0) && (rgbD0 == G_CCMUX_PRIMITIVE)) {
        textureBlend = true;
        textureBlendReverse = true;
    }

    if ((rgbA0 == G_CCMUX_1) && (rgbB0 == (G_CCMUX_0 & 0xF)) && (rgbD0 == (G_CCMUX_0 & 0x7))) {
        rgbComb = color_comb(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, rgbC0);
    }
    if ((alphaA0 == G_ACMUX_1) && (alphaB0 == G_ACMUX_0) && (alphaD0 == G_ACMUX_0)) {
        alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, alphaC0);
    }
    if (colorMulShadePrim) {
        rgbComb = color_comb(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_SHADE);
        colorMulPrim = true;
    }
    if (((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE) &&
        gfx_cc_is_one(alphaA0, alphaB0, alphaC0, alphaD0)) {
        if (gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1)) {

            alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_PRIMITIVE);
        } else if (gfx_cc_is_combined_mul_shade(alphaA1, alphaB1, alphaC1, alphaD1)) {

            alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE);
        }
    }
    if (gfx_cc_is_two_cycle_texture_blend_mul_shade(rgbA0, rgbB0, rgbC0, rgbD0, rgbA1, rgbB1, rgbC1,
                                                    rgbD1, G_CCMUX_ENV_ALPHA) ||
        gfx_cc_is_two_cycle_texture_blend_mul_shade(rgbA0, rgbB0, rgbC0, rgbD0, rgbA1, rgbB1, rgbC1,
                                                    rgbD1, G_CCMUX_LOD_FRACTION)) {
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = rgbC0 == G_CCMUX_LOD_FRACTION;
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE, G_CCMUX_0);
        if (gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1)) {
            alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_PRIMITIVE);
        }
    }
    if ((rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_TEXEL0) &&
        (rgbC0 == G_CCMUX_ENV_ALPHA) && (rgbD0 == G_CCMUX_TEXEL0) &&
        (rgbA1 == G_CCMUX_COMBINED) && (rgbB1 == (G_CCMUX_0 & 0xF)) &&
        (rgbC1 == G_CCMUX_PRIMITIVE) && (rgbD1 == (G_CCMUX_0 & 0x7))) {
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = false;
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE, G_CCMUX_0);
    }
    if (gfx_cc_is_two_cycle_texture_blend_passthrough(rgbA0, rgbB0, rgbC0, rgbD0, rgbA1, rgbB1, rgbC1,
                                                       rgbD1, G_CCMUX_ENV_ALPHA)) {
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = false;

        rgbComb = color_comb(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0);
        alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_1);
    }
    if (((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE) &&
        gfx_cc_is_combined_mul_environment(alphaA1, alphaB1, alphaC1, alphaD1)) {

        alphaMulEnv = true;
    }
    if (twoTextureBlend &&
        gfx_cc_is_alpha_two_texture_blend(alphaA0, alphaB0, alphaC0, alphaD0,
                                          twoTextureBlendUsesPrimLod ? G_ACMUX_LOD_FRACTION
                                                                     : G_ACMUX_ENVIRONMENT)) {
        if (gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1)) {
            alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        } else if (gfx_cc_is_combined_mul_shade(alphaA1, alphaB1, alphaC1, alphaD1)) {
            alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_SHADE, G_ACMUX_0);
        } else {
            alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0);
        }
        twoTextureAlphaBlend = true;
    }
    if (gfx_cc_is_two_texture_lod_tint(rgbA0, rgbB0, rgbC0, rgbD0, alphaA0, alphaB0, alphaC0,
                                       alphaD0, rgbA1, rgbB1, rgbC1, rgbD1, alphaA1, alphaB1,
                                       alphaC1, alphaD1)) {
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE, G_CCMUX_0);
        alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        textureBlend = true;
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = true;
        twoTextureAlphaBlend = true;

    }
    if (gfx_cc_is_two_cycle_texture_tint(rgbA0, rgbB0, rgbC0, rgbD0, rgbA1, rgbB1, rgbC1, rgbD1)) {
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE, G_CCMUX_0);
        if (gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1) &&
            !gfx_cc_is_one(alphaA0, alphaB0, alphaC0, alphaD0)) {
            alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        } else if (gfx_cc_is_one(alphaA1, alphaB1, alphaC1, alphaD1)) {
            alphaComb = color_comb(G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_1);
        }
        textureBlend = true;

        if ((rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_TEXEL0) &&
            (rgbC0 == G_CCMUX_ENV_ALPHA) && (rgbD0 == G_CCMUX_TEXEL0)) {
            twoTextureBlend = true;
            twoTextureBlendUsesPrimLod = false;
            twoTextureAlphaBlend =
                gfx_cc_is_alpha_two_texture_blend(alphaA0, alphaB0, alphaC0, alphaD0,
                                                  G_ACMUX_ENVIRONMENT);
            singleTextureEnvAlphaTint = true;
        }
    }
    if (gfx_cc_is_two_cycle_texture_shade_prim_tint(rgbA0, rgbB0, rgbC0, rgbD0, rgbA1, rgbB1, rgbC1, rgbD1)) {
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE, G_CCMUX_0);
        textureBlend = true;
        textureBlendShade = true;
    }

    textureTintUsesPrimLod = textureTintUsesPrimLod ||
        gfx_cc_is_two_texture_prim_lod_tint(rgbA0, rgbB0, rgbC0, rgbD0, alphaA0, alphaB0, alphaC0,
                                            alphaD0, rgbA1, rgbB1, rgbC1, rgbD1, alphaA1, alphaB1,
                                            alphaC1, alphaD1);
    if (textureTintUsesPrimLod) {
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE, G_CCMUX_0);
        alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        textureBlend = true;
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = true;
        twoTextureAlphaBlend = true;
    }

    if (gfx_cc_is_weather_crossfade_tint(rgbA0, rgbB0, rgbC0, rgbD0, alphaA0, alphaB0,
                                         alphaC0, alphaD0, rgbA1, rgbB1, rgbC1, rgbD1,
                                         alphaA1, alphaB1, alphaC1, alphaD1)) {
        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE, G_CCMUX_0);
        alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        textureBlend = true;
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = true;
        twoTextureAlphaBlend = alphaC0 == G_ACMUX_PRIM_LOD_FRAC;
    }

    if (gfx_cc_is_two_texture_self_modulated_tint(rgbA0, rgbB0, rgbC0, rgbD0, alphaA0, alphaB0,
                                                   alphaC0, alphaD0, rgbA1, rgbB1, rgbC1, rgbD1,
                                                   alphaA1, alphaB1, alphaC1, alphaD1)) {

        rgbComb = color_comb(G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE, G_CCMUX_0);
        alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        textureBlend = true;
        twoTextureBlend = true;
        twoTextureBlendUsesPrimLod = true;
        twoTextureAlphaBlend = true;
    }

    singleTexturePrimLodTint =
        gfx_cc_is_single_texture_prim_lod_tint(rgbA0, rgbB0, rgbC0, rgbD0, rgbA1, rgbB1, rgbC1, rgbD1);
    singleTextureEnvAlphaTint =
        gfx_cc_is_standalone_heart_tint(rgbA0, rgbB0, rgbC0, rgbD0, alphaA0, alphaB0, alphaC0,
                                        alphaD0, rgbA1, rgbB1, rgbC1, rgbD1, alphaA1, alphaB1,
                                        alphaC1, alphaD1) || singleTextureEnvAlphaTint;

    if (((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE) &&
        (rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
        (rgbC0 == G_CCMUX_PRIM_LOD_FRAC) && (rgbD0 == G_CCMUX_TEXEL0) &&
        (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
        (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
        (alphaA0 == G_ACMUX_TEXEL1) &&
        ((alphaB0 == G_ACMUX_0) || (alphaB0 == G_ACMUX_1)) &&
        (alphaC0 == G_ACMUX_PRIM_LOD_FRAC) && (alphaD0 == G_ACMUX_TEXEL0) &&
        gfx_cc_is_combined_mul_shade(alphaA1, alphaB1, alphaC1, alphaD1)) {
        rgbComb = color_comb(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0);
        alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_SHADE, G_ACMUX_0);
        textureBlend = true;
        textureBlendShade = false;
        twoTextureBlend = false;
        twoTextureBlendUsesPrimLod = false;
        twoTextureAlphaBlend = false;
        dinFireTint = true;
    }

    if (((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE) &&
        (gfx_cc_is_two_i4_env_prim_tint(rgbA0, rgbB0, rgbC0, rgbD0, alphaA0, alphaB0,
                                       alphaC0, alphaD0, rgbA1, rgbB1, rgbC1, rgbD1,
                                       alphaA1, alphaB1, alphaC1, alphaD1) ||
         gfx_cc_is_two_cycle_texture_blend_mul_shade(rgbA0, rgbB0, rgbC0, rgbD0,
                                                     rgbA1, rgbB1, rgbC1, rgbD1,
                                                     G_CCMUX_ENV_ALPHA))) {
        twoIntensityEnvPrimPrecombine = true;
    }

    flameTextureAtlas = flameTextureAtlas ||
        (((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE) &&
        (rgbA0 == G_CCMUX_TEXEL1) && (rgbB0 == G_CCMUX_PRIMITIVE) &&
        (rgbC0 == G_CCMUX_PRIM_LOD_FRAC) && (rgbD0 == G_CCMUX_TEXEL0) &&
        (rgbA1 == G_CCMUX_PRIMITIVE) && (rgbB1 == G_CCMUX_ENVIRONMENT) &&
        (rgbC1 == G_CCMUX_COMBINED) && (rgbD1 == G_CCMUX_ENVIRONMENT) &&
        (alphaA0 == G_ACMUX_TEXEL1) && (alphaB0 == G_ACMUX_1) &&
        (alphaC0 == G_ACMUX_PRIM_LOD_FRAC) && (alphaD0 == G_ACMUX_TEXEL0) &&
        gfx_cc_is_combined_mul_primitive(alphaA1, alphaB1, alphaC1, alphaD1));
    if (flameTextureAtlas) {

        rgbComb = color_comb(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0);
        alphaComb = color_comb(G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0);
        textureBlend = false;
        twoTextureBlend = false;
        twoTextureAlphaBlend = false;
    }

    gfx_dp_set_combine_mode(rgbComb, alphaComb, colorMulEnv, colorMulPrim, textureBlend, textureBlendShade,
                            textureBlendReverse, twoTextureBlend, twoTextureBlendUsesPrimLod, twoTextureAlphaBlend, alphaMulEnv,
                            twoIntensityEnvPrimPrecombine, flameTextureAtlas,
                            textureTintUsesPrimLod || singleTexturePrimLodTint,
                            singleTextureEnvAlphaTint, dinFireTint);
#undef COMB_FIELD
}

static void gfx_dp_set_env_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if ((rdp.env_color.r == r) && (rdp.env_color.g == g) && (rdp.env_color.b == b) &&
        (rdp.env_color.a == a)) {
        return;
    }

    rdp.env_color.r = r;
    rdp.env_color.g = g;
    rdp.env_color.b = b;
    rdp.env_color.a = a;
    if (rdp.combine_texture_tint_uses_prim_lod || rdp.combine_texture_tint_uses_env_alpha ||
        rdp.combine_two_intensity_env_prim_precombine || rdp.combine_flame_texture_atlas) {
        gfx_mark_tri_pipeline_dirty();
    }
}

static void gfx_dp_set_prim_color(uint8_t lod_frac, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if ((rdp.prim_color.r == r) && (rdp.prim_color.g == g) && (rdp.prim_color.b == b) &&
        (rdp.prim_color.a == a) && (rdp.prim_lod_frac == lod_frac)) {
        return;
    }

    rdp.prim_lod_frac = lod_frac;
    rdp.prim_color.r = r;
    rdp.prim_color.g = g;
    rdp.prim_color.b = b;
    rdp.prim_color.a = a;
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_dp_set_fog_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if ((rdp.fog_color.r == r) && (rdp.fog_color.g == g) && (rdp.fog_color.b == b) &&
        (rdp.fog_color.a == a)) {
        return;
    }
    if (rendering_state.tri_pipeline.use_fog) {
        gfx_flush();
    }
    rdp.fog_color.r = r;
    rdp.fog_color.g = g;
    rdp.fog_color.b = b;
    rdp.fog_color.a = a;
}

static void gfx_dp_set_fill_color(uint32_t packed_color) {
    uint16_t col16 = (uint16_t)packed_color;
    uint32_t r = col16 >> 11;
    uint32_t g = (col16 >> 6) & 0x1f;
    uint32_t b = (col16 >> 1) & 0x1f;
    uint32_t a = col16 & 1;
    rdp.fill_color.r = SCALE_5_8(r);
    rdp.fill_color.g = SCALE_5_8(g);
    rdp.fill_color.b = SCALE_5_8(b);
    rdp.fill_color.a = a * 255;
}

static void gfx_dp_set_prim_depth(uint16_t z) {
    rdp.prim_depth = z;
}

static bool gfx_rectangle_covers_width(int32_t ulx, int32_t lrx) {
    return (ulx <= 0) && (lrx >= ((SCREEN_WIDTH - 1) << 2));
}

static bool gfx_rectangle_covers_screen(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    return gfx_rectangle_covers_width(ulx, lrx) && (uly <= 0) && (lry >= ((SCREEN_HEIGHT - 1) << 2));
}

static void gfx_draw_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, bool force_fullscreen,
                               bool force_full_width) {
    uint32_t saved_other_mode_h = rdp.other_mode_h;
    uint32_t cycle_type = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = (rdp.other_mode_h & ~(3U << G_MDSFT_TEXTFILT)) | G_TF_POINT;
        gfx_mark_tri_pipeline_dirty();
    }

    float ulxf = ulx;
    float ulyf = uly;
    float lrxf = lrx;
    float lryf = lry;

    if (force_fullscreen) {

        const float activeWidth = sPs2Widescreen ? (float)gfx_current_dimensions.width
                                                    : (float)gfx_current_dimensions.height * (4.0f / 3.0f);
        const float activeX = ((float)gfx_current_dimensions.width - activeWidth) * 0.5f;
        ulxf = activeX;
        lrxf = activeX + activeWidth;
        ulyf = 0.0f;
        lryf = gfx_current_dimensions.height;
    } else {
        const float halfWidth = (float)gfx_current_dimensions.width * 0.5f;
        const float halfHeight = (float)gfx_current_dimensions.height * 0.5f;
        const float hudOffset = gfx_hud_anchor_offset_pixels();

        ulyf = (ulyf / (4.0f * HALF_SCREEN_HEIGHT)) - 1.0f;
        lryf = (lryf / (4.0f * HALF_SCREEN_HEIGHT)) - 1.0f;

        if (force_full_width) {
            const float activeWidth = (float)gfx_current_dimensions.height * (4.0f / 3.0f);
            const float activeX = ((float)gfx_current_dimensions.width - activeWidth) * 0.5f;
            ulxf = activeX;
            lrxf = activeX + activeWidth;
        } else {
            ulxf = ulxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
            lrxf = lrxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
            ulxf = gfx_adjust_x_for_aspect_ratio(ulxf);
            lrxf = gfx_adjust_x_for_aspect_ratio(lrxf);
            ulxf = (ulxf * halfWidth) + halfWidth + hudOffset;
            lrxf = (lrxf * halfWidth) + halfWidth + hudOffset;
        }

        ulyf = (ulyf * halfHeight) + halfHeight;
        lryf = (lryf * halfHeight) + halfHeight;
    }

    struct VertexColor* ul = &rsp.loaded_vertices_2D[0];
    struct VertexColor* lr = &rsp.loaded_vertices_2D[1];

    ul->x = (unsigned short)ulxf;
    ul->y = (unsigned short)ulyf;

    lr->x = (unsigned short)lrxf;
    lr->y = (unsigned short)lryf;

    struct XYWidthHeight default_viewport = {0, 0, gfx_current_dimensions.width, gfx_current_dimensions.height};
    struct XYWidthHeight viewport_saved = rdp.viewport;
    struct XYWidthHeight scissor_saved = rdp.scissor;
    uint32_t geometry_mode_saved = rsp.geometry_mode;

    rdp.viewport = default_viewport;
    rdp.viewport_or_scissor_changed = true;

    rsp.geometry_mode = (rdp.other_mode_l & (Z_CMP | Z_UPD)) ? G_ZBUFFER : 0;
    gfx_mark_tri_pipeline_dirty();

    gfx_sp_tri1_2d(0, 1, 2);

    rsp.geometry_mode = geometry_mode_saved;
    rdp.viewport = viewport_saved;
    rdp.scissor = scissor_saved;
    rdp.viewport_or_scissor_changed = true;
    gfx_mark_tri_pipeline_dirty();

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = saved_other_mode_h;
        gfx_mark_tri_pipeline_dirty();
    }
}

static void gfx_dp_texture_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t uls, int16_t ult, int16_t dsdx, int16_t dtdy, bool flip) {
    _UNUSED(tile);

    uint32_t saved_combine_mode = rdp.combine_mode;
    bool saved_combine_color_mul_env = rdp.combine_color_mul_env;
    bool saved_combine_color_mul_prim = rdp.combine_color_mul_prim;
    bool saved_combine_texture_blend_reverse = rdp.combine_texture_blend_reverse;
    bool saved_combine_two_texture_blend = rdp.combine_two_texture_blend;
    bool saved_combine_two_texture_blend_uses_prim_lod = rdp.combine_two_texture_blend_uses_prim_lod;
    bool saved_combine_two_texture_alpha_blend = rdp.combine_two_texture_alpha_blend;
    bool saved_combine_alpha_mul_env = rdp.combine_alpha_mul_env;
    bool saved_combine_two_intensity_env_prim_precombine = rdp.combine_two_intensity_env_prim_precombine;
    bool saved_combine_flame_texture_atlas = rdp.combine_flame_texture_atlas;
    bool saved_combine_texture_tint_uses_prim_lod = rdp.combine_texture_tint_uses_prim_lod;
    bool saved_combine_texture_tint_uses_env_alpha = rdp.combine_texture_tint_uses_env_alpha;
    bool saved_combine_din_fire_tint = rdp.combine_din_fire_tint;
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {

        dsdx >>= 2;

        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_TEXEL0), color_comb(0, 0, 0, G_ACMUX_TEXEL0),
                                false, false, false, false, false, false, false, false, false, false, false, false, false, false);

        lrx += 1 << 2;
        lry += 1 << 2;
    }

    if (flip) {
        dsdx = -dsdx;
        dtdy = -dtdy;
    }
    int16_t width = !flip ? lrx - ulx : lry - uly;
    int16_t height = !flip ? lry - uly : lrx - ulx;
    float lrs = ((uls << 7) + dsdx * width) >> 7;
    float lrt = ((ult << 7) + dtdy * height) >> 7;

    struct VertexColor* ul = &rsp.loaded_vertices_2D[0];
    struct VertexColor* lr = &rsp.loaded_vertices_2D[1];

    #if 0
    if (!flip) {
        ll->u = uls;
        ll->v = lrt;
        ur->u = lrs;
        ur->v = ult;
    } else {
        ll->u = lrs;
        ll->v = ult;
        ur->u = uls;
        ur->v = lrt;
    }
    #endif

    {
        ul->u = uls;
        ul->v = ult;
        lr->u = lrs;
        lr->v = lrt;
        gfx_draw_rectangle(ulx, uly, lrx, lry, false, false);
    }
    rdp.combine_mode = saved_combine_mode;
    rdp.combine_color_mul_env = saved_combine_color_mul_env;
    rdp.combine_color_mul_prim = saved_combine_color_mul_prim;
    rdp.combine_texture_blend_reverse = saved_combine_texture_blend_reverse;
    rdp.combine_two_texture_blend = saved_combine_two_texture_blend;
    rdp.combine_two_texture_blend_uses_prim_lod = saved_combine_two_texture_blend_uses_prim_lod;
    rdp.combine_two_texture_alpha_blend = saved_combine_two_texture_alpha_blend;
    rdp.combine_alpha_mul_env = saved_combine_alpha_mul_env;
    rdp.combine_two_intensity_env_prim_precombine = saved_combine_two_intensity_env_prim_precombine;
    rdp.combine_flame_texture_atlas = saved_combine_flame_texture_atlas;
    rdp.combine_texture_tint_uses_prim_lod = saved_combine_texture_tint_uses_prim_lod;
    rdp.combine_texture_tint_uses_env_alpha = saved_combine_texture_tint_uses_env_alpha;
    rdp.combine_din_fire_tint = saved_combine_din_fire_tint;
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_dp_fill_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    if (rdp.color_image_address == rdp.z_buf_address) {

        return;
    }
    uint32_t mode = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));
    bool use_fill_color = (mode == G_CYC_COPY || mode == G_CYC_FILL);

    if (use_fill_color) {

        lrx += 1 << 2;
        lry += 1 << 2;
    }

    for (int i = 0; i < 2; i++) {
        struct VertexColor* v = &rsp.loaded_vertices_2D[i];
        v->color = use_fill_color ? rdp.fill_color : rdp.prim_color;
    }

    uint32_t saved_combine_mode = rdp.combine_mode;
    bool saved_combine_color_mul_env = rdp.combine_color_mul_env;
    bool saved_combine_color_mul_prim = rdp.combine_color_mul_prim;
    bool saved_combine_texture_blend_reverse = rdp.combine_texture_blend_reverse;
    bool saved_combine_two_texture_blend = rdp.combine_two_texture_blend;
    bool saved_combine_two_texture_blend_uses_prim_lod = rdp.combine_two_texture_blend_uses_prim_lod;
    bool saved_combine_two_texture_alpha_blend = rdp.combine_two_texture_alpha_blend;
    bool saved_combine_alpha_mul_env = rdp.combine_alpha_mul_env;
    bool saved_combine_two_intensity_env_prim_precombine = rdp.combine_two_intensity_env_prim_precombine;
    bool saved_combine_flame_texture_atlas = rdp.combine_flame_texture_atlas;
    bool saved_combine_texture_tint_uses_prim_lod = rdp.combine_texture_tint_uses_prim_lod;
    bool saved_combine_texture_tint_uses_env_alpha = rdp.combine_texture_tint_uses_env_alpha;
    bool saved_combine_din_fire_tint = rdp.combine_din_fire_tint;
    if (use_fill_color) {
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_SHADE), color_comb(0, 0, 0, G_ACMUX_SHADE),
                                false, false, false, false, false, false, false, false, false, false, false, false, false, false);
    }
    gfx_draw_rectangle(ulx, uly, lrx, lry, gfx_rectangle_covers_screen(ulx, uly, lrx, lry),
                       gfx_rectangle_covers_width(ulx, lrx));
    if (use_fill_color) {
        rdp.combine_mode = saved_combine_mode;
        rdp.combine_color_mul_env = saved_combine_color_mul_env;
        rdp.combine_color_mul_prim = saved_combine_color_mul_prim;
        rdp.combine_texture_blend_reverse = saved_combine_texture_blend_reverse;
        rdp.combine_two_texture_blend = saved_combine_two_texture_blend;
        rdp.combine_two_texture_blend_uses_prim_lod = saved_combine_two_texture_blend_uses_prim_lod;
        rdp.combine_two_texture_alpha_blend = saved_combine_two_texture_alpha_blend;
        rdp.combine_alpha_mul_env = saved_combine_alpha_mul_env;
        rdp.combine_two_intensity_env_prim_precombine = saved_combine_two_intensity_env_prim_precombine;
        rdp.combine_flame_texture_atlas = saved_combine_flame_texture_atlas;
        rdp.combine_texture_tint_uses_prim_lod = saved_combine_texture_tint_uses_prim_lod;
        rdp.combine_texture_tint_uses_env_alpha = saved_combine_texture_tint_uses_env_alpha;
        rdp.combine_din_fire_tint = saved_combine_din_fire_tint;
        gfx_mark_tri_pipeline_dirty();
    }
}

static void gfx_dp_set_z_image(void *z_buf_address) {
    rdp.z_buf_address = z_buf_address;
}

static void gfx_dp_set_color_image(uint32_t format, uint32_t size, uint32_t width, void* address) {
    _UNUSED(format);
    _UNUSED(size);
    _UNUSED(width);

    rdp.color_image_address = address;
}

static void gfx_sp_set_other_mode(uint32_t shift, uint32_t num_bits, uint64_t mode) {
    uint64_t mask = (((uint64_t)1 << num_bits) - 1) << shift;
    uint64_t om = rdp.other_mode_l | ((uint64_t)rdp.other_mode_h << 32);
    om = (om & ~mask) | mode;
    if ((rdp.other_mode_l == (uint32_t)om) && (rdp.other_mode_h == (uint32_t)(om >> 32))) {
        return;
    }
    rdp.other_mode_l = (uint32_t)om;
    rdp.other_mode_h = (uint32_t)(om >> 32);
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_dp_set_other_mode(uint32_t mode_h, uint32_t mode_l) {
    if ((rdp.other_mode_h == mode_h) && (rdp.other_mode_l == mode_l)) {
        return;
    }
    rdp.other_mode_h = mode_h;
    rdp.other_mode_l = mode_l;
    gfx_mark_tri_pipeline_dirty();
}

static void gfx_dp_noop(uint32_t tag) {
    if (tag == OOT_PS2_PAUSE_BG_CAPTURE_TAG) {

        gfx_flush();
        gfx_ps2_capture_pause_background_current();
        gfx_mark_tri_pipeline_dirty();
        return;
    }
    if (tag == OOT_PS2_PRERENDER_DEPTH_BEGIN || tag == OOT_PS2_PRERENDER_DEPTH_END) {
        bool enable = (tag == OOT_PS2_PRERENDER_DEPTH_BEGIN);
        if (enable != sPs2ForceDepthOnly) {

            gfx_flush();
            gfx_ps2_set_prerender_depth_only(enable);
            sPs2ForceDepthOnly = enable;
            gfx_mark_tri_pipeline_dirty();
        }
        return;
    }
    if ((tag & 0xFFFFFF00U) == OOT_PORT_HUD_ANCHOR_TAG) {
        OotPortHudAnchor anchor = tag & 0xFF;

        if ((anchor <= OOT_PORT_HUD_ANCHOR_RIGHT) && (anchor != sHudAnchor)) {

            sHudAnchor = anchor;
            gfx_update_screen_metrics();
        }
    }
}

static inline bool gfx_addr_looks_segmented(uintptr_t addr) {
    uint8_t segment = addr >> 24;
    uintptr_t offset = addr & 0x00FFFFFFU;
    bool segmentMapped;

    if ((segment == 0) || (segment >= NUM_SEGMENTS)) {
        return false;
    }

    if (!gfx_addr_is_native(addr)) {
        return true;
    }

    segmentMapped = rsp.segments[segment] != NULL;
    segmentMapped = segmentMapped || (gSegments[segment] != 0);

    if ((offset == 0) && segmentMapped) {
        return true;
    }

    if (gfx_is_valid_native_read_range_cached(addr, 1)) {
        return false;
    }

    if (offset < PS2_SEGMENTED_COLLISION_OFFSET_MAX) {
        if (segmentMapped) {
            return true;
        }
    }

    return true;
}

static void gfx_log_unmapped_segment(uintptr_t addr) {
    static s32 sUnmappedSegmentLogCount = 0;

    if (sUnmappedSegmentLogCount < 16) {
        printf("oot-port gfx unmapped segment addr=%08lx segment=%lu offset=%06lx\n", (unsigned long)addr,
               (unsigned long)(addr >> 24), (unsigned long)(addr & 0x00FFFFFFU));
    } else if (sUnmappedSegmentLogCount == 16) {
        printf("oot-port gfx unmapped segment logs suppressed\n");
    }

    sUnmappedSegmentLogCount++;
}

static void gfx_log_segment_translation(uintptr_t addr, void* base, void* translated) {
#if OOT_PS2_GFX_DIAGNOSTICS
    static s32 sSegmentTranslateLogCount = 0;
    uint8_t segment = addr >> 24;

    if ((segment != 0x0C) && (segment != 0x0D)) {
        return;
    }

    if (sSegmentTranslateLogCount < 32) {
        printf("oot-port gfx segment translate addr=%08lx segment=%u offset=%06lx base=%08lx out=%08lx\n",
               (unsigned long)addr, segment, (unsigned long)(addr & 0x00FFFFFFU), (unsigned long)(uintptr_t)base,
               (unsigned long)(uintptr_t)translated);
    } else if (sSegmentTranslateLogCount == 32) {
        printf("oot-port gfx segment translate logs suppressed\n");
    }

    sSegmentTranslateLogCount++;
#else
    _UNUSED(addr);
    _UNUSED(base);
    _UNUSED(translated);
#endif
}

static void gfx_log_bad_dl_cursor(const char* reason, uintptr_t raw, uintptr_t translated) {
    static s32 sBadDlCursorLogCount = 0;

    if (sBadDlCursorLogCount < 32) {
        printf("oot-port gfx bad dl cursor reason=%s raw=%08lx translated=%08lx cur=%08lx/%08lx/%08lx "
               "seg1=%08lx seg2=%08lx seg3=%08lx seg4=%08lx seg6=%08lx seg8=%08lx seg9=%08lx sega=%08lx "
               "segc=%08lx segd=%08lx segacmd=%08lx/%08lx/%08lx\n",
               reason, (unsigned long)raw, (unsigned long)translated, (unsigned long)sCurrentCmd.addr,
               (unsigned long)sCurrentCmd.w0, (unsigned long)sCurrentCmd.w1,
               (unsigned long)(uintptr_t)rsp.segments[1], (unsigned long)(uintptr_t)rsp.segments[2],
               (unsigned long)(uintptr_t)rsp.segments[3], (unsigned long)(uintptr_t)rsp.segments[4],
               (unsigned long)(uintptr_t)rsp.segments[6], (unsigned long)(uintptr_t)rsp.segments[8],
               (unsigned long)(uintptr_t)rsp.segments[9], (unsigned long)(uintptr_t)rsp.segments[10],
               (unsigned long)(uintptr_t)rsp.segments[12], (unsigned long)(uintptr_t)rsp.segments[13],
               (unsigned long)rsp.segment_cmd[10].addr, (unsigned long)rsp.segment_cmd[10].w0,
               (unsigned long)rsp.segment_cmd[10].w1);
    } else if (sBadDlCursorLogCount == 32) {
        printf("oot-port gfx bad dl cursor logs suppressed\n");
    }

    sBadDlCursorLogCount++;
}

static bool gfx_validate_dl_cursor(uintptr_t raw, Gfx** cmdP) {
    uintptr_t translated = (uintptr_t)*cmdP;
    uintptr_t normalized;
    size_t cacheIndex;

    if (!gfx_normalize_native_range(translated, sizeof(Gfx), &normalized)) {
        gfx_log_bad_dl_cursor("unmapped", raw, translated);
        return false;
    }

    if ((normalized & (sizeof(Gfx) - 1)) != 0) {
        gfx_log_bad_dl_cursor("unaligned", raw, normalized);
        return false;
    }

    cacheIndex = (normalized >> 3) & (GFX_VALIDATED_DL_CURSOR_CACHE_SIZE - 1);
    if (sValidatedDlCursorCache[cacheIndex] == normalized) {
        *cmdP = (Gfx*)normalized;
        return true;
    }

    if (!gfx_is_valid_native_dl_range(normalized, sizeof(Gfx))) {
        gfx_log_bad_dl_cursor("non-dl-native-range", raw, normalized);
        return false;
    }

    sValidatedDlCursorCache[cacheIndex] = normalized;
    *cmdP = (Gfx*)normalized;
    return true;
}

static inline void* gfx_runtime_symbol_addr(uintptr_t addr) {
    if (addr == (uintptr_t)&D_01000000 && rsp.segments[1] != NULL) {
        return rsp.segments[1];
    }

    switch (addr) {
        case PS2_ASSET_SYMBOL_GIDENTITYMTX:
            return &gIdentityMtx;
        default:
            return NULL;
    }
}

static __attribute__((noinline)) void *gfx_resolve_segmented_address_slow(uintptr_t w1) {
    void* runtimeSymbol = gfx_runtime_symbol_addr(w1);

    if (runtimeSymbol != NULL) {
        return runtimeSymbol;
    }

    if (gfx_addr_looks_segmented(w1)) {
        uint8_t segment = w1 >> 24;
        uintptr_t offset = w1 & 0x00FFFFFFU;

        if (rsp.segments[segment] != NULL) {
            uintptr_t baseValue = (uintptr_t)rsp.segments[segment];
            uintptr_t translatedValue = baseValue + offset;
            uintptr_t normalized;
            void* translated;

            if (!gfx_addr_is_native(baseValue) && gfx_addr_looks_segmented(baseValue) &&
                gfx_addr_looks_segmented(translatedValue) &&
                (translatedValue != w1)) {
                translated = gfx_resolve_segmented_address_slow(translatedValue);
                if (translated != (void*)translatedValue) {
                    gfx_log_segment_translation(w1, rsp.segments[segment], translated);
                    return translated;
                }
            }

            if (gfx_normalize_native_addr(translatedValue, &normalized)) {
                translatedValue = normalized;
            }

            translated = (void*)translatedValue;

            gfx_log_segment_translation(w1, rsp.segments[segment], translated);
            return translated;
        }

        if (gSegments[segment] != 0) {
            uintptr_t translatedValue = gSegments[segment] + offset;
            uintptr_t normalized;
            void* translated;

            if (gfx_normalize_native_addr(translatedValue, &normalized)) {
                translatedValue = normalized;
            }
            translated = (void*)translatedValue;
            gfx_log_segment_translation(w1, (void*)(uintptr_t)gSegments[segment], translated);
            return translated;
        }

        gfx_log_unmapped_segment(w1);
        return NULL;
    }

    {
        uintptr_t normalized;

        if (gfx_normalize_native_addr(w1, &normalized)) {
            return (void*)normalized;
        }
    }

    return NULL;
}

static __attribute__((noinline, no_instrument_function)) void *seg_addr_cached_slow(uintptr_t w1) {
    {
        uint8_t segment = w1 >> 24;

        if ((segment == 1) && (rsp.segments[1] != NULL)) {
            uintptr_t translatedValue = (uintptr_t)rsp.segments[1] + (w1 & 0x00FFFFFFU);
            uintptr_t normalized;
            u32 loadedFlags;

            if (gfx_normalize_native_addr(translatedValue, &normalized)) {
                translatedValue = normalized;
            }
            if (OotPort_GetLoadedExternalAssetRangeFlags((const void*)translatedValue, 1, &loadedFlags)) {
                return (void*)translatedValue;
            }
            return gfx_resolve_segmented_address_slow(w1);
        }
    }
    uint8_t segment = w1 >> 24;
    uintptr_t rspBase = 0;
    uintptr_t globalBase = 0;
    uintptr_t mappingBase;
    size_t cacheIndex;
    GfxSegmentAddressCacheEntry* entry;
    void* resolved;

    if (w1 == (uintptr_t)&D_01000000) {
        rspBase = (uintptr_t)rsp.segments[1];
        globalBase = gSegments[1];
    } else if ((segment != 0) && (segment < NUM_SEGMENTS)) {
        rspBase = (uintptr_t)rsp.segments[segment];
        if (rspBase == 0) {
            globalBase = gSegments[segment];
        }
    }

    if (segment == 1) {
        return gfx_resolve_segmented_address_slow(w1);
    }

    mappingBase = rspBase != 0 ? rspBase : globalBase;
    cacheIndex = ((w1 >> 3) ^ (w1 >> 13) ^ (mappingBase >> 4) ^ (mappingBase >> 15)) &
                 (GFX_SEGMENT_ADDRESS_CACHE_SIZE - 1);
    entry = &sSegmentAddressCache[cacheIndex];
    if (__builtin_expect((entry->raw == w1) && (entry->rspBase == rspBase) &&
                         (entry->globalBase == globalBase), 1)) {
        return entry->resolved;
    }

    resolved = gfx_resolve_segmented_address_slow(w1);

    uint8_t rspBaseSegment = rspBase >> 24;
    bool chainedRspBase = (rspBase != 0) && !gfx_addr_is_native(rspBase) &&
                          (rspBaseSegment != 0) && (rspBaseSegment < NUM_SEGMENTS);

    if (!chainedRspBase) {
        entry->raw = w1;
        entry->rspBase = rspBase;
        entry->globalBase = globalBase;
        entry->resolved = resolved;
    }

    return resolved;
}

#if defined(F3DEX_GBI_2)
#define GFX_S2DEX_BG_MAX_UPLOAD_WIDTH 256U
#define GFX_S2DEX_BG_MAX_UPLOAD_HEIGHT 256U

static void gfx_s2dex_bg_compute_upload_size(uint32_t sourceWidth, uint32_t sourceHeight, uint32_t* contentWidth,
                                             uint32_t* contentHeight, uint32_t* uploadWidth,
                                             uint32_t* uploadHeight) {
    *contentWidth = sourceWidth;
    *contentHeight = sourceHeight;

    *uploadWidth = gfx_ps2_next_power_of_two(*contentWidth);
    *uploadHeight = gfx_ps2_next_power_of_two(*contentHeight);
}

static bool gfx_s2dex_bg_upload_rgba16_texture(const uint8_t* source, uint32_t width, uint32_t height,
                                               uint32_t rowBytes, uint32_t sourceSpan, uint32_t contentWidth,
                                               uint32_t contentHeight, uint32_t uploadWidth, uint32_t uploadHeight,
                                               struct TextureHashmapNode* node) {
    const size_t uploadSize = (size_t)uploadWidth * uploadHeight * sizeof(uint16_t);
    GfxTextureSwapState swapState;
    uint16_t* dst;

    dst = (uint16_t*)gfx_ps2_texture_import_scratch(uploadSize);

    memset(dst, 0, uploadSize);
    swapState = gfx_texture_source_swap_state(source, sourceSpan);

    for (uint32_t y = 0; y < contentHeight; y++) {
        uint32_t sourceY = (uint64_t)y * height / contentHeight;
        const uint8_t* row = source + (size_t)sourceY * rowBytes;
        uint16_t* dstRow = dst + (size_t)y * uploadWidth;

        for (uint32_t x = 0; x < contentWidth; x++) {
            uint32_t sourceX = (uint64_t)x * width / contentWidth;
            uint16_t col16 = gfx_read_texture_source_be16(row, sourceX * sizeof(uint16_t), &swapState);
            const uint8_t a = col16 & 1;
            const uint8_t r = (col16 >> 11) & 0x1f;
            const uint8_t g = (col16 >> 6) & 0x1f;
            const uint8_t b = (col16 >> 1) & 0x1f;

            dstRow[x] = (a << 15) | (b << 10) | (g << 5) | r;
        }
    }

    texman_upload(uploadWidth, uploadHeight, PS2_TEXFMT_5551, dst);
    node->upload_width = uploadWidth;
    node->upload_height = uploadHeight;
    node->mirror_s = false;
    node->mirror_t = false;
    return true;
}

static bool gfx_s2dex_bg_prepare_texture(const uint8_t* source, uint32_t width, uint32_t height, uint8_t fmt,
                                         uint8_t siz, uint32_t* contentWidth, uint32_t* contentHeight) {
    const int tile = 0;
    bool dynamicHit;
    uint32_t rowBytes;
    uint32_t sourceSpan;
    uint32_t uploadWidth;
    uint32_t uploadHeight;
    unsigned int uploadSize;

    if ((source == NULL) || (width == 0) || (height == 0) || (height > (UINT32_MAX / width))) {
        gfx_log_bad_texture_source(tile, "s2dex-bg-dimensions", source, 1);
        return false;
    }

    rowBytes = gfx_texture_row_bytes(width, siz);
    if ((rowBytes == 0) || (height > (UINT32_MAX / rowBytes))) {
        gfx_log_bad_texture_source(tile, "s2dex-bg-rowbytes", source, rowBytes);
        return false;
    }

    sourceSpan = rowBytes * height;
    if ((fmt != G_IM_FMT_RGBA) || (siz != G_IM_SIZ_16b)) {
        gfx_log_bad_texture_source(tile, "s2dex-bg-format", source, sourceSpan);
        return false;
    }

    if (!gfx_normalize_texture_source(&source, sourceSpan)) {
        gfx_log_bad_texture_source(tile, "s2dex-bg-source", source, sourceSpan);
        return false;
    }

    gfx_s2dex_bg_compute_upload_size(width, height, contentWidth, contentHeight, &uploadWidth, &uploadHeight);

    uploadSize = uploadWidth * uploadHeight * sizeof(uint16_t);
    if (!texman_vram_space_available(uploadSize) || !texman_texture_slot_available()) {
        gfx_texture_cache_clear();
    }

    gfx_flush();

    rdp.texture_to_load.addr = source;
    rdp.texture_to_load.fmt = fmt;
    rdp.texture_to_load.siz = siz;
    rdp.texture_to_load.width = width;
    rdp.texture_to_load.tile_number = tile;
    GFX_CAPTURE_CMD(rdp.texture_to_load.image_cmd, sCurrentCmd);

    rdp.loaded_texture[tile].addr = source;
    rdp.loaded_texture[tile].size_bytes = sourceSpan;
    rdp.loaded_texture[tile].source_size_bytes = sourceSpan;
    rdp.loaded_texture[tile].row_stride_bytes = rowBytes;
    rdp.loaded_texture[tile].load_row_bytes = rowBytes;
    rdp.loaded_texture[tile].source_nibble_offset = 0;
    GFX_CAPTURE_CMD(rdp.loaded_texture[tile].image_cmd, sCurrentCmd);
    GFX_CAPTURE_CMD(rdp.loaded_texture[tile].load_cmd, sCurrentCmd);

    TextureTileState* tileState = gfx_get_texture_tile(tile);

    tileState->fmt = fmt;
    tileState->siz = siz;
    tileState->cms = G_TX_CLAMP;
    tileState->cmt = G_TX_CLAMP;
    tileState->masks = G_TX_NOMASK;
    tileState->maskt = G_TX_NOMASK;
    tileState->shifts = G_TX_NOLOD;
    tileState->shiftt = G_TX_NOLOD;
    tileState->line_size_bytes = rowBytes;
    tileState->uls = 0;
    tileState->ult = 0;
    tileState->lrs = (width - 1) << G_TEXTURE_IMAGE_FRAC;
    tileState->lrt = (height - 1) << G_TEXTURE_IMAGE_FRAC;

    if (!gfx_texture_cache_lookup(tile, &rendering_state.textures[tile], source, fmt, siz, true, &dynamicHit)) {
        if (!gfx_s2dex_bg_upload_rgba16_texture(source, width, height, rowBytes, sourceSpan, *contentWidth,
                                                *contentHeight, uploadWidth, uploadHeight,
                                                rendering_state.textures[tile])) {
            rendering_state.textures[tile] = NULL;
            rdp.textures_changed[tile] = true;
            return false;
        }

        gfx_rapi->select_texture(tile, rendering_state.textures[tile]->texture_id);
        gfx_rapi->set_sampler_parameters(tile, false, tileState->cms, tileState->cmt,
                                         tileState->masks, tileState->maskt);
        rendering_state.bound_texture_id = rendering_state.textures[tile]->texture_id;
        rendering_state.bound_texture_tile = tile;
    }

    rdp.textures_changed[tile] = false;
    gfx_mark_tri_pipeline_dirty();
    return true;
}

static uint32_t gfx_s2dex_bg_texcoord_span(uint32_t frameSpan, uint16_t scale, bool scaled) {
    if (scaled && (scale != 0)) {
        return (uint32_t)(((uint64_t)frameSpan * scale) >> 7);
    }

    return frameSpan * 8;
}

static GFX_DL_HANDLER void gfx_sp_s2dex_bg_rect(uint32_t opcode, const void* bgAddr) {
    const uObjBg* bg;
    const uint8_t* source;
    uint32_t imageWidth;
    uint32_t imageHeight;
    int32_t frameX;
    int32_t frameY;
    uint32_t frameW;
    uint32_t frameH;
    uint32_t texSpanS;
    uint32_t texSpanT;
    uint32_t texStartS;
    uint32_t texStartT;
    uint32_t contentWidth;
    uint32_t contentHeight;
    struct VertexColor* ul = &rsp.loaded_vertices_2D[0];
    struct VertexColor* lr = &rsp.loaded_vertices_2D[1];
    uint32_t savedCombineMode;
    bool savedColorMulEnv;
    bool savedColorMulPrim;
    bool savedTextureBlendReverse;
    bool savedTwoTextureBlend;
    bool savedTwoTextureBlendUsesPrimLod;
    bool savedTwoTextureAlphaBlend;
    bool savedAlphaMulEnv;
    bool savedTwoIntensityEnvPrimPrecombine;
    bool savedFlameTextureAtlas;
    bool savedTextureTintUsesPrimLod;
    bool savedTextureTintUsesEnvAlpha;
    const bool scaled = opcode == G_BG_1CYC;

    if (bgAddr == NULL) {
        gfx_log_bad_data_source("s2dex-bg", bgAddr, sizeof(uObjBg));
        return;
    }

    if (((uintptr_t)bgAddr & (sizeof(uint32_t) - 1)) != 0) {
        gfx_log_bad_data_source("s2dex-bg-align", bgAddr, sizeof(uObjBg));
        return;
    }

    bg = (const uObjBg*)bgAddr;
    imageWidth = bg->b.imageW >> 2;
    imageHeight = bg->b.imageH >> 2;
    source = (const uint8_t*)seg_addr((uintptr_t)bg->b.imagePtr);

    if (!gfx_s2dex_bg_prepare_texture(source, imageWidth, imageHeight, bg->b.imageFmt, bg->b.imageSiz, &contentWidth,
                                      &contentHeight)) {
        return;
    }

    frameX = bg->b.frameX;
    frameY = bg->b.frameY;
    frameW = scaled ? bg->s.frameW : bg->b.frameW;
    frameH = scaled ? bg->s.frameH : bg->b.frameH;
    texSpanS = gfx_s2dex_bg_texcoord_span(frameW, bg->s.scaleW, scaled);
    texSpanT = gfx_s2dex_bg_texcoord_span(frameH, bg->s.scaleH, scaled);
    texStartS = (uint64_t)bg->b.imageX * contentWidth / imageWidth;
    texStartT = (uint64_t)bg->b.imageY * contentHeight / imageHeight;
    texSpanS = (uint64_t)texSpanS * contentWidth / imageWidth;
    texSpanT = (uint64_t)texSpanT * contentHeight / imageHeight;

    ul->u = texStartS;
    ul->v = texStartT;
    ul->color = white_color;
    lr->u = texStartS + texSpanS;
    lr->v = texStartT + texSpanT;
    lr->color = white_color;

    savedCombineMode = rdp.combine_mode;
    savedColorMulEnv = rdp.combine_color_mul_env;
    savedColorMulPrim = rdp.combine_color_mul_prim;
    savedTextureBlendReverse = rdp.combine_texture_blend_reverse;
    savedTwoTextureBlend = rdp.combine_two_texture_blend;
    savedTwoTextureBlendUsesPrimLod = rdp.combine_two_texture_blend_uses_prim_lod;
    savedTwoTextureAlphaBlend = rdp.combine_two_texture_alpha_blend;
    savedAlphaMulEnv = rdp.combine_alpha_mul_env;
    savedTwoIntensityEnvPrimPrecombine = rdp.combine_two_intensity_env_prim_precombine;
    savedFlameTextureAtlas = rdp.combine_flame_texture_atlas;
    savedTextureTintUsesPrimLod = rdp.combine_texture_tint_uses_prim_lod;
    savedTextureTintUsesEnvAlpha = rdp.combine_texture_tint_uses_env_alpha;
    bool savedDinFireTint = rdp.combine_din_fire_tint;
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_TEXEL0), color_comb(0, 0, 0, G_ACMUX_TEXEL0),
                                false, false, false, false, false, false, false, false, false, false, false, false, false, false);
    }

    gfx_flush();
    {
        const uint32_t savedGeometryMode = rsp.geometry_mode;
        const uint32_t savedOtherModeL = rdp.other_mode_l;

        rsp.geometry_mode &= ~G_ZBUFFER;
        rdp.other_mode_l &= ~(Z_CMP | Z_UPD);
        gfx_mark_tri_pipeline_dirty();
        gfx_draw_rectangle(frameX, frameY, frameX + frameW, frameY + frameH, false, false);

        rsp.geometry_mode = savedGeometryMode;
        rdp.other_mode_l = savedOtherModeL;
        gfx_mark_tri_pipeline_dirty();
    }

    rdp.combine_mode = savedCombineMode;
    rdp.combine_color_mul_env = savedColorMulEnv;
    rdp.combine_color_mul_prim = savedColorMulPrim;
    rdp.combine_texture_blend_reverse = savedTextureBlendReverse;
    rdp.combine_two_texture_blend = savedTwoTextureBlend;
    rdp.combine_two_texture_blend_uses_prim_lod = savedTwoTextureBlendUsesPrimLod;
    rdp.combine_two_texture_alpha_blend = savedTwoTextureAlphaBlend;
    rdp.combine_alpha_mul_env = savedAlphaMulEnv;
    rdp.combine_two_intensity_env_prim_precombine = savedTwoIntensityEnvPrimPrecombine;
    rdp.combine_flame_texture_atlas = savedFlameTextureAtlas;
    rdp.combine_texture_tint_uses_prim_lod = savedTextureTintUsesPrimLod;
    rdp.combine_texture_tint_uses_env_alpha = savedTextureTintUsesEnvAlpha;
    rdp.combine_din_fire_tint = savedDinFireTint;

    rendering_state.bound_texture_id = 0;
    rendering_state.bound_texture_tile = -1;
    rdp.textures_changed[0] = true;
    rdp.textures_changed[1] = true;
    gfx_mark_tri_pipeline_dirty();
}
#endif

static bool gfx_translate_dl_cursor(Gfx** cmdP) {
    uintptr_t raw = (uintptr_t)*cmdP;
    uintptr_t normalized;
    bool normalizedNative = gfx_normalize_native_range(raw, sizeof(Gfx), &normalized);

    if (normalizedNative && gfx_is_valid_native_dl_range(normalized, sizeof(Gfx))) {
        *cmdP = (Gfx*)normalized;
        return gfx_validate_dl_cursor(raw, cmdP);
    }

    {
        uint8_t segment = raw >> 24;
        bool mappedSegment = (segment != 0) && (segment < NUM_SEGMENTS) && (rsp.segments[segment] != NULL);

        mappedSegment = mappedSegment || ((segment != 0) && (segment < NUM_SEGMENTS) && (gSegments[segment] != 0));

        if (mappedSegment || gfx_addr_looks_segmented(raw)) {
            void* translated;

            if (mappedSegment && (rsp.segments[segment] != NULL)) {
                uintptr_t translatedValue = (uintptr_t)rsp.segments[segment] + (raw & 0x00FFFFFFU);

                if (gfx_normalize_native_addr(translatedValue, &normalized)) {
                    translatedValue = normalized;
                }
                translated = (void*)translatedValue;
            } else {
                translated = seg_addr(raw);
            }

            if ((translated == NULL) || (translated == (void*)raw)) {
                gfx_log_bad_dl_cursor("untranslated-segment", raw, (uintptr_t)translated);
                return false;
            }

            *cmdP = (Gfx*)translated;
            return gfx_validate_dl_cursor(raw, cmdP);
        }
    }

    if (normalizedNative) {
        *cmdP = (Gfx*)normalized;
    }

    return gfx_validate_dl_cursor(raw, cmdP);
}

#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1U << width) - 1))

static inline __attribute__((always_inline)) bool gfx_ps2_try_texture_block_fastpath(Gfx** cmdP) {
    Gfx* c = *cmdP;
    const uint32_t op1 = c[1].words.w0 >> 24;
    const uint32_t op2 = c[2].words.w0 >> 24;
    const uint32_t op3 = c[3].words.w0 >> 24;
    const uint32_t op4 = c[4].words.w0 >> 24;
    const uint32_t op5 = c[5].words.w0 >> 24;
    const uint32_t op6 = c[6].words.w0 >> 24;

    if (__builtin_expect(op1 != G_SETTILE || op2 != G_RDPLOADSYNC || op3 != G_LOADBLOCK ||
                         op4 != G_RDPPIPESYNC || op5 != G_SETTILE || op6 != G_SETTILESIZE, 0)) {
        return false;
    }

    {
        const uint32_t w0 = c[0].words.w0;
        gfx_dp_set_texture_image((w0 >> 21) & 7U, (w0 >> 19) & 3U,
                                 (w0 & 0xFFFU) + 1U, seg_addr(c[0].words.w1));
    }
    {
        const uint32_t w0 = c[1].words.w0;
        const uint32_t w1 = c[1].words.w1;
        gfx_dp_set_tile((w0 >> 21) & 7U, (w0 >> 19) & 3U, (w0 >> 9) & 0x1FFU,
                        w0 & 0x1FFU, (w1 >> 24) & 7U, (w1 >> 20) & 0xFU,
                        (w1 >> 18) & 3U, (w1 >> 14) & 0xFU, (w1 >> 10) & 0xFU,
                        (w1 >> 8) & 3U, (w1 >> 4) & 0xFU, w1 & 0xFU);
    }
    {
        const uint32_t w0 = c[3].words.w0;
        const uint32_t w1 = c[3].words.w1;
        gfx_dp_load_block((w1 >> 24) & 7U, (w0 >> 12) & 0xFFFU, w0 & 0xFFFU,
                          (w1 >> 12) & 0xFFFU, w1 & 0xFFFU);
    }
    {
        const uint32_t w0 = c[5].words.w0;
        const uint32_t w1 = c[5].words.w1;
        gfx_dp_set_tile((w0 >> 21) & 7U, (w0 >> 19) & 3U, (w0 >> 9) & 0x1FFU,
                        w0 & 0x1FFU, (w1 >> 24) & 7U, (w1 >> 20) & 0xFU,
                        (w1 >> 18) & 3U, (w1 >> 14) & 0xFU, (w1 >> 10) & 0xFU,
                        (w1 >> 8) & 3U, (w1 >> 4) & 0xFU, w1 & 0xFU);
    }
    {
        const uint32_t w0 = c[6].words.w0;
        const uint32_t w1 = c[6].words.w1;
        gfx_dp_set_tile_size((w1 >> 24) & 7U, (w0 >> 12) & 0xFFFU, w0 & 0xFFFU,
                             (w1 >> 12) & 0xFFFU, w1 & 0xFFFU);
    }

#if OOT_PS2_PERF_BENCH

    sPerformanceCommandCount += 6;
    sPs2OpcodeCounts[G_SETTILE] += 2;
    sPs2OpcodeCounts[G_RDPLOADSYNC]++;
    sPs2OpcodeCounts[G_LOADBLOCK]++;
    sPs2OpcodeCounts[G_RDPPIPESYNC]++;
    sPs2OpcodeCounts[G_SETTILESIZE]++;
#endif
    *cmdP = c + 6;
    return true;
}

static void gfx_run_dl(Gfx* cmd) {
#define GFX_DL_RETURN_STACK_SIZE 64
    Gfx* returnStack[GFX_DL_RETURN_STACK_SIZE];
    uint32_t returnDepth = 0;

    if (!gfx_translate_dl_cursor(&cmd)) {
        return;
    }

    for (;;) {
        uint32_t opcode = cmd->words.w0 >> 24;
#if OOT_PS2_PERF_BENCH
        sPerformanceCommandCount++;
#if OOT_PS2_PERF_BENCH
        sPs2OpcodeCounts[opcode]++;
#endif
#endif
        if (__builtin_expect(opcode == G_SETTIMG, 0) && gfx_ps2_try_texture_block_fastpath(&cmd)) {
            ++cmd;
            continue;
        }

        if (__builtin_expect((opcode == G_RDPPIPESYNC) || (opcode == G_RDPLOADSYNC) ||
                             (opcode == G_RDPTILESYNC) || (opcode == G_RDPFULLSYNC), 0)) {
            ++cmd;
            continue;
        }

        switch (opcode) {

            case G_MTX:
#if defined(F3DEX_GBI_2)
                gfx_sp_matrix(C0(0, 8) ^ G_MTX_PUSH, (const int32_t *) seg_addr(cmd->words.w1));
#else
                gfx_sp_matrix(C0(16, 8), (const int32_t *) seg_addr(cmd->words.w1));
#endif
                break;
            case (uint8_t)G_POPMTX:
#if defined(F3DEX_GBI_2)
                gfx_sp_pop_matrix(cmd->words.w1 / 64);
#else
                gfx_sp_pop_matrix(1);
#endif
                break;
            case G_MOVEMEM:
#if defined(F3DEX_GBI_2)
                gfx_sp_movemem(C0(0, 8), C0(8, 8) * 8, seg_addr(cmd->words.w1));
#else
                gfx_sp_movemem(C0(16, 8), 0, seg_addr(cmd->words.w1));
#endif
                break;
            case (uint8_t)G_MOVEWORD:
#if defined(F3DEX_GBI_2)
                gfx_sp_moveword(C0(16, 8), C0(0, 16), cmd->words.w1);
#else
                gfx_sp_moveword(C0(0, 8), C0(8, 16), cmd->words.w1);
#endif
                break;
            case (uint8_t)G_TEXTURE:
#if defined(F3DEX_GBI_2)
                gfx_sp_texture(C1(16, 16), C1(0, 16), C0(11, 3), C0(8, 3), C0(1, 7));
#else
                gfx_sp_texture(C1(16, 16), C1(0, 16), C0(11, 3), C0(8, 3), C0(0, 8));
#endif
                break;
            case (uint8_t)G_RDPHALF_1:
                rsp.rdp_half_1 = cmd->words.w1;
                break;
            case (uint8_t)G_BRANCH_Z:

                if (rsp.rdp_half_1 != 0) {
                    Gfx* branchTarget = (Gfx*)seg_addr(rsp.rdp_half_1);

                    if (!gfx_translate_dl_cursor(&branchTarget)) {
                        return;
                    }

                    cmd = branchTarget;
                    continue;
                }
                break;
            case G_VTX:
#if defined(F3DEX_GBI_2)
                if (!gfx_sp_vertex_f3dex2(cmd->words.w0, cmd->words.w1)) {
                    return;
                }
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
                if (!gfx_sp_vertex(C0(10, 6), C0(16, 8) / 2, seg_addr(cmd->words.w1))) {
                    return;
                }
#else
                if (!gfx_sp_vertex((C0(0, 16)) / sizeof(Vtx), C0(16, 4), seg_addr(cmd->words.w1))) {
                    return;
                }
#endif
                break;
            case (uint8_t)G_CULLDL:
            {
                uint32_t firstVertex;
                uint32_t lastVertex;

#if defined(F3DEX_GBI_2) || defined(F3DEX_GBI) || defined(F3DLP_GBI)
                firstVertex = C0(0, 16) / 2;
                lastVertex = C1(0, 16) / 2;
#else
                const uint32_t endOffset = C1(0, 16) / 40;

                firstVertex = C0(0, 16) / 40;
                if (endOffset == 0) {
                    break;
                }
                lastVertex = endOffset - 1;
#endif

                if (gfx_sp_cull_display_list(firstVertex, lastVertex)) {
                    if (returnDepth == 0) {
                        return;
                    }
                    cmd = returnStack[--returnDepth];
                    continue;
                }
                break;
            }
            case G_DL:
            {
                const bool push = C0(16, 1) == G_DL_PUSH;
                const uintptr_t rawTarget = (uintptr_t)cmd->words.w1;
                Gfx* target;

                if (rawTarget == 0) {
                    target = NULL;
                } else {
                    target = (Gfx*)seg_addr(rawTarget);
                }

                if (target == NULL || !gfx_validate_dl_cursor(rawTarget, &target)) {
                    if (push) {
                        cmd++;
                        continue;
                    }
                    if (returnDepth == 0) {
                        return;
                    }
                    cmd = returnStack[--returnDepth];
                    continue;
                }

                if (push) {
                    if (returnDepth >= GFX_DL_RETURN_STACK_SIZE) {
                        gfx_log_bad_dl_cursor("return-stack-overflow", (uintptr_t)target,
                                              (uintptr_t)target);
                        return;
                    }
                    returnStack[returnDepth++] = cmd + 1;
                }
                cmd = target;
                continue;
            }
#if defined(F3DEX_GBI_2)
            case (uint8_t)G_BG_1CYC:
            case (uint8_t)G_BG_COPY:
                gfx_sp_s2dex_bg_rect(opcode, seg_addr(cmd->words.w1));
                break;
#endif
            case (uint8_t)G_ENDDL:
                if (returnDepth == 0) {
                    return;
                }
                cmd = returnStack[--returnDepth];
                continue;
#if defined(F3DEX_GBI_2)
            case G_GEOMETRYMODE:
                gfx_sp_geometry_mode(~C0(0, 24), cmd->words.w1);
                break;
#else
            case (uint8_t)G_SETGEOMETRYMODE:
                gfx_sp_geometry_mode(0, cmd->words.w1);
                break;
            case (uint8_t)G_CLEARGEOMETRYMODE:
                gfx_sp_geometry_mode(cmd->words.w1, 0);
                break;
#endif
            case (uint8_t)G_TRI1:
#if defined(F3DEX_GBI_2)
                gfx_sp_triangles(cmd->words.w0, 0, 1);
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
                gfx_sp_triangles(cmd->words.w1, 0, 1);
#else
                gfx_sp_triangles(cmd->words.w1, 0, 1);
#endif
                break;
#if defined(F3DEX_GBI_2) || defined(F3DEX_GBI) || defined(F3DLP_GBI)
            case (uint8_t)G_TRI2:

                bool ps2RunPipelinePrepared = false;
                const uint32_t ps2RunCullMode = rsp.geometry_mode & G_CULL_BOTH;
                for (;;) {
                    gfx_sp_triangles_run(cmd->words.w0, cmd->words.w1, 2,
                                         &ps2RunPipelinePrepared, ps2RunCullMode);
                    if (((cmd + 1)->words.w0 >> 24) != (uint8_t)G_TRI2) {
                        break;
                    }
                    ++cmd;
#if OOT_PS2_PERF_BENCH
                    sPerformanceCommandCount++;
                    sPs2OpcodeCounts[(uint8_t)G_TRI2]++;
#endif
                }
                break;
#endif
#if defined(F3DEX_GBI_2)
            case (uint8_t)G_QUAD:
                gfx_sp_triangles(cmd->words.w0, cmd->words.w1, 2);
                break;
#endif
            case (uint8_t)G_SETOTHERMODE_L:
#if defined(F3DEX_GBI_2)
                gfx_sp_set_other_mode(31 - C0(8, 8) - C0(0, 8), C0(0, 8) + 1, cmd->words.w1);
#else
                gfx_sp_set_other_mode(C0(8, 8), C0(0, 8), cmd->words.w1);
#endif
                break;
            case (uint8_t)G_SETOTHERMODE_H:
#if defined(F3DEX_GBI_2)
                gfx_sp_set_other_mode(63 - C0(8, 8) - C0(0, 8), C0(0, 8) + 1, (uint64_t) cmd->words.w1 << 32);
#else
                gfx_sp_set_other_mode(C0(8, 8) + 32, C0(0, 8), (uint64_t) cmd->words.w1 << 32);
#endif
                break;

            case G_NOOP:
                gfx_dp_noop(cmd->words.w1);
                break;
            case G_RDPSETOTHERMODE:
                gfx_dp_set_other_mode(cmd->words.w0 & 0x00FFFFFFU, cmd->words.w1);
                break;
            case G_SETTIMG:
                gfx_dp_set_texture_image(C0(21, 3), C0(19, 2), C0(0, 12) + 1, seg_addr(cmd->words.w1));
                break;
            case G_LOADBLOCK:
                gfx_dp_load_block(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTILE:
                gfx_dp_load_tile(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETTILE:
                gfx_dp_set_tile(C0(21, 3), C0(19, 2), C0(9, 9), C0(0, 9), C1(24, 3), C1(20, 4), C1(18, 2), C1(14, 4), C1(10, 4), C1(8, 2), C1(4, 4), C1(0, 4));
                break;
            case G_SETTILESIZE:
                gfx_dp_set_tile_size(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTLUT:
                gfx_dp_load_tlut(C1(24, 3), C1(14, 10));
                break;
            case G_SETENVCOLOR:
                gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETPRIMCOLOR:
                gfx_dp_set_prim_color(C0(0, 8), C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFOGCOLOR:
                gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFILLCOLOR:
                gfx_dp_set_fill_color(cmd->words.w1);
                break;
            case G_SETPRIMDEPTH:
                gfx_dp_set_prim_depth(C1(16, 16));
                break;
            case G_SETCOMBINE:
                gfx_dp_set_combine(cmd->words.w0, cmd->words.w1);
                break;

            case G_TEXRECT:
            case G_TEXRECTFLIP:
            {
                int32_t lrx, lry, tile, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;
#if defined(F3DEX_GBI_2E)
                lrx = (int32_t)(C0(0, 24) << 8) >> 8;
                lry = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                ulx = (int32_t)(C0(0, 24) << 8) >> 8;
                uly = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                uls = C0(16, 16);
                ult = C0(0, 16);
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
#else
                lrx = C0(12, 12);
                lry = C0(0, 12);
                tile = C1(24, 3);
                ulx = C1(12, 12);
                uly = C1(0, 12);
                ++cmd;
                uls = C1(16, 16);
                ult = C1(0, 16);
                ++cmd;
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
#endif
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, opcode == G_TEXRECTFLIP);
                break;
            }
            case G_FILLRECT:
#if defined(F3DEX_GBI_2E)
            {
                int32_t lrx, lry, ulx, uly;
                lrx = (int32_t)(C0(0, 24) << 8) >> 8;
                lry = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                ulx = (int32_t)(C0(0, 24) << 8) >> 8;
                uly = (int32_t)(C1(0, 24) << 8) >> 8;
                gfx_dp_fill_rectangle(ulx, uly, lrx, lry);
                break;
            }
#else
                gfx_dp_fill_rectangle(C1(12, 12), C1(0, 12), C0(12, 12), C0(0, 12));
                break;
#endif
            case G_SETSCISSOR:
                gfx_dp_set_scissor(C1(24, 2), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETZIMG:
                gfx_dp_set_z_image(seg_addr(cmd->words.w1));
                break;
            case G_SETCIMG:
                gfx_dp_set_color_image(C0(21, 3), C0(19, 2), C0(0, 11), seg_addr(cmd->words.w1));
                break;
        }
        ++cmd;
    }
#undef GFX_DL_RETURN_STACK_SIZE
}

static void gfx_sp_reset() {
    rsp.modelview_matrix_stack_size = 1;
    rsp.current_num_lights = 2;
    rsp.lights_changed = true;
    rsp.rdp_half_1 = 0;
    sHudAnchor = OOT_PORT_HUD_ANCHOR_NONE;
    sHudViewportFullscreen = true;
    gfx_update_screen_metrics();
    memset(rsp.segments, 0, sizeof(rsp.segments));
}

void gfx_set_ps2_widescreen(bool enabled) {
    if (sPs2Widescreen == enabled) return;
    gfx_flush();
    sPs2Widescreen = enabled;
    gfx_update_screen_metrics();
    rdp.viewport_or_scissor_changed = true;
    rendering_state.tri_pipeline_dirty = true;
    rendering_state.backend_state_dirty = true;
}

bool gfx_get_ps2_widescreen(void) {
    return sPs2Widescreen;
}

void gfx_get_dimensions(uint32_t *width, uint32_t *height) {
    gfx_wapi->get_dimensions(width, height);
}

void gfx_set_dimensions(uint32_t width, uint32_t height) {
    if ((width == 0) || (height == 0) ||
        ((gfx_current_dimensions.width == width) && (gfx_current_dimensions.height == height))) {
        return;
    }

    gfx_current_dimensions.width = width;
    gfx_current_dimensions.height = height;
    gfx_update_screen_metrics();
    gfx_rapi->on_resize();
}

float times[30];
float time_avg;
float time_first_200;
int total_frame_counter;
int frame_counter;

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *game_name, bool start_in_fullscreen) {
    gfx_wapi = wapi;
    gfx_rapi = rapi;
    gfx_wapi->init(game_name, start_in_fullscreen);
    gfx_rapi->init();
    rendering_state.color_combiner_valid = false;
    rendering_state.tri_pipeline_dirty = true;
    rendering_state.backend_state_dirty = true;

    int i;
    for(i=0;i<30;i++){
        times[i] = 0.0f;
    }
    frame_counter = 0;
    time_avg = 0.0f;
    time_first_200 = 0;
    total_frame_counter = 0;

    static uint32_t precomp_shaders[] = {
        0x01200200,
        0x00000045,
        0x00000200,
        0x01200a00,
        0x00000a00,
        0x01a00045,
        0x00000551,
        0x01045045,
        0x05a00a00,
        0x01200045,
        0x05045045,
        0x01045a00,
        0x01a00a00,
        0x0000038d,
        0x01081081,
        0x0120038d,
        0x03200045,
        0x03200a00,
        0x01a00a6f,
        0x01141045,
        0x07a00a00,
        0x05200200,
        0x03200200,
        0x09200200,
        0x0920038d,
        0x09200045
    };
    for (size_t i = 0; i < sizeof(precomp_shaders) / sizeof(uint32_t); i++) {
        gfx_lookup_or_create_shader_program(precomp_shaders[i]);
    }

    memcpy(rsp.P_matrix, identity_matrix, sizeof(identity_matrix));
    memcpy(rsp.modelview_matrix_stack[0], identity_matrix, sizeof(identity_matrix));
    sPs2MvpDirty = true;

    gfx_wapi->get_dimensions(&gfx_current_dimensions.width, &gfx_current_dimensions.height);
    if (gfx_current_dimensions.height == 0) {

        gfx_current_dimensions.height = 1;
    }
    gfx_update_screen_metrics();
}

struct GfxRenderingAPI *gfx_get_current_rendering_api(void) {
    return gfx_rapi;
}

void gfx_start_frame(void) {

    sTextureCacheFrameSerial++;
    if (sTextureCacheFrameSerial == 0) {
        sTextureCacheFrameSerial = 1;
    }
#if defined(OOTDEBUG) || OOT_PS2_PERF_BENCH
    sPerformanceCommandCount = 0;
    sPerformanceInputTriangleCount = 0;
    sPerformanceOutputTriangleCount = 0;
    sPerformanceFlushCount = 0;
    sPerformanceDrawCallCount = 0;
    sPerformanceOpaqueDrawCallCount = 0;
    sPerformanceOpaqueTriangleCount = 0;
    sPerformanceTranslucentDrawCallCount = 0;
    sPerformanceTranslucentTriangleCount = 0;
    sPerformanceMaxBatchTriangles = 0;
    sPerformanceTextureUploadCount = 0;
    sPerformanceTextureSameFlushCount = 0;
    sPerformanceTextureChangeCount = 0;
#if OOT_PS2_PERF_BENCH
    memset(sPs2OpcodeCounts, 0, sizeof(sPs2OpcodeCounts));
#if OOT_PS2_DEEP_PROFILE
    sPs2DeepVtxCycles = 0;
    sPs2DeepTriCycles = 0;
    sPs2DeepFlushCycles = 0;
#endif
#endif
#endif
    memset(sValidatedDlCursorCache, 0, sizeof(sValidatedDlCursorCache));
    memset(sValidatedReadCache, 0, sizeof(sValidatedReadCache));
    gfx_wapi->handle_events();
}

void gfx_run(Gfx *commands) {
#if OOT_PS2_PERF_BENCH
    uint64_t displayListStartUsec;
    uint64_t displayListEndUsec;
    uint64_t submitEndUsec;
#endif

    gfx_sp_reset();
    sPs2MvpDirty = true;

    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return;
    }
    dropped_frame = false;
    gfx_rapi->start_frame();

    rendering_state.bound_texture_id = 0;
    rendering_state.bound_texture_tile = -1;
    gfx_mark_tri_pipeline_dirty();
    sUploadedProjectionValid = false;
#if OOT_PS2_PERF_BENCH
    displayListStartUsec = (uint64_t)(gfx_wapi->get_time() * 1000000.0);
#endif
#if OOT_PS2_STAGE_DIAG
    double ps2DiagDlBegin = gfx_wapi->get_time();
#endif
    gfx_run_dl(commands);
    gfx_flush();
#if OOT_PS2_STAGE_DIAG
    double ps2DiagDlEnd = gfx_wapi->get_time();
#endif
#if OOT_PS2_PERF_BENCH
    displayListEndUsec = (uint64_t)(gfx_wapi->get_time() * 1000000.0);
#endif
    gfx_rapi->end_frame();
    gfx_wapi->swap_buffers_begin();
#if OOT_PS2_STAGE_DIAG
    double ps2DiagSubmitEnd = gfx_wapi->get_time();
    {
        static unsigned ps2DiagFrames;
        static double ps2DiagDlAccum;
        static double ps2DiagSubmitAccum;
        ps2DiagFrames++;
        ps2DiagDlAccum += ps2DiagDlEnd - ps2DiagDlBegin;
        ps2DiagSubmitAccum += ps2DiagSubmitEnd - ps2DiagDlEnd;
        if (ps2DiagFrames >= 30U) {
            printf("PS2STAGE dl_us=%u submit_us=%u\n",
                   (unsigned)(ps2DiagDlAccum * 1000000.0 / ps2DiagFrames),
                   (unsigned)(ps2DiagSubmitAccum * 1000000.0 / ps2DiagFrames));
            ps2DiagFrames = 0;
            ps2DiagDlAccum = 0.0;
            ps2DiagSubmitAccum = 0.0;
        }
    }
#endif
#if OOT_PS2_PERF_BENCH
    submitEndUsec = (uint64_t)(gfx_wapi->get_time() * 1000000.0);
    {
        static uint32_t benchFrame;
        static uint64_t dlAccum, submitAccum;
        static uint32_t cmdAccum, inTriAccum, outTriAccum, flushAccum, drawAccum, uploadAccum, texSameAccum, texChangeAccum;
        static uint32_t maxBatch;
#if OOT_PS2_DEEP_PROFILE
        static uint64_t deepVtxAccum, deepTriAccum, deepFlushAccum;
        deepVtxAccum += sPs2DeepVtxCycles;
        deepTriAccum += sPs2DeepTriCycles;
        deepFlushAccum += sPs2DeepFlushCycles;
#endif
        dlAccum += displayListEndUsec - displayListStartUsec;
        submitAccum += submitEndUsec - displayListEndUsec;
        cmdAccum += sPerformanceCommandCount;
        inTriAccum += sPerformanceInputTriangleCount;
        outTriAccum += sPerformanceOutputTriangleCount;
        flushAccum += sPerformanceFlushCount;
        drawAccum += sPerformanceDrawCallCount;
        uploadAccum += sPerformanceTextureUploadCount;
        texSameAccum += sPerformanceTextureSameFlushCount;
        texChangeAccum += sPerformanceTextureChangeCount;
        if (sPerformanceMaxBatchTriangles > maxBatch) maxBatch = sPerformanceMaxBatchTriangles;
        {
            static uint32_t opAccum[256];
            for (unsigned oi=0; oi<256; oi++) opAccum[oi] += sPs2OpcodeCounts[oi];
            if (benchFrame + 1 >= 20) {
                for (int rank=0; rank<10; rank++) {
                    unsigned best=0; uint32_t bestv=0;
                    for (unsigned oi=0; oi<256; oi++) if (opAccum[oi]>bestv) { bestv=opAccum[oi]; best=oi; }
                    if(bestv==0)break;
                    printf("PS2OP rank=%d op=%02x count=%lu\n",rank,(unsigned)best,(unsigned long)(bestv/20U));
                    opAccum[best]=0;
                }
                memset(opAccum,0,sizeof(opAccum));
            }
        }
        benchFrame++;
        if (benchFrame >= 20) {
#if OOT_PS2_DEEP_PROFILE
            printf("PS2DEEP vtx_count=%llu tri_count=%llu flush_count=%llu\n",
                   (unsigned long long)(deepVtxAccum/benchFrame),
                   (unsigned long long)(deepTriAccum/benchFrame),
                   (unsigned long long)(deepFlushAccum/benchFrame));
            deepVtxAccum = deepTriAccum = deepFlushAccum = 0;
#endif
            printf("PS2GFX dl_us=%llu submit_us=%llu cmd=%u in=%u out=%u flush=%u draw=%u upload=%u texchg=%u texsame=%u maxbatch=%u\n",
                   (unsigned long long)(dlAccum/benchFrame), (unsigned long long)(submitAccum/benchFrame),
                   cmdAccum/benchFrame, inTriAccum/benchFrame, outTriAccum/benchFrame,
                   flushAccum/benchFrame, drawAccum/benchFrame, uploadAccum/benchFrame,
                   texChangeAccum/benchFrame, texSameAccum/benchFrame, maxBatch);
            benchFrame=0; dlAccum=submitAccum=0; cmdAccum=inTriAccum=outTriAccum=flushAccum=drawAccum=uploadAccum=texSameAccum=texChangeAccum=0; maxBatch=0;
        }
    }
#endif

}

void gfx_end_frame(void) {

    if (!dropped_frame) {
        gfx_rapi->finish_render();
        gfx_wapi->swap_buffers_end();
    }

}

void gfx_invalidate_render_state(void) {
    gfx_flush();

    if (gfx_rapi != NULL) {
        gfx_rapi->unload_shader(NULL);
    }

    rendering_state.shader_program = NULL;
    rendering_state.color_combiner_valid = false;
    rendering_state.tri_pipeline_dirty = true;
    rendering_state.backend_state_dirty = true;
    sUploadedProjectionValid = false;
}

void gfx_render_callback_frame(void (*draw_callback)(void *arg), void *arg) {
    gfx_start_frame();

    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return;
    }

    dropped_frame = false;
    gfx_rapi->start_frame();
    sUploadedProjectionValid = false;

    if (draw_callback != NULL) {
        draw_callback(arg);
    }

    gfx_rapi->end_frame();
    gfx_wapi->swap_buffers_begin();
    gfx_end_frame();
    gfx_invalidate_render_state();
}
