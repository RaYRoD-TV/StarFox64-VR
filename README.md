# Star Fox 64 VR

A full PCVR port of Star Fox 64, built on [Starship](https://github.com/HarbourMasters/Starship)
(the HarbourMasters PC port) with an OpenXR layer on top. Put on a headset and you're flying the
Arwing for real: the scene renders once per eye with full head tracking, and the motion controllers
drive everything - flight, menus, all of it.

No headset connected? The same exe runs as the normal flat game.

## Quick start

1. **Download** the latest zip from [Releases](https://github.com/RaYRoD-TV/StarFox64-VR/releases)
   and extract it anywhere.
2. **Start your VR runtime** - Quest Link / Air Link, Virtual Desktop, or SteamVR.
3. **Run `Starship.exe`.** On first launch a file picker opens: select your own Star Fox 64 **US**
   ROM (`.z64`). The game builds its asset archive from it once and you're in.

That's it. With the headset on you'll spawn straight into VR; the desktop window mirrors what you see.

- No copyrighted assets are included - you provide your own ROM dump. Supported: US 1.0
  (SHA-1 `D8B1088520F7C5F81433292A9258C1184AFA1457`) and US 1.1
  (SHA-1 `09F0D105F476B00EFA5303A3EBC42E60A7753B7A`). ROM in `.n64` format? Convert it to `.z64`
  first (hack64.net/tools/swapper.php).
- Force a mode with `--vr` (headset required) or `--novr` (always flat). With no flag it
  auto-detects.
- Windows only for now. Built against OpenXR 1.0, so any PCVR-capable headset should work
  (tested on Quest via Link and Virtual Desktop).

## Controls (motion controllers)

| Control | Action |
|---|---|
| Left stick | flight stick / menu navigation |
| Right trigger or A | fire laser (hold to charge) |
| Left trigger or B | smart bomb |
| Left grip | bank left (double-squeeze = barrel roll) |
| Right grip | bank right (double-squeeze = barrel roll) |
| Right stick up / down | boost / brake |
| **Right stick click** | **cycle view mode** |
| Left stick click | open / close the desktop settings menu |
| Menu button | pause |

A gamepad or keyboard keeps working alongside the controllers.

## View modes

Click the right stick any time to cycle:

1. **Third Person** - the classic chase cam, life-size and in stereo (default).
2. **First Person** - the Arwing is hidden and your eye sits in the pilot's seat. Barrel rolls
   roll *you* (toggleable).
3. **Cockpit** - the game's own cockpit camera with dashboard and glass (on-rails stages).
4. **Diorama** - the level shrunk to a tabletop in front of you. Lean in and look around it.
5. **Theater** - the flat game on a big head-locked screen. Maximum comfort, zero stereo.

## Tuning it to your taste

Two menus, same options:

- **In the headset:** pause, then pull the **right trigger**. A scrollable list of every VR option -
  view mode, world scale, stereo depth, HUD size/distance, hide HUD, sky dome, fog, draw distance,
  resolution scale, and more. Left stick to navigate and change values; changes apply live with the
  game visible behind the menu.
- **On the desktop (or on the in-headset panel):** click the **left stick** and use the pointer -
  everything is under **Enhancements → VR**.

A few worth knowing:

- **Internal Resolution** - supersampling. 1.5-2x is a big clarity upgrade if your GPU keeps up.
- **World Scale** - how big the world feels. Lower = bigger.
- **Fog** - distance fog rebuilt for VR (the stock N64 fog doesn't survive the per-eye projection).
  Tune where it starts and ends, or turn it off.
- **Hide HUD** - clears the 2D overlay for a clean view. Radio messages stay.
- **Mixed Reality** - on Quest, the Diorama tabletop can sit in your real room via passthrough.
- Feeling queasy? Theater mode is always one stick-click away, and Stereo Depth can be dialed down.

## Status

This is a beta. It's had plenty of flight time, but VR touches everything, so expect some rough
edges - if something breaks or feels off, open an issue and I'll get on it.

## Credits

- [HarbourMasters Starship](https://github.com/HarbourMasters/Starship) - the PC port this is built
  on. None of this happens without their work. Lead developers:
  [SonicDcer](https://www.github.com/sonicdcer) and [Lywx](https://www.github.com/kiritodv).
- [libultraship](https://github.com/Kenix3/libultraship) - the renderer/platform layer (this port
  uses a fork with the VR render hooks).

More of my work - VR ports, reverse engineering, engine dev: [rayrodtv.com](https://rayrodtv.com/)

Star Fox 64 is a Nintendo game; this project ships no game assets and does not condone piracy.
