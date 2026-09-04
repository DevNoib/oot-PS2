#include <string.h>

#include <iopcontrol.h>
#include <ps2_filesystem_driver.h>
#include <ps2_joystick_driver.h>
#include <sbv_patches.h>
#include <sifrpc.h>

#include "console_logo_state.h"
#include "fault.h"
#include "file_select_state.h"
#include "game.h"
#include "gfx.h"
#include "libc64/malloc.h"
#include "map_select_state.h"
#include "oot_port_asset_loader.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_renderer.h"
#include "oot_ps2_runtime_patch.h"
#include "play_state.h"
#include "setup_state.h"
#include "title_setup_state.h"

void OotPortGame_Init(void);
void Graph_Init(GraphicsContext* gfxCtx);
void Graph_Destroy(GraphicsContext* gfxCtx);
void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState);

static size_t OotPs2StateSize(GameStateFunc init) {
    if (init == Setup_Init) {
        return sizeof(SetupState);
    }
    if (init == ConsoleLogo_Init) {
        return sizeof(ConsoleLogoState);
    }
    if (init == TitleSetup_Init) {
        return sizeof(TitleSetupState);
    }
    if (init == Play_Init) {
        return sizeof(PlayState);
    }
    if (init == MapSelect_Init) {
        return sizeof(MapSelectState);
    }
    if (init == FileSelect_Init) {
        return sizeof(FileSelectState);
    }
    return sizeof(GameState);
}

static s32 OotPs2BootIsDisc(const char* path) {
    if (path == NULL) {
        return false;
    }

    return (strncmp(path, "cdrom0:", 7) == 0) || (strncmp(path, "cdrom:", 6) == 0) ||
           (strncmp(path, "cdfs:", 5) == 0);
}

static void OotPs2Drivers_Init(const char* executablePath) {
    s32 discBoot = OotPs2BootIsDisc(executablePath);

    SifInitRpc(0);
    if (!discBoot) {
        while (!SifIopReset(NULL, 0)) {
        }
        while (!SifIopSync()) {
        }
        SifInitRpc(0);
    }

    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    if (discBoot) {
        init_ps2_filesystem_driver();
    } else {
        init_only_boot_ps2_filesystem_driver();
    }

    init_joystick_driver(true);
}

int main(int argc, char** argv) {
    GraphicsContext gfxCtx;
    GameState* gameState;
    GameStateFunc nextInit = Setup_Init;
    size_t nextSize = sizeof(SetupState);
    const char* executablePath = ((argc > 0) && (argv != NULL) && (argv[0] != NULL)) ? argv[0] : NULL;

    OotPs2Thread_ChangePriority(OotPs2Thread_GetId(), 0x30);
    OotPs2Platform_SetDiscBoot(OotPs2BootIsDisc(executablePath));
    OotPs2Drivers_Init(executablePath);

    osInitialize();
    OotPs2Renderer_Init();

    if (!OotPort_AssetInit(executablePath)) {
        return 1;
    }

    if (!OotPs2RuntimePatch_Apply()) {
        return 1;
    }

    OotPortGame_Init();
    Graph_Init(&gfxCtx);

    while (nextInit != NULL) {
        gameState = SYSTEM_ARENA_MALLOC(nextSize, __FILE__, __LINE__);
        if (gameState == NULL) {
            Fault_AddHungupAndCrash(__FILE__, __LINE__);
        }

        memset(gameState, 0, nextSize);
        GameState_Init(gameState, nextInit, &gfxCtx);

        while (GameState_IsRunning(gameState)) {
            Graph_Update(&gfxCtx, gameState);
        }

        nextInit = GameState_GetInit(gameState);
        nextSize = gameState->size != 0 ? gameState->size : OotPs2StateSize(nextInit);
        GameState_Destroy(gameState);
        SYSTEM_ARENA_FREE(gameState, __FILE__, __LINE__);
    }

    Graph_Destroy(&gfxCtx);
    return 0;
}
