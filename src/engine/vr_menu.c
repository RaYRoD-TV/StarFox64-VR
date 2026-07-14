// Native in-game VR options overlay for the Star Fox 64 VR build: a pause + button menu.
// Drawn with the game's own large-font text (Graphics_DisplayLargeText) so it lands on the VR
// head-locked panel automatically, and edits the same VR CVars as the desktop Enhancements > VR menu.
//
// Open it while PAUSED with the RIGHT TRIGGER on the motion controllers, or the R button on a regular
// gamepad. Stick (or D-pad) up/down moves the highlight (the list SCROLLS - it holds more rows than fit),
// left/right changes the highlighted value, and A activates DEFAULTS / RESUME or cycles VIEW MODE. The
// opening button or B closes it. It is only reachable while paused, so it can never fight live flight
// input, and it force-closes the instant you unpause - there is no way to get stuck in it.
#include "global.h"
#include <stdio.h>
#include <string.h>
#include "vr_menu.h"
#include "port/vr/vr.h"
#include "properties.h" // VR_PORT_VERSION_CAPS - the release version shown in the menu header

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

// Row kinds for the data-driven menu table.
enum { RT_MODE, RT_FLOATV, RT_TOGGLE, RT_ACTION, RT_FOG };

typedef struct {
    const char* label;
    const char* cvar; // NULL for RT_ACTION
    u8 type;
    f32 lo, hi, step, def;
} VrMenuRow;

// Every VR option, in one scrollable list. Keep labels within the large font's charset (A-Z 0-9 space . -).
static const VrMenuRow kRows[] = {
    { "VIEW MODE",     "gVRViewMode",             RT_MODE,   0, 0, 0, 0 },
    { "WORLD SCALE",   "gVRWorldScale",           RT_FLOATV, 5.0f, 2000.0f, 5.0f, 25.0f },
    { "3RD PERS DIST", "gVRThirdPersonDist",      RT_FLOATV, -15.0f, 50.0f, 0.5f, 0.0f },
    { "EYE HEIGHT",    "gVREyeHeight",            RT_FLOATV, -1.0f, 1.0f, 0.02f, 0.16f },
    { "FP FORWARD",    "gVRFirstPersonFwd",       RT_FLOATV, -30.0f, 30.0f, 0.5f, 14.0f },
    { "FP SCALE",      "gVRFirstPersonScale",     RT_FLOATV, 5.0f, 500.0f, 5.0f, 25.0f },
    { "FP EYE HT",     "gVRFirstPersonEyeHeight", RT_FLOATV, -1.0f, 1.0f, 0.02f, 0.0f },
    { "FLIP CAM",      "gVRFlipCam",              RT_TOGGLE, 0, 0, 0, 1 },
    { "LOOP CAM",      "gVRLoopCam",              RT_TOGGLE, 0, 0, 0, 1 },
    { "RS TURN",       "gVRRightStickTurn",       RT_TOGGLE, 0, 0, 0, 0 },
    { "COCKPIT FWD",   "gVRCockpitFwd",           RT_FLOATV, -5.0f, 20.0f, 0.25f, 0.0f },
    { "COCKPIT HT",    "gVRCockpitHeight",        RT_FLOATV, -2.0f, 5.0f, 0.05f, -0.15f },
    { "COCKPIT GLASS", "gVRCockpitGlass",         RT_TOGGLE, 0, 0, 0, 1 },
    { "COMM SCREEN",   "gVRCockpitComm",          RT_TOGGLE, 0, 0, 0, 1 },
    { "VR CUTSCENES",  "gVRCutscenes",            RT_TOGGLE, 0, 0, 0, 0 },
    { "DIORAMA DIST",  "gVRDioramaDist",          RT_FLOATV, 0.1f, 3.0f, 0.01f, 0.25f },
    { "DIORAMA SCALE", "gVRDioramaWorldScale",    RT_FLOATV, 20.0f, 2000.0f, 5.0f, 800.0f },
    { "DIORAMA HT",    "gVRDioramaHeight",        RT_FLOATV, -1.5f, 1.0f, 0.01f, -0.16f },
    { "STEREO",        "gVRStereo",               RT_FLOATV, 0.0f, 1.5f, 0.05f, 0.5f },
    { "HUD SIZE",      "gVRHudScale",             RT_FLOATV, 0.2f, 1.2f, 0.05f, 0.35f },
    { "HUD DIST",      "gVRHudDist",              RT_FLOATV, 0.5f, 8.0f, 0.1f, 2.9f },
    { "HIDE HUD",      "gVRHideHud",              RT_TOGGLE, 0, 0, 0, 0 },
    { "SKY DOME",      "gVRSkyDome",              RT_TOGGLE, 0, 0, 0, 1 },
    { "BACKDROP",      "gVRLevelBackdrop",        RT_TOGGLE, 0, 0, 0, 1 },
    { "SKY BRIGHT",    "gVRSkyBright",            RT_FLOATV, 0.2f, 2.0f, 0.05f, 1.0f },
    { "CLOUD ALPHA",   "gVRCloudAlpha",           RT_FLOATV, 0.0f, 3.0f, 0.1f, 1.0f },
    { "CLOUD COVER",   "gVRCloudCover",           RT_FLOATV, 0.0f, 1.0f, 0.05f, 1.0f },
    { "FOG",           "gVRFogMode",              RT_FOG,    0, 0, 0, 1 },
    { "FOG NEAR",      "gVRFogNear",              RT_FLOATV, 0.0f, 2000.0f, 25.0f, 150.0f },
    { "FOG FAR",       "gVRFogFar",               RT_FLOATV, 50.0f, 5000.0f, 25.0f, 500.0f },
    { "NO BACKFACE",   "gVRDisableCulling",       RT_TOGGLE, 0, 0, 0, 0 },
    { "DRAW DIST",     "gVRDrawDistance",         RT_FLOATV, 1.0f, 8.0f, 0.5f, 1.0f },
    { "RESOLUTION",    "gInternalResolution",     RT_FLOATV, 0.5f, 4.0f, 0.1f, 1.0f },
    { "COCKPIT FLOOR", "gVRCockpitFloor",         RT_TOGGLE, 0, 0, 0, 1 },
    { "FLOOR HEIGHT",  "gVRCockpitFloorY",        RT_FLOATV, -50.0f, 20.0f, 1.0f, -6.0f },
    { "DEFAULTS",      NULL,                      RT_ACTION, 0, 0, 0, 0 },
    { "RESUME",        NULL,                      RT_ACTION, 0, 0, 0, 0 },
};
#define ROW_COUNT ((s32) (sizeof(kRows) / sizeof(kRows[0])))
#define VR_VIEW_MODE_COUNT 5
#define VISIBLE_ROWS 8 // rows shown at once; the list scrolls to keep the highlight in view

