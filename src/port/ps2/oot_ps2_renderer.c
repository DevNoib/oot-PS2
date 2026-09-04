#include "oot_ps2_renderer.h"
#include <stdbool.h>
#include "gfx/gfx_fast3d.h"
#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_window_manager_api.h"
#include "oot_ps2_platform.h"
#include "oot_ps2_crash.h"

void OotPs2Renderer_Init(void) {
}

void OotPs2Renderer_RenderDisplayList(Gfx* dl) {
}

void OotPs2Renderer_RenderTask(const OSTask* task) {
}

void OotPs2Renderer_RenderMenu(const char* title, const char* const* lines, int lineCount, int selectedIndex,
                               const char* statusMessage, int firstRow, int totalRows) {
}

void OotPs2Renderer_SetWidescreen(bool enabled) {
}

bool OotPs2Renderer_IsWidescreen(void) {
    return false;
}

void OotPs2Renderer_RequestPauseBackground(void) {
}

bool OotPs2Renderer_IsPauseBackgroundReady(void) {
    return false;
}

void OotPs2Renderer_SetPauseBackgroundActive(bool active) {
}

void OotPs2Renderer_SetFpsVisible(bool enabled) {
}

bool OotPs2Renderer_IsFpsVisible(void) {
    return false;
}

void OotPs2Renderer_SetPrerenderRoomState(bool active, u32 roomKey) {
}

void OotPs2Renderer_SetBootProgress(int code, const char* label) {
}

void OotPs2Renderer_ShowBootProgress(int code, const char* label) {
}

void OotPs2Renderer_HideBootProgress(void) {
}
