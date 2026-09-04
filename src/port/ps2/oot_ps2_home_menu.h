#ifndef OOT_PS2_HOME_MENU_H
#define OOT_PS2_HOME_MENU_H

#include <stdbool.h>

typedef enum OotPs2HomeMenuResult {
    OOT_PS2_HOME_MENU_RESULT_NONE,
    OOT_PS2_HOME_MENU_RESULT_MAP_SELECT,
    OOT_PS2_HOME_MENU_RESULT_EXIT_GAME,
} OotPs2HomeMenuResult;

void OotPs2HomeMenu_Init(void);
bool OotPs2HomeMenu_PollToggle(void);
bool OotPs2HomeMenu_IsOpen(void);
OotPs2HomeMenuResult OotPs2HomeMenu_RunFrame(void);

#endif
