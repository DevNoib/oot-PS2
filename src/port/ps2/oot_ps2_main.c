#include <stdio.h>
#include <string.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <ps2_filesystem_driver.h>
#include <ps2_joystick_driver.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include "attributes.h"
#include "console_logo_state.h"
#include "fault.h"
#include "file_select_state.h"
#include "game.h"
#include "gfx.h"
#include "libc64/malloc.h"
#include "map_select_state.h"
#include "oot_port_asset_loader.h"
#include "oot_port_controls.h"
#include "oot_ps2_renderer.h"
#include "oot_ps2_runtime_patch.h"
#include "oot_ps2_home_menu.h"
#include "save.h"
#include "sram.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_crash.h"
#include "play_state.h"
#include "regs.h"
#include "setup_state.h"
#include "title_setup_state.h"

int main(int argc, char** argv) {
    return 0;
}
