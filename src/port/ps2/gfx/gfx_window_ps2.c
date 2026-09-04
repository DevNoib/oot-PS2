#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <dmaKit.h>
#include <gsKit.h>
#include <gsInline.h>
#include <gif_registers.h>
#include <kernel.h>
#include <timer.h>

#include "gfx_window_manager_api.h"
#include "../oot_ps2_platform.h"

#define OOT_PS2_DISPLAY_WIDTH 640
#define OOT_PS2_DISPLAY_HEIGHT 448

GSGLOBAL* gs_global;

static volatile bool sGsInitActive;
static volatile bool sAsyncFrameSubmit;
static bool sFramePending;
static int sDisplayedBufferIndex;
static bool sPauseBackgroundActive;
static bool sPauseBackgroundCapturePending;
static bool sPauseBackgroundCaptured;
static u32 sPauseBackgroundVram;
static u32 sPauseBackgroundBytes;

#define OOT_PS2_PAUSE_BG_PSM GS_PSM_CT16
#define OOT_PS2_PAUSE_BG_WIDTH 320
#define OOT_PS2_PAUSE_BG_HEIGHT 224

extern void gfx_ps2_invalidate_register_cache(void);
static void GfxPs2Window_AllocPauseBackground(void);

void __real_SetGsCrt(s16 interlace, s16 mode, s16 field);
void __real_dmaKit_wait_fast(void);
void __real_dmaKit_send_ucab(u16 channel, void* data, u32 size);
int __real_DIntr(void);
int __real_EIntr(void);
u64 __real_GsPutIMR(u64 imr);
u32 __real_gsKit_vram_alloc(GSGLOBAL* gsGlobal, u32 size, u8 type);
u32 __real_gsKit_texture_size(int width, int height, int psm);
void __real_gsKit_TexManager_init(GSGLOBAL* gsGlobal);
void __real_gsKit_clear(GSGLOBAL* gsGlobal, u64 color);
void __real_gsKit_queue_exec(GSGLOBAL* gsGlobal);
void __real_gsKit_sync_flip(GSGLOBAL* gsGlobal);
void __real_gsKit_finish(void);
void __real_gsKit_queue_reset(GSQUEUE* queue);

int __wrap_DIntr(void) {
    if (sGsInitActive && OotPs2Platform_IsDiscBoot()) {
        return 1;
    }
    return __real_DIntr();
}

int __wrap_EIntr(void) {
    if (sGsInitActive && OotPs2Platform_IsDiscBoot()) {
        return 1;
    }
    return __real_EIntr();
}

u64 __wrap_GsPutIMR(u64 imr) {
    return __real_GsPutIMR(imr);
}

u32 __wrap_gsKit_texture_size(int width, int height, int psm) {
    return __real_gsKit_texture_size(width, height, psm);
}

u32 __wrap_gsKit_vram_alloc(GSGLOBAL* gsGlobal, u32 size, u8 type) {
    return __real_gsKit_vram_alloc(gsGlobal, size, type);
}

void __wrap_gsKit_TexManager_init(GSGLOBAL* gsGlobal) {
    __real_gsKit_TexManager_init(gsGlobal);
}

void __wrap_gsKit_clear(GSGLOBAL* gsGlobal, u64 color) {
    __real_gsKit_clear(gsGlobal, color);
}

void __wrap_gsKit_queue_exec(GSGLOBAL* gsGlobal) {
    __real_gsKit_queue_exec(gsGlobal);
}

void __wrap_gsKit_finish(void) {
    if (!sAsyncFrameSubmit) {
        __real_gsKit_finish();
    }
}

void __wrap_gsKit_sync_flip(GSGLOBAL* gsGlobal) {
    __real_gsKit_sync_flip(gsGlobal);
}

void __wrap_gsKit_queue_reset(GSQUEUE* queue) {
    __real_gsKit_queue_reset(queue);
}

void __wrap_SetGsCrt(s16 interlace, s16 mode, s16 field) {
    __real_SetGsCrt(interlace, mode, field);
}

void __wrap_dmaKit_send_ucab(u16 channel, void* data, u32 size) {
    __real_dmaKit_send_ucab(channel, data, size);
}