// Indexed by VrViewMode (vr.h): 0=Third, 1=First, 2=Cockpit, 3=Diorama, 4=Theater.
static const char* const kViewModeName[VR_VIEW_MODE_COUNT] = { "THIRD PERSON", "FIRST PERSON", "COCKPIT",
                                                               "DIORAMA", "THEATER" };

// Indexed by gVRFogMode: 0=off, 1=world-distance fog (the VR rebuild), 2=the game's stock fog (drowns the
// flying modes under the VR projection - kept selectable for comparison). Theater/Diorama always use stock.
#define VR_FOG_MODE_COUNT 3
static const char* const kFogModeName[VR_FOG_MODE_COUNT] = { "OFF", "WORLD", "STOCK" };

static s32 sOpen = 0;
static s32 sSel = 0;
static s32 sScroll = 0; // index of the first visible row
static s32 sNavCd = 0;  // frames until the held stick can step again (repeat throttle)

// Reset every editable row to its default value.
static void reset_defaults(void) {
    s32 i;
    for (i = 0; i < ROW_COUNT; i++) {
        const VrMenuRow* r = &kRows[i];
        if (r->type == RT_FLOATV) {
            CVarSetFloat(r->cvar, r->def);
        } else if ((r->type == RT_TOGGLE) || (r->type == RT_MODE) || (r->type == RT_FOG)) {
            CVarSetInteger(r->cvar, (s32) r->def);
        }
    }
}

