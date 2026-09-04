#ifndef OOT_PS2_RENDERER_H
#define OOT_PS2_RENDERER_H

#include <stdbool.h>
#include "sched.h"

void OotPs2Renderer_Init(void);
void OotPs2Renderer_RenderDisplayList(Gfx* dl);
void OotPs2Renderer_RenderTask(const OSTask* task);
void OotPs2Renderer_RenderMenu(const char* title, const char* const* lines, int lineCount, int selectedIndex,
                               const char* statusMessage, int firstRow, int totalRows);
void OotPs2Renderer_SetWidescreen(bool enabled);
bool OotPs2Renderer_IsWidescreen(void);
void OotPs2Renderer_RequestPauseBackground(void);
bool OotPs2Renderer_IsPauseBackgroundReady(void);
void OotPs2Renderer_SetPauseBackgroundActive(bool active);
void OotPs2Renderer_SetFpsVisible(bool enabled);
bool OotPs2Renderer_IsFpsVisible(void);
void OotPs2Renderer_SetPrerenderRoomState(bool active, u32 roomKey);
void OotPs2Renderer_SetBootProgress(int code, const char* label);
void OotPs2Renderer_ShowBootProgress(int code, const char* label);
void OotPs2Renderer_HideBootProgress(void);

#endif