void __wrap_dmaKit_wait_fast(void) {
    if (!sGsInitActive) {
        __real_dmaKit_wait_fast();
        return;
    }

    if (dmaKit_wait(DMA_CHANNEL_GIF, 8000000) < 0) {
        *(volatile u32*)0x1000A000 = 0;
        *(volatile u32*)0x1000A020 = 0;
        *(volatile u32*)0x1000A030 = 0;
        GIF_REG_CTRL = GIF_SET_CTRL(1, 0);
        __asm__ volatile("sync.p; nop;");
        GIF_REG_CTRL = 0;
        dmaKit_chan_init(DMA_CHANNEL_GIF);
    }
}

void gfx_ps2_prepare_crash_screen(void) {
    *(volatile u32*)0x1000A000 = 0;
    *(volatile u32*)0x1000A010 = 0;
    *(volatile u32*)0x1000A020 = 0;
    *(volatile u32*)0x1000A030 = 0;
    __asm__ volatile("sync.p; nop;");
    GIF_REG_CTRL = GIF_SET_CTRL(1, 0);
    __asm__ volatile("sync.p; nop;");
    GIF_REG_CTRL = 0;
    GS_RESET();
    __asm__ volatile("sync.p; nop;");
    *GS_CSR = 0;
    dmaKit_chan_init(DMA_CHANNEL_GIF);
}

static int GfxPs2WaitGifBounded(void) {
    u32 timeout = 8000000;

    while (((*(volatile u32*)0x1000A000) & 0x100) != 0 && timeout-- != 0) {
        __asm__ volatile("nop");
    }
    if (((*(volatile u32*)0x1000A000) & 0x100) != 0) {
        *(volatile u32*)0x1000A000 = 0;
        *(volatile u32*)0x1000A020 = 0;
        *(volatile u32*)0x1000A030 = 0;
        GIF_REG_CTRL = GIF_SET_CTRL(1, 0);
        __asm__ volatile("sync.p; nop;");
        GIF_REG_CTRL = 0;
        dmaKit_chan_init(DMA_CHANNEL_GIF);
        return 0;
    }
    return 1;
}

static int GfxPs2WaitFinishBounded(void) {
    u32 timeout = 8000000;

    while (!(GS_CSR_FINISH) && timeout-- != 0) {
        __asm__ volatile("nop");
    }
    if (!(GS_CSR_FINISH)) {
        return 0;
    }
    return 1;
}

static void GfxPs2SetBufferAttributesDisc(GSGLOBAL* g) {
    g->StartXOffset = 0;
    g->StartYOffset = 0;
    g->StartX = 492;
    g->StartY = 34;
    g->DW = 2880;
    g->DH = 480;
    g->MagH = (g->DW / g->Width) - 1;
    g->MagV = (g->DH / g->Height) - 1;
    g->StartX += (g->DW - ((g->MagH + 1) * g->Width)) / 2;
    g->StartY += (g->DH - ((g->MagV + 1) * g->Height)) / 2;
    g->StartY &= ~1;
    g->DW = (g->MagH + 1) * g->Width;
    g->DH = (g->MagV + 1) * g->Height;
}