// Change the value on the current row by dir (-1 / +1). A on an action row activates it (dir ignored).
static void row_change(s32 row, s32 dir, s32 activate) {
    const VrMenuRow* r = &kRows[row];
    switch (r->type) {
        case RT_MODE: {
            s32 vm = CVarGetInteger(r->cvar, 0) + (activate ? 1 : dir);
            vm = (vm % VR_VIEW_MODE_COUNT + VR_VIEW_MODE_COUNT) % VR_VIEW_MODE_COUNT; // wrap 0..4
            CVarSetInteger(r->cvar, vm);
            break;
        }
        case RT_FOG: {
            s32 fm = CVarGetInteger(r->cvar, (s32) r->def) + (activate ? 1 : dir);
            fm = (fm % VR_FOG_MODE_COUNT + VR_FOG_MODE_COUNT) % VR_FOG_MODE_COUNT; // wrap 0..2
            CVarSetInteger(r->cvar, fm);
            break;
        }
        case RT_FLOATV: {
            f32 v = CVarGetFloat(r->cvar, r->def) + (f32) dir * r->step;
            if (v < r->lo) {
                v = r->lo;
            }
            if (v > r->hi) {
                v = r->hi;
            }
            CVarSetFloat(r->cvar, v);
            break;
        }
        case RT_TOGGLE:
            CVarSetInteger(r->cvar, CVarGetInteger(r->cvar, (s32) r->def) ? 0 : 1);
            break;
        case RT_ACTION:
            if (activate) {
                if (strcmp(r->label, "RESUME") == 0) {
                    sOpen = 0;
                } else {
                    reset_defaults();
                }
            }
            break;
    }
}

