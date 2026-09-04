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

#define MAX_TEXTURES 768
#define MAX_SHADERS 1024
#define TEXCACHE_SIZE (4 * 1024 * 1024)
#define ALIGN(v,a) (((v)+((a)-1))&~((a)-1))
#define BMODE_BLEND GS_SETREG_ALPHA(0,1,0,1,128)
#if !defined(OOT_PS2_RAPI_PROFILE)
#define OOT_PS2_RAPI_PROFILE 0
#endif
#if OOT_PS2_RAPI_PROFILE
static inline uint32_t ps2_prof_count(void){ uint32_t v; __asm__ volatile("mfc0 %0, $9" : "=r"(v)); return v; }
static uint64_t sProfDrawCycles=0,sProfTexCycles=0,sProfUntexCycles=0,sProfExactCycles=0,sProfFogCycles=0;
static uint64_t sProfUploadCycles=0,sProfBindCycles=0;
static uint32_t sProfDrawCalls=0,sProfExactCalls=0,sProfFogCalls=0,sProfUploadCalls=0,sProfBindCalls=0,sProfBindTransfers=0,sProfFrames=0;
static uint64_t sProfFrameDrawCycles=0,sProfFrameTexCycles=0,sProfFrameUntexCycles=0,sProfFrameExactCycles=0,sProfFrameFogCycles=0;
static uint64_t sProfFrameUploadCycles=0,sProfFrameBindCycles=0;
static uint32_t sProfFrameDrawCalls=0,sProfFrameExactCalls=0,sProfFrameFogCalls=0,sProfFrameUploadCalls=0,sProfFrameBindCalls=0,sProfFrameBindTransfers=0;
#endif

#define PS2_SETREG_FOGCOL(R,G,B) ((u64)((R)&0xff) | ((u64)((G)&0xff)<<8) | ((u64)((B)&0xff)<<16))

extern GSGLOBAL *gs_global;
void gfx_ps2_restore_pause_background(void);
bool gfx_ps2_pause_background_active(void);
static void ps2_restore_prerender_fallback(void);
void texman_clear(void);

typedef union TexCoord { struct { float s,t; }; uint64_t word; } __attribute__((packed,aligned(8))) TexCoord;
typedef union ColorQ { struct { uint8_t r,g,b,a; float q; }; uint32_t rgba; uint64_t word; } __attribute__((packed,aligned(8))) ColorQ;
typedef struct Ps2Fast { float u,v,real_u,real_v; uint32_t rgba; float x,y,z,q; uint32_t fog_color,fog; } __attribute__((packed,aligned(4))) Ps2Fast;
typedef struct Ps2FogColor { uint32_t rgba; float x,y,z; } __attribute__((packed,aligned(4))) Ps2FogColor;
typedef struct Ps2FogTextured { float u,v; uint32_t rgba; float x,y,z,q; } __attribute__((packed,aligned(4))) Ps2FogTextured;
typedef struct VertexColor2D { int16_t u,v; uint32_t rgba; uint16_t x,y,z; } __attribute__((packed,aligned(4))) VertexColor2D;

enum Ps2TextureMode {
    PS2_TEXTURE_MODE_MODULATE = 0,
    PS2_TEXTURE_MODE_DECAL,
    PS2_TEXTURE_MODE_BLEND,
    PS2_TEXTURE_MODE_REPLACE,
};

struct ShaderProgram {
    bool enabled;
    uint32_t shader_id;
    struct CCFeatures cc;
    int mix;
    bool texture_used[2];
    int texture_ord[2];
    int num_inputs;

    bool alpha_test;
    uint8_t alpha_ref;
    bool depth_only;
    bool texture_alpha;
    uint8_t tfx;
    uint8_t texture_mode;
};
struct Texture {
    GSTEXTURE tex;
    uint32_t clamp_s,clamp_t;
    uint8_t cms,cmt,masks,maskt;
    size_t backing_size;
    uint32_t content_hash;
    uint32_t upload_serial;
};
struct Viewport { float x,y,w,h,hw,hh,cx,cy; };
struct Clip { int x0,y0,x1,y1; };

static struct ShaderProgram shader_pool[MAX_SHADERS];
#define PS2_SHADER_HASH_SLOTS 2048U

static uint16_t shader_hash[PS2_SHADER_HASH_SLOTS];
static unsigned shader_count;
static struct ShaderProgram *cur_shader;
static struct Texture tex_pool[MAX_TEXTURES];
static uint32_t tex_count;

static u32 sIntensityT8Clut[256] __attribute__((aligned(128)));
static bool sIntensityT8ClutReady;
static struct Texture *cur_tex[2], *last_tex;
static bool sampler_linear[2];
static uint32_t sampler_clamp_s[2]={GS_CMODE_REPEAT,GS_CMODE_REPEAT};
static uint32_t sampler_clamp_t[2]={GS_CMODE_REPEAT,GS_CMODE_REPEAT};
static uint8_t sampler_cms[2], sampler_cmt[2], sampler_masks[2], sampler_maskt[2];
static uint8_t *tex_cache,*tex_ptr,*tex_end;
static struct Viewport r_view;
static struct Clip r_clip;
static bool z_test=true,z_write=true,z_decal=false,do_blend=false;
static float z_offset;
static uint32_t sEnvColor=0xffffffff;
static bool sTextureBlendReverse;
static bool sTextureBlendPrecolor;
static bool sDinFireTint;
static bool sTwoTextureBlendActive;
static bool sTwoTextureEnvPrimTint;
static bool sSkipContentHash;
static bool sSuppressFpsOverlay;
static bool sFpsOverlayEnabled=true;
static int sBootProgressCode = -1;
static char sBootProgressLabel[40];
static int sLastTexFilter=-1;
static u64 sLastTestReg=~0ULL;
static u64 sLastClampReg=~0ULL;
static u64 sLastScissorReg=~0ULL;
static u64 sLastZbufReg=~0ULL;
static u64 sLastAlphaReg=~0ULL;
static u64 sLastTex0Reg=~0ULL;
static uint32_t sLastFogColor=0xffffffffU;
static bool sPs2SkipNextFogPass=false;
static GSTEXTURE* sLastBoundGsTex;
#define PS2_REVERSE_TINT_CACHE_SLOTS 16
typedef struct Ps2ReverseTintCacheEntry {
    struct Texture tex;
    uint8_t* mem;
    size_t capacity;
    const struct Texture* source;
    uint32_t source_serial;
    uint32_t prim_rgb;
    uint32_t env_rgb;
    uint32_t stamp;
    uint32_t last_used_frame;
    bool valid;
} Ps2ReverseTintCacheEntry;
static Ps2ReverseTintCacheEntry sReverseTintCache[PS2_REVERSE_TINT_CACHE_SLOTS];
static uint32_t sReverseTintStamp;

#define PS2_BLEND_CACHE_SLOTS 256
#define PS2_BLEND_CACHE_BUDGET (4U * 1024U * 1024U)
typedef struct Ps2BlendCacheEntry {
    struct Texture tex;
    uint8_t* mem;
    size_t capacity;
    const struct Texture* source;
    uint32_t source_serial;
    uint32_t base_rgb;
    uint32_t target_rgb;
    uint8_t kind;
    uint32_t stamp;
    uint32_t last_used_frame;
    bool valid;
} Ps2BlendCacheEntry;
static Ps2BlendCacheEntry sBlendCache[PS2_BLEND_CACHE_SLOTS];
static uint32_t sBlendStamp;
static size_t sBlendCacheBytes;
static uint32_t sPs2RenderFrameSerial = 1;
static uint32_t sTextureUploadSerial = 1;
typedef struct Ps2FogPassState {
    bool active;
    bool saved_z_write;
    bool saved_blend;
    bool saved_alpha_test;
    bool saved_texture_alpha;
    uint8_t saved_tfx;
    struct ShaderProgram* saved_shader;
} Ps2FogPassState;
static Ps2FogPassState sFogPass;
typedef struct Ps2PrerenderFallback {
    struct Texture* texture;
    float x1, y1, x2, y2, u1, v1, u2, v2;
    int z1, z2;
    u64 color;
    unsigned missedFrames;
    bool valid;
    bool seenThisFrame;
} Ps2PrerenderFallback;
static Ps2PrerenderFallback sPrerenderFallback;
static bool sPrerender3dStarted;
static bool sPrerenderRoomActive;
static bool sPrerenderDepthOnlyPass;
static u32 sPrerenderRoomKey = 0xffffffffU;
void gfx_ps2_set_texture_blend_reverse(bool enabled){sTextureBlendReverse=enabled;}
void gfx_ps2_set_texture_blend_precolor(bool enabled){sTextureBlendPrecolor=enabled;}
void gfx_ps2_set_din_fire_tint(bool enabled){sDinFireTint=enabled;}
void gfx_ps2_set_two_texture_blend_active(bool enabled){sTwoTextureBlendActive=enabled;}
void gfx_ps2_set_two_texture_env_prim_tint(bool enabled){sTwoTextureEnvPrimTint=enabled;}
void gfx_ps2_set_skip_content_hash(bool enabled){sSkipContentHash=enabled;}
void gfx_ps2_set_fps_overlay_enabled(bool enabled){sFpsOverlayEnabled=enabled;}
bool gfx_ps2_get_fps_overlay_enabled(void){return sFpsOverlayEnabled;}
void gfx_ps2_set_prerender_room_state(bool active, u32 roomKey){
    if (sPrerenderRoomActive != active || sPrerenderRoomKey != roomKey) {

        sPrerenderFallback.valid=false;
        sPrerenderFallback.texture=NULL;
        sPrerenderFallback.seenThisFrame=false;
        sPrerenderFallback.missedFrames=0;
        sPrerenderRoomActive=active;
        sPrerenderRoomKey=roomKey;
    }
}

void gfx_ps2_invalidate_register_cache(void){
    sLastTexFilter=-1;
    sLastTestReg=~0ULL;
    sLastClampReg=~0ULL;
    sLastScissorReg=~0ULL;
    sLastZbufReg=~0ULL;
    sLastAlphaReg=~0ULL;
    sLastTex0Reg=~0ULL;
    sLastFogColor=0xffffffffU;
    sPs2SkipNextFogPass=false;
    sLastBoundGsTex=NULL;
}

static inline uint64_t colorq_from_rgba(uint32_t rgba,float q,bool textured){
    ColorQ c;

    const uint32_t alphaHalf = ((((rgba >> 24) + 1U) >> 1) & 0xffU) << 24;
    if(textured){
        const uint32_t rgbHalf = ((rgba & 0x00fefefeU) >> 1) + (rgba & 0x00010101U);
        c.rgba = rgbHalf | alphaHalf;
    }else{
        c.rgba = (rgba & 0x00ffffffU) | alphaHalf;
    }
    c.q=q;
    return c.word;
}
static struct ShaderProgram* ps2_lookup_shader(uint32_t id);
static struct ShaderProgram* ps2_effective_shader(struct ShaderProgram* p);
static bool ps2_z01(void){return true;}
static void ps2_unload_shader(struct ShaderProgram*p){
    struct ShaderProgram* effective=ps2_effective_shader(p);
    if(p==NULL || cur_shader==effective) cur_shader=NULL;
}
static void ps2_load_shader(struct ShaderProgram*p){cur_shader=ps2_effective_shader(p);}
static struct ShaderProgram* ps2_create_shader(uint32_t id){
    struct CCFeatures f; gfx_cc_get_features(id,&f);

    if(shader_count>=MAX_SHADERS){
        printf("oot-ps2 shader pool exhausted count=%u id=%08x\n", shader_count, (unsigned)id);
        abort();
    }
    const unsigned shaderIndex = shader_count++;
    struct ShaderProgram*p=&shader_pool[shaderIndex]; memset(p,0,sizeof(*p));
    p->enabled=true;
    p->shader_id=id;
    p->cc=f;
    p->num_inputs=f.num_inputs;

    p->texture_used[0]=f.used_textures[0] || f.used_textures[1];
    p->texture_used[1]=false;
    p->alpha_test=f.opt_texture_edge || ((id & SHADER_OPT_ALPHA_THRESHOLD) != 0);
    p->alpha_ref=f.opt_texture_edge ? 0x2a : 0;
    p->depth_only=(id & SHADER_OPT_DEPTH_ONLY) != 0;
    p->texture_alpha=false;
    if(f.opt_alpha){
        for(int i=0;i<4;i++){
            if(f.c[1][i]==SHADER_TEXEL0 || f.c[1][i]==SHADER_TEXEL0A || f.c[1][i]==SHADER_TEXEL1){
                p->texture_alpha=true;
                break;
            }
        }
    }

    p->texture_mode = PS2_TEXTURE_MODE_MODULATE;
    if (p->texture_used[0] && f.num_inputs) {
        p->mix = 4;
        if (f.opt_texture_blend) {
            p->texture_mode = PS2_TEXTURE_MODE_BLEND;
        } else {
            switch (id) {
                case 0x0000038DU:
                case 0x01045A00U:
                case 0x01200A00U:
                    p->texture_mode = PS2_TEXTURE_MODE_DECAL;
                    break;
                case 0x00000551U:
                    p->texture_mode = PS2_TEXTURE_MODE_BLEND;
                    break;
                default:
                    break;
            }
        }
    } else if (p->texture_used[0]) {
        p->mix = 1;
    } else if (f.num_inputs > 1) {
        p->mix = 5;
    } else if (f.num_inputs) {
        p->mix = 2;
    }
    if (id == 0x01A00045U) {
        p->texture_mode = PS2_TEXTURE_MODE_REPLACE;
    }

    p->tfx = (p->texture_mode == PS2_TEXTURE_MODE_REPLACE ||
              (p->texture_mode == PS2_TEXTURE_MODE_DECAL && !p->texture_alpha)) ? 1 : 0;

    uint32_t slot = (id * 2654435761U) & (PS2_SHADER_HASH_SLOTS - 1U);
    for (uint32_t probe = 0; probe < PS2_SHADER_HASH_SLOTS; probe++) {
        if (shader_hash[slot] == 0U) {
            shader_hash[slot] = (uint16_t)(shaderIndex + 1U);
            break;
        }
        slot = (slot + 1U) & (PS2_SHADER_HASH_SLOTS - 1U);
    }
    cur_shader=ps2_effective_shader(p); return p;
}
static struct ShaderProgram* ps2_lookup_shader(uint32_t id){
    uint32_t slot = (id * 2654435761U) & (PS2_SHADER_HASH_SLOTS - 1U);
    for (uint32_t probe = 0; probe < PS2_SHADER_HASH_SLOTS; probe++) {
        const uint16_t encoded = shader_hash[slot];
        if (encoded == 0U) return NULL;
        struct ShaderProgram* p = &shader_pool[(unsigned)encoded - 1U];
        if (p->shader_id == id) return p;
        slot = (slot + 1U) & (PS2_SHADER_HASH_SLOTS - 1U);
    }
    return NULL;
}
static void ps2_shader_info(struct ShaderProgram*p,uint8_t*n,bool t[2]){*n=(uint8_t)p->num_inputs;t[0]=p->texture_used[0];t[1]=p->texture_used[1];}
static struct ShaderProgram* ps2_effective_shader(struct ShaderProgram* p){
    if(p==NULL)return NULL;

    switch(p->shader_id){
        case 0x09200045U:
        case 0x09200200U:
        case 0x0920038DU:
        case 0x09200A00U:{
            struct ShaderProgram* remap=ps2_lookup_shader(0x00000045U);
            return remap!=NULL?remap:p;
        }
        default:
            return p;
    }
}

static void ps2_release_texture_residency(GSTEXTURE* tex){
    if(!gs_global || !tex) return;

    gsKit_TexManager_free(gs_global, tex);
    tex->Vram=0;
    tex->VramClut=0;
    if(sLastBoundGsTex==tex){
        sLastBoundGsTex=NULL;
        sLastTex0Reg=~0ULL;
        sLastTexFilter=-1;
    }
}