static int GfxPs2InitScreenDisc(GSGLOBAL* g) {
    u64* pData;
    u64* pStore;
    int size = 19;
    u32 bytes;

    GfxPs2SetBufferAttributesDisc(g);
    GS_RESET();
    __asm__ volatile("sync.p; nop;");
    *GS_CSR = 0;
    __real_GsPutIMR(0x00007F00);
    __real_SetGsCrt(g->Interlace, g->Mode, g->Field);

    GS_SET_PMODE(0, 1, 0, 1, 0, 0x80);
    GS_SET_DISPFB1(0, g->Width / 64, g->PSM, 0, 0);
    GS_SET_DISPFB2(0, g->Width / 64, g->PSM, 0, 0);
    GS_SET_DISPLAY1(g->StartX, g->StartY, g->MagH, g->MagV, g->DW - 1, g->DH - 1);
    GS_SET_DISPLAY2(g->StartX, g->StartY, g->MagH, g->MagV, g->DW - 1, g->DH - 1);
    GS_SET_BGCOLOR(g->BGColor->Red, g->BGColor->Green, g->BGColor->Blue);

    g->FirstFrame = GS_SETTING_ON;
    if (g->ZBuffering == GS_SETTING_OFF) {
        g->Test->ZTE = GS_SETTING_ON;
        g->Test->ZTST = 1;
    }
    g->CurrentPointer = 0;

    bytes = __real_gsKit_texture_size(g->Width, g->Height, g->PSM);
    g->ScreenBuffer[0] = __real_gsKit_vram_alloc(g, bytes, GSKIT_ALLOC_SYSBUFFER);

    if (g->DoubleBuffering == GS_SETTING_OFF) {
        g->ScreenBuffer[1] = g->ScreenBuffer[0];
    } else {
        g->ScreenBuffer[1] = __real_gsKit_vram_alloc(g, bytes, GSKIT_ALLOC_SYSBUFFER);
    }

    if (g->ZBuffering == GS_SETTING_ON) {
        bytes = __real_gsKit_texture_size(g->Width, g->Height, g->PSMZ);
        g->ZBuffer = __real_gsKit_vram_alloc(g, bytes, GSKIT_ALLOC_SYSBUFFER);
    }
    g->TexturePointer = g->CurrentPointer;

    pData = pStore = (u64*)g->dma_misc;
    *pData++ = GIF_TAG(size - 1, 1, 0, 0, GSKIT_GIF_FLG_PACKED, 1);
    *pData++ = GIF_AD;
    *pData++ = 1;
    *pData++ = GS_PRMODECONT;
    *pData++ = GS_SETREG_FRAME_1(g->ScreenBuffer[0] / 8192, g->Width / 64, g->PSM, 0);
    *pData++ = GS_FRAME_1;
    *pData++ = GS_SETREG_XYOFFSET_1(g->OffsetX, g->OffsetY);
    *pData++ = GS_XYOFFSET_1;
    *pData++ = GS_SETREG_SCISSOR_1(0, g->Width - 1, 0, g->Height - 1);
    *pData++ = GS_SCISSOR_1;
    *pData++ = GS_SETREG_TEST(g->Test->ATE, g->Test->ATST, g->Test->AREF, g->Test->AFAIL,
                              g->Test->DATE, g->Test->DATM, g->Test->ZTE, g->Test->ZTST);
    *pData++ = GS_TEST_1;
    *pData++ = GS_SETREG_CLAMP(g->Clamp->WMS, g->Clamp->WMT, g->Clamp->MINU, g->Clamp->MAXU,
                               g->Clamp->MINV, g->Clamp->MAXV);
    *pData++ = GS_CLAMP_1;
    if (g->ZBuffering == GS_SETTING_ON) {
        if ((g->PSM == GS_PSM_CT16) && (g->PSMZ != GS_PSMZ_16)) {
            g->PSMZ = GS_PSMZ_16;
        }
        if ((g->PSM != GS_PSM_CT16) && (g->PSMZ == GS_PSMZ_16)) {
            g->PSMZ = GS_PSMZ_16S;
        }
        *pData++ = GS_SETREG_ZBUF_1(g->ZBuffer / 8192, g->PSMZ, 0);
        *pData++ = GS_ZBUF_1;
    } else {
        *pData++ = GS_SETREG_ZBUF_1(0, g->PSM, 1);
        *pData++ = GS_ZBUF_1;
    }
    *pData++ = GS_SETREG_COLCLAMP(255);
    *pData++ = GS_COLCLAMP;
    *pData++ = GS_SETREG_FRAME_1(g->ScreenBuffer[1] / 8192, g->Width / 64, g->PSM, 0);
    *pData++ = GS_FRAME_2;
    *pData++ = GS_SETREG_XYOFFSET_1(g->OffsetX, g->OffsetY);
    *pData++ = GS_XYOFFSET_2;
    *pData++ = GS_SETREG_SCISSOR_1(0, g->Width - 1, 0, g->Height - 1);
    *pData++ = GS_SCISSOR_2;
    *pData++ = GS_SETREG_TEST(g->Test->ATE, g->Test->ATST, g->Test->AREF, g->Test->AFAIL,
                              g->Test->DATE, g->Test->DATM, g->Test->ZTE, g->Test->ZTST);
    *pData++ = GS_TEST_2;
    *pData++ = GS_SETREG_CLAMP(g->Clamp->WMS, g->Clamp->WMT, g->Clamp->MINU, g->Clamp->MAXU,
                               g->Clamp->MINV, g->Clamp->MAXV);
    *pData++ = GS_CLAMP_2;
    if (g->ZBuffering == GS_SETTING_ON) {
        *pData++ = GS_SETREG_ZBUF_1(g->ZBuffer / 8192, g->PSMZ, 0);
        *pData++ = GS_ZBUF_2;
    } else {
        *pData++ = GS_SETREG_ZBUF_1(0, g->PSM, 1);
        *pData++ = GS_ZBUF_2;
    }
    *pData++ = GS_BLEND_BACK2FRONT;
    *pData++ = GS_ALPHA_1;
    *pData++ = GS_BLEND_BACK2FRONT;
    *pData++ = GS_ALPHA_2;
    *pData++ = GS_SETREG_DIMX(g->DitherMatrix[0], g->DitherMatrix[1], g->DitherMatrix[2], g->DitherMatrix[3],
                              g->DitherMatrix[4], g->DitherMatrix[5], g->DitherMatrix[6], g->DitherMatrix[7],
                              g->DitherMatrix[8], g->DitherMatrix[9], g->DitherMatrix[10], g->DitherMatrix[11],
                              g->DitherMatrix[12], g->DitherMatrix[13], g->DitherMatrix[14], g->DitherMatrix[15]);
    *pData++ = GS_DIMX;
    *pData++ = GS_SETREG_TEXA(0x00, 0, 0x80);
    *pData++ = GS_TEXA;

    __real_dmaKit_send_ucab(DMA_CHANNEL_GIF, pStore, size);
    GfxPs2WaitGifBounded();
    __real_gsKit_clear(g, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    __real_gsKit_queue_exec(g);
    GfxPs2WaitGifBounded();
    GfxPs2WaitFinishBounded();

    GS_SET_DISPFB1(g->ScreenBuffer[0] / 8192, g->Width / 64, g->PSM, 0, 0);
    GS_SET_DISPFB2(g->ScreenBuffer[0] / 8192, g->Width / 64, g->PSM, 0, 0);
    GS_SET_DISPLAY1(g->StartX, g->StartY, g->MagH, g->MagV, g->DW - 1, g->DH - 1);
    GS_SET_DISPLAY2(g->StartX, g->StartY, g->MagH, g->MagV, g->DW - 1, g->DH - 1);
    GS_SET_PMODE(0, 1, 0, 1, 0, 0x80);
    g->ActiveBuffer = 0;
    sDisplayedBufferIndex = 0;
    g->PrimContext = 0;
    g->FirstFrame = GS_SETTING_OFF;
    __real_gsKit_queue_reset(g->Os_Queue);
    return 1;
}

static void GfxPs2Window_Init(const char* gameName, bool startInFullscreen) {
    (void)gameName;
    (void)startInFullscreen;

    gs_global = gsKit_init_global();
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8,
                1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gs_global->Mode = GS_MODE_NTSC;
    gs_global->Width = OOT_PS2_DISPLAY_WIDTH;
    gs_global->Height = OOT_PS2_DISPLAY_HEIGHT;
    gs_global->Interlace = GS_INTERLACED;
    gs_global->Field = GS_FIELD;
    gs_global->ZBuffering = GS_SETTING_ON;
    gs_global->DoubleBuffering = GS_SETTING_ON;
    gs_global->PrimAAEnable = GS_SETTING_OFF;

    gs_global->PSM = GS_PSM_CT24;
    gs_global->PSMZ = GS_PSMZ_16;

    ResetEE(INIT_VU1 | INIT_VIF1 | INIT_GIF);
    __asm__ volatile("sync.p; nop;");
    if (OotPs2Platform_IsDiscBoot()) {
        sGsInitActive = true;
        GfxPs2InitScreenDisc(gs_global);
        sGsInitActive = false;
    } else {
        sGsInitActive = true;
        gsKit_init_screen(gs_global);
        sGsInitActive = false;
    }
    GfxPs2Window_AllocPauseBackground();
    gsKit_TexManager_init(gs_global);
}

static void GfxPs2Window_AllocPauseBackground(void) {
    if (sPauseBackgroundVram != 0 || gs_global == NULL) return;

    sPauseBackgroundBytes = __real_gsKit_texture_size(OOT_PS2_PAUSE_BG_WIDTH, OOT_PS2_PAUSE_BG_HEIGHT,
                                                       OOT_PS2_PAUSE_BG_PSM);
    sPauseBackgroundVram = __real_gsKit_vram_alloc(gs_global, sPauseBackgroundBytes, GSKIT_ALLOC_SYSBUFFER);
}

static void GfxPs2Window_QueueFrameState(u32 frameAddr, u32 framePsm, u32 frameWidth,
                                           u32 scissorW, u32 scissorH) {
    u64* p=gsKit_heap_alloc(gs_global,4,64,GIF_AD);
    *p++=GIF_TAG_AD(4); *p++=GIF_AD;
    *p++=GS_SETREG_FRAME_1(frameAddr/8192U,frameWidth/64U,framePsm,0); *p++=GS_FRAME_1;
    *p++=GS_SETREG_SCISSOR(0,scissorW-1,0,scissorH-1); *p++=GS_SCISSOR_1;
    *p++=GS_SETREG_TEST(0,1,0,0,0,0,0,1); *p++=GS_TEST_1;
    *p++=GS_SETREG_ZBUF_1(gs_global->ZBuffer/8192U,gs_global->PSMZ,1); *p++=GS_ZBUF_1;
}

static void GfxPs2Window_QueueScaledSnapshot(u32 sourceAddr,u32 sourcePsm,u32 sourceW,u32 sourceH,
                                             u32 targetAddr,u32 targetPsm,u32 targetW,u32 targetH,
                                             int filter){
    GSTEXTURE tex;
    const int oldAlpha=gs_global->PrimAlphaEnable, oldFog=gs_global->PrimFogEnable, oldAA=gs_global->PrimAAEnable;
    memset(&tex,0,sizeof(tex));
    tex.Width=sourceW; tex.Height=sourceH; tex.PSM=(u8)sourcePsm; tex.TBW=sourceW/64U;
    tex.Vram=sourceAddr; tex.Filter=filter;
    gs_global->PrimAlphaEnable=0; gs_global->PrimFogEnable=0; gs_global->PrimAAEnable=0;
    GfxPs2Window_QueueFrameState(targetAddr,targetPsm,targetW,targetW,targetH);
    {
        u64* p=gsKit_heap_alloc(gs_global,2,32,GIF_AD);
        *p++=GIF_TAG_AD(2); *p++=GIF_AD;
        *p++=0; *p++=GS_TEXFLUSH;
        *p++=GS_SETREG_CLAMP(GS_CMODE_CLAMP,GS_CMODE_CLAMP,0,sourceW-1,0,sourceH-1); *p++=GS_CLAMP_1;
    }
    gsKit_prim_sprite_texture_3d(gs_global,&tex,0.0f,0.0f,0,0.0f,0.0f,
                                 (float)targetW,(float)targetH,0,(float)sourceW,(float)sourceH,
                                 GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
    gs_global->PrimAlphaEnable=oldAlpha; gs_global->PrimFogEnable=oldFog; gs_global->PrimAAEnable=oldAA;
}

static bool GfxPs2Window_QueuePauseBackgroundCaptureCurrent(void) {
    const int drawBuffer = gs_global != NULL ? (gs_global->ActiveBuffer & 1) : 0;
    const u32 src = gs_global != NULL ? gs_global->ScreenBuffer[drawBuffer] : 0;
    const u32 dst = sPauseBackgroundVram;

    if (!sPauseBackgroundActive || !sPauseBackgroundCapturePending ||
        gs_global == NULL || src == 0 || dst == 0) {
        return false;
    }

    GfxPs2Window_QueueScaledSnapshot(src,gs_global->PSM,gs_global->Width,gs_global->Height,
                                     dst,OOT_PS2_PAUSE_BG_PSM,OOT_PS2_PAUSE_BG_WIDTH,OOT_PS2_PAUSE_BG_HEIGHT,
                                     GS_FILTER_LINEAR);
    GfxPs2Window_QueueFrameState(src,gs_global->PSM,gs_global->Width,gs_global->Width,gs_global->Height);
    gfx_ps2_invalidate_register_cache();
    sPauseBackgroundCapturePending = false;
    sPauseBackgroundCaptured = true;
    return true;
}

void gfx_ps2_capture_pause_background_current(void) {
    (void)GfxPs2Window_QueuePauseBackgroundCaptureCurrent();
}

static void GfxPs2Window_SetKeyboardCallbacks(bool (*isKeyDown)(int), bool (*isKeyPressed)(int),
                                              void (*allKeysUp)(void)) {
    (void)isKeyDown;
    (void)isKeyPressed;
    (void)allKeysUp;
}

static void GfxPs2Window_SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool)) {
    (void)onFullscreenChanged;
}

