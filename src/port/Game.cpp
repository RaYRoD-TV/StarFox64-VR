#include <libultraship.h>

#include <Fast3D/interpreter.h>
#include "Engine.h"
#include "port/vr/vr.h"

#include <cstring>

extern "C" {
#include <sf64mesg.h>
    void Main_SetVIMode(void);
    void Main_Initialize(void);
    void Main_ThreadEntry(void* arg);
    void Lib_FillScreen(u8 setFill);
    void Graphics_ThreadUpdate();
    void AudioThread_CreateTask();
}

extern "C"
void Graphics_PushFrame(Gfx* data) {
    GameEngine::ProcessGfxCommands(data);
}

extern "C" void Timer_Update();

// src/port/Engine.cpp - releases OpenXR on quit so no runtime thread outlives the window.
extern "C" void GameEngine_TerminateVr(void);

void push_frame() {
    Graphics_ThreadUpdate();
    GameEngine::StartAudioFrame();
    GameEngine::Instance->StartFrame();
    Timer_Update();
    // thread5_iteration();
    GameEngine::EndAudioFrame();
}

#ifdef _WIN32
int SDL_main(int argc, char **argv) {
#else
#if defined(__cplusplus) && defined(PLATFORM_IOS)
extern "C"
#endif
int main(int argc, char *argv[]) {
#endif
    // VR enable decision: --vr forces VR on, --novr forces it off, otherwise auto-enable when a
    // headset is connected. Must run before GameEngine::Create() so the constructor can force the
    // OpenGL backend (OpenXR binds to WGL) before the window is created.
    {
        bool forceVr = false, forceNoVr = false;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--vr") == 0) {
                forceVr = true;
            } else if (strcmp(argv[i], "--novr") == 0) {
                forceNoVr = true;
            }
        }
        if (forceVr || (!forceNoVr && vr_headset_present())) {
            vr_request_enable();
        }
    }

    GameEngine::Create();
    Main_SetVIMode();
    Lib_FillScreen(1);
    Main_Initialize();
    Main_ThreadEntry(NULL);
    while (WindowIsRunning()) {
        push_frame();
    }
    GameEngine::Instance->Destroy();
    GameEngine_TerminateVr(); // release OpenXR last so no runtime thread keeps the process alive
    return 0;
}