static uint32_t ps2_new_texture(void){

    if(tex_count>=MAX_TEXTURES) texman_clear();
    uint32_t id=tex_count++; struct Texture*t=&tex_pool[id];
    if(sPrerenderFallback.texture==t)sPrerenderFallback.valid=false;
    if(t->tex.Mem!=NULL || t->upload_serial!=0){
        ps2_release_texture_residency(&t->tex);
    }
    memset(t,0,sizeof(*t)); t->tex.Filter=GS_FILTER_NEAREST; t->clamp_s=t->clamp_t=GS_CMODE_REPEAT;

    last_tex=t;
    return id;
}
static void ps2_select_texture(int tile,uint32_t id){
    if(tile<0||tile>1||id>=MAX_TEXTURES)return;
    cur_tex[tile]=last_tex=&tex_pool[id];
    last_tex->tex.Filter=sampler_linear[tile]?GS_FILTER_LINEAR:GS_FILTER_NEAREST;
    last_tex->clamp_s=sampler_clamp_s[tile]; last_tex->clamp_t=sampler_clamp_t[tile];

    last_tex->cms=sampler_cms[tile]; last_tex->cmt=sampler_cmt[tile];
    last_tex->masks=sampler_masks[tile]; last_tex->maskt=sampler_maskt[tile];
}

static void *alloc_tex_bytes(size_t n){
    n=ALIGN(n,128);
    if(!tex_cache || n>(size_t)(tex_end-tex_ptr)){
        printf("oot-ps2 texture backing invariant failed need=%u remain=%u\n",
               (unsigned)n, tex_cache?(unsigned)(tex_end-tex_ptr):0U);
        abort();
    }
    void*p=tex_ptr; tex_ptr+=n; return p;
}

static void ps2_init_intensity_t8_clut(void){
    if(sIntensityT8ClutReady)return;
    for(unsigned i=0;i<256;i++){
        const uint32_t v=i;
        const uint32_t a=(v+1U)>>1;
        sIntensityT8Clut[i]=v|(v<<8)|(v<<16)|(a<<24);
    }

    for(unsigned i=0;i<256;i++){
        if((i&0x18U)==0x08U){
            const uint32_t tmp=sIntensityT8Clut[i];
            sIntensityT8Clut[i]=sIntensityT8Clut[i+8U];
            sIntensityT8Clut[i+8U]=tmp;
        }
    }
    SyncDCache(sIntensityT8Clut,(uint8_t*)sIntensityT8Clut+sizeof(sIntensityT8Clut));
    sIntensityT8ClutReady=true;
}

static void ps2_upload_texture(const uint8_t*buf,int w,int h,unsigned type){
    if(!last_tex||w<=0||h<=0)return;
#if OOT_PS2_RAPI_PROFILE
    const uint32_t profUploadStart=ps2_prof_count();
#endif
    struct Texture *pt=last_tex;
    if(sPrerenderFallback.texture==pt)sPrerenderFallback.valid=false;
    GSTEXTURE*t=&pt->tex;
    const int new_psm=(type==1)?GS_PSM_CT16:((type==5)?GS_PSM_T8:GS_PSM_CT32);
    const size_t pixels=(size_t)w*h;
    const size_t n=pixels*((new_psm==GS_PSM_CT16)?2U:((new_psm==GS_PSM_T8)?1U:4U));
    const bool reuse=(t->Mem!=NULL && pt->backing_size>=n && t->PSM==new_psm &&
                      t->Width==(u32)w && t->Height==(u32)h);

    if(reuse){

        gsKit_TexManager_invalidate(gs_global,t);
    }else{

        if(t->Mem!=NULL || pt->upload_serial!=0){
            ps2_release_texture_residency(t);
        }
        t->Mem=alloc_tex_bytes(n);
        pt->backing_size=n;
    }
    t->Width=w;t->Height=h;t->PSM=new_psm;t->Filter=GS_FILTER_NEAREST;t->Vram=0;t->VramClut=0;
    if(new_psm==GS_PSM_T8){
        ps2_init_intensity_t8_clut();
        t->ClutPSM=GS_PSM_CT32;
        t->Clut=sIntensityT8Clut;
        t->ClutStorageMode=GS_CLUT_STORAGE_CSM1;
    }else{
        t->ClutPSM=0;
        t->Clut=NULL;
        t->ClutStorageMode=GS_CLUT_STORAGE_CSM1;
    }
    pt->upload_serial = ++sTextureUploadSerial;
    if (sTextureUploadSerial == 0) { sTextureUploadSerial = 1; pt->upload_serial = 1; }

    if(type==1){
        memcpy(t->Mem,buf,n);
    }else if(type==5){
        memcpy(t->Mem,buf,n);
    }else if(type==8){
        memcpy(t->Mem,buf,n);
    }else{
        u32*out=(u32*)t->Mem;
        if(type==2){
            const uint16_t*in=(const uint16_t*)buf; for(size_t i=0;i<pixels;i++){uint16_t v=in[i]; uint8_t r=(v&0xf)*17,g=((v>>4)&0xf)*17,b=((v>>8)&0xf)*17,a=(uint8_t)(((((v>>12)&0xf)*17)+1)>>1); out[i]=r|(g<<8)|(b<<16)|((u32)a<<24);}
        }else{
            const u32*in=(const u32*)buf;
            for(size_t i=0;i<pixels;i++){u32 v=in[i]; u32 a=(((v>>24)&0xff)+1)>>1; out[i]=(v&0x00ffffffU)|(a<<24);}
        }
    }

    pt->content_hash=0;
    if(!sSkipContentHash && new_psm==GS_PSM_CT32 && w==32 && h==32){
        const uint8_t* hp=(const uint8_t*)t->Mem;
        uint32_t hval=2166136261U;
        for(size_t i=0;i<n;i++){hval^=hp[i];hval*=16777619U;}
        pt->content_hash=hval;
    }
    SyncDCache(t->Mem,(u8*)t->Mem+n);
#if OOT_PS2_RAPI_PROFILE
    {
        const uint32_t d = (uint32_t)(ps2_prof_count()-profUploadStart);
        sProfUploadCycles += d;
        sProfFrameUploadCycles += d;
    }
    sProfUploadCalls++;
    sProfFrameUploadCalls++;
#endif
}

float identity_matrix[4][4] __attribute__((aligned(16))) = {
    {1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}
};

int gfx_vram_space_available(void){ return tex_cache && tex_ptr < tex_end; }
int texman_vram_space_available(unsigned int size){
    if(!tex_cache) return 0;
    size_t n=ALIGN((size_t)size,128);
    return n <= (size_t)(tex_end-tex_ptr);
}
int texman_texture_slot_available(void){ return tex_count < MAX_TEXTURES; }
void texman_clear(void){
    sPrerenderFallback.valid=false;
    sPrerenderFallback.texture=NULL;
    if(gs_global){

        gsKit_queue_exec(gs_global);
        dmaKit_wait_fast();
        gsKit_queue_reset(gs_global->Os_Queue);
        gsKit_TexManager_init(gs_global);
        sLastBoundGsTex=NULL;
        sLastTex0Reg=~0ULL;
    }
    for(unsigned i=0;i<PS2_BLEND_CACHE_SLOTS;i++){
        sBlendCache[i].valid=false;
        sBlendCache[i].source=NULL;
        sBlendCache[i].source_serial=0;
        sBlendCache[i].last_used_frame=0;
        sBlendCache[i].tex.tex.Vram=0;
        sBlendCache[i].tex.tex.VramClut=0;
    }

    for(unsigned i=0;i<PS2_REVERSE_TINT_CACHE_SLOTS;i++){
        sReverseTintCache[i].valid=false;
        sReverseTintCache[i].source=NULL;
        sReverseTintCache[i].source_serial=0;
        sReverseTintCache[i].last_used_frame=0;
        sReverseTintCache[i].tex.tex.Vram=0;
        sReverseTintCache[i].tex.tex.VramClut=0;
    }
    memset(tex_pool,0,sizeof(tex_pool)); tex_count=0; cur_tex[0]=cur_tex[1]=last_tex=NULL;
    if(tex_cache){ tex_ptr=tex_cache; tex_end=tex_cache+TEXCACHE_SIZE; }
}
void texman_upload(int width,int height,unsigned int type,const void*buffer){ ps2_upload_texture((const uint8_t*)buffer,width,height,type); }

static inline void ps2_bind_texture(GSTEXTURE*t){

    if (__builtin_expect(sLastBoundGsTex == t && t->Vram != 0, 1)) {
        return;
    }
    const u32 oldVram=t->Vram;

#if OOT_PS2_RAPI_PROFILE
    const uint32_t profBindStart=ps2_prof_count();
#endif
    const unsigned transferred=gsKit_TexManager_bind(gs_global,t);
#if OOT_PS2_RAPI_PROFILE
    {
        const uint32_t d = (uint32_t)(ps2_prof_count()-profBindStart);
        sProfBindCycles += d;
        sProfFrameBindCycles += d;
    }
    sProfBindCalls++;
    sProfFrameBindCalls++;
    if(transferred) { sProfBindTransfers++; sProfFrameBindTransfers++; }
#endif
    if(transferred || t->Vram!=oldVram || sLastBoundGsTex!=t){
        sLastTex0Reg=~0ULL;
        sLastTexFilter=-1;
    }
    sLastBoundGsTex=t;
}
static inline uint32_t cm_ps2(uint32_t v,uint32_t mask){return ((v&2)||mask==0)?GS_CMODE_CLAMP:GS_CMODE_REPEAT;}
static void ps2_sampler(int tile,bool linear,uint32_t cms,uint32_t cmt,uint32_t masks,uint32_t maskt){
    if(tile<0||tile>1)return;
    sampler_linear[tile]=linear; sampler_clamp_s[tile]=cm_ps2(cms,masks); sampler_clamp_t[tile]=cm_ps2(cmt,maskt);
    sampler_cms[tile]=(uint8_t)cms; sampler_cmt[tile]=(uint8_t)cmt;
    sampler_masks[tile]=(uint8_t)masks; sampler_maskt[tile]=(uint8_t)maskt;
    if(cur_tex[tile]){cur_tex[tile]->tex.Filter=linear?GS_FILTER_LINEAR:GS_FILTER_NEAREST;cur_tex[tile]->clamp_s=sampler_clamp_s[tile];cur_tex[tile]->clamp_t=sampler_clamp_t[tile];cur_tex[tile]->cms=(uint8_t)cms;cur_tex[tile]->cmt=(uint8_t)cmt;cur_tex[tile]->masks=(uint8_t)masks;cur_tex[tile]->maskt=(uint8_t)maskt;}
}
static void ps2_env_color(uint8_t r,uint8_t g,uint8_t b,uint8_t a){sEnvColor=r|(g<<8)|(b<<16)|(a<<24);(void)sEnvColor;}
static void ps2_depth_test(bool e){z_test=e;}
static void ps2_depth_mask(bool write){
    u64 reg;
    z_write=write;
    reg=GS_SETREG_ZBUF_1(gs_global->ZBuffer/8192,gs_global->PSMZ,!write);
    if(reg==sLastZbufReg)return;
    sLastZbufReg=reg;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;*p++=reg;*p++=GS_ZBUF_1+gs_global->PrimContext;
}
static void ps2_zdecal(bool e){z_decal=e;z_offset=e?32.0f:0.0f;}
static void ps2_viewport(int x,int y,int w,int h){

    int top=gs_global->Height-y-h;
    r_view.x=x;r_view.y=top;r_view.w=w;r_view.h=h;r_view.hw=w*.5f;r_view.hh=h*.5f;r_view.cx=x+r_view.hw;r_view.cy=top+r_view.hh;
}
static void set_scissor_raw(int x0,int y0,int x1,int y1){
    u64 reg=GS_SETREG_SCISSOR_1(x0,x1,y0,y1);
    if(reg==sLastScissorReg)return;
    sLastScissorReg=reg;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;*p++=reg;*p++=GS_SCISSOR_1+gs_global->PrimContext;
}
static void ps2_scissor(int x,int y,int w,int h){

    int sy=gs_global->Height-y-h;
    r_clip.x0=x;r_clip.y0=sy;r_clip.x1=x+w-1;r_clip.y1=sy+h-1;
    if(r_clip.x0<0)r_clip.x0=0;
    if(r_clip.y0<0)r_clip.y0=0;
    if(r_clip.x1>=gs_global->Width)r_clip.x1=gs_global->Width-1;
    if(r_clip.y1>=gs_global->Height)r_clip.y1=gs_global->Height-1;
    set_scissor_raw(r_clip.x0,r_clip.y0,r_clip.x1,r_clip.y1);
}
static void set_blend(bool e){
    u64 reg=e?BMODE_BLEND:0;
    do_blend=e;gs_global->PrimAlphaEnable=e;gs_global->PrimAlpha=reg;
    if(reg==sLastAlphaReg)return;
    sLastAlphaReg=reg;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;*p++=reg;*p++=GS_ALPHA_1+gs_global->PrimContext;
}
static void ps2_alpha(bool e){set_blend(e);}
static inline unsigned ilog2ceil(unsigned v);
static void update_test(void){
    gs_global->Test->ATE=cur_shader&&cur_shader->alpha_test;
    gs_global->Test->ATST=gs_global->Test->ATE?6:1;
    gs_global->Test->AREF=(cur_shader&&cur_shader->alpha_test)?cur_shader->alpha_ref:0;

    gs_global->Test->ZTE=1;gs_global->Test->ZTST=z_test?2:1;
    u64 reg=GS_SETREG_TEST(gs_global->Test->ATE,gs_global->Test->ATST,gs_global->Test->AREF,gs_global->Test->AFAIL,gs_global->Test->DATE,gs_global->Test->DATM,gs_global->Test->ZTE,gs_global->Test->ZTST);
    if(reg==sLastTestReg)return;
    sLastTestReg=reg;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;
    *p++=reg;*p++=GS_TEST_1+gs_global->PrimContext;
}
static void clamp_tex(struct Texture*t,bool edge_s,bool edge_t){
    uint32_t wms=t->clamp_s,wmt=t->clamp_t,minu=0,maxu=0,minv=0,maxv=0;
    const uint32_t width=t->tex.Width>0?(uint32_t)t->tex.Width:1U;
    const uint32_t height=t->tex.Height>0?(uint32_t)t->tex.Height:1U;

    if((t->cms&2) || t->masks==0){
        wms=GS_CMODE_REGION_CLAMP; minu=0; maxu=width-1;
    }else if(!(t->cms&1) && t->masks>0 && t->masks<10){

        uint32_t period=1u<<t->masks;
        uint32_t texpot=1u<<ilog2ceil(width);
        if(period<texpot){ wms=GS_CMODE_REGION_REPEAT; minu=period-1; maxu=0; }
    }else if(!(t->cms&1) && t->masks==0 && edge_s && t->tex.Filter==GS_FILTER_LINEAR){

        wms=GS_CMODE_REGION_CLAMP; minu=0; maxu=width-1;
    }
    if((t->cmt&2) || t->maskt==0){
        wmt=GS_CMODE_REGION_CLAMP; minv=0; maxv=height-1;
    }else if(!(t->cmt&1) && t->maskt>0 && t->maskt<10){
        uint32_t period=1u<<t->maskt;
        uint32_t texpot=1u<<ilog2ceil(height);
        if(period<texpot){ wmt=GS_CMODE_REGION_REPEAT; minv=period-1; maxv=0; }
    }else if(!(t->cmt&1) && t->maskt==0 && edge_t && t->tex.Filter==GS_FILTER_LINEAR){
        wmt=GS_CMODE_REGION_CLAMP; minv=0; maxv=height-1;
    }
    gs_global->Clamp->WMS=wms;gs_global->Clamp->WMT=wmt;
    u64 reg=GS_SETREG_CLAMP(wms,wmt,minu,maxu,minv,maxv);
    if(reg==sLastClampReg)return;
    sLastClampReg=reg;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;
    *p++=reg;*p++=GS_CLAMP_1+gs_global->PrimContext;
}