static void GfxPs2Window_SetFullscreen(bool fullscreen) {
    (void)fullscreen;
}

static void GfxPs2Window_MainLoop(void (*runOneGameIter)(void)) {
    runOneGameIter();
}

static void GfxPs2Window_GetDimensions(uint32_t* width, uint32_t* height) {
    *width = OOT_PS2_DISPLAY_WIDTH;
    *height = OOT_PS2_DISPLAY_HEIGHT;
}

static void GfxPs2Window_HandleEvents(void) {
}

void gfx_ps2_request_pause_background(void) {
    if (!sPauseBackgroundActive) return;
    sPauseBackgroundCapturePending = true;
    sPauseBackgroundCaptured = false;
}

void gfx_ps2_set_pause_background_active(bool active) {
    if (!active) {
        sPauseBackgroundCapturePending = false;
        sPauseBackgroundCaptured = false;
    }
    sPauseBackgroundActive = active;
}

bool gfx_ps2_pause_background_active(void) {
    return sPauseBackgroundActive && sPauseBackgroundCaptured && sPauseBackgroundVram != 0;
}

void gfx_ps2_restore_pause_background(void) {
    if (!gfx_ps2_pause_background_active()) return;
    GfxPs2Window_QueueScaledSnapshot(sPauseBackgroundVram,OOT_PS2_PAUSE_BG_PSM,
                                     OOT_PS2_PAUSE_BG_WIDTH,OOT_PS2_PAUSE_BG_HEIGHT,
                                     gs_global->ScreenBuffer[gs_global->ActiveBuffer&1],gs_global->PSM,
                                     gs_global->Width,gs_global->Height,GS_FILTER_LINEAR);
    GfxPs2Window_QueueFrameState(gs_global->ScreenBuffer[gs_global->ActiveBuffer&1],gs_global->PSM,
                                 gs_global->Width,gs_global->Width,gs_global->Height);
    gfx_ps2_invalidate_register_cache();
}

