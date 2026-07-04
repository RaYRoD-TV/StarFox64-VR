#include "sys.h"
#include <math.h>
#include "port/vr/vr.h"

OSContPad gControllerHold[4];
OSContPad gControllerPress[4];
u8 gControllerPlugged[4];
u32 gControllerLock;
u8 gControllerRumbleEnabled[4];
OSContPad sNextController[4];
OSContPad sPrevController[4];
OSContStatus sControllerStatus[4];
OSPfs sControllerMotor[4];

void Controller_AddDeadZone(s32 contrNum) {
    s32 temp_v0 = gControllerHold[contrNum].stick_x;
    s32 temp_a2 = gControllerHold[contrNum].stick_y;
    s32 var_a0;
    s32 var_v0;

    if ((temp_v0 >= -16) && (temp_v0 <= 16)) {
        var_a0 = 0;
    } else if (temp_v0 > 16) {
        var_a0 = temp_v0 - 16;
    } else {
        var_a0 = temp_v0 + 16;
    }

    if ((temp_a2 >= -16) && (temp_a2 <= 16)) {
        var_v0 = 0;
    } else if (temp_a2 > 16) {
        var_v0 = temp_a2 - 16;
    } else {
        var_v0 = temp_a2 + 16;
    }

    if (var_a0 > 60) {
        var_a0 = 60;
    }
    if (var_a0 < -60) {
        var_a0 = -60;
    }
    if (var_v0 > 60) {
        var_v0 = 60;
    }
    if (var_v0 < -60) {
        var_v0 = -60;
    }
    gControllerPress[contrNum].stick_x = var_a0;
    gControllerPress[contrNum].stick_y = var_v0;
}

void Controller_Init(void) {
    u8 sp1F;
    s32 i;

    osContInit(&gSerialEventQueue, &sp1F, sControllerStatus);
    for (i = 0; i < 4; i++) {
        gControllerPlugged[i] = (sp1F >> i) & 1;
        gControllerRumbleEnabled[i] = 0;
    }
}

// LTODO: Fix this
#define osContGetStatus(x) true

// VR motion controllers (OpenXR) as the first pad. Merged into sNextController[0] AHEAD of the
// edge detection below, so every consumer - flight input, the stock menus, the map screen - sees
// them exactly like a physical pad, fresh-press edges included. The layout is flight-shaped:
//   left stick            flight stick (and menu navigation)
//   right trigger / A     fire laser, hold to charge (A)
//   left trigger / B      smart bomb (B)
//   left grip             bank left  (Z; double-squeeze = barrel roll left)
//   right grip            bank right (R; double-squeeze = barrel roll right)
//   right stick up        boost   (the game's stock boost button)
//   right stick down      brake
//   right stick click / X cockpit view toggle (C-Up)
//   left stick click      open / close the settings menu (handled in the engine, not here)
//   menu button           pause (Start)
// Per axis the stronger source wins, so a gamepad on the desk stays usable alongside the
// motion controllers.
bool GameEngine_GameInputBlocked(void); // src/port/Engine.cpp - true while the settings menu owns input
static void Controller_MergeVr(void) {
    unsigned vr = vr_controller_buttons();
    float ls[2];
    float rs[2];
    float m;
    u16 b = 0;
    s8 vx, vy;

    if (!vr_controllers_active()) {
        return;
    }
    // The settings menu is open with controller nav: the sticks drive the menu (fed to it by the
    // engine), not the ship. Physical pads are blocked by the same predicate inside libultraship.
    if (GameEngine_GameInputBlocked()) {
        return;
    }
    if (vr & (VR_BTN_A | VR_BTN_RTRIGGER)) {
        b |= A_BUTTON; // fire laser / charge (menu select)
    }
    if (vr & (VR_BTN_B | VR_BTN_LTRIGGER)) {
        b |= B_BUTTON; // smart bomb (menu back)
    }
    if (vr & VR_BTN_LGRIP) {
        b |= Z_TRIG; // bank left; double-squeeze = barrel roll left
    }
    if (vr & VR_BTN_RGRIP) {
        b |= R_TRIG; // bank right; double-squeeze = barrel roll right
    }
    if (vr & VR_BTN_MENU) {
        b |= START_BUTTON;
    }
    // Cockpit view toggle (C-Up). Suppressed in Diorama mode - there you're looking at the shrunk-down
    // tabletop, not sitting in the ship, so flipping the game's cockpit camera just jars the framing.
    if ((vr & (VR_BTN_RSTICK | VR_BTN_X)) && vr_get_view_mode() != VR_VIEW_DIORAMA) {
        b |= U_CBUTTONS;
    }

    vr_controller_stick(0, ls);
    vr_controller_stick(1, rs);

    // Right stick: forward = boost, back = brake (the stock C-Left / C-Down assignments).
    if (rs[1] > 0.5f) {
        b |= L_CBUTTONS;
    }
    if (rs[1] < -0.5f) {
        b |= D_CBUTTONS;
    }
    sNextController[0].button |= b;

    // Left stick flies. Radial deadzone, rescaled so full deflection still reaches full lock;
    // OpenXR's +y-up convention matches the N64 stick, no flip.
    m = sqrtf(ls[0] * ls[0] + ls[1] * ls[1]);
    if (m > 0.12f) {
        f32 g;
        if (m > 1.0f) {
            ls[0] /= m;
            ls[1] /= m;
            m = 1.0f;
        }
        g = (m - 0.12f) / (1.0f - 0.12f) * 80.0f / m;
        vx = (s8) (ls[0] * g);
        vy = (s8) (ls[1] * g);
        if ((vx < 0 ? -vx : vx) > (sNextController[0].stick_x < 0 ? -sNextController[0].stick_x
                                                                  : sNextController[0].stick_x)) {
            sNextController[0].stick_x = vx;
        }
        if ((vy < 0 ? -vy : vy) > (sNextController[0].stick_y < 0 ? -sNextController[0].stick_y
                                                                  : sNextController[0].stick_y)) {
            sNextController[0].stick_y = vy;
        }
    }
    sNextController[0].err_no = 0;
}