#define GIF_TAG_TRI_ST_REGS ((u64)GS_PRIM<<0)|((u64)GS_ST<<4)|((u64)GS_RGBAQ<<8)|((u64)GS_XYZ2<<12)|((u64)GS_ST<<16)|((u64)GS_RGBAQ<<20)|((u64)GS_XYZ2<<24)|((u64)GS_ST<<28)|((u64)GS_RGBAQ<<32)|((u64)GS_XYZ2<<36)
#define GIF_TAG_TRI_ST_FOG_REGS ((u64)GS_PRIM<<0)|((u64)GS_ST<<4)|((u64)GS_RGBAQ<<8)|((u64)GS_XYZF2<<12)|((u64)GS_ST<<16)|((u64)GS_RGBAQ<<20)|((u64)GS_XYZF2<<24)|((u64)GS_ST<<28)|((u64)GS_RGBAQ<<32)|((u64)GS_XYZF2<<36)
#define GIF_TAG_TRI_FOG_REGS ((u64)GS_PRIM<<0)|((u64)GS_RGBAQ<<4)|((u64)GS_XYZF2<<8)|((u64)GS_RGBAQ<<12)|((u64)GS_XYZF2<<16)|((u64)GS_RGBAQ<<20)|((u64)GS_XYZF2<<24)|((u64)GIF_NOP<<28)
#define OOT_PS2_GIF_PRIM_TRIANGLE_GOURAUD_FOG 0x7F01
#define OOT_PS2_GIF_PRIM_TRIANGLE_TEXTURED_ST 0x7F02
#define OOT_PS2_GIF_PRIM_TRIANGLE_TEXTURED_ST_FOG 0x7F03
#define OOT_PS2_GIF_PRIM_SPRITE_TEXTURED 0x7F04
#define OOT_PS2_GIF_PRIM_TRIANGLE_GOURAUD 0x7F05
static inline unsigned ilog2ceil(unsigned v){ return v<=1U ? 0U : (32U-(unsigned)__builtin_clz(v-1U)); }
static void set_fog_color(uint32_t rgb){
    if(rgb==sLastFogColor)return;
    sLastFogColor=rgb;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;
    *p++=PS2_SETREG_FOGCOL(rgb&0xff,(rgb>>8)&0xff,(rgb>>16)&0xff);*p++=GS_FOGCOL;
}
static inline void ps2_set_texfilter_cached(int filter){
    if(filter==sLastTexFilter)return;
    gsKit_set_texfilter(gs_global,filter);
    sLastTexFilter=filter;
}
static inline u64 ps2_tex0_reg(const GSTEXTURE*t,int tw,int th){
    const bool indexed=t->VramClut!=0;
    return GS_SETREG_TEX0(t->Vram/256,t->TBW,t->PSM,tw,th,
        (cur_shader&&cur_shader->texture_alpha)?1:0,cur_shader?cur_shader->tfx:0,
        indexed?t->VramClut/256:0,indexed?t->ClutPSM:0,indexed?t->ClutStorageMode:0,0,
        indexed?GS_CLUT_STOREMODE_LOAD:GS_CLUT_STOREMODE_NOLOAD);
}
static inline void ps2_set_tex0_cached(GSTEXTURE*t){
    const int tw=ilog2ceil((unsigned)t->Width),th=ilog2ceil((unsigned)t->Height);
    const u64 reg=ps2_tex0_reg(t,tw,th);
    if(reg==sLastTex0Reg)return;
    sLastTex0Reg=reg;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;*p++=reg;*p++=GS_TEX0_1+gs_global->PrimContext;
}
static inline float ps2_colorq_q(uint64_t word){ ColorQ c; c.word=word; return c.q; }

static const float sPs2MaskInvPeriod[10] = {
    0.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f,
    0.015625f, 0.0078125f, 0.00390625f, 0.001953125f
};

static inline void ps2_normalize_repeat_axis(float* a1,float* a2,float* a3,
                                              float q1,float q2,float q3,
                                              uint8_t cm,uint8_t mask){
    if((cm&2) || mask==0 || mask>=10) return;
    if(!__builtin_isfinite(q1)||!__builtin_isfinite(q2)||!__builtin_isfinite(q3)) return;
    if((q1>-0.000001f&&q1<0.000001f)||(q2>-0.000001f&&q2<0.000001f)||(q3>-0.000001f&&q3<0.000001f)) return;
    const float u1=*a1/q1, u2=*a2/q2, u3=*a3/q3;
    if(!__builtin_isfinite(u1)||!__builtin_isfinite(u2)||!__builtin_isfinite(u3)) return;
    float period=(float)(1u<<mask);
    float invPeriod=sPs2MaskInvPeriod[mask];

    if(cm&1){ period*=2.0f; invPeriod*=0.5f; }
    const float center=(u1+u2+u3)*(1.0f/3.0f);
    float cycles=center*invPeriod;
    const int whole=(int)(cycles>=0.0f ? cycles+0.5f : cycles-0.5f);
    const float shift=(float)whole*period;
    *a1=(u1-shift)*q1;
    *a2=(u2-shift)*q2;
    *a3=(u3-shift)*q3;
}

static inline void ps2_normalize_repeat_axis_known(float* a1,float* a2,float* a3,
                                                    float u1,float u2,float u3,
                                                    float q1,float q2,float q3,
                                                    uint8_t cm,uint8_t mask){
    if((cm&2) || mask==0 || mask>=10) return;
    if(!__builtin_isfinite(u1)||!__builtin_isfinite(u2)||!__builtin_isfinite(u3)) return;
    float period=(float)(1u<<mask);
    float invPeriod=sPs2MaskInvPeriod[mask];
    if(cm&1){ period*=2.0f; invPeriod*=0.5f; }
    const float center=(u1+u2+u3)*(1.0f/3.0f);
    const float cycles=center*invPeriod;
    const int whole=(int)(cycles>=0.0f ? cycles+0.5f : cycles-0.5f);
    const float shift=(float)whole*period;
    *a1=(u1-shift)*q1;
    *a2=(u2-shift)*q2;
    *a3=(u3-shift)*q3;
}

static inline void tri_tex_emit_reserved_known(u64** pp,struct Texture*pt,
    float x1,float y1,int z1,float s1,float tt1,float ru1,float rv1,uint64_t c1,
    float x2,float y2,int z2,float s2,float tt2,float ru2,float rv2,uint64_t c2,
    float x3,float y3,int z3,float s3,float tt3,float ru3,float rv3,uint64_t c3,
    bool fog,uint8_t f1,uint8_t f2,uint8_t f3){
    const float q1=ps2_colorq_q(c1),q2=ps2_colorq_q(c2),q3=ps2_colorq_q(c3);
    ps2_normalize_repeat_axis_known(&s1,&s2,&s3,ru1,ru2,ru3,q1,q2,q3,pt->cms,pt->masks);
    ps2_normalize_repeat_axis_known(&tt1,&tt2,&tt3,rv1,rv2,rv3,q1,q2,q3,pt->cmt,pt->maskt);
    const int ix1=gsKit_float_to_int_x(gs_global,x1),iy1=gsKit_float_to_int_y(gs_global,y1);
    const int ix2=gsKit_float_to_int_x(gs_global,x2),iy2=gsKit_float_to_int_y(gs_global,y2);
    const int ix3=gsKit_float_to_int_x(gs_global,x3),iy3=gsKit_float_to_int_y(gs_global,y3);
    TexCoord a={{s1,tt1}},b={{s2,tt2}},c={{s3,tt3}};
    u64* p=*pp;
    *p++=GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE,1,1,fog?1:0,gs_global->PrimAlphaEnable,0,0,gs_global->PrimContext,0);
    *p++=a.word;*p++=c1;*p++=(fog?GS_SETREG_XYZF2(ix1,iy1,z1,f1):GS_SETREG_XYZ2(ix1,iy1,z1));
    *p++=b.word;*p++=c2;*p++=(fog?GS_SETREG_XYZF2(ix2,iy2,z2,f2):GS_SETREG_XYZ2(ix2,iy2,z2));
    *p++=c.word;*p++=c3;*p++=(fog?GS_SETREG_XYZF2(ix3,iy3,z3,f3):GS_SETREG_XYZ2(ix3,iy3,z3));
    *pp=p;
}

static inline void tri_tex_emit_reserved(u64** pp,struct Texture*pt,
    float x1,float y1,int z1,float s1,float tt1,uint64_t c1,
    float x2,float y2,int z2,float s2,float tt2,uint64_t c2,
    float x3,float y3,int z3,float s3,float tt3,uint64_t c3,
    bool fog,uint8_t f1,uint8_t f2,uint8_t f3){
    const float q1=ps2_colorq_q(c1),q2=ps2_colorq_q(c2),q3=ps2_colorq_q(c3);
    ps2_normalize_repeat_axis(&s1,&s2,&s3,q1,q2,q3,pt->cms,pt->masks);
    ps2_normalize_repeat_axis(&tt1,&tt2,&tt3,q1,q2,q3,pt->cmt,pt->maskt);
    const int ix1=gsKit_float_to_int_x(gs_global,x1),iy1=gsKit_float_to_int_y(gs_global,y1);
    const int ix2=gsKit_float_to_int_x(gs_global,x2),iy2=gsKit_float_to_int_y(gs_global,y2);
    const int ix3=gsKit_float_to_int_x(gs_global,x3),iy3=gsKit_float_to_int_y(gs_global,y3);
    TexCoord a={{s1,tt1}},b={{s2,tt2}},c={{s3,tt3}};
    u64* p=*pp;
    *p++=GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE,1,1,fog?1:0,gs_global->PrimAlphaEnable,0,0,gs_global->PrimContext,0);
    *p++=a.word;*p++=c1;*p++=(fog?GS_SETREG_XYZF2(ix1,iy1,z1,f1):GS_SETREG_XYZ2(ix1,iy1,z1));
    *p++=b.word;*p++=c2;*p++=(fog?GS_SETREG_XYZF2(ix2,iy2,z2,f2):GS_SETREG_XYZ2(ix2,iy2,z2));
    *p++=c.word;*p++=c3;*p++=(fog?GS_SETREG_XYZF2(ix3,iy3,z3,f3):GS_SETREG_XYZ2(ix3,iy3,z3));
    *pp=p;
}

static size_t tri_tex_batch_safe_count(size_t requested,bool fog){
    const int type=fog?OOT_PS2_GIF_PRIM_TRIANGLE_TEXTURED_ST_FOG:OOT_PS2_GIF_PRIM_TRIANGLE_TEXTURED_ST;
    size_t room=GS_GIF_BLOCKSIZE;

    if(gs_global->CurQueue->last_type==type && gs_global->CurQueue->same_obj>0 &&
       gs_global->CurQueue->same_obj<GS_GIF_BLOCKSIZE){
        room=(size_t)(GS_GIF_BLOCKSIZE-gs_global->CurQueue->same_obj);
    }
    if(room==0)room=1;
    return requested<room?requested:room;
}

static u64* tri_tex_batch_begin(struct Texture*pt,size_t count,bool fog){
    if(count==0)return NULL;
    GSTEXTURE*t=&pt->tex;
    ps2_set_texfilter_cached(t->Filter);
    ps2_set_tex0_cached(t);
    const int type=fog?OOT_PS2_GIF_PRIM_TRIANGLE_TEXTURED_ST_FOG:OOT_PS2_GIF_PRIM_TRIANGLE_TEXTURED_ST;

    if(count>GS_GIF_BLOCKSIZE)count=GS_GIF_BLOCKSIZE;
    u64*p_store=(u64*)gsKit_heap_alloc(gs_global,(int)(5U*count),(int)(80U*count),type);
    u64*p=p_store;
    if(p_store==gs_global->CurQueue->last_tag){
        *p++=GIF_TAG(0,1,0,0,GSKIT_GIF_FLG_REGLIST,10);
        *p++=fog?GIF_TAG_TRI_ST_FOG_REGS:GIF_TAG_TRI_ST_REGS;
    }
    if(count>1)gs_global->CurQueue->same_obj+=(int)(count-1U);
    return p;
}

static void tri_tex(struct Texture*pt,float x1,float y1,int z1,float s1,float tt1,uint64_t c1,float x2,float y2,int z2,float s2,float tt2,uint64_t c2,float x3,float y3,int z3,float s3,float tt3,uint64_t c3,bool fog,uint8_t f1,uint8_t f2,uint8_t f3){
    u64*p=tri_tex_batch_begin(pt,1,fog);
    tri_tex_emit_reserved(&p,pt,x1,y1,z1,s1,tt1,c1,x2,y2,z2,s2,tt2,c2,x3,y3,z3,s3,tt3,c3,fog,f1,f2,f3);
}

static size_t tri_col_batch_safe_count(size_t requested,bool fog){
    const int type=fog?OOT_PS2_GIF_PRIM_TRIANGLE_GOURAUD_FOG:OOT_PS2_GIF_PRIM_TRIANGLE_GOURAUD;
    size_t room=GS_GIF_BLOCKSIZE;
    if(gs_global->CurQueue->last_type==type && gs_global->CurQueue->same_obj>0 &&
       gs_global->CurQueue->same_obj<GS_GIF_BLOCKSIZE){
        room=(size_t)(GS_GIF_BLOCKSIZE-gs_global->CurQueue->same_obj);
    }
    if(room==0)room=1;
    return requested<room?requested:room;
}
static u64* tri_col_batch_begin(size_t count,bool fog){
    if(count==0)return NULL;
    const int type=fog?OOT_PS2_GIF_PRIM_TRIANGLE_GOURAUD_FOG:OOT_PS2_GIF_PRIM_TRIANGLE_GOURAUD;
    if(count>GS_GIF_BLOCKSIZE)count=GS_GIF_BLOCKSIZE;
    u64*p_store=(u64*)gsKit_heap_alloc(gs_global,(int)(4U*count),(int)(64U*count),type);
    u64*p=p_store;
    if(p_store==gs_global->CurQueue->last_tag){
        *p++=GIF_TAG_TRIANGLE_GOURAUD(0);
        *p++=fog?GIF_TAG_TRI_FOG_REGS:GIF_TAG_TRIANGLE_GOURAUD_REGS;
    }
    if(count>1)gs_global->CurQueue->same_obj+=(int)(count-1U);
    return p;
}
static inline void tri_col_emit_reserved(u64**pp,
    float x1,float y1,int z1,uint64_t c1,float x2,float y2,int z2,uint64_t c2,
    float x3,float y3,int z3,uint64_t c3,bool fog,uint8_t f1,uint8_t f2,uint8_t f3){
    const int ix1=gsKit_float_to_int_x(gs_global,x1),iy1=gsKit_float_to_int_y(gs_global,y1);
    const int ix2=gsKit_float_to_int_x(gs_global,x2),iy2=gsKit_float_to_int_y(gs_global,y2);
    const int ix3=gsKit_float_to_int_x(gs_global,x3),iy3=gsKit_float_to_int_y(gs_global,y3);
    u64*p=*pp;
    *p++=GS_SETREG_PRIM(GS_PRIM_PRIM_TRIANGLE,1,0,fog?1:0,gs_global->PrimAlphaEnable,0,0,gs_global->PrimContext,0);
    *p++=c1;*p++=(fog?GS_SETREG_XYZF2(ix1,iy1,z1,f1):GS_SETREG_XYZ2(ix1,iy1,z1));
    *p++=c2;*p++=(fog?GS_SETREG_XYZF2(ix2,iy2,z2,f2):GS_SETREG_XYZ2(ix2,iy2,z2));
    *p++=c3;*p++=(fog?GS_SETREG_XYZF2(ix3,iy3,z3,f3):GS_SETREG_XYZ2(ix3,iy3,z3));
    *p++=0;
    *pp=p;
}
static void sprite_tex(GSTEXTURE*t,float x1,float y1,int z1,float u1,float v1,float x2,float y2,int z2,float u2,float v2,uint64_t col){
    ps2_set_texfilter_cached(t->Filter);
    const int tw=ilog2ceil((unsigned)t->Width), th=ilog2ceil((unsigned)t->Height);
    const int ix1=gsKit_float_to_int_x(gs_global,x1), iy1=gsKit_float_to_int_y(gs_global,y1);
    const int ix2=gsKit_float_to_int_x(gs_global,x2), iy2=gsKit_float_to_int_y(gs_global,y2);
    const int iu1=gsKit_float_to_int_u(t,u1), iv1=gsKit_float_to_int_v(t,v1);
    const int iu2=gsKit_float_to_int_u(t,u2), iv2=gsKit_float_to_int_v(t,v2);
    u64*p_store;u64*p;
    p_store=p=gsKit_heap_alloc(gs_global,4,64,OOT_PS2_GIF_PRIM_SPRITE_TEXTURED);
    if(p_store==gs_global->CurQueue->last_tag){
        *p++=GIF_TAG_SPRITE_TEXTURED(0);
        *p++=GIF_TAG_SPRITE_TEXTURED_REGS(gs_global->PrimContext);
    }
    *p++=ps2_tex0_reg(t,tw,th);
    sLastTex0Reg=~0ULL;
    *p++=GS_SETREG_PRIM(GS_PRIM_PRIM_SPRITE,0,1,gs_global->PrimFogEnable,gs_global->PrimAlphaEnable,gs_global->PrimAAEnable,1,gs_global->PrimContext,0);
    *p++=col;
    *p++=GS_SETREG_UV(iu1,iv1);
    *p++=GS_SETREG_XYZ2(ix1,iy1,z1);
    *p++=GS_SETREG_UV(iu2,iv2);
    *p++=GS_SETREG_XYZ2(ix2,iy2,z2);
    *p++=0;
}