static bool GfxPs2Window_StartFrame(void) {
    if (sFramePending) {
        u32 timeout = 12000000U;
        const int completed = gs_global->ActiveBuffer & 1;
        int nextDraw = completed;

        while (!(GS_CSR_FINISH) && timeout-- != 0) { __asm__ volatile("nop"); }

        if (!(GS_CSR_FINISH)) return false;

        sDisplayedBufferIndex = completed;
        GS_SET_DISPFB2(gs_global->ScreenBuffer[sDisplayedBufferIndex] / 8192,
                       gs_global->Width / 64, gs_global->PSM, 0, 0);

        if (gs_global->DoubleBuffering != GS_SETTING_OFF) {
            nextDraw = completed ^ 1;
        }

        gs_global->ActiveBuffer = (u8)nextDraw;
        gsKit_setactive(gs_global);
        sFramePending = false;
    }
    return true;
}

static void GfxPs2Window_SwapBuffersBegin(void) {
}

static void GfxPs2Window_SwapBuffersEnd(void) {
    sAsyncFrameSubmit = true;
    gsKit_queue_exec(gs_global);
    sAsyncFrameSubmit = false;
    sFramePending = true;
    gsKit_TexManager_nextFrame(gs_global);
}

static double GfxPs2Window_GetTime(void) {
    u32 seconds;
    u32 microseconds;

    TimerBusClock2USec(GetTimerSystemTime(), &seconds, &microseconds);
    return seconds + (double)microseconds / 1000000.0;
}

struct GfxWindowManagerAPI gfx_ps2_wapi = {
    GfxPs2Window_Init,
    GfxPs2Window_SetKeyboardCallbacks,
    GfxPs2Window_SetFullscreenChangedCallback,
    GfxPs2Window_SetFullscreen,
    GfxPs2Window_MainLoop,
    GfxPs2Window_GetDimensions,
    GfxPs2Window_HandleEvents,
    GfxPs2Window_StartFrame,
    GfxPs2Window_SwapBuffersBegin,
    GfxPs2Window_SwapBuffersEnd,
    GfxPs2Window_GetTime,
};