void Controller_UpdateInput(void) {
    s32 i;

    Controller_MergeVr();

    for (i = 0; i < 4; i++) {
        gControllerPlugged[i] = osContGetStatus(i);
        if ((gControllerPlugged[i] == 1) && (sNextController[i].err_no == 0)) {
            sPrevController[i] = gControllerHold[i];
            gControllerHold[i] = sNextController[i];
            gControllerPress[i].button =
                (gControllerHold[i].button ^ sPrevController[i].button) & gControllerHold[i].button;
            Controller_AddDeadZone(i);
        } else {
            gControllerHold[i].button = gControllerHold[i].stick_x = gControllerHold[i].stick_y =
                gControllerHold[i].err_no = gControllerPress[i].button = gControllerPress[i].stick_x =
                    gControllerPress[i].stick_y = gControllerPress[i].err_no = 0;
        }
    }
}

void Controller_ReadData(void) {
    s32 i;

    if (gControllerLock != 0) {
        gControllerLock--;
        for (i = 0; i < 4; i++) {
            sNextController[i].button = sNextController[i].stick_x = sNextController[i].stick_y =
                sNextController[i].err_no = 0;
        }
    } else {
        osContStartReadData(&gSerialEventQueue);
        osContGetReadData(sNextController);
    }
}

bool Save_ReadData(void) {
    return Save_ReadEeprom(&gSaveIOBuffer) == 0;
}

bool Save_WriteData(void) {
    return Save_WriteEeprom(&gSaveIOBuffer) == 0;
}

void Controller_Rumble(void) {
    s32 i;

    // Feel it in the hands: the game pulses gControllerRumbleFlags each tick while rumbling, which
    // re-arms a short burst on both motion controllers - the buzz dies on its own when the pulses stop.
    if (vr_controllers_active() && (gControllerRumbleFlags[0] != 0)) {
        vr_controller_rumble(0.7f, 0.08f);
    }

    for (i = 0; i < 4; i++) {
        if ((gControllerPlugged[i] != 0) && (sControllerStatus[i].err_no == 0)) {
            if (sControllerStatus[i].status & 1) {
                if (gControllerRumbleEnabled[i] == 0) {
                    if (osMotorInit(&gSerialEventQueue, &sControllerMotor[i], i)) {
                        gControllerRumbleEnabled[i] = 0;
                    } else {
                        gControllerRumbleEnabled[i] = 1;
                    }
                }
                if (gControllerRumbleEnabled[i] == 1) {
                    if (gControllerRumbleFlags[i] != 0) {
                        if (osMotorStart(&sControllerMotor[i])) {
                            gControllerRumbleEnabled[i] = 0;
                        }
                    } else {
                        if (osMotorStop(&sControllerMotor[i])) {
                            gControllerRumbleEnabled[i] = 0;
                        }
                    }
                }
            } else {
                gControllerRumbleEnabled[i] = 0;
            }
        }
    }
    for (i = 0; i < 4; i++) {
        gControllerRumbleFlags[i] = 0;
    }
}
