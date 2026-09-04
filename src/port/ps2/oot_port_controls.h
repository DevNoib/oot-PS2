#ifndef OOT_PORT_CONTROLS_H
#define OOT_PORT_CONTROLS_H

#include <stddef.h>
#include <stdint.h>

#include "ultra64/ultratypes.h"

#define OOT_PORT_CONTROLS_INI_PATH "controls.ini"
#define OOT_PORT_CONTROLS_DEADZONE_MIN 0
#define OOT_PORT_CONTROLS_DEADZONE_MAX 80

void OotPortControls_InitDefaults(void);
s32 OotPortControls_Load(void);
s32 OotPortControls_Save(void);
void OotPortControls_ResetDefaults(void);

u16 OotPortControls_MapButtons(u32 pspButtons);
s8 OotPortControls_MapStick(u8 raw);

int OotPortControls_GetBindingCount(void);
const char* OotPortControls_GetBindingName(int index);
void OotPortControls_GetBindingValueText(int index, char* buffer, size_t bufferSize);
void OotPortControls_CycleBinding(int index, int direction);
int OotPortControls_GetDeadzone(void);
void OotPortControls_SetDeadzone(int deadzone);
void OotPortControls_AdjustDeadzone(int delta);

#endif
