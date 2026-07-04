@echo off
REM Launch Starship in VR. Start your VR runtime first (Quest Link, Air Link, Virtual Desktop,
REM or SteamVR). With no headset, drop the --vr and it runs flat.
cd /d "%~dp0build\x64\Release"
start "" Starship.exe --vr