// Format the value shown on a row into buf.
static void row_value(s32 row, char* buf, s32 bufLen) {
    const VrMenuRow* r = &kRows[row];
    switch (r->type) {
        case RT_MODE: {
            s32 vm = CVarGetInteger(r->cvar, 0);
            if ((vm < 0) || (vm >= VR_VIEW_MODE_COUNT)) {
                vm = 0;
            }
            snprintf(buf, bufLen, "%s", kViewModeName[vm]);
            break;
        }
        case RT_FLOATV: {
            f32 v = CVarGetFloat(r->cvar, r->def);
            if (r->step >= 1.0f) {
                snprintf(buf, bufLen, "%.0f", v);
            } else if (r->step >= 0.1f) {
                snprintf(buf, bufLen, "%.1f", v);
            } else {
                snprintf(buf, bufLen, "%.2f", v);
            }
            break;
        }
        case RT_TOGGLE:
            snprintf(buf, bufLen, "%s", CVarGetInteger(r->cvar, (s32) r->def) ? "ON" : "OFF");
            break;
        case RT_FOG: {
            s32 fm = CVarGetInteger(r->cvar, (s32) r->def);
            if ((fm < 0) || (fm >= VR_FOG_MODE_COUNT)) {
                fm = 1;
            }
            snprintf(buf, bufLen, "%s", kFogModeName[fm]);
            break;
        }
        default:
            buf[0] = 0;
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

// Blank only the derived PRESS state for this tick. Unlike consume_pad0 this leaves the hold state
// alone, so sys_joybus's next edge derivation sees the true previous frame and the phantom-press
// chain a zeroed hold state manufactures ends here instead of re-arming every tick.
static void swallow_press0(void) {
    gControllerPress[0].button = 0;
    gControllerPress[0].stick_x = gControllerPress[0].stick_y = 0;
}

static unsigned sPrevBtns = 0; // last frame's VR button mask, for edge detection
static u16 sPrevPadBtns = 0;   // last frame's RAW gamepad hold mask - our own edge base. gControllerPress
                               // can't be used here: sys_joybus derives it with gControllerHold as the
                               // previous state, and consume_pad0 zeroes that - so a HELD button would
                               // read as freshly pressed every tick and R would flap the menu open/closed.
static s32 sSwallowTick = 0;   // 1 = the menu closed last tick; swallow one more pad tick, because the
                               // zeroed hold state makes sys_joybus re-report every still-held button as
                               // a fresh press (R would toggle the pause screen's reticles on close).

// The native VR menu reads its own inputs (not the pause screen's), so the trigger meaning is unambiguous
// and the pause UI never competes for the same buttons. With motion controllers: opened while paused with
// the RIGHT TRIGGER, left stick navigates, A activates, right trigger / B closes. With a regular gamepad
// the merged N64 pad drives the same scheme: R opens/closes, stick or D-pad navigates, A activates, B
// closes. When open it renders on the flat panel (VrMenu_IsOpen -> Engine.cpp) and the pause screen's own
// UI is suppressed (fox_hud.c), so it stands alone.
void VrMenu_Update(void) {
    unsigned btns, edge;
    float ls[2];
    s32 up, down, left, right, activate, close, openMenu;
    s32 vrPads;

    if (!vr_is_active()) {
        sOpen = 0;
        sPrevBtns = 0;
        sPrevPadBtns = 0;
        sSwallowTick = 0;
        return;
    }
    vrPads = vr_controllers_active();
    // Only reachable while paused -> never competes with live flight input, and auto-closes on unpause.
    // Tracking the raw hold mask here means pausing with a button already down can't edge-fire the menu.
    if (gPlayState != PLAY_PAUSE) {
        sOpen = 0;
        sPrevBtns = vrPads ? vr_controller_buttons() : 0;
        sPrevPadBtns = gControllerHold[0].button;
        sSwallowTick = 0;
        return;
    }

    if (vrPads) {
        btns = vr_controller_buttons();
        edge = btns & ~sPrevBtns; // freshly pressed this frame
        sPrevBtns = btns;
        sPrevPadBtns = gControllerHold[0].button;
        openMenu = (edge & VR_BTN_RTRIGGER) != 0;
        activate = (edge & VR_BTN_A) != 0;
        close = (edge & (VR_BTN_RTRIGGER | VR_BTN_B)) != 0;
        vr_controller_stick(0, ls); // left stick, -1..1 (+y up)
    } else {
        // Regular gamepad: the merged N64 pad, edge-detected against our OWN previous-frame copy of
        // the raw hold mask (see sPrevPadBtns above for why gControllerPress is off limits). D-pad
        // holds fold into the stick axes and share the same repeat throttle.
        u16 hold = gControllerHold[0].button;
        u16 padEdge = hold & ~sPrevPadBtns;
        sPrevPadBtns = hold;
        sPrevBtns = 0;
        openMenu = (padEdge & R_TRIG) != 0;
        activate = (padEdge & A_BUTTON) != 0;
        close = (padEdge & (R_TRIG | B_BUTTON)) != 0;
        ls[0] = gControllerHold[0].stick_x / 80.0f; // N64 stick range ~ +-80, +y up
        ls[1] = gControllerHold[0].stick_y / 80.0f;
        if (hold & L_JPAD) {
            ls[0] = -1.0f;
        }
        if (hold & R_JPAD) {
            ls[0] = 1.0f;
        }
        if (hold & U_JPAD) {
            ls[1] = 1.0f;
        }
        if (hold & D_JPAD) {
            ls[1] = -1.0f;
        }
    }

    if (!sOpen) {
        if (sSwallowTick) {
            sSwallowTick = 0;
            swallow_press0(); // the phantom-press tick right after closing (see sSwallowTick above)
        }
        if (openMenu) { // right trigger (or gamepad R) opens it while paused
            sOpen = 1;
            sSel = 0;
            sScroll = 0;
            sNavCd = 0;
            consume_pad0(); // swallow the opening press so the pause screen doesn't see it
        }
        return;
    }

    // --- navigation: throttled repeat from the stick, actions on the edge ---
    if (sNavCd > 0) {
        sNavCd--;
    }
    up = down = left = right = 0;
    if (sNavCd == 0) {
        if (ls[1] > 0.5f) {
            up = 1;
        } else if (ls[1] < -0.5f) {
            down = 1;
        } else if (ls[0] < -0.5f) {
            left = 1;
        } else if (ls[0] > 0.5f) {
            right = 1;
        }
        if (up || down || left || right) {
            sNavCd = 8; // ~0.13s between repeats
        }
    }

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
    if (!sOpen) {
        sSwallowTick = 1; // closed this tick (button or RESUME) - guard the phantom-press tick that follows
    }

    consume_pad0();
}

void VrMenu_Draw(void) {
    s32 i;
    s32 y;
    char value[24];

    if (!vr_is_active()) {
        return;
    }
    if (!sOpen) {
        // Hint on the pause screen so the menu is discoverable in the headset. Sits on the very bottom
        // edge, below the teammate status row (which reaches ~y 215), so it can't overlap the stock
        // pause text; the dark strip keeps it readable over the world. Establish the large-text
        // pipeline so the glyphs render regardless of what the HUD left in the combiner.
        if (gPlayState == PLAY_PAUSE) {
            Graphics_FillRectangle(&gMasterDisp, 58, 221, 262, 236, 8, 10, 26, 150);
            RCP_SetupDL(&gMasterDisp, SETUPDL_83_POINT);
            gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);
            Graphics_DisplayLargeText(64, 225, 0.5f, 0.5f,
                                      vr_controllers_active() ? "R TRIGGER - VR OPTIONS" : "R BUTTON - VR OPTIONS");
        }
        return;
    }

    // Keep the highlight inside the visible window (scroll the list).
    if (sSel < sScroll) {
        sScroll = sSel;
    }
    if (sSel >= sScroll + VISIBLE_ROWS) {
        sScroll = sSel - VISIBLE_ROWS + 1;
    }
    if (sScroll > ROW_COUNT - VISIBLE_ROWS) {
        sScroll = ROW_COUNT - VISIBLE_ROWS;
    }
    if (sScroll < 0) {
        sScroll = 0;
    }

    // Dark panel behind the text so the menu reads against the bright sky/world. Semi-transparent so the
    // LIVE stereo view renders through it - every option change (view mode, scale, stereo, sky, fog, ...) is
    // visible behind the menu in real time. The text is drawn on top at full opacity, so it stays readable.
    Graphics_FillRectangle(&gMasterDisp, 22, 24, 306, 214, 8, 10, 26, 150);

    // Graphics_FillRectangle leaves a flat PRIMITIVE combine behind; the IA8 glyph primitive doesn't set its
    // own, so without restoring the large-text pipeline here every character samples nothing and draws as the
    // panel's flat colour (invisible). Re-establish it, then colour each line via the prim register.
    RCP_SetupDL(&gMasterDisp, SETUPDL_83_POINT);

    gDPSetPrimColor(gMasterDisp++, 0, 0, 190, 215, 255, 255);
    Graphics_DisplayLargeText(112, 30, 0.72f, 0.72f, "VR OPTIONS");
    // Release version in the corner, so "which build is this?" is answerable from any headset screenshot.
    gDPSetPrimColor(gMasterDisp++, 0, 0, 150, 160, 190, 255);
    Graphics_DisplayLargeText(262, 33, 0.45f, 0.45f, VR_PORT_VERSION_CAPS);

    // "..." markers when there are rows scrolled off above / below the window.
    if (sScroll > 0) {
        gDPSetPrimColor(gMasterDisp++, 0, 0, 150, 160, 190, 255);
        Graphics_DisplayLargeText(158, 44, 0.5f, 0.5f, "...");
    }

    // Label in a left column, value in a fixed right column, each drawn on its own so a long value can't push
    // a combined line off the panel edge. The highlighted row is cyan.
    y = 54;
    for (i = sScroll; (i < sScroll + VISIBLE_ROWS) && (i < ROW_COUNT); i++) {
        row_value(i, value, sizeof(value));
        if (i == sSel) {
            gDPSetPrimColor(gMasterDisp++, 0, 0, 120, 225, 255, 255);
        } else {
            gDPSetPrimColor(gMasterDisp++, 0, 0, 235, 235, 235, 255);
        }
        Graphics_DisplayLargeText(40, y, 0.55f, 0.55f, (char*) kRows[i].label);
        if (value[0] != 0) {
            Graphics_DisplayLargeText(170, y, 0.55f, 0.55f, value);
        }
        y += 18;
    }

    if (sScroll + VISIBLE_ROWS < ROW_COUNT) {
        gDPSetPrimColor(gMasterDisp++, 0, 0, 150, 160, 190, 255);
        Graphics_DisplayLargeText(158, 202, 0.5f, 0.5f, "...");
    }
}

s32 VrMenu_IsOpen(void) {
    return sOpen;
}
