#include "oot_port_controls.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "controller.h"
#include "oot_port_asset_loader.h"

void OotPortControls_InitDefaults(void) {
}

void OotPortControls_ResetDefaults(void) {
}

s32 OotPortControls_Load(void) {
    return 0;
}

s32 OotPortControls_Save(void) {
    return 0;
}

u16 OotPortControls_MapButtons(u32 buttons) {
    return 0;
}

s8 OotPortControls_MapStick(u8 raw) {
    return 0;
}

int OotPortControls_GetBindingCount(void) {
    return 0;
}

const char* OotPortControls_GetBindingName(int index) {
    return NULL;
}

void OotPortControls_GetBindingValueText(int index, char* buffer, size_t bufferSize) {
}

void OotPortControls_CycleBinding(int index, int direction) {
}

int OotPortControls_GetDeadzone(void) {
    return 0;
}

void OotPortControls_SetDeadzone(int deadzone) {
}

void OotPortControls_AdjustDeadzone(int delta) {
}
