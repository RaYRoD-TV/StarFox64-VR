// Native in-game VR options overlay for the Star Fox 64 VR build: a pause + button menu.
// Drawn with the game's own large-font text (Graphics_DisplayLargeText) so it lands on the VR
// head-locked panel automatically, and edits the same VR CVars as the desktop Enhancements > VR menu.
//
// Open it while PAUSED by pulling the RIGHT TRIGGER on the motion controllers. Navigate rows with the left
// stick up/down, change a value with the stick left/right, and press A to activate DEFAULTS / RESUME or
// cycle VIEW MODE. The right trigger or B closes it. It is only reachable while the game is paused, so it
// can never fight live flight input, and it force-closes the instant you unpause - there is no way to get
// stuck in it.
#include "global.h"
#include <stdio.h>
#include <string.h>
#include "vr_menu.h"
#include "port/vr/vr.h"

// The game's 2D large-font string renderer (fox_std_lib.c) - charset is A-Z, 0-9, space, '.', '-'.
extern void Graphics_DisplayLargeText(s32 xPos, s32 yPos, f32 xScale, f32 yScale, char* text);
// Filled 2D rectangle (fox_std_lib.c) - used for the menu's dark backing panel.
extern void Graphics_FillRectangle(Gfx** gfxPtr, s32 ulx, s32 uly, s32 lrx, s32 lry, u8 r, u8 g, u8 b, u8 a);
// libultraship console-variable bridge.
extern f32 CVarGetFloat(const char* name, f32 defaultValue);
extern void CVarSetFloat(const char* name, f32 value);
extern s32 CVarGetInteger(const char* name, s32 defaultValue);
extern void CVarSetInteger(const char* name, s32 value);
// The N64 pads (sys_joybus.c). Player 1 drives the menu.
extern OSContPad gControllerHold[4];
extern OSContPad gControllerPress[4];

enum {
    ROW_VIEWMODE,
    ROW_SCALE,    // mode-aware: Third/First/Diorama world scale
    ROW_STEREO,
    ROW_HUDSIZE,
    ROW_HUDDIST,
    ROW_SKYDOME,
    ROW_DEFAULTS,
    ROW_RESUME,
    ROW_COUNT
};

static const char* const kRowLabel[ROW_COUNT] = {
    "VIEW MODE", "WORLD SCALE", "STEREO", "HUD SIZE", "HUD DIST", "SKY DOME", "DEFAULTS", "RESUME",
};
static const char* const kViewModeName[4] = { "THIRD PERSON", "FIRST PERSON", "THEATER", "DIORAMA" };

static s32 sOpen = 0;
static s32 sSel = 0;
static s32 sNavCd = 0; // frames until the held stick can step again (repeat throttle)

// Mode-aware scale row: which CVar + range the WORLD SCALE row edits for the current view mode.
static const char* scale_cvar(f32* lo, f32* hi, f32* step, f32* def) {
    s32 vm = CVarGetInteger("gVRViewMode", 0);
    if (vm == 1) { *lo = 5.0f;  *hi = 500.0f;  *step = 5.0f; *def = 25.0f;  return "gVRFirstPersonScale"; }
    if (vm == 3) { *lo = 20.0f; *hi = 2000.0f; *step = 5.0f; *def = 800.0f; return "gVRDioramaWorldScale"; }
    *lo = 5.0f; *hi = 2000.0f; *step = 5.0f; *def = 25.0f;
    return "gVRWorldScale";
}

static void adjustf(const char* name, f32 def, f32 lo, f32 hi, f32 step, s32 dir) {
    f32 v = CVarGetFloat(name, def) + (f32) dir * step;
    if (v < lo) {
        v = lo;
    }
    if (v > hi) {
        v = hi;
    }
    CVarSetFloat(name, v);
}

