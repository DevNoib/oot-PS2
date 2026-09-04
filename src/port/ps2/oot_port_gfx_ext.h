#ifndef OOT_PORT_GFX_EXT_H
#define OOT_PORT_GFX_EXT_H

#include "ultra64/gbi.h"

#define OOT_PORT_HUD_ANCHOR_TAG 0x48554400U
#define OOT_PS2_PAUSE_BG_CAPTURE_TAG 0x50534247U

typedef enum OotPortHudAnchor {
    OOT_PORT_HUD_ANCHOR_NONE,
    OOT_PORT_HUD_ANCHOR_LEFT,
    OOT_PORT_HUD_ANCHOR_CENTER,
    OOT_PORT_HUD_ANCHOR_RIGHT,
} OotPortHudAnchor;

#define gOotPortSetHudAnchor(pkt, anchor) gDPNoOpTag((pkt), OOT_PORT_HUD_ANCHOR_TAG | (anchor))
#define gOotPortCapturePauseBackground(pkt) gDPNoOpTag((pkt), OOT_PS2_PAUSE_BG_CAPTURE_TAG)

#endif