static inline void ps2_set_frame_write_mask(u32 mask){
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;
    *p++=GS_SETREG_FRAME_1(gs_global->ScreenBuffer[gs_global->ActiveBuffer & 1]/8192,
                           gs_global->Width/64,gs_global->PSM,mask);
    *p++=GS_FRAME_1+gs_global->PrimContext;
}

void gfx_ps2_set_prerender_depth_only(bool enabled){
    sPrerenderDepthOnlyPass=enabled;
    ps2_set_frame_write_mask(enabled ? 0xffffffffU : 0U);
}

static inline bool ps2_texture_cpu_rgba_supported(const struct Texture* source){
    return source!=NULL && source->tex.Mem!=NULL && source->tex.Width>0 && source->tex.Height>0 &&
           (source->tex.PSM==GS_PSM_CT32 || source->tex.PSM==GS_PSM_CT16 || source->tex.PSM==GS_PSM_T8);
}

static inline uint32_t ps2_texture_cpu_rgba(const struct Texture* source,size_t index){
    if(source->tex.PSM==GS_PSM_CT32){
        return ((const uint32_t*)source->tex.Mem)[index];
    }
    if(source->tex.PSM==GS_PSM_T8){
        const uint32_t v=((const uint8_t*)source->tex.Mem)[index];
        return v|(v<<8)|(v<<16)|(((v+1U)>>1)<<24);
    }
    const uint16_t v=((const uint16_t*)source->tex.Mem)[index];
    const uint32_t r=(uint32_t)(v&0x1fU)*255U/31U;
    const uint32_t g=(uint32_t)((v>>5)&0x1fU)*255U/31U;
    const uint32_t b=(uint32_t)((v>>10)&0x1fU)*255U/31U;
    const uint32_t a=(v&0x8000U)?0x80U:0U;
    return r|(g<<8)|(b<<16)|(a<<24);
}

static struct Texture* ps2_reverse_tint_texture(struct Texture* source, uint32_t prim, uint32_t env){
    const size_t pixels=(size_t)source->tex.Width*(size_t)source->tex.Height;
    const size_t bytes=pixels*4U;
    const uint32_t primRgb=prim&0x00ffffffU;
    const uint32_t envRgb=env&0x00ffffffU;
    Ps2ReverseTintCacheEntry* entry=NULL;
    Ps2ReverseTintCacheEntry* victim=NULL;

    if(!ps2_texture_cpu_rgba_supported(source) || pixels==0)return source;

    for(unsigned i=0;i<PS2_REVERSE_TINT_CACHE_SLOTS;i++){
        Ps2ReverseTintCacheEntry* e=&sReverseTintCache[i];
        if(e->valid && e->source==source && e->source_serial==source->upload_serial &&
           e->prim_rgb==primRgb && e->env_rgb==envRgb){
            e->tex.tex.Filter=source->tex.Filter; e->tex.clamp_s=source->clamp_s; e->tex.clamp_t=source->clamp_t;
            e->tex.cms=source->cms; e->tex.cmt=source->cmt; e->tex.masks=source->masks; e->tex.maskt=source->maskt;
            e->stamp=++sReverseTintStamp; e->last_used_frame=sPs2RenderFrameSerial;
            return &e->tex;
        }
        if(!e->valid && entry==NULL)entry=e;
        if(e->valid && (victim==NULL || e->stamp<victim->stamp))victim=e;
    }

    if(entry==NULL){
        entry=victim;
        if(entry==NULL)return source;

        if(entry->last_used_frame==sPs2RenderFrameSerial && gs_global && gs_global->Os_Queue->tag_size!=0){
            gsKit_queue_exec(gs_global);
            dmaKit_wait_fast();
            gsKit_queue_reset(gs_global->Os_Queue);
        }
        if(entry->tex.tex.Mem!=NULL)ps2_release_texture_residency(&entry->tex.tex);
    }

    if(entry->capacity<bytes){
        if(entry->mem)free(entry->mem);
        entry->capacity=ALIGN(bytes,128);
        entry->mem=memalign(128,entry->capacity);
        if(!entry->mem){entry->capacity=0;entry->valid=false;return source;}
    }

    uint32_t*out=(uint32_t*)entry->mem;
    const int pr=(int)(primRgb&0xff),pg=(int)((primRgb>>8)&0xff),pb=(int)((primRgb>>16)&0xff);
    const int er=(int)(envRgb&0xff),eg=(int)((envRgb>>8)&0xff),eb=(int)((envRgb>>16)&0xff);
    for(size_t i=0;i<pixels;i++){
        const uint32_t v=ps2_texture_cpu_rgba(source,i);
        const int tr=(int)(v&0xff),tg=(int)((v>>8)&0xff),tb=(int)((v>>16)&0xff);
        int r=pr+((er-pr)*tr+127)/255;
        int g=pg+((eg-pg)*tg+127)/255;
        int b=pb+((eb-pb)*tb+127)/255;
        if(r<0)r=0;else if(r>255)r=255;
        if(g<0)g=0;else if(g>255)g=255;
        if(b<0)b=0;else if(b>255)b=255;
        out[i]=(uint32_t)r|((uint32_t)g<<8)|((uint32_t)b<<16)|(v&0xff000000U);
    }
    SyncDCache(entry->mem,entry->mem+bytes);
    entry->tex=*source;
    entry->tex.tex.Mem=(u32*)entry->mem;
    entry->tex.tex.PSM=GS_PSM_CT32;
    entry->tex.tex.Vram=0;
    entry->tex.tex.VramClut=0;
    entry->tex.backing_size=entry->capacity;
    if(entry->tex.tex.Width==64 && entry->tex.tex.Height==64){
        entry->tex.clamp_s=GS_CMODE_CLAMP;
        entry->tex.clamp_t=GS_CMODE_CLAMP;
    }
    entry->source=source;
    entry->source_serial=source->upload_serial;
    entry->prim_rgb=primRgb;
    entry->env_rgb=envRgb;
    entry->stamp=++sReverseTintStamp;
    entry->last_used_frame=sPs2RenderFrameSerial;
    entry->valid=true;
    return &entry->tex;
}
static inline uint32_t ps2_reverse_neutral_color(uint32_t rgba){return 0x00ffffffU|(rgba&0xff000000U);}

static void ps2_blend_cache_sync_if_live(const Ps2BlendCacheEntry* entry){
    if(entry==NULL || !entry->valid || entry->last_used_frame!=sPs2RenderFrameSerial || !gs_global)return;
    if(gs_global->Os_Queue->tag_size!=0){
        gsKit_queue_exec(gs_global);
        dmaKit_wait_fast();
        gsKit_queue_reset(gs_global->Os_Queue);
    }
}

static void ps2_blend_cache_free_entry(Ps2BlendCacheEntry* entry){
    if(entry==NULL)return;
    ps2_blend_cache_sync_if_live(entry);
    if(entry->tex.tex.Mem!=NULL)ps2_release_texture_residency(&entry->tex.tex);
    if(entry->mem!=NULL){
        if(sBlendCacheBytes>=entry->capacity)sBlendCacheBytes-=entry->capacity;
        else sBlendCacheBytes=0;
        free(entry->mem);
    }
    entry->mem=NULL;
    entry->capacity=0;
    entry->valid=false;
    entry->source=NULL;
    entry->source_serial=0;
    entry->kind=0;
    entry->last_used_frame=0;
    entry->tex.tex.Mem=NULL;
    entry->tex.tex.Vram=0;
    entry->tex.tex.VramClut=0;
}

static bool ps2_blend_cache_make_room(Ps2BlendCacheEntry* keep,const struct Texture* source,size_t new_capacity){
    const size_t keep_capacity=(keep!=NULL)?keep->capacity:0;
    if(new_capacity>PS2_BLEND_CACHE_BUDGET)return false;
    while(sBlendCacheBytes-((sBlendCacheBytes>=keep_capacity)?keep_capacity:0)+new_capacity>
          PS2_BLEND_CACHE_BUDGET){
        Ps2BlendCacheEntry* victim=NULL;
        for(unsigned i=0;i<PS2_BLEND_CACHE_SLOTS;i++){
            Ps2BlendCacheEntry* e=&sBlendCache[i];
            if(e==keep || e->mem==NULL || &e->tex==source)continue;
            if(victim==NULL || e->stamp<victim->stamp)victim=e;
        }
        if(victim==NULL)return false;
        ps2_blend_cache_free_entry(victim);
    }
    return true;
}

enum {
    PS2_DERIVED_BLEND = 0,
    PS2_DERIVED_DECAL = 1,
};

static struct Texture* ps2_derived_texture(struct Texture* source, uint32_t base, uint32_t target, uint8_t kind){
    if(!ps2_texture_cpu_rgba_supported(source))return source;
    const uint32_t baseRgb=base&0x00ffffffU, targetRgb=target&0x00ffffffU;
    const size_t pixels=(size_t)source->tex.Width*(size_t)source->tex.Height;
    const size_t bytes=pixels*4U;
    Ps2BlendCacheEntry* entry=NULL; Ps2BlendCacheEntry* victim=NULL;
    for(unsigned i=0;i<PS2_BLEND_CACHE_SLOTS;i++){
        Ps2BlendCacheEntry* e=&sBlendCache[i];
        if(e->valid && e->source==source && e->source_serial==source->upload_serial &&
           e->base_rgb==baseRgb && e->target_rgb==targetRgb && e->kind==kind){
            e->tex.tex.Filter=source->tex.Filter; e->tex.clamp_s=source->clamp_s; e->tex.clamp_t=source->clamp_t;
            e->tex.cms=source->cms; e->tex.cmt=source->cmt; e->tex.masks=source->masks; e->tex.maskt=source->maskt;
            e->stamp=++sBlendStamp; e->last_used_frame=sPs2RenderFrameSerial; return &e->tex;
        }
        if(!e->valid && entry==NULL)entry=e;
        if(e->valid && (victim==NULL || e->stamp<victim->stamp))victim=e;
    }
    if(entry==NULL){
        entry=victim; if(entry==NULL)return source;

        if(entry->last_used_frame==sPs2RenderFrameSerial && gs_global && gs_global->Os_Queue->tag_size!=0){
            gsKit_queue_exec(gs_global); dmaKit_wait_fast(); gsKit_queue_reset(gs_global->Os_Queue);
        }
        if(entry->tex.tex.Mem!=NULL)ps2_release_texture_residency(&entry->tex.tex);
    }
    if(entry->capacity<bytes){
        const size_t new_capacity=ALIGN(bytes,128);
        if(!ps2_blend_cache_make_room(entry,source,new_capacity))return source;
        if(entry->mem){
            if(sBlendCacheBytes>=entry->capacity)sBlendCacheBytes-=entry->capacity;
            else sBlendCacheBytes=0;
            free(entry->mem);
        }
        entry->capacity=new_capacity;
        entry->mem=memalign(128,entry->capacity);
        if(!entry->mem){
            entry->capacity=0;
            entry->valid=false;
            entry->tex.tex.Mem=NULL;
            entry->tex.tex.Vram=0;
            entry->tex.tex.VramClut=0;
            return source;
        }
        sBlendCacheBytes+=entry->capacity;
    }
    uint32_t* out=(uint32_t*)entry->mem;
    const int br=(int)(baseRgb&0xff), bg=(int)((baseRgb>>8)&0xff), bb=(int)((baseRgb>>16)&0xff);
    const int tr=(int)(targetRgb&0xff), tg=(int)((targetRgb>>8)&0xff), tb=(int)((targetRgb>>16)&0xff);
    for(size_t i=0;i<pixels;i++){
        const uint32_t v=ps2_texture_cpu_rgba(source,i);
        const int ir=(int)(v&0xff), ig=(int)((v>>8)&0xff), ib=(int)((v>>16)&0xff);
        int r,g,b;
        if(kind==PS2_DERIVED_DECAL){

            int a=(int)((v>>24)&0xff);
            if(a>0x80)a=0x80;

            r=(br*(128-a)+ir*a+64)>>7;
            g=(bg*(128-a)+ig*a+64)>>7;
            b=(bb*(128-a)+ib*a+64)>>7;
        }else{

            r=br+((tr-br)*ir+127)/255;
            g=bg+((tg-bg)*ig+127)/255;
            b=bb+((tb-bb)*ib+127)/255;
        }
        if(r<0)r=0;else if(r>255)r=255;
        if(g<0)g=0;else if(g>255)g=255;
        if(b<0)b=0;else if(b>255)b=255;
        out[i]=(uint32_t)r|((uint32_t)g<<8)|((uint32_t)b<<16)|(v&0xff000000U);
    }
    SyncDCache(entry->mem,entry->mem+bytes);
    entry->tex=*source; entry->tex.tex.Mem=(u32*)entry->mem; entry->tex.tex.PSM=GS_PSM_CT32; entry->tex.tex.Vram=0; entry->tex.tex.VramClut=0;
    entry->tex.backing_size=entry->capacity; entry->source=source; entry->source_serial=source->upload_serial;
    entry->base_rgb=baseRgb; entry->target_rgb=targetRgb; entry->kind=kind; entry->stamp=++sBlendStamp;
    entry->last_used_frame=sPs2RenderFrameSerial; entry->valid=true;
    return &entry->tex;
}

static struct Texture* ps2_blend_texture(struct Texture* source, uint32_t base, uint32_t target){
    return ps2_derived_texture(source,base,target,PS2_DERIVED_BLEND);
}

static struct Texture* ps2_decal_texture(struct Texture* source, uint32_t base){
    return ps2_derived_texture(source,base,0,PS2_DERIVED_DECAL);
}