// Change the value on the current row by dir (-1 / +1). A on an action row activates it (dir ignored).
static void row_change(s32 row, s32 dir, s32 activate) {
    f32 lo, hi, step, def;
    const char* cv;
    switch (row) {
        case ROW_VIEWMODE: {
            s32 vm = CVarGetInteger("gVRViewMode", 0) + (activate ? 1 : dir);
            vm &= 3; // wrap 0..3
            CVarSetInteger("gVRViewMode", vm);
            break;
        }
        case ROW_SCALE:
            cv = scale_cvar(&lo, &hi, &step, &def);
            adjustf(cv, def, lo, hi, step, dir);
            break;
        case ROW_STEREO:
            adjustf("gVRStereo", 0.5f, 0.0f, 1.5f, 0.05f, dir);
            break;
        case ROW_HUDSIZE:
            adjustf("gVRHudScale", 0.35f, 0.2f, 1.2f, 0.05f, dir);
            break;
        case ROW_HUDDIST:
            adjustf("gVRHudDist", 2.9f, 0.5f, 8.0f, 0.1f, dir);
            break;
        case ROW_SKYDOME:
            CVarSetInteger("gVRSkyDome", CVarGetInteger("gVRSkyDome", 1) ? 0 : 1);
            break;
        case ROW_DEFAULTS:
            if (activate) {
                CVarSetInteger("gVRViewMode", 0);
                CVarSetFloat("gVRWorldScale", 25.0f);
                CVarSetFloat("gVRFirstPersonScale", 25.0f);
                CVarSetFloat("gVRDioramaWorldScale", 800.0f);
                CVarSetFloat("gVRStereo", 0.5f);
                CVarSetFloat("gVRHudScale", 0.35f);
                CVarSetFloat("gVRHudDist", 2.9f);
                CVarSetInteger("gVRSkyDome", 1);
            }
            break;
        case ROW_RESUME:
            if (activate) {
                sOpen = 0;
            }
            break;
    }
}

// Blank pad 0 for the rest of the frame so neither the pause screen nor gameplay reacts while the menu
// owns input. Safe because the game is frozen at PLAY_PAUSE. Also clears the MERGED pad, so the VR
// controls that opened/drive the menu (which get mapped onto the N64 pad) don't leak into the pause UI.
static void consume_pad0(void) {
    gControllerPress[0].button = 0;
    gControllerPress[0].stick_x = gControllerPress[0].stick_y = 0;
    gControllerHold[0].button = 0;
    gControllerHold[0].stick_x = gControllerHold[0].stick_y = 0;
}

static unsigned sPrevBtns = 0; // last frame's VR button mask, for edge detection

// The native VR menu is driven DIRECTLY off the motion controllers (not the merged N64 pad), so the
// trigger meaning is unambiguous and the pause screen never competes for the same buttons. Opened while
// paused with the RIGHT TRIGGER. Left stick navigates, A activates,
// right trigger / B closes. When open it renders on the flat panel (VrMenu_IsOpen -> Engine.cpp) and the
// pause screen's own UI is suppressed (fox_hud.c), so it stands alone.
void VrMenu_Update(void) {
    unsigned btns, edge;
    float ls[2];
    s32 up, down, left, right, activate, close;

    // Requires live motion controllers; without them use the desktop Enhancements > VR menu instead.
    if (!vr_is_active() || !vr_controllers_active()) {
        sOpen = 0;
        sPrevBtns = 0;
        return;
    }
    // Only reachable while paused -> never competes with live flight input, and auto-closes on unpause.
    if (gPlayState != PLAY_PAUSE) {
        sOpen = 0;
        sPrevBtns = vr_controller_buttons();
        return;
    }

    btns = vr_controller_buttons();
    edge = btns & ~sPrevBtns; // freshly pressed this frame
    sPrevBtns = btns;

    if (!sOpen) {
        if (edge & VR_BTN_RTRIGGER) { // RIGHT TRIGGER opens it while paused
            sOpen = 1;
            sSel = 0;
            sNavCd = 0;
            consume_pad0(); // swallow the trigger's merged press so the pause screen doesn't see it
        }
        return;
    }

    // --- navigation: throttled repeat from the left stick, actions on the edge ---
    if (sNavCd > 0) {
        sNavCd--;
    }
    vr_controller_stick(0, ls); // left stick, -1..1 (+y up)
    up = down = left = right = 0;
    if (sNavCd == 0) {
        if (ls[1] > 0.5f) { up = 1; }
        else if (ls[1] < -0.5f) { down = 1; }
        else if (ls[0] < -0.5f) { left = 1; }
        else if (ls[0] > 0.5f) { right = 1; }
        if (up || down || left || right) {
            sNavCd = 8; // ~0.13s between repeats
        }
    }
    activate = (edge & VR_BTN_A) != 0;
    close = (edge & (VR_BTN_RTRIGGER | VR_BTN_B)) != 0;

    if (up) {
        sSel = (sSel + ROW_COUNT - 1) % ROW_COUNT;
    }
    if (down) {
        sSel = (sSel + 1) % ROW_COUNT;
    }
    if (left) {
        row_change(sSel, -1, 0);
    }
    if (right) {
        row_change(sSel, +1, 0);
    }
    if (activate) {
        row_change(sSel, 0, 1);
    }
    if (close) {
        sOpen = 0;
    }

    consume_pad0();
}

