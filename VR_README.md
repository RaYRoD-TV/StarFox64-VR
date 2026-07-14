# Starship VR - notes

Technical notes for the VR build. The main README covers normal play.

## What it does

VR runs through OpenXR. The scene is drawn twice per frame, once per eye, with real head tracking, so
you sit with the Arwing and look around. At startup the exe checks OpenXR for a connected headset and
turns VR on if it finds one; with no headset (or no OpenXR runtime) it runs as the normal flat game.
Force either way with --vr or --novr.

VR uses the OpenGL renderer, because OpenXR is bound to the GL context here. When VR turns on it
switches the renderer to OpenGL automatically (the same backend you can pick by hand in Settings).
Built against OpenXR 1.0, so it works on runtimes like Virtual Desktop's VDXR as well as SteamVR and
the Oculus runtime.

## Running it

Start your VR runtime first (Quest Link, Air Link, Virtual Desktop, or SteamVR) if you want VR. Run
Starship.exe (or play_vr.bat). On first launch, pick your Star Fox 64 US ROM so it can build sf64.o2r;
there's no game data in the exe.

## Motion controllers

The VR controllers work as the first pad, in gameplay and in every menu:

- left stick: flight stick (and menu navigation)
- right trigger or A: fire laser, hold to charge (A)
- left trigger or B: smart bomb (B)
- left grip: bank left - double-squeeze for a barrel roll left (Z)
- right grip: bank right - double-squeeze for a barrel roll right (R)
- right stick forward: boost; right stick back: brake
- right stick right (a clear sideways flick): answer ROB and incoming radio calls (C-Right)
- right stick click: step to the next view mode
- left stick click: open / close the settings menu (it shows on a panel in the headset)
- menu button: pause (Start)
- Y: answer ROB and incoming radio calls (C-Right)
- X: pause - for controllers whose menu button belongs to the system (PSVR2)

Right Stick Turning (off by default, in both option menus) adds steering on the right stick's X axis;
it retires the flick-to-answer gesture so a hard turn can't answer a call - Y still answers.

A regular gamepad or the keyboard keeps working alongside them; per axis the stronger source wins.
Rumble from the game reaches both controllers.

## View modes

Third Person (the chase cam in stereo, default), First Person (riding at the pilot's seat), Cockpit
(the game's own in-cockpit camera, with dash comm screen, adjustable seat and optional glass), Diorama
(the level shrunk to a tabletop - a great fit for all-range arenas), and Theater (flat screen floating
in front of you, max comfort). Mixed Reality passthrough (Quest) puts the Diorama tabletop in your
real room.

## VR settings

View mode, per-mode world scale, camera distance and eye height, stereo depth, the sky dome, and the
HUD plane's size and distance are all live-tunable under Enhancements > VR in the menu (F1). The menu
shows up on a panel inside the headset and can be driven with a gamepad (menubar controller navigation
turns on automatically in VR); the mouse works on it too.

There is also a native in-headset options overlay for the essentials, drawn in the game's own font so
it always lands on the head-locked panel: pause the game (menu button / Start), then pull the RIGHT
TRIGGER - or press R on a regular gamepad. The left stick or D-pad moves between rows, left/right
changes a value, and A activates. It closes with the button that opened it, B, or by unpausing.

## Building

Same as a normal Starship build (Visual Studio + CMake). The only extra dependency is the OpenXR
loader, which vcpkg pulls in automatically (openxr-loader, added to the package list). Run
build_vr.bat, or:

    cmake -S . -B build/x64 -G "Visual Studio 18 2026" -A x64
    cmake --build build/x64 --config Release --target Starship --parallel

The OpenXR loader is linked statically, so there are no extra runtime DLLs to ship beyond what
Starship already needs.

## Where the VR code lives

- src/port/vr/vr.cpp, vr.h - the OpenXR work: session setup, per-eye view/projection matrices, stereo,
  head tracking, the head-locked HUD/menu panel, motion controllers, frame submit, and the startup
  headset check. Plain C interface so it can be called from the C++ renderer and the C game code.
- src/port/Engine.cpp - the per-frame VR loop (in RunCommands): begin frame, render each eye, submit;
  forcing the OpenGL backend when VR is on; the menu-on-panel path.
- src/port/Game.cpp - the --vr / --novr flags and the startup headset auto-detect.
- src/sys/sys_joybus.c - the motion-controller merge into pad 0, and the rumble bridge.
- src/engine/fox_display.c - the Third Person camera distance push.
- libultraship/src/graphic/Fast3D/interpreter.cpp, interpreter.h, backends/gfx_opengl.cpp - small,
  contained renderer hooks: the per-eye projection substitution in GfxSpMatrix, the aspect-ratio
  bypass, the HUD-plane remap for screen-space rectangles, and RunVrEye/RunVrPanel (interpret the
  display list into an off-screen eye target). All gated so the flat path is unchanged when VR is off.

## Still rough

Beta. Stereo, head tracking, the four view modes, the head-locked HUD, the world-anchored sky dome and
the motion controllers are in. Comfort features (cockpit frame, vignette) are not built yet, and the
remaining 2D overlays (title, map) ride a head-locked plane.