static inline bool ps2_is_deku_warp_portal_tint(const struct Texture* tex, uint32_t rgba){
    return cur_shader!=NULL && cur_shader->shader_id==0x11045045U && tex!=NULL &&
           tex->tex.PSM==GS_PSM_CT32 && tex->tex.Width==256 && tex->tex.Height==128 &&
           (rgba&0x00ffffffU)==0x00ff0000U;
}
static inline bool ps2_is_pause_name_tint(const struct Texture* tex, uint32_t rgba){
    return cur_shader!=NULL && cur_shader->shader_id==0x11045551U && tex!=NULL && do_blend &&
           tex->tex.Width==128 && tex->tex.Height==16 && (rgba&0x00ffffffU)==0x00281e14U;
}
#define DEKU_WATER_TEX_HASH 0x95512fd3U
#define DEKU_WATER_MIN_SHADE 157U
static inline bool ps2_is_deku_water_material(const struct Texture* tex){
    return cur_shader!=NULL && cur_shader->shader_id==0x03200045U && tex!=NULL && do_blend &&
           tex->tex.PSM==GS_PSM_CT32 && tex->tex.Width==32 && tex->tex.Height==32 &&
           tex->masks==5 && tex->maskt==5 && tex->content_hash==DEKU_WATER_TEX_HASH;
}
static inline uint32_t ps2_deku_water_color(uint32_t rgba){
    unsigned r=rgba&0xffU,g=(rgba>>8)&0xffU,b=(rgba>>16)&0xffU;
    if(r<DEKU_WATER_MIN_SHADE)r=DEKU_WATER_MIN_SHADE;
    if(g<DEKU_WATER_MIN_SHADE)g=DEKU_WATER_MIN_SHADE;
    if(b<DEKU_WATER_MIN_SHADE)b=DEKU_WATER_MIN_SHADE;
    return (rgba&0xff000000U)|r|(g<<8)|(b<<16);
}
static inline uint32_t ps2_target_rgb_preserve_alpha(uint32_t rgba){
    return (sEnvColor&0x00ffffffU)|(rgba&0xff000000U);
}

static void ps2_set_alpha_packet(uint64_t alpha, bool enable);

enum Ps2PassColorMode {
    PS2_PASS_ORIGINAL = 0,
    PS2_PASS_NEUTRAL_RGB,
    PS2_PASS_TARGET_RGB,
};

static inline uint32_t ps2_pass_color(uint32_t rgba, enum Ps2PassColorMode mode, uint32_t target){
    if(mode==PS2_PASS_NEUTRAL_RGB){
        return 0x00ffffffU|(rgba&0xff000000U);
    }
    if(mode==PS2_PASS_TARGET_RGB){
        return (target&0x00ffffffU)|(rgba&0xff000000U);
    }
    return rgba;
}

static void ps2_draw_fast_untextured_pass(const Ps2Fast* v,size_t tris){
    for(size_t i=0;i<tris;i++){
        const Ps2Fast* t=&v[i*3U];
        float x[3],y[3];int z[3];
        for(int k=0;k<3;k++){x[k]=t[k].x;y[k]=t[k].y;z[k]=(int)t[k].z;}
        gsKit_prim_triangle_gouraud_3d(gs_global,
            x[0],y[0],z[0],x[1],y[1],z[1],x[2],y[2],z[2],
            colorq_from_rgba(t[0].rgba,t[0].q,false),
            colorq_from_rgba(t[1].rgba,t[1].q,false),
            colorq_from_rgba(t[2].rgba,t[2].q,false));
    }
}

static void ps2_draw_fast_textured_pass(const Ps2Fast* v,size_t tris,struct Texture* tex,
                                        enum Ps2PassColorMode mode,uint32_t target){
    if(tex==NULL)return;
    ps2_bind_texture(&tex->tex);
    clamp_tex(tex,false,false);
    sLastTex0Reg=~0ULL;
    size_t done=0;
    while(done<tris){
        const size_t wanted=((tris-done)>GS_GIF_BLOCKSIZE)?GS_GIF_BLOCKSIZE:(tris-done);
        const size_t batch=tri_tex_batch_safe_count(wanted,false);
        u64* packet=tri_tex_batch_begin(tex,batch,false);
        for(size_t i=0;i<batch;i++){
            const Ps2Fast* t=&v[(done+i)*3U];
            uint32_t c[3];
            float x[3],y[3];int z[3];
            for(int k=0;k<3;k++){
                c[k]=ps2_pass_color(t[k].rgba,mode,target);
                x[k]=t[k].x; y[k]=t[k].y; z[k]=(int)t[k].z;
            }
            tri_tex_emit_reserved_known(&packet,tex,
                x[0],y[0],z[0],t[0].u,t[0].v,t[0].real_u,t[0].real_v,colorq_from_rgba(c[0],t[0].q,true),
                x[1],y[1],z[1],t[1].u,t[1].v,t[1].real_u,t[1].real_v,colorq_from_rgba(c[1],t[1].q,true),
                x[2],y[2],z[2],t[2].u,t[2].v,t[2].real_u,t[2].real_v,colorq_from_rgba(c[2],t[2].q,true),
                false,255,255,255);
        }
        done+=batch;
    }
}

static bool ps2_draw_ps2_blend_exact(const Ps2Fast* v,size_t tris,struct Texture* source){
    if(v==NULL || tris==0 || source==NULL || cur_shader==NULL)return false;

    const bool saved_tex_alpha=cur_shader->texture_alpha;
    const uint8_t saved_tfx=cur_shader->tfx;
    const bool saved_z_write=z_write;
    const bool saved_blend=do_blend;
    const u64 saved_alpha=gs_global->PrimAlpha;
    const bool saved_alpha_enable=gs_global->PrimAlphaEnable;
    struct Texture* alpha_mask=NULL;

    if(saved_tex_alpha){
        alpha_mask=ps2_blend_texture(source,0x00ffffffU,0x00ffffffU);
        if(alpha_mask==NULL || alpha_mask==source)return false;
    }

    if(saved_tex_alpha){
        cur_shader->tfx=0;
        cur_shader->texture_alpha=true;
        sLastTex0Reg=~0ULL;
        ps2_draw_fast_textured_pass(v,tris,alpha_mask,PS2_PASS_ORIGINAL,sEnvColor);
    }else{
        ps2_draw_fast_untextured_pass(v,tris);
    }

    if(saved_z_write)ps2_depth_mask(false);
    cur_shader->tfx=0;
    cur_shader->texture_alpha=saved_tex_alpha;
    sLastTex0Reg=~0ULL;

    if(saved_blend){

        ps2_set_alpha_packet(GS_SETREG_ALPHA(2,0,0,1,128),true);
        ps2_draw_fast_textured_pass(v,tris,source,PS2_PASS_ORIGINAL,sEnvColor);
        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,0,1,128),true);
        ps2_draw_fast_textured_pass(v,tris,source,PS2_PASS_TARGET_RGB,sEnvColor);
    }else{

        ps2_set_alpha_packet(GS_SETREG_ALPHA(1,0,2,2,128),true);
        ps2_draw_fast_textured_pass(v,tris,source,PS2_PASS_ORIGINAL,sEnvColor);
        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,2,1,128),true);
        ps2_draw_fast_textured_pass(v,tris,source,PS2_PASS_TARGET_RGB,sEnvColor);
    }

    ps2_set_alpha_packet(saved_alpha,saved_alpha_enable);
    if(saved_z_write)ps2_depth_mask(true);
    cur_shader->texture_alpha=saved_tex_alpha;
    cur_shader->tfx=saved_tfx;
    sLastTex0Reg=~0ULL;
    return true;
}

static bool ps2_draw_ps2_decal_rgba_exact(const Ps2Fast* v,size_t tris,struct Texture* source){
    if(v==NULL || tris==0 || source==NULL || cur_shader==NULL || cur_shader->alpha_test)return false;

    const bool saved_tex_alpha=cur_shader->texture_alpha;
    const uint8_t saved_tfx=cur_shader->tfx;
    const bool saved_z_write=z_write;
    const bool saved_alpha_test=cur_shader->alpha_test;
    const bool saved_blend=do_blend;
    const u64 saved_alpha=gs_global->PrimAlpha;
    const bool saved_alpha_enable=gs_global->PrimAlphaEnable;
    struct Texture* alpha_mask=NULL;

    if(saved_blend){
        alpha_mask=ps2_blend_texture(source,0x00ffffffU,0x00ffffffU);
        if(alpha_mask==NULL || alpha_mask==source)return false;
    }

    ps2_draw_fast_untextured_pass(v,tris);
    if(saved_z_write)ps2_depth_mask(false);
    cur_shader->alpha_test=false;
    update_test();

    if(saved_blend){

        cur_shader->tfx=0;
        cur_shader->texture_alpha=true;
        sLastTex0Reg=~0ULL;
        ps2_set_alpha_packet(GS_SETREG_ALPHA(2,0,0,1,128),true);
        ps2_draw_fast_textured_pass(v,tris,alpha_mask,PS2_PASS_ORIGINAL,0);
        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,0,1,128),true);
        ps2_draw_fast_textured_pass(v,tris,source,PS2_PASS_NEUTRAL_RGB,0);
    }else{

        cur_shader->tfx=1;
        cur_shader->texture_alpha=true;
        sLastTex0Reg=~0ULL;
        ps2_set_alpha_packet(BMODE_BLEND,true);
        ps2_draw_fast_textured_pass(v,tris,source,PS2_PASS_NEUTRAL_RGB,0);
    }

    ps2_set_alpha_packet(saved_alpha,saved_alpha_enable);
    if(saved_z_write)ps2_depth_mask(true);
    cur_shader->alpha_test=saved_alpha_test;
    cur_shader->texture_alpha=saved_tex_alpha;
    cur_shader->tfx=saved_tfx;
    update_test();
    sLastTex0Reg=~0ULL;
    return true;
}

static void ps2_draw_sprite_untextured_pass(float x1,float y1,int z1,float x2,float y2,int z2,uint32_t rgba){
    (void)z2;
    gsKit_prim_sprite(gs_global,x1,y1,x2,y2,z1,colorq_from_rgba(rgba,1.0f,false));
}

static void ps2_draw_sprite_textured_pass(struct Texture* tex,
                                          float x1,float y1,int z1,float u1,float v1,
                                          float x2,float y2,int z2,float u2,float v2,
                                          uint32_t rgba,enum Ps2PassColorMode mode,uint32_t target){
    if(tex==NULL)return;
    const uint32_t c=ps2_pass_color(rgba,mode,target);
    ps2_bind_texture(&tex->tex);
    const bool edge_s=(u1>=-1.0f && u1<=tex->tex.Width+1.0f && u2>=-1.0f && u2<=tex->tex.Width+1.0f);
    const bool edge_t=(v1>=-1.0f && v1<=tex->tex.Height+1.0f && v2>=-1.0f && v2<=tex->tex.Height+1.0f);
    clamp_tex(tex,edge_s,edge_t);
    sLastTex0Reg=~0ULL;
    sprite_tex(&tex->tex,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,colorq_from_rgba(c,1.0f,true));
}

static bool ps2_draw_ps2_blend_exact_2d(const VertexColor2D* q,struct Texture* source,
                                         float x1,float y1,int z1,float u1,float v1,
                                         float x2,float y2,int z2,float u2,float v2){
    if(q==NULL || source==NULL || cur_shader==NULL)return false;

    const bool saved_tex_alpha=cur_shader->texture_alpha;
    const uint8_t saved_tfx=cur_shader->tfx;
    const bool saved_z_write=z_write;
    const bool saved_blend=do_blend;
    const u64 saved_alpha=gs_global->PrimAlpha;
    const bool saved_alpha_enable=gs_global->PrimAlphaEnable;
    struct Texture* alpha_mask=NULL;

    if(saved_tex_alpha){
        alpha_mask=ps2_blend_texture(source,0x00ffffffU,0x00ffffffU);
        if(alpha_mask==NULL || alpha_mask==source)return false;
    }

    if(saved_tex_alpha){
        cur_shader->tfx=0;
        cur_shader->texture_alpha=true;
        ps2_draw_sprite_textured_pass(alpha_mask,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_ORIGINAL,sEnvColor);
    }else{
        ps2_draw_sprite_untextured_pass(x1,y1,z1,x2,y2,z2,q[0].rgba);
    }

    if(saved_z_write)ps2_depth_mask(false);
    cur_shader->tfx=0;
    cur_shader->texture_alpha=saved_tex_alpha;
    sLastTex0Reg=~0ULL;

    if(saved_blend){
        ps2_set_alpha_packet(GS_SETREG_ALPHA(2,0,0,1,128),true);
        ps2_draw_sprite_textured_pass(source,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_ORIGINAL,sEnvColor);
        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,0,1,128),true);
        ps2_draw_sprite_textured_pass(source,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_TARGET_RGB,sEnvColor);
    }else{
        ps2_set_alpha_packet(GS_SETREG_ALPHA(1,0,2,2,128),true);
        ps2_draw_sprite_textured_pass(source,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_ORIGINAL,sEnvColor);
        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,2,1,128),true);
        ps2_draw_sprite_textured_pass(source,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_TARGET_RGB,sEnvColor);
    }

    ps2_set_alpha_packet(saved_alpha,saved_alpha_enable);
    if(saved_z_write)ps2_depth_mask(true);
    cur_shader->texture_alpha=saved_tex_alpha;
    cur_shader->tfx=saved_tfx;
    sLastTex0Reg=~0ULL;
    return true;
}

static bool ps2_draw_ps2_decal_rgba_exact_2d(const VertexColor2D* q,struct Texture* source,
                                              float x1,float y1,int z1,float u1,float v1,
                                              float x2,float y2,int z2,float u2,float v2){
    if(q==NULL || source==NULL || cur_shader==NULL || cur_shader->alpha_test)return false;

    const bool saved_tex_alpha=cur_shader->texture_alpha;
    const uint8_t saved_tfx=cur_shader->tfx;
    const bool saved_z_write=z_write;
    const bool saved_blend=do_blend;
    const u64 saved_alpha=gs_global->PrimAlpha;
    const bool saved_alpha_enable=gs_global->PrimAlphaEnable;
    struct Texture* alpha_mask=NULL;

    if(saved_blend){
        alpha_mask=ps2_blend_texture(source,0x00ffffffU,0x00ffffffU);
        if(alpha_mask==NULL || alpha_mask==source)return false;
    }

    ps2_draw_sprite_untextured_pass(x1,y1,z1,x2,y2,z2,q[0].rgba);
    if(saved_z_write)ps2_depth_mask(false);

    if(saved_blend){
        cur_shader->tfx=0;
        cur_shader->texture_alpha=true;
        sLastTex0Reg=~0ULL;

        ps2_set_alpha_packet(GS_SETREG_ALPHA(2,0,0,1,128),true);
        ps2_draw_sprite_textured_pass(alpha_mask,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_ORIGINAL,0);
        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,0,1,128),true);
        ps2_draw_sprite_textured_pass(source,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_NEUTRAL_RGB,0);
    }else{
        cur_shader->tfx=1;
        cur_shader->texture_alpha=true;
        sLastTex0Reg=~0ULL;
        ps2_set_alpha_packet(BMODE_BLEND,true);
        ps2_draw_sprite_textured_pass(source,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,
                                      q[0].rgba,PS2_PASS_NEUTRAL_RGB,0);
    }

    ps2_set_alpha_packet(saved_alpha,saved_alpha_enable);
    if(saved_z_write)ps2_depth_mask(true);
    cur_shader->texture_alpha=saved_tex_alpha;
    cur_shader->tfx=saved_tfx;
    sLastTex0Reg=~0ULL;
    return true;
}

static void ps2_set_alpha_packet(uint64_t alpha, bool enable){
    gs_global->PrimAlpha=alpha;
    gs_global->PrimAlphaEnable=enable;
    if(alpha==sLastAlphaReg)return;
    sLastAlphaReg=alpha;
    u64*p=gsKit_heap_alloc(gs_global,1,16,GIF_AD);*p++=GIF_TAG_AD(1);*p++=GIF_AD;*p++=alpha;*p++=GS_ALPHA_1+gs_global->PrimContext;
}

