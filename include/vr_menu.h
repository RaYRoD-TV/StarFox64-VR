#ifndef VR_MENU_H
#define VR_MENU_H

#include "sf64thread.h"

// Native in-game VR options overlay: a pause + button menu. Drawn with the game's own
// large-font text so it lands on the VR head-locked panel automatically, and edits the same VR CVars as the
// desktop Enhancements > VR menu. Opened while PAUSED by pulling the RIGHT TRIGGER on the motion controllers,
// so you can tune view mode / world scale / stereo / HUD without leaving the headset for the desktop.

// Per-frame input + state. Call once each frame from the pause update, BEFORE the pause screen reads input;
// while the menu is open it consumes pad 0 so the pause screen doesn't also navigate. No-op outside VR.
void VrMenu_Update(void);

// Draw the overlay (when open) and the "R TRIGGER - VR OPTIONS" hint (when paused, closed). Call at the end
// of the HUD draw so it sits on top. No-op outside VR / when there's nothing to show.
void VrMenu_Draw(void);

// True while the overlay is open - the VR engine loop routes the frame to the flat panel so the menu is
// stable and readable instead of riding the in-world HUD plane.
s32 VrMenu_IsOpen(void);

#endif // VR_MENU_H