// Format the value shown on a row into buf.
static void row_value(s32 row, char* buf, s32 bufLen) {
    f32 lo, hi, step, def;
    switch (row) {
        case ROW_VIEWMODE:
            snprintf(buf, bufLen, "%s", kViewModeName[CVarGetInteger("gVRViewMode", 0) & 3]);
            break;
        case ROW_SCALE:
            snprintf(buf, bufLen, "%.0f", CVarGetFloat(scale_cvar(&lo, &hi, &step, &def), def));
            break;
        case ROW_STEREO:
            snprintf(buf, bufLen, "%.2f", CVarGetFloat("gVRStereo", 0.5f));
            break;
        case ROW_HUDSIZE:
            snprintf(buf, bufLen, "%.2f", CVarGetFloat("gVRHudScale", 0.35f));
            break;
        case ROW_HUDDIST:
            snprintf(buf, bufLen, "%.1f", CVarGetFloat("gVRHudDist", 2.9f));
            break;
        case ROW_SKYDOME:
            snprintf(buf, bufLen, "%s", CVarGetInteger("gVRSkyDome", 1) ? "ON" : "OFF");
            break;
        default:
            buf[0] = 0;
            break;
    }
}

void VrMenu_Draw(void) {
    s32 i;
    s32 y;
    char line[48];
    char value[24];

    if (!vr_is_active()) {
        return;
    }
    if (!sOpen) {
        // Hint on the pause screen so the menu is discoverable in the headset.
        if (gPlayState == PLAY_PAUSE && vr_controllers_active()) {
            Graphics_DisplayLargeText(60, 210, 0.6f, 0.6f, "R TRIGGER - VR OPTIONS");
        }
        return;
    }

    // Subtle dark panel behind the text so the menu reads against the bright sky/world. Semi-transparent
    // so the paused game still shows through. Drawn first, then the text on top.
    Graphics_FillRectangle(&gMasterDisp, 36, 28, 286, 210, 8, 10, 26, 165);

    Graphics_DisplayLargeText(108, 40, 0.8f, 0.8f, "VR OPTIONS");
    y = 66;
    for (i = 0; i < ROW_COUNT; i++) {
        row_value(i, value, sizeof(value));
        // A leading '-' marks the selected row (the font has no '>' glyph); action rows show no value.
        if (value[0] != 0) {
            snprintf(line, sizeof(line), "%s%s  %s", (i == sSel) ? "- " : "  ", kRowLabel[i], value);
        } else {
            snprintf(line, sizeof(line), "%s%s", (i == sSel) ? "- " : "  ", kRowLabel[i]);
        }
        Graphics_DisplayLargeText(60, y, 0.7f, 0.7f, line);
        y += 18;
    }
}

s32 VrMenu_IsOpen(void) {
    return sOpen;
}