static void ps2_draw(float*raw,size_t len,size_t tris){
    (void)len;if(!raw||!tris)return;
#if OOT_PS2_RAPI_PROFILE
    const uint32_t profStart=ps2_prof_count();
    int profPath=0;
#endif
    const bool restorePrerenderAfterDraw=!sPrerender3dStarted;
    if(!sPrerender3dStarted)sPrerender3dStarted=true;
    const bool depth_only=cur_shader&&cur_shader->depth_only;
    const bool saved_alpha_enable=gs_global->PrimAlphaEnable;
    if(depth_only){
        ps2_set_frame_write_mask(0xffffffffU);
        gs_global->PrimAlphaEnable=0;
    }
    update_test();
    Ps2Fast*v=(Ps2Fast*)raw;
    struct Texture *draw_tex=last_tex;
    bool textured=draw_tex!=NULL && cur_shader && (cur_shader->texture_used[0]||cur_shader->texture_used[1]);

    bool blend_single_pass=false;
    if(textured && !depth_only && cur_shader!=NULL &&
       cur_shader->texture_mode==PS2_TEXTURE_MODE_BLEND && sTextureBlendPrecolor &&
       !sTextureBlendReverse && !sDinFireTint && !sTwoTextureEnvPrimTint &&
       !sTwoTextureBlendActive && !cur_shader->cc.opt_texture_blend_shade &&
       (do_blend || cur_shader->texture_alpha) &&
       ps2_texture_cpu_rgba_supported(draw_tex) && cur_shader->shader_id!=0x11141551U){
        const uint32_t baseRgb=v[0].rgba&0x00ffffffU;
        bool uniform=true;
        for(size_t i=1;i<tris*3;i++){
            if((v[i].rgba&0x00ffffffU)!=baseRgb){uniform=false;break;}
        }
        if(uniform){

            struct Texture* derived=ps2_blend_texture(draw_tex,baseRgb,sEnvColor);
            if(derived!=draw_tex){
                draw_tex=derived;
                blend_single_pass=true;
            }
        }
    }
    if(textured && !depth_only && cur_shader!=NULL){
        if(cur_shader->texture_mode==PS2_TEXTURE_MODE_BLEND && sTextureBlendPrecolor && !blend_single_pass &&
           !sTextureBlendReverse && !sDinFireTint && !sTwoTextureEnvPrimTint &&
           ps2_draw_ps2_blend_exact(v,tris,draw_tex)){
#if OOT_PS2_RAPI_PROFILE
            profPath=1;
#endif
            goto draw_complete;
        }
        if(cur_shader->texture_mode==PS2_TEXTURE_MODE_DECAL && cur_shader->texture_alpha &&
           ps2_draw_ps2_decal_rgba_exact(v,tris,draw_tex)){
#if OOT_PS2_RAPI_PROFILE
            profPath=1;
#endif
            goto draw_complete;
        }
    }

    const bool reverse_tint=textured && sTextureBlendReverse && ps2_texture_cpu_rgba_supported(draw_tex);
    bool blend_tint=blend_single_pass;
    bool env_prim_tint=false;
    if(sTwoTextureEnvPrimTint && sTwoTextureBlendActive && textured &&
       ps2_texture_cpu_rgba_supported(draw_tex)){
        const uint32_t primRgb=v[0].rgba&0x00ffffffU;
        bool uniform=true;
        for(size_t i=1;i<tris*3;i++){
            if((v[i].rgba&0x00ffffffU)!=primRgb){uniform=false;break;}
        }
        if(uniform){
            struct Texture* tinted=ps2_blend_texture(draw_tex,sEnvColor,primRgb);
            env_prim_tint=(tinted!=draw_tex);
            draw_tex=tinted;
        }
    }
    if(reverse_tint){
        draw_tex=ps2_reverse_tint_texture(draw_tex,sEnvColor,v[0].rgba);
    }else if(sDinFireTint && textured && cur_shader && cur_shader->cc.opt_texture_blend &&
             !cur_shader->cc.opt_texture_blend_shade && ps2_texture_cpu_rgba_supported(draw_tex)){

        const uint32_t baseRgb=v[0].rgba&0x00ffffffU; bool uniform=true;
        for(size_t i=1;i<tris*3;i++) if((v[i].rgba&0x00ffffffU)!=baseRgb){uniform=false;break;}
        if(uniform){ struct Texture* tinted=ps2_blend_texture(draw_tex,baseRgb,sEnvColor); blend_tint=(tinted!=draw_tex); draw_tex=tinted; }
    }else if(!blend_single_pass && sTextureBlendPrecolor && !sTwoTextureBlendActive && textured && cur_shader &&
             cur_shader->cc.opt_texture_blend && (do_blend || cur_shader->texture_alpha) &&
             !cur_shader->cc.opt_texture_blend_shade && ps2_texture_cpu_rgba_supported(draw_tex) &&
             cur_shader->shader_id!=0x11141551U){

        const uint32_t baseRgb=v[0].rgba&0x00ffffffU; bool uniform=true;
        for(size_t i=1;i<tris*3;i++) if((v[i].rgba&0x00ffffffU)!=baseRgb){uniform=false;break;}
        if(uniform){ struct Texture* tinted=ps2_blend_texture(draw_tex,baseRgb,sEnvColor); blend_tint=(tinted!=draw_tex); draw_tex=tinted; }
    }
    const bool deku_warp_tint=textured && !reverse_tint && !blend_tint && ps2_is_deku_warp_portal_tint(draw_tex,v[0].rgba);
    const bool pause_name_tint=textured && !reverse_tint && !blend_tint && ps2_is_pause_name_tint(draw_tex,v[0].rgba);
    const bool deku_water=textured && !reverse_tint && !blend_tint && ps2_is_deku_water_material(draw_tex);
    if(textured){
        ps2_bind_texture(&draw_tex->tex);

        clamp_tex(draw_tex,false,false);
    }

    const bool has_fog = cur_shader != NULL && cur_shader->cc.opt_fog;

    const bool prefer_native_fog = has_fog && !depth_only && !do_blend && cur_shader != NULL &&
                                   !cur_shader->texture_alpha && !sTwoTextureBlendActive &&
                                   !sTextureBlendPrecolor && !reverse_tint && !blend_tint &&
                                   !env_prim_tint && !deku_warp_tint && !pause_name_tint && !deku_water;
    const bool exact_tint=textured && sTextureBlendPrecolor && cur_shader &&
                          cur_shader->cc.opt_texture_blend && !reverse_tint && !blend_tint &&
                          !cur_shader->cc.opt_texture_blend_shade && !do_blend && !has_fog;
    sPs2SkipNextFogPass = prefer_native_fog;
    if(prefer_native_fog)set_fog_color(v[0].fog_color);
    if(exact_tint){

        Ps2Fast*basev=v;

        for(size_t i=0;i<tris;i++){
            Ps2Fast*t=&basev[i*3];
            gsKit_prim_triangle_gouraud_3d(gs_global,t[0].x,t[0].y,(int)t[0].z,t[1].x,t[1].y,(int)t[1].z,t[2].x,t[2].y,(int)t[2].z,
                colorq_from_rgba(t[0].rgba,t[0].q,false),colorq_from_rgba(t[1].rgba,t[1].q,false),colorq_from_rgba(t[2].rgba,t[2].q,false));
        }
        const u64 saved_alpha=gs_global->PrimAlpha;
        const bool saved_alpha_enable2=gs_global->PrimAlphaEnable;

        ps2_set_alpha_packet(GS_SETREG_ALPHA(1,0,2,2,128),true);
        for(size_t i=0;i<tris;i++){
            Ps2Fast*t=&basev[i*3];
            tri_tex(draw_tex,t[0].x,t[0].y,(int)t[0].z,t[0].u,t[0].v,colorq_from_rgba(t[0].rgba,t[0].q,true),t[1].x,t[1].y,(int)t[1].z,t[1].u,t[1].v,colorq_from_rgba(t[1].rgba,t[1].q,true),t[2].x,t[2].y,(int)t[2].z,t[2].u,t[2].v,colorq_from_rgba(t[2].rgba,t[2].q,true),false,255,255,255);
        }

        ps2_set_alpha_packet(GS_SETREG_ALPHA(0,2,2,1,128),true);
        for(size_t i=0;i<tris;i++){
            Ps2Fast*t=&basev[i*3];
            const uint32_t c0=(sEnvColor&0x00ffffffU)|(t[0].rgba&0xff000000U);
            const uint32_t c1=(sEnvColor&0x00ffffffU)|(t[1].rgba&0xff000000U);
            const uint32_t c2=(sEnvColor&0x00ffffffU)|(t[2].rgba&0xff000000U);
            tri_tex(draw_tex,t[0].x,t[0].y,(int)t[0].z,t[0].u,t[0].v,colorq_from_rgba(c0,t[0].q,true),t[1].x,t[1].y,(int)t[1].z,t[1].u,t[1].v,colorq_from_rgba(c1,t[1].q,true),t[2].x,t[2].y,(int)t[2].z,t[2].u,t[2].v,colorq_from_rgba(c2,t[2].q,true),false,255,255,255);
        }
        ps2_set_alpha_packet(saved_alpha,saved_alpha_enable2);
    }else if(textured){

        size_t done=0;
        while(done<tris){
            const size_t wanted=((tris-done)>GS_GIF_BLOCKSIZE)?GS_GIF_BLOCKSIZE:(tris-done);
            const size_t batch=tri_tex_batch_safe_count(wanted,prefer_native_fog);
            u64* packet=tri_tex_batch_begin(draw_tex,batch,prefer_native_fog);
            for(size_t i=0;i<batch;i++){
                Ps2Fast* t=&v[(done+i)*3U];
                float x[3],y[3];int z[3];
                for(int k=0;k<3;k++){x[k]=t[k].x;y[k]=t[k].y;z[k]=(int)t[k].z;}
                const uint8_t f0=(uint8_t)t[0].fog,f1=(uint8_t)t[1].fog,f2=(uint8_t)t[2].fog;
                uint32_t c0,c1,c2;
                const bool neutralTint=reverse_tint||blend_tint||env_prim_tint;
                const bool targetTint=deku_warp_tint||pause_name_tint;
                if(__builtin_expect(!neutralTint && !targetTint && !deku_water,1)){
                    c0=t[0].rgba; c1=t[1].rgba; c2=t[2].rgba;
                }else if(neutralTint){
                    c0=ps2_reverse_neutral_color(t[0].rgba); c1=ps2_reverse_neutral_color(t[1].rgba); c2=ps2_reverse_neutral_color(t[2].rgba);
                }else if(targetTint){
                    c0=ps2_target_rgb_preserve_alpha(t[0].rgba); c1=ps2_target_rgb_preserve_alpha(t[1].rgba); c2=ps2_target_rgb_preserve_alpha(t[2].rgba);
                }else{
                    c0=ps2_deku_water_color(t[0].rgba); c1=ps2_deku_water_color(t[1].rgba); c2=ps2_deku_water_color(t[2].rgba);
                }
                tri_tex_emit_reserved_known(&packet,draw_tex,
                    x[0],y[0],z[0],t[0].u,t[0].v,t[0].real_u,t[0].real_v,colorq_from_rgba(c0,t[0].q,true),
                    x[1],y[1],z[1],t[1].u,t[1].v,t[1].real_u,t[1].real_v,colorq_from_rgba(c1,t[1].q,true),
                    x[2],y[2],z[2],t[2].u,t[2].v,t[2].real_u,t[2].real_v,colorq_from_rgba(c2,t[2].q,true),
                    prefer_native_fog,f0,f1,f2);
            }
            done+=batch;
        }
    }else{
        const bool fog=prefer_native_fog;
        size_t done=0;
        while(done<tris){
            const size_t wanted=((tris-done)>GS_GIF_BLOCKSIZE)?GS_GIF_BLOCKSIZE:(tris-done);
            const size_t batch=tri_col_batch_safe_count(wanted,fog);
            u64*packet=tri_col_batch_begin(batch,fog);
            for(size_t i=0;i<batch;i++){
                Ps2Fast*t=&v[(done+i)*3U];
                float x[3],y[3];int z[3];
                for(int k=0;k<3;k++){x[k]=t[k].x;y[k]=t[k].y;z[k]=(int)t[k].z;}
                tri_col_emit_reserved(&packet,
                    x[0],y[0],z[0],colorq_from_rgba(t[0].rgba,t[0].q,false),
                    x[1],y[1],z[1],colorq_from_rgba(t[1].rgba,t[1].q,false),
                    x[2],y[2],z[2],colorq_from_rgba(t[2].rgba,t[2].q,false),fog,
                    (uint8_t)t[0].fog,(uint8_t)t[1].fog,(uint8_t)t[2].fog);
            }
            done+=batch;
        }
    }
draw_complete:
    if(depth_only){
        gs_global->PrimAlphaEnable=saved_alpha_enable;
        if(!sPrerenderDepthOnlyPass)ps2_set_frame_write_mask(0);
    }
    if(restorePrerenderAfterDraw)ps2_restore_prerender_fallback();
#if OOT_PS2_RAPI_PROFILE
    {
        const uint32_t d=ps2_prof_count()-profStart;
        sProfDrawCycles+=d; sProfDrawCalls++;
        sProfFrameDrawCycles+=d; sProfFrameDrawCalls++;
        if(profPath){ sProfExactCycles+=d; sProfExactCalls++; sProfFrameExactCycles+=d; sProfFrameExactCalls++; }
        else if(textured){ sProfTexCycles+=d; sProfFrameTexCycles+=d; }
        else { sProfUntexCycles+=d; sProfFrameUntexCycles+=d; }
    }
#endif
}
static void ps2_restore_fog_pass_state(bool emit_state){
    if(!sFogPass.active)return;
    cur_shader=sFogPass.saved_shader;
    if(cur_shader){
        cur_shader->alpha_test=sFogPass.saved_alpha_test;
        cur_shader->texture_alpha=sFogPass.saved_texture_alpha;
        cur_shader->tfx=sFogPass.saved_tfx;
    }
    if(emit_state){
        ps2_depth_mask(sFogPass.saved_z_write);
        update_test();
        set_blend(sFogPass.saved_blend);
    }else{

        z_write=sFogPass.saved_z_write;
        do_blend=sFogPass.saved_blend;
        if(gs_global){
            gs_global->PrimAlphaEnable=do_blend;
            gs_global->PrimAlpha=do_blend?BMODE_BLEND:0;
        }
    }
    sLastTex0Reg=~0ULL;
    sFogPass.active=false;
}

static void ps2_draw_fog(float*raw,size_t len,size_t tris,bool texture_alpha,bool restore){
    if(sPs2SkipNextFogPass){
        sPs2SkipNextFogPass=false;
        return;
    }
    if(raw==NULL || tris==0){
        if(restore)ps2_restore_fog_pass_state(true);
        return;
    }
#if OOT_PS2_RAPI_PROFILE
    const uint32_t profFogStart=ps2_prof_count();
#endif

    if(!sFogPass.active){
        sFogPass.active=true;
        sFogPass.saved_z_write=z_write;
        sFogPass.saved_blend=do_blend;
        sFogPass.saved_shader=cur_shader;
        sFogPass.saved_alpha_test=cur_shader?cur_shader->alpha_test:false;
        sFogPass.saved_texture_alpha=cur_shader?cur_shader->texture_alpha:false;
        sFogPass.saved_tfx=cur_shader?cur_shader->tfx:0;

        if(cur_shader)cur_shader->alpha_test=false;

        ps2_depth_mask(false);
        update_test();
        set_blend(true);
    }

    bool malformed=false;
    if(texture_alpha){
        const size_t expected=tris*3U*sizeof(Ps2FogTextured);
        if(len<expected){malformed=true;goto fog_restore;}
        Ps2FogTextured* v=(Ps2FogTextured*)raw;
        struct Texture* source=last_tex;
        if(source!=NULL){

            struct Texture* mask=ps2_blend_texture(source,0x00ffffffU,0x00ffffffU);
            if(cur_shader){cur_shader->texture_alpha=true;cur_shader->tfx=0;}
            ps2_bind_texture(&mask->tex);
            clamp_tex(mask,false,false);
            sLastTex0Reg=~0ULL;

            for(size_t i=0;i<tris;i++){
                Ps2FogTextured* q=&v[i*3U];
                tri_tex(mask,
                    q[0].x,q[0].y,(int)q[0].z,q[0].u,q[0].v,colorq_from_rgba(q[0].rgba,q[0].q,true),
                    q[1].x,q[1].y,(int)q[1].z,q[1].u,q[1].v,colorq_from_rgba(q[1].rgba,q[1].q,true),
                    q[2].x,q[2].y,(int)q[2].z,q[2].u,q[2].v,colorq_from_rgba(q[2].rgba,q[2].q,true),
                    false,255,255,255);
            }
        }
    }else{
        const size_t expected=tris*3U*sizeof(Ps2FogColor);
        if(len<expected){malformed=true;goto fog_restore;}
        Ps2FogColor* v=(Ps2FogColor*)raw;
        for(size_t i=0;i<tris;i++){
            Ps2FogColor* q=&v[i*3U];
            gsKit_prim_triangle_gouraud_3d(gs_global,
                q[0].x,q[0].y,(int)q[0].z,q[1].x,q[1].y,(int)q[1].z,q[2].x,q[2].y,(int)q[2].z,
                colorq_from_rgba(q[0].rgba,1.0f,false),
                colorq_from_rgba(q[1].rgba,1.0f,false),
                colorq_from_rgba(q[2].rgba,1.0f,false));
        }
    }

fog_restore:

    if(malformed || restore)ps2_restore_fog_pass_state(true);
#if OOT_PS2_RAPI_PROFILE
    {
        const uint32_t d = (uint32_t)(ps2_prof_count()-profFogStart);
        sProfFogCycles += d; sProfFogCalls++;
        sProfFrameFogCycles += d; sProfFrameFogCalls++;
    }
#endif
}

void gfx_ps2_draw_triangles_2d(float *raw,size_t unused,size_t count){
    (void)unused;(void)count;
    VertexColor2D*q=(VertexColor2D*)raw;
    if(!q)return;
    const bool depth_only=cur_shader&&cur_shader->depth_only;
    const bool saved_alpha_enable=gs_global->PrimAlphaEnable;
    if(depth_only){
        ps2_set_frame_write_mask(0xffffffffU);
        gs_global->PrimAlphaEnable=0;
    }
    update_test();
    float x1=q[0].x,y1=q[0].y,x2=q[1].x,y2=q[1].y;
    float u1=q[0].u,v1=q[0].v,u2=q[1].u,v2=q[1].v;
    const int z1=(int)q[0].z,z2=(int)q[1].z;
    struct Texture *draw_tex=last_tex;
    const bool textured=draw_tex!=NULL && cur_shader && (cur_shader->texture_used[0]||cur_shader->texture_used[1]);

    if(textured && cur_shader && cur_shader->shader_id==0x41045200U &&
       draw_tex->tex.Width==8 && draw_tex->tex.Height==16){
        const float glyph_h=16.0f*((float)gs_global->Height/240.0f);
        if((y2-y1) > glyph_h*2.0f && (v2-v1) > 32.0f){
            y2=y1+glyph_h;
            v2=v1+16.0f;
        }
    }

    const bool prerender_candidate=textured && draw_tex->tex.Width==512 && draw_tex->tex.Height==256 &&
                                   draw_tex->tex.PSM==GS_PSM_CT16 && sPrerenderRoomActive &&
                                   (x2-x1)>=300.0f && (y2-y1)>=200.0f;

    bool blend_single_pass_2d=false;
    bool decal_single_pass_2d=false;
    const uint8_t saved_2d_tfx=cur_shader!=NULL?cur_shader->tfx:0;
    const bool saved_2d_texture_alpha=cur_shader!=NULL?cur_shader->texture_alpha:false;
    if(textured && !depth_only && !prerender_candidate && cur_shader!=NULL &&
       cur_shader->texture_mode==PS2_TEXTURE_MODE_BLEND && sTextureBlendPrecolor &&
       !sTextureBlendReverse && !sDinFireTint && !sTwoTextureEnvPrimTint &&
       !sTwoTextureBlendActive && !cur_shader->cc.opt_texture_blend_shade &&
       ps2_texture_cpu_rgba_supported(draw_tex)){
        const uint32_t baseRgb=q[0].rgba&0x00ffffffU;
        if(baseRgb==(q[1].rgba&0x00ffffffU)){
            struct Texture* derived=ps2_blend_texture(draw_tex,baseRgb,sEnvColor);
            if(derived!=draw_tex){draw_tex=derived;blend_single_pass_2d=true;}
        }
    }

    if(textured && !depth_only && !prerender_candidate && cur_shader!=NULL &&
       cur_shader->texture_mode==PS2_TEXTURE_MODE_DECAL && cur_shader->texture_alpha &&
       !cur_shader->alpha_test && ps2_texture_cpu_rgba_supported(draw_tex)){
        const uint32_t baseRgb=q[0].rgba&0x00ffffffU;
        if(baseRgb==(q[1].rgba&0x00ffffffU)){
            struct Texture* derived=ps2_decal_texture(draw_tex,baseRgb);
            if(derived!=draw_tex){
                draw_tex=derived;
                decal_single_pass_2d=true;
                cur_shader->tfx=1;
                cur_shader->texture_alpha=false;
                sLastTex0Reg=~0ULL;
            }
        }
    }
    if(textured && !depth_only && !prerender_candidate && cur_shader!=NULL){
        if(cur_shader->texture_mode==PS2_TEXTURE_MODE_BLEND && sTextureBlendPrecolor && !blend_single_pass_2d &&
           !sTextureBlendReverse && !sDinFireTint && !sTwoTextureEnvPrimTint &&
           ps2_draw_ps2_blend_exact_2d(q,draw_tex,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2)){
            goto cleanup;
        }
        if(cur_shader->texture_mode==PS2_TEXTURE_MODE_DECAL && cur_shader->texture_alpha && !decal_single_pass_2d &&
           ps2_draw_ps2_decal_rgba_exact_2d(q,draw_tex,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2)){
            goto cleanup;
        }
    }

    const bool reverse_tint=textured && sTextureBlendReverse && ps2_texture_cpu_rgba_supported(draw_tex);
    bool blend_tint=blend_single_pass_2d;
    bool env_prim_tint=false;
    if(sTwoTextureEnvPrimTint && sTwoTextureBlendActive && textured &&
       ps2_texture_cpu_rgba_supported(draw_tex)){
        const uint32_t primRgb=q[0].rgba&0x00ffffffU;
        const uint32_t primRgb1=q[1].rgba&0x00ffffffU;
        if(primRgb==primRgb1){
            struct Texture* tinted=ps2_blend_texture(draw_tex,sEnvColor,primRgb);
            env_prim_tint=(tinted!=draw_tex);
            draw_tex=tinted;
        }
    }
    if(reverse_tint){
        draw_tex=ps2_reverse_tint_texture(draw_tex,sEnvColor,q[0].rgba);
    }else if(sTextureBlendPrecolor && !blend_single_pass_2d && !sTwoTextureBlendActive && textured &&
             cur_shader && cur_shader->cc.opt_texture_blend && !cur_shader->cc.opt_texture_blend_shade &&
             ps2_texture_cpu_rgba_supported(draw_tex)){
        const uint32_t base0=q[0].rgba&0x00ffffffU, base1=q[1].rgba&0x00ffffffU;
        if(base0==base1){ struct Texture* tinted=ps2_blend_texture(draw_tex,base0,sEnvColor); blend_tint=(tinted!=draw_tex); draw_tex=tinted; }
    }
    const uint64_t col=colorq_from_rgba((reverse_tint||blend_tint||env_prim_tint)?ps2_reverse_neutral_color(q[0].rgba):q[0].rgba,1.0f,textured);
    if(textured){
        ps2_bind_texture(&draw_tex->tex);

        const bool edge_s=(u1>=-1 && u1<=draw_tex->tex.Width+1 && u2>=-1 && u2<=draw_tex->tex.Width+1);
        const bool edge_t=(v1>=-1 && v1<=draw_tex->tex.Height+1 && v2>=-1 && v2<=draw_tex->tex.Height+1);
        clamp_tex(draw_tex,edge_s,edge_t);
        if(draw_tex->tex.Width==512 && draw_tex->tex.Height==256 && draw_tex->tex.PSM==GS_PSM_CT16){
            if(sPrerenderRoomActive && (x2-x1)>=300.0f && (y2-y1)>=200.0f){
                sPrerenderFallback.texture=draw_tex;
                sPrerenderFallback.x1=x1;sPrerenderFallback.y1=y1;sPrerenderFallback.x2=x2;sPrerenderFallback.y2=y2;
                sPrerenderFallback.u1=u1;sPrerenderFallback.v1=v1;sPrerenderFallback.u2=u2;sPrerenderFallback.v2=v2;
                sPrerenderFallback.z1=z1;sPrerenderFallback.z2=z2;sPrerenderFallback.color=col;
                sPrerenderFallback.valid=true;sPrerenderFallback.seenThisFrame=true;sPrerenderFallback.missedFrames=0;
            }
            gsKit_prim_sprite_texture_3d(gs_global,&draw_tex->tex,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,col);
        }else
            sprite_tex(&draw_tex->tex,x1,y1,z1,u1,v1,x2,y2,z2,u2,v2,col);
    }else{
        gsKit_prim_sprite(gs_global,x1,y1,x2,y2,z1,col);
    }
cleanup:
    if(decal_single_pass_2d && cur_shader!=NULL){
        cur_shader->tfx=saved_2d_tfx;
        cur_shader->texture_alpha=saved_2d_texture_alpha;
        sLastTex0Reg=~0ULL;
    }
    if(depth_only){
        gs_global->PrimAlphaEnable=saved_alpha_enable;
        if(!sPrerenderDepthOnlyPass)ps2_set_frame_write_mask(0);
    }
}

static void ps2_init(void) {
    u64* p;

    sLastTexFilter=-1;
    sLastTestReg=~0ULL;
    sLastClampReg=~0ULL;
    sLastScissorReg=~0ULL;
    sLastZbufReg=~0ULL;
    sLastAlphaReg=~0ULL;

    OotPs2Trace_Log("rapi mode switch begin");
    gsKit_mode_switch(gs_global, GS_ONESHOT);
    OotPs2Trace_Log("rapi mode switch done");
    gs_global->Test->ZTST = 2;
    OotPs2Trace_Log("rapi heap alloc begin");
    p = gsKit_heap_alloc(gs_global, 1, 16, GIF_AD);
    OotPs2Trace_Log("rapi heap alloc done ptr=%p", p);
    *p++ = GIF_TAG_AD(1);
    *p++ = GIF_AD;
    *p++ = GS_SETREG_TEXA(0, 0, 0x80);
    *p++ = GS_TEXA;
    OotPs2Trace_Log("rapi initial queue exec begin");
    gsKit_queue_exec(gs_global);
    OotPs2Trace_Log("rapi initial queue exec done");
    OotPs2Trace_Log("rapi initial queue reset begin");
    gsKit_queue_reset(gs_global->Os_Queue);
    OotPs2Trace_Log("rapi initial queue reset done");
    OotPs2Trace_Log("rapi texture cache alloc begin size=%u", (unsigned)TEXCACHE_SIZE);
    tex_cache = memalign(128, TEXCACHE_SIZE);
    OotPs2Trace_Log("rapi texture cache alloc done ptr=%p", tex_cache);
    if (!tex_cache) {
        printf("oot-ps2: texture cache alloc failed\n");
        abort();
    }
    tex_ptr = tex_cache;
    tex_end = tex_cache + TEXCACHE_SIZE;
    OotPs2Trace_Log("rapi viewport begin");
    ps2_viewport(0, 0, gs_global->Width, gs_global->Height);
    OotPs2Trace_Log("rapi viewport done");
    OotPs2Trace_Log("rapi scissor begin");
    ps2_scissor(0, 0, gs_global->Width, gs_global->Height);
    OotPs2Trace_Log("rapi scissor done");
}
static void ps2_restore_prerender_fallback(void){
    if(!sPrerenderRoomActive){
        sPrerenderFallback.valid=false;
        sPrerenderFallback.texture=NULL;
        return;
    }
    if(!sPrerenderFallback.valid||sPrerenderFallback.seenThisFrame)return;
    struct Texture*t=sPrerenderFallback.texture;
    if(!t||t->tex.Vram==0){sPrerenderFallback.valid=false;return;}
    const bool old_test=z_test,old_write=z_write,old_blend=do_blend;
    const struct Clip old_clip=r_clip;
    z_test=false;ps2_depth_mask(false);set_blend(false);update_test();
    set_scissor_raw(0,0,gs_global->Width-1,gs_global->Height-1);
    ps2_bind_texture(&t->tex);clamp_tex(t,true,true);
    gsKit_prim_sprite_texture_3d(gs_global,&t->tex,
        sPrerenderFallback.x1,sPrerenderFallback.y1,sPrerenderFallback.z1,
        sPrerenderFallback.u1,sPrerenderFallback.v1,
        sPrerenderFallback.x2,sPrerenderFallback.y2,sPrerenderFallback.z2,
        sPrerenderFallback.u2,sPrerenderFallback.v2,sPrerenderFallback.color);
    sLastBoundGsTex=NULL;sLastTex0Reg=~0ULL;sLastTexFilter=-1;
    set_scissor_raw(old_clip.x0,old_clip.y0,old_clip.x1,old_clip.y1);
    z_test=old_test;ps2_depth_mask(old_write);update_test();set_blend(old_blend);
}
static void ps2_resize(void){}
static void ps2_start(void){
#if OOT_PS2_RAPI_PROFILE
    sProfFrameDrawCycles=sProfFrameTexCycles=sProfFrameUntexCycles=sProfFrameExactCycles=sProfFrameFogCycles=0;
    sProfFrameUploadCycles=sProfFrameBindCycles=0;
    sProfFrameDrawCalls=sProfFrameExactCalls=sProfFrameFogCalls=sProfFrameUploadCalls=sProfFrameBindCalls=sProfFrameBindTransfers=0;
#endif

    ps2_restore_fog_pass_state(false);

    sTextureBlendReverse=false;
    sTextureBlendPrecolor=false;
    sDinFireTint=false;
    sTwoTextureBlendActive=false;
    sTwoTextureEnvPrimTint=false;
    sPs2RenderFrameSerial++;
    if(sPs2RenderFrameSerial==0) sPs2RenderFrameSerial=1;
    if(!sPrerenderRoomActive){
        sPrerenderFallback.valid=false;
        sPrerenderFallback.texture=NULL;
    }
    sPrerenderFallback.seenThisFrame=false;
    sPrerender3dStarted=false;
    sLastTexFilter=-1;
    sLastTestReg=~0ULL;
    sLastClampReg=~0ULL;
    sLastScissorReg=~0ULL;
    sLastZbufReg=~0ULL;
    sLastAlphaReg=~0ULL;
    sLastTex0Reg=~0ULL;

    const bool old_write=z_write, old_test=z_test, old_blend=do_blend;
    const struct Clip old_clip=r_clip;
    set_blend(false);
    ps2_depth_mask(true);
    z_test=false; update_test();
    set_scissor_raw(0,0,gs_global->Width-1,gs_global->Height-1);
    const bool pause_bg=gfx_ps2_pause_background_active();
    const u64 black=GS_SETREG_RGBAQ(0,0,0,0x80,0);
    if(pause_bg)ps2_set_frame_write_mask(0xffffffffU);

    gsKit_prim_sprite(gs_global,0,0,gs_global->Width,gs_global->Height,0,black);
    if(pause_bg){
        ps2_set_frame_write_mask(0);
        gfx_ps2_restore_pause_background();
    }
    set_scissor_raw(old_clip.x0,old_clip.y0,old_clip.x1,old_clip.y1);
    z_test=old_test;
    ps2_depth_mask(old_write);
    update_test();
    set_blend(old_blend);
}
static const uint8_t* ps2_fps_glyph(char c){
    static const uint8_t f[7]={31,16,16,30,16,16,16};
    static const uint8_t p[7]={30,17,17,30,16,16,16};
    static const uint8_t sg[7]={15,16,16,14,1,1,30};
    static const uint8_t colon[7]={0,4,4,0,4,4,0};
    static const uint8_t dot[7]={0,0,0,0,0,6,6};
    static const uint8_t sp[7]={0,0,0,0,0,0,0};
    static const uint8_t num[10][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
        {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
    };
    if(c>='0'&&c<='9')return num[c-'0'];
    if(c=='F')return f;
    if(c=='P')return p;
    if(c=='S')return sg;
    if(c==':')return colon;
    if(c=='.')return dot;
    return sp;
}
static void ps2_fps_char(float x,float y,char c,u64 color){
    const uint8_t*rows=ps2_fps_glyph(c);
    const float scale=2.0f;
    for(int row=0;row<7;row++){
        uint8_t bits=rows[row];
        int col=0;
        while(col<5){
            while(col<5 && !(bits&(1u<<(4-col))))col++;
            if(col>=5)break;
            int start=col;
            while(col<5 && (bits&(1u<<(4-col))))col++;
            gsKit_prim_sprite(gs_global,x+start*scale,y+row*scale,x+col*scale,y+(row+1)*scale,0,color);
        }
    }
}
static void ps2_fps_overlay(void){
    static u64 window_start;
    static u32 frames;
    static char text[16]="FPS: 0.0";
    u64 now=OotPs2Time_GetUsec();
    if(window_start==0)window_start=now;
    frames++;
    if(now-window_start>=500000ULL){
        const u64 elapsed=now-window_start;
        const u32 fps10=(u32)(((u64)frames*10000000ULL+elapsed/2)/elapsed);
        const u32 whole=fps10/10U;
        const u32 frac=fps10%10U;
        char*q=text;
        *q++='F';*q++='P';*q++='S';*q++=':';*q++=' ';
        if(whole>=100U)*q++=(char)('0'+(whole/100U)%10U);
        if(whole>=10U)*q++=(char)('0'+(whole/10U)%10U);
        *q++=(char)('0'+whole%10U);
        *q++='.';*q++=(char)('0'+frac);*q='\0';
        frames=0;
        window_start=now;
    }
    const bool old_test=z_test,old_blend=do_blend;
    const struct Clip old_clip=r_clip;
    z_test=false;
    set_blend(false);
    update_test();
    set_scissor_raw(0,0,gs_global->Width-1,gs_global->Height-1);
    const u64 white=GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0);
    float x=5.0f;
    for(const char*q=text;*q;q++,x+=12.0f)ps2_fps_char(x,5.0f,*q,white);
    set_scissor_raw(old_clip.x0,old_clip.y0,old_clip.x1,old_clip.y1);
    z_test=old_test;
    update_test();
    set_blend(old_blend);
}
static const uint8_t* ps2_build_glyph(char c){
    static const uint8_t lower_v[7]={0,0,17,17,17,10,4};
    static const uint8_t lower_u[7]={0,0,17,17,17,19,13};
    static const uint8_t lower_i[7]={4,0,12,4,4,4,14};
    static const uint8_t lower_l[7]={12,4,4,4,4,4,14};
    static const uint8_t lower_d[7]={1,1,15,17,17,19,13};
    if(c=='v')return lower_v;
    if(c=='u')return lower_u;
    if(c=='i')return lower_i;
    if(c=='l')return lower_l;
    if(c=='d')return lower_d;
    return ps2_fps_glyph(c);
}
static void ps2_build_char(float x,float y,char c,float scale,u64 color){
    const uint8_t*rows;
    if(c=='B'){
        static const uint8_t upper_b[7]={30,17,17,30,17,17,30};
        rows=upper_b;
    }else{
        rows=ps2_build_glyph(c);
    }
    for(int row=0;row<7;row++){
        const uint8_t bits=rows[row];
        for(int col=0;col<5;col++){
            if(bits&(1u<<(4-col)))
                gsKit_prim_sprite(gs_global,x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale,0,color);
        }
    }
}
static void ps2_build_overlay(void){
    static const char text[]="v 0.03.5 Build";
    const float scale=2.5f;
    const float advance=6.0f*scale;
    const float width=(float)(sizeof(text)-1)*advance;
    const float x0=(float)gs_global->Width-width-8.0f;
    const float y=(float)gs_global->Height-(7.0f*scale)-8.0f;
    const bool old_test=z_test,old_blend=do_blend;
    const struct Clip old_clip=r_clip;
    z_test=false;
    set_blend(false);
    update_test();
    set_scissor_raw(0,0,gs_global->Width-1,gs_global->Height-1);
    const u64 white=GS_SETREG_RGBAQ(0x60,0x60,0x60,0x80,0);
    float x=x0;
    for(const char*q=text;*q;q++,x+=advance)ps2_build_char(x,y,*q,scale,white);
    set_scissor_raw(old_clip.x0,old_clip.y0,old_clip.x1,old_clip.y1);
    z_test=old_test;
    update_test();
    set_blend(old_blend);
}
static void ps2_menu_rect(float x0,float y0,float x1,float y1,u64 color){
    gsKit_prim_sprite(gs_global,x0,y0,x1,y1,0,color);
}

#define PS2_HOME_MENU_WIDTH 480.0f
#define PS2_HOME_MENU_HEIGHT 272.0f
#define PS2_HOME_HIGHLIGHT_R 38
#define PS2_HOME_HIGHLIGHT_G 92
#define PS2_HOME_HIGHLIGHT_B 78

#include "oot_ps2_home_font.inc"

static float ps2_home_scale(void){
    const float sx=(float)gs_global->Width/PS2_HOME_MENU_WIDTH;
    const float sy=(float)gs_global->Height/PS2_HOME_MENU_HEIGHT;
    const float fit=sx<sy?sx:sy;

    return fit*0.86f;
}
static float ps2_home_x(float x){
    const float sc=ps2_home_scale();
    return ((float)gs_global->Width-PS2_HOME_MENU_WIDTH*sc)*0.5f+x*sc;
}
static float ps2_home_y(float y){
    const float sc=ps2_home_scale();
    return ((float)gs_global->Height-PS2_HOME_MENU_HEIGHT*sc)*0.5f+y*sc;
}
static u8 ps2_home_alpha(unsigned a255){
    return (u8)((a255*128U+127U)/255U);
}
static u64 ps2_home_rgba(unsigned r,unsigned g,unsigned b,unsigned a){
    return GS_SETREG_RGBAQ(r>>1,g>>1,b>>1,ps2_home_alpha(a),0);
}
static void ps2_home_rect(float x,float y,float w,float h,u64 color){
    if(w<=0.0f||h<=0.0f)return;
    ps2_menu_rect(ps2_home_x(x),ps2_home_y(y),ps2_home_x(x+w),ps2_home_y(y+h),color);
}

static float ps2_home_text_width(const char* text, float size) {
    float width = 0.0f;
    if (text == NULL) {
        return 0.0f;
    }
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c > 126) {
            c = '?';
        }
        width += (float)sPs2HomeGlyphs[c - 32].advance * size;
    }
    return width;
}

static void ps2_home_draw_glyph(float x, float baseline, unsigned char c, float size, u64 color) {
    const Ps2HomeGlyph* glyph;
    if (c < 32 || c > 126) {
        c = '?';
    }
    glyph = &sPs2HomeGlyphs[c - 32];
    for (unsigned row = 0; row < glyph->h; row++) {
        unsigned bits = sPs2HomeFontRows[glyph->row + row];
        unsigned col = 0;
        while (col < glyph->w) {
            while (col < glyph->w && ((bits >> col) & 1U) == 0) {
                col++;
            }
            if (col >= glyph->w) {
                break;
            }
            unsigned begin = col;
            while (col < glyph->w && ((bits >> col) & 1U) != 0) {
                col++;
            }
            ps2_home_rect(x + ((float)glyph->xoff + (float)begin) * size,
                          baseline + ((float)glyph->yoff + (float)row) * size,
                          ((float)(col - begin)) * size, size, color);
        }
    }
}

static void ps2_home_draw_text_pass(float x, float baseline, const char* text, float size, u64 color,
                                    bool centered) {
    float cursor;
    if (text == NULL || text[0] == '\0') {
        return;
    }
    cursor = centered ? x - ps2_home_text_width(text, size) * 0.5f : x;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c > 126) {
            c = '?';
        }
        ps2_home_draw_glyph(cursor, baseline, c, size, color);
        cursor += (float)sPs2HomeGlyphs[c - 32].advance * size;
    }
}

static void ps2_home_text(float x, float baseline, const char* text, float size, u64 color, u64 shadow,
                          bool centered) {

    ps2_home_draw_text_pass(x + 1.0f, baseline + 1.0f, text, size, shadow, centered);
    ps2_home_draw_text_pass(x, baseline, text, size, color, centered);
}

static void ps2_home_render_main(const char*const*lines,int lineCount,int selectedIndex){
    const u64 dim=ps2_home_rgba(0,0,0,96);
    const u64 panel=ps2_home_rgba(0,0,0,132);
    const u64 selected=ps2_home_rgba(PS2_HOME_HIGHLIGHT_R,PS2_HOME_HIGHLIGHT_G,PS2_HOME_HIGHLIGHT_B,205);
    const u64 normal=ps2_home_rgba(218,224,218,255);
    const u64 white=ps2_home_rgba(255,255,245,255);
    const u64 shadow=ps2_home_rgba(0,0,0,180);
    ps2_home_rect(0,0,480,272,dim);
    ps2_home_rect(112,34,256,210,panel);
    for(int i=0;i<lineCount;i++){
        const float y=62.0f+(float)i*38.0f;
        u64 color=normal;
        if(i==selectedIndex){
            ps2_home_rect(128,y-22.0f,224,28,selected);
            color=white;
        }
        ps2_home_text(240,y,lines[i],0.72f,color,shadow,true);
    }
}

static void ps2_home_render_submenu(const char*title,const char*const*lines,int lineCount,int selectedIndex,
                                    const char*statusMessage,int firstRow,int totalRows,bool video){
    const u64 dim=ps2_home_rgba(0,0,0,112);
    const u64 panel=ps2_home_rgba(0,0,0,154);
    const u64 selected=ps2_home_rgba(PS2_HOME_HIGHLIGHT_R,PS2_HOME_HIGHLIGHT_G,PS2_HOME_HIGHLIGHT_B,205);
    const u64 normal=ps2_home_rgba(218,224,218,255);
    const u64 white=ps2_home_rgba(255,255,245,255);
    const u64 hint=ps2_home_rgba(170,190,180,255);
    const u64 shadow=ps2_home_rgba(0,0,0,180);
    const float titleY=video?48.0f:50.0f;
    const float firstY=video?76.0f:82.0f;
    ps2_home_rect(0,0,480,272,dim);
    ps2_home_rect(34,20,412,232,panel);
    ps2_home_text(240,titleY,title,0.82f,white,shadow,true);
    for(int i=0;i<lineCount;i++){
        const float y=firstY+(float)i*22.0f;
        u64 color=normal;
        if(i==selectedIndex){
            ps2_home_rect(54,y-17.0f,372,22,selected);
            color=white;
        }
        ps2_home_text(70,y,lines[i],0.68f,color,shadow,false);
    }
    if(!video){
        if(firstRow>0)ps2_home_text(422,82,"^",0.48f,hint,ps2_home_rgba(0,0,0,160),true);
        if(firstRow+lineCount<totalRows)ps2_home_text(422,214,"v",0.48f,hint,ps2_home_rgba(0,0,0,160),true);
    }
    if(statusMessage&&statusMessage[0]){
        ps2_home_text(240,242,statusMessage,0.48f,hint,ps2_home_rgba(0,0,0,160),true);
    }
}

void gfx_ps2_set_boot_progress(int code, const char* label) {
    sBootProgressCode = code;
    if (label == NULL) {
        sBootProgressLabel[0] = '\0';
    } else {
        snprintf(sBootProgressLabel, sizeof(sBootProgressLabel), "%s", label);
    }
}

static void ps2_boot_progress_draw_contents(int code) {
    char codeText[16];
    const u64 white = ps2_home_rgba(255,255,245,255);
    const u64 shadow = ps2_home_rgba(0,0,0,220);

    snprintf(codeText, sizeof(codeText), "%03d", code < 0 ? 0 : code);
    ps2_home_rect(0,0,480,272,ps2_home_rgba(0,0,0,255));
    ps2_home_text(240,108,"Boot Check",0.72f,white,shadow,true);
    ps2_home_text(240,148,codeText,1.45f,white,shadow,true);
}

void gfx_ps2_render_boot_progress(int code, const char* label) {
    const bool old_test=z_test, old_blend=do_blend, old_write=z_write;
    const struct Clip old_clip=r_clip;
    (void)label;
    sSuppressFpsOverlay=true;
    z_test=false; z_write=false; set_blend(true); update_test(); ps2_depth_mask(false);
    set_scissor_raw(0,0,gs_global->Width-1,gs_global->Height-1);
    ps2_boot_progress_draw_contents(code);
    set_scissor_raw(old_clip.x0,old_clip.y0,old_clip.x1,old_clip.y1);
    z_test=old_test; z_write=old_write; ps2_depth_mask(old_write); update_test(); set_blend(old_blend);
}

void gfx_ps2_render_menu(const char*title,const char*const*lines,int lineCount,int selectedIndex,
                         const char*statusMessage,int firstRow,int totalRows){
    sSuppressFpsOverlay=true;
    const bool old_test=z_test,old_blend=do_blend,old_write=z_write;
    const struct Clip old_clip=r_clip;
    z_test=false;z_write=false;set_blend(true);update_test();ps2_depth_mask(false);
    set_scissor_raw(0,0,gs_global->Width-1,gs_global->Height-1);

    if(title==NULL){
        ps2_home_render_main(lines,lineCount,selectedIndex);
    }else if(strcmp(title,"Video Settings")==0){
        ps2_home_render_submenu(title,lines,lineCount,selectedIndex,statusMessage,firstRow,totalRows,true);
    }else{
        ps2_home_render_submenu(title,lines,lineCount,selectedIndex,statusMessage,firstRow,totalRows,false);
    }

    set_scissor_raw(old_clip.x0,old_clip.y0,old_clip.x1,old_clip.y1);
    z_test=old_test;z_write=old_write;ps2_depth_mask(old_write);update_test();set_blend(old_blend);
}

static void ps2_end(void){
#if OOT_PS2_RAPI_PROFILE
    if(++sProfFrames>=20){
        printf("PS2RAPI draw=%llu tex=%llu untex=%llu exact=%llu fog=%llu upload=%llu bind=%llu calls=%u exact_calls=%u fog_calls=%u upload_calls=%u bind_calls=%u bind_xfer=%u\n",
            (unsigned long long)sProfDrawCycles,(unsigned long long)sProfTexCycles,
            (unsigned long long)sProfUntexCycles,(unsigned long long)sProfExactCycles,
            (unsigned long long)sProfFogCycles,(unsigned long long)sProfUploadCycles,(unsigned long long)sProfBindCycles,
            (unsigned)sProfDrawCalls,(unsigned)sProfExactCalls,(unsigned)sProfFogCalls,(unsigned)sProfUploadCalls,
            (unsigned)sProfBindCalls,(unsigned)sProfBindTransfers);
        sProfDrawCycles=sProfTexCycles=sProfUntexCycles=sProfExactCycles=sProfFogCycles=0;
        sProfUploadCycles=sProfBindCycles=0;
        sProfDrawCalls=sProfExactCalls=sProfFogCalls=sProfUploadCalls=sProfBindCalls=sProfBindTransfers=0;sProfFrames=0;
    }
#endif
    if(sSuppressFpsOverlay){
        sSuppressFpsOverlay=false;
    }else if(sFpsOverlayEnabled){
        ps2_fps_overlay();
    }
    ps2_build_overlay();
}
static void ps2_finish(void){}

struct GfxRenderingAPI gfx_ps2_rapi={ps2_z01,ps2_unload_shader,ps2_load_shader,ps2_create_shader,ps2_lookup_shader,ps2_shader_info,ps2_new_texture,ps2_select_texture,ps2_upload_texture,ps2_sampler,ps2_env_color,ps2_depth_test,ps2_depth_mask,ps2_zdecal,ps2_viewport,ps2_scissor,ps2_alpha,ps2_draw,ps2_draw_fog,ps2_init,ps2_resize,ps2_start,ps2_end,ps2_finish};
