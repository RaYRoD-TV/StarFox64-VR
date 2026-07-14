#include "ImguiUI.h"
#include "UIWidgets.h"
#include "ResolutionEditor.h"

#include <spdlog/spdlog.h>
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "libultraship/src/Context.h"

#include <imgui_internal.h>
#include <libultraship/libultraship.h>
#include <Fast3D/interpreter.h>
#include "port/Engine.h"
#include "port/notification/notification.h"
#include "utils/StringHelper.h"
#include "properties.h" // VR_PORT_VERSION_STR - the release version shown at the top of the VR menu

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#include <port/switch/SwitchPerformanceProfiles.h>
#endif

extern "C" {
#include "sys.h"
#include <sf64audio_provisional.h>
#include <sf64context.h>
}

namespace GameUI {
std::shared_ptr<GameMenuBar> mGameMenuBar;
std::shared_ptr<Ship::GuiWindow> mConsoleWindow;
std::shared_ptr<Ship::GuiWindow> mStatsWindow;
std::shared_ptr<Ship::GuiWindow> mInputEditorWindow;
std::shared_ptr<Ship::GuiWindow> mGfxDebuggerWindow;
std::shared_ptr<Notification::Window> mNotificationWindow;
std::shared_ptr<AdvancedResolutionSettings::AdvancedResolutionSettingsWindow> mAdvancedResolutionSettingsWindow;

void SetupGuiElements() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();

    auto& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(4.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.Colors[ImGuiCol_MenuBarBg] = UIWidgets::Colors::DarkGray;

    mGameMenuBar = std::make_shared<GameMenuBar>("gOpenMenuBar", CVarGetInteger("gOpenMenuBar", 0));
    gui->SetMenuBar(mGameMenuBar);

    if (gui->GetMenuBar() && !gui->GetMenuBar()->IsVisible()) {
#if defined(__SWITCH__) || defined(__WIIU__)
        Notification::Emit({ .message = "Press - to access enhancements menu", .remainingTime = 10.0f });
#else
        Notification::Emit({ .message = "Press F1 to access enhancements menu", .remainingTime = 10.0f });
#endif
    }

    mStatsWindow = gui->GetGuiWindow("Stats");
    if (mStatsWindow == nullptr) {
        SPDLOG_ERROR("Could not find stats window");
    }

    mConsoleWindow = gui->GetGuiWindow("Console");
    if (mConsoleWindow == nullptr) {
        SPDLOG_ERROR("Could not find console window");
    }

    mInputEditorWindow = gui->GetGuiWindow("Input Editor");
    if (mInputEditorWindow == nullptr) {
        SPDLOG_ERROR("Could not find input editor window");
        return;
    }

    mGfxDebuggerWindow = gui->GetGuiWindow("GfxDebuggerWindow");
    if (mGfxDebuggerWindow == nullptr) {
        SPDLOG_ERROR("Could not find input GfxDebuggerWindow");
    }

    mAdvancedResolutionSettingsWindow = std::make_shared<AdvancedResolutionSettings::AdvancedResolutionSettingsWindow>("gAdvancedResolutionEditorEnabled", "Advanced Resolution Settings");
    gui->AddGuiWindow(mAdvancedResolutionSettingsWindow);
    mNotificationWindow = std::make_shared<Notification::Window>("gNotifications", "Notifications Window");
    gui->AddGuiWindow(mNotificationWindow);
    mNotificationWindow->Show();
}

void Destroy() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    gui->RemoveAllGuiWindows();

    mAdvancedResolutionSettingsWindow = nullptr;
    mConsoleWindow = nullptr;
    mStatsWindow = nullptr;
    mInputEditorWindow = nullptr;
    mNotificationWindow = nullptr;
}

std::string GetWindowButtonText(const char* text, bool menuOpen) {
    char buttonText[100] = "";
    if (menuOpen) {
        strcat(buttonText, ICON_FA_CHEVRON_RIGHT " ");
    }
    strcat(buttonText, text);
    if (!menuOpen) { strcat(buttonText, "  "); }
    return buttonText;
}
}

static const char* filters[3] = {
#ifdef __WIIU__
        "",
#else
        "Three-Point",
#endif
        "Linear", "None"
};

static const char* voiceLangs[] = {
    "Original", /*"Japanese",*/ "Lylat"
};
static const char* voiceLangsSPA[] = {
    "Español", /*"Japanese",*/ "Lylat"
};

void DrawSpeakerPositionEditor() {
    static ImVec2 lastCanvasPos;
    ImGui::Text("Speaker Position Editor");
    ImVec2 canvasSize = ImVec2(200, 200); // Static canvas size
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(canvasPos.x + canvasSize.x / 2, canvasPos.y + canvasSize.y / 2);

    // Speaker positions
    static ImVec2 speakerPositions[4];
    static bool initialized = false;
    static float radius = 80.0f;

    // Reset positions if canvas position changed (window resized/moved)
    if (!initialized || (lastCanvasPos.x != canvasPos.x || lastCanvasPos.y != canvasPos.y)) {
        const char* cvarNames[4] = { "gPositionFrontLeft", "gPositionFrontRight", "gPositionRearLeft", "gPositionRearRight" };
        float angles[4] = { 240.f, 300.f, 160.f, 20.f }; // Default angles
        
        for (int i = 0; i < 4; i++) {
            int savedAngle = CVarGetInteger(cvarNames[i], -1);
            if (savedAngle != -1) {
                angles[i] = static_cast<float>(savedAngle);
            }

            float rad = angles[i] * (M_PI / 180.0f);
            speakerPositions[i] = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
        }
        initialized = true;
        lastCanvasPos = canvasPos;
    }

    // Draw canvas
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(26, 26, 26, 255));
    drawList->AddCircleFilled(center, 5.0f, IM_COL32(255, 255, 255, 255)); // Central person

    // Draw circle line for speaker positions
    drawList->AddCircle(center, radius, IM_COL32(163, 163, 163, 255), 100);

    // Add markers at 0, 22.5, 45, etc.
    for (float angle = 0; angle < 360; angle += 22.5f) {
        float rad = angle * (M_PI / 180.0f);
        ImVec2 markerStart = ImVec2(center.x + (radius - 5) * cosf(rad), center.y + (radius - 5) * sinf(rad));
        ImVec2 markerEnd = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
        drawList->AddLine(markerStart, markerEnd, IM_COL32(163, 163, 163, 255));
    }

    const char* speakerLabels[4] = { "L", "R", "RL", "RR" };
    const char* cvarNames[4] = { "gPositionFrontLeft", "gPositionFrontRight", "gPositionRearLeft", "gPositionRearRight" };

    const float snapThreshold = 2.5f; // Degrees within which snapping occurs

    for (int i = 0; i < 4; i++) {
        // Draw speaker as a darker blue circle
        drawList->AddCircleFilled(speakerPositions[i], 10.0f, IM_COL32(34, 52, 78, 255)); // Dark blue color
        drawList->AddText(ImVec2(speakerPositions[i].x - 6, speakerPositions[i].y - 6), IM_COL32(255, 255, 255, 255), speakerLabels[i]);

        // Handle dragging
        ImGui::SetCursorScreenPos(ImVec2(speakerPositions[i].x - 10, speakerPositions[i].y - 10));
        ImGui::InvisibleButton(speakerLabels[i], ImVec2(20, 20));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            ImVec2 newPos = ImVec2(speakerPositions[i].x + mouseDelta.x, speakerPositions[i].y + mouseDelta.y);

            // Constrain position to the circle
            ImVec2 direction = ImVec2(newPos.x - center.x, newPos.y - center.y);
            float length = sqrtf(direction.x * direction.x + direction.y * direction.y);
            ImVec2 constrainedPos = ImVec2(center.x + (direction.x / length) * radius, center.y + (direction.y / length) * radius);

            // Calculate angle of the constrained position
            float angle = atan2f(constrainedPos.y - center.y, constrainedPos.x - center.x) * (180.0f / M_PI);
            if (angle < 0) angle += 360.0f;

            // Snap to the nearest 22.5-degree marker if within the snap threshold
            float snappedAngle = roundf(angle / 22.5f) * 22.5f;
            if (fabsf(snappedAngle - angle) <= snapThreshold) {
                float rad = snappedAngle * (M_PI / 180.0f);
                constrainedPos = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
            }

            speakerPositions[i] = constrainedPos;
            ImGui::ResetMouseDragDelta();

            // Save the updated angle to CVar after dragging
            float updatedAngle = atan2f(speakerPositions[i].y - center.y, speakerPositions[i].x - center.x) * (180.0f / M_PI);
            if (updatedAngle < 0) updatedAngle += 360.0f;
            CVarSetInteger(cvarNames[i], static_cast<int>(updatedAngle));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame(); // Mark for saving
        }

        // Calculate angle and save to CVar
        float angle = atan2f(speakerPositions[i].y - center.y, speakerPositions[i].x - center.x) * (180.0f / M_PI);
        if (angle < 0) angle += 360.0f;
        CVarSetInteger(cvarNames[i], static_cast<int>(angle));
    }

    // Reset cursor position for button placement
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 10));
    if (ImGui::Button("Reset Positions")) {
        float defaultAngles[4] = { 240.f, 300.f, 160.f, 20.f };
        for (int i = 0; i < 4; i++) {
            float rad = defaultAngles[i] * (M_PI / 180.0f);
            speakerPositions[i] = ImVec2(center.x + radius * cosf(rad), center.y + radius * sinf(rad));
            CVarSetInteger(cvarNames[i], static_cast<int>(defaultAngles[i]));
        }
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    // Reset cursor position to ensure canvas size remains static
    ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y + 10));
}

void DrawSettingsMenu(){
    if(UIWidgets::BeginMenu("Settings")){
        if (UIWidgets::BeginMenu("Audio")) {
            UIWidgets::CVarSliderFloat("Master Volume", "gGameMasterVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            });
            if (UIWidgets::CVarSliderFloat("Main Music Volume", "gMainMusicVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            })) {
                float val = CVarGetFloat("gMainMusicVolume", 1.0f) * 100;
                gSaveFile.save.data.musicVolume = (u8) val;
                Audio_SetVolume(AUDIO_TYPE_MUSIC, (u8) val);
            }
            if (UIWidgets::CVarSliderFloat("Voice Volume", "gVoiceVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            })) {
                float val = CVarGetFloat("gVoiceVolume", 1.0f) * 100;
                gSaveFile.save.data.voiceVolume = (u8) val;
                Audio_SetVolume(AUDIO_TYPE_VOICE, (u8) val);
            }
            if (UIWidgets::CVarSliderFloat("Sound Effects Volume", "gSFXMusicVolume", 0.0f, 1.0f, 1.0f, {
                .format = "%.0f%%",
                .isPercentage = true,
            })) {
                float val = CVarGetFloat("gSFXMusicVolume", 1.0f) * 100;
                gSaveFile.save.data.sfxVolume = (u8) val;
                Audio_SetVolume(AUDIO_TYPE_SFX, (u8) val);
            }

            static std::unordered_map<Ship::AudioBackend, const char*> audioBackendNames = {
                    { Ship::AudioBackend::WASAPI, "Windows Audio Session API" },
                    { Ship::AudioBackend::SDL, "SDL" },
            };

            ImGui::Text("Audio API (Needs reload)");
            auto currentAudioBackend = Ship::Context::GetInstance()->GetAudio()->GetCurrentAudioBackend();

            if (Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1) {
                UIWidgets::DisableComponent(ImGui::GetStyle().Alpha * 0.5f);
            }
            if (ImGui::BeginCombo("##AApi", audioBackendNames[currentAudioBackend])) {
                for (uint8_t i = 0; i < Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size(); i++) {
                    auto backend = Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->data()[i];
                    if (ImGui::Selectable(audioBackendNames[backend], backend == currentAudioBackend)) {
                        Ship::Context::GetInstance()->GetAudio()->SetCurrentAudioBackend(backend);
                    }
                }
                ImGui::EndCombo();
            }
            if (Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1) {
                UIWidgets::ReEnableComponent("");
            }
            
            UIWidgets::PaddedEnhancementCheckbox("Surround 5.1 (Needs reload)", "gAudioChannelsSetting", 1, 0);
            
            if (CVarGetInteger("gAudioChannelsSetting", 0) == 1) {
                // Subwoofer threshold
                UIWidgets::CVarSliderInt("Subwoofer threshold (Hz)", "gSubwooferThreshold", 10u, 1000u, 80u, {
                    .tooltip = "The threshold for the subwoofer to be activated. Any sound under this frequency will be played on the subwoofer.",
                    .format = "%d",
                });

                // Rear music volume slider
                UIWidgets::CVarSliderFloat("Rear music volume", "gVolumeRearMusic", 0.0f, 1.0f, 1.0f, {
                    .format = "%.0f%%",
                    .isPercentage = true,
                });

                // Configurable positioning of speakers
                DrawSpeakerPositionEditor();
            }

            ImGui::EndMenu();
        }
        
        if (!GameEngine::HasVersion(SF64_VER_JP) || GameEngine::HasVersion(SF64_VER_EU)) {
            UIWidgets::Spacer(0);
            if (UIWidgets::BeginMenu("Language")) {
                ImGui::Dummy(ImVec2(150, 0.0f));
                if (!GameEngine::HasVersion(SF64_VER_JP) && (GameEngine::HasVersion(SF64_VER_EU) || GameEngine::HasVersion(SF64_VER_EU_SPA))) {
                    if (GameEngine::HasVersion(SF64_VER_EU_SPA)) {
                        //UIWidgets::Spacer(0);
                        if (UIWidgets::CVarCombobox("Voices", "gVoiceLanguage", voiceLangsSPA, 
                            {
                                .tooltip = "Changes the language of the voice acting in the game",
                                .defaultIndex = 0,
                            })) {
                                Audio_SetVoiceLanguage(CVarGetInteger("gVoiceLanguage", 0));
                            };
                    } else {
                        //UIWidgets::Spacer(0);
                        if (UIWidgets::CVarCombobox("Voices", "gVoiceLanguage", voiceLangs, 
                            {
                                .tooltip = "Changes the language of the voice acting in the game",
                                .defaultIndex = 0,
                            })) {
                                Audio_SetVoiceLanguage(CVarGetInteger("gVoiceLanguage", 0));
                            };
                    }
                } else {
                    if (UIWidgets::Button("Install JP/EU Audio")) {
                        if (GameEngine::GenAssetFile(false)){
                            GameEngine::ShowMessage("Success", "Audio assets installed. Changes will be applied on the next startup.", SDL_MESSAGEBOX_INFORMATION);
                        }
                        Ship::Context::GetInstance()->GetWindow()->Close();
                    }
                }
                ImGui::EndMenu();
            }
        }
        
        UIWidgets::Spacer(0);

        if (UIWidgets::BeginMenu("Controller")) {
            UIWidgets::WindowButton("Controller Mapping", "gInputEditorWindow", GameUI::mInputEditorWindow);

            UIWidgets::Spacer(0);

            UIWidgets::CVarCheckbox("Menubar Controller Navigation", "gControlNav", {
                .tooltip = "Allows controller navigation of the SOH menu bar (Settings, Enhancements,...)\nCAUTION: This will disable game inputs while the menubar is visible.\n\nD-pad to move between items, A to select, and X to grab focus on the menu bar"
            });

            UIWidgets::CVarCheckbox("Invert Y Axis", "gInvertYAxis",{
                .tooltip = "Inverts the Y axis for controlling vehicles"
            });

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    ImGui::SetCursorPosY(0.0f);
    if (UIWidgets::BeginMenu("Graphics")) {
        UIWidgets::WindowButton("Resolution Editor", "gAdvancedResolutionEditorEnabled", GameUI::mAdvancedResolutionSettingsWindow);

        UIWidgets::Spacer(0);

        // Previously was running every frame, and nothing was setting it? Maybe a bad copy/paste?
        // Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(CVarGetFloat("gInternalResolution", 1));
        // UIWidgets::Tooltip("Multiplies your output resolution by the value inputted, as a more intensive but effective form of anti-aliasing");
#ifndef __WIIU__
        if (UIWidgets::CVarSliderInt("MSAA: %d", "gMSAAValue", 1, 8, 1, {
            .tooltip = "Activates multi-sample anti-aliasing when above 1x up to 8x for 8 samples for every pixel"
        })) {
            Ship::Context::GetInstance()->GetWindow()->SetMsaaLevel(CVarGetInteger("gMSAAValue", 1));
        }
#endif

        { // FPS Slider
            const int minFps = 30;
            static int maxFps = 360;
            int currentFps = 0;
        #ifdef __WIIU__
            UIWidgets::Spacer(0);
            // only support divisors of 60 on the Wii U
            if (currentFps > 60) {
                currentFps = 60;
            } else {
                currentFps = 60 / (60 / currentFps);
            }

            int fpsSlider = 1;
            if (currentFps == 30) {
                ImGui::Text("FPS: Original (30)");
            } else {
                ImGui::Text("FPS: %d", currentFps);
                if (currentFps == 30) {
                    fpsSlider = 2;
                } else { // currentFps == 60
                    fpsSlider = 3;
                }
            }
            if (CVarGetInteger("gMatchRefreshRate", 0)) {
                UIWidgets::DisableComponent(ImGui::GetStyle().Alpha * 0.5f);
            }

            if (ImGui::Button(" - ##WiiUFPS")) {
                fpsSlider--;
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 7.0f);

            UIWidgets::Spacer(0);

            ImGui::PushItemWidth(std::min((ImGui::GetContentRegionAvail().x - 60.0f), 260.0f));
            ImGui::SliderInt("##WiiUFPSSlider", &fpsSlider, 1, 3, "", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 7.0f);
            if (ImGui::Button(" + ##WiiUFPS")) {
                fpsSlider++;
            }

            if (CVarGetInteger("gMatchRefreshRate", 0)) {
                UIWidgets::ReEnableComponent("");
            }
            if (fpsSlider > 3) {
                fpsSlider = 3;
            } else if (fpsSlider < 1) {
                fpsSlider = 1;
            }

            if (fpsSlider == 1) {
                currentFps = 20;
            } else if (fpsSlider == 2) {
                currentFps = 30;
            } else if (fpsSlider == 3) {
                currentFps = 60;
            }
            CVarSetInteger("gInterpolationFPS", currentFps);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        #else
            bool matchingRefreshRate = CVarGetInteger("gMatchRefreshRate", 0);
            UIWidgets::CVarSliderInt((currentFps == 30) ? "FPS: Original (30)" : "FPS: %d", "gInterpolationFPS", minFps, maxFps, 60, {
                .disabled = matchingRefreshRate
            });
        #endif
            UIWidgets::Tooltip(
                "Uses Matrix Interpolation to create extra frames, resulting in smoother graphics. This is purely "
                "visual and does not impact game logic, execution of glitches etc.\n\n"
                "A higher target FPS than your monitor's refresh rate will waste resources, and might give a worse result."
            );
        } // END FPS Slider

        UIWidgets::PaddedEnhancementCheckbox("Match Refresh Rate", "gMatchRefreshRate", true, false);
        UIWidgets::Tooltip("Matches interpolation value to the refresh rate of your display.");

        if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() == Ship::WindowBackend::FAST3D_DXGI_DX11) {
            UIWidgets::PaddedEnhancementCheckbox("Render parallelization","gRenderParallelization", true, false, {}, {}, {}, true);
            UIWidgets::Tooltip(
                "This setting allows the CPU to work on one frame while GPU works on the previous frame.\n"
                "Recommended if you can't reach the FPS you set, despite it being set below your refresh rate "
                "or if you notice other performance problems.\n"
                "Adds up to one frame of input lag under certain scenarios.");
        }
      
        UIWidgets::PaddedSeparator(true, true, 3.0f, 3.0f);

        static std::unordered_map<Ship::WindowBackend, const char*> windowBackendNames = {
                { Ship::WindowBackend::FAST3D_DXGI_DX11, "DirectX" },
                { Ship::WindowBackend::FAST3D_SDL_OPENGL, "OpenGL"},
                { Ship::WindowBackend::FAST3D_SDL_METAL, "Metal" }
        };

        ImGui::Text("Renderer API (Needs reload)");
        Ship::WindowBackend runningWindowBackend = Ship::Context::GetInstance()->GetWindow()->GetWindowBackend();
        Ship::WindowBackend configWindowBackend;
        int configWindowBackendId = Ship::Context::GetInstance()->GetConfig()->GetInt("Window.Backend.Id", -1);
        if (configWindowBackendId != -1 && configWindowBackendId < static_cast<int>(Ship::WindowBackend::WINDOW_BACKEND_COUNT)) {
            configWindowBackend = static_cast<Ship::WindowBackend>(configWindowBackendId);
        } else {
            configWindowBackend = runningWindowBackend;
        }

        if (Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->size() <= 1) {
            UIWidgets::DisableComponent(ImGui::GetStyle().Alpha * 0.5f);
        }
        if (ImGui::BeginCombo("##RApi", windowBackendNames[configWindowBackend])) {
            for (size_t i = 0; i < Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->size(); i++) {
                auto backend = Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->data()[i];
                if (ImGui::Selectable(windowBackendNames[backend], backend == configWindowBackend)) {
                    Ship::Context::GetInstance()->GetConfig()->SetInt("Window.Backend.Id", static_cast<int>(backend));
                    Ship::Context::GetInstance()->GetConfig()->SetString("Window.Backend.Name",
                                                                        windowBackendNames[backend]);
                    Ship::Context::GetInstance()->GetConfig()->Save();
                }
            }
            ImGui::EndCombo();
        }
        if (Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends()->size() <= 1) {
            UIWidgets::ReEnableComponent("");
        }

        if (Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync()) {
            UIWidgets::PaddedEnhancementCheckbox("Enable Vsync", "gVsyncEnabled", true, false, false, "", UIWidgets::CheckboxGraphics::Cross, true);
            UIWidgets::Tooltip("Removes tearing, but clamps your max FPS to your displays refresh rate.");
        }

        if (Ship::Context::GetInstance()->GetWindow()->SupportsWindowedFullscreen()) {
            UIWidgets::PaddedEnhancementCheckbox("Windowed fullscreen", "gSdlWindowedFullscreen", true, false);
        }

        if (Ship::Context::GetInstance()->GetWindow()->GetGui()->SupportsViewports()) {
            UIWidgets::PaddedEnhancementCheckbox("Allow multi-windows", "gEnableMultiViewports", true, false, false, "", UIWidgets::CheckboxGraphics::Cross, true);
            UIWidgets::Tooltip("Allows windows to be able to be dragged off of the main game window. Requires a reload to take effect.");
        }

        UIWidgets::PaddedEnhancementCheckbox("Enable Alternative Assets", "gEnhancements.Mods.AlternateAssets");
        // If more filters are added to LUS, make sure to add them to the filters list here
        ImGui::Text("Texture Filter (Needs reload)");
        UIWidgets::EnhancementCombobox("gTextureFilter", filters, 0);

        UIWidgets::PaddedEnhancementCheckbox("Apply Point Filtering to UI Elements", "gHUDPointFiltering", true, false, false, "", UIWidgets::CheckboxGraphics::Cross, true);
        UIWidgets::Spacer(0);

        Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGameOverlay()->DrawSettings();

        ImGui::EndMenu();
    }
}

void DrawMenuBarIcon() {
    static bool gameIconLoaded = false;
    if (!gameIconLoaded) {
        // Ship::Context::GetInstance()->GetWindow()->GetGui()->LoadGuiTexture("Game_Icon", "textures/icons/gIcon.png", ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        gameIconLoaded = false;
    }

    if (Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName("Game_Icon")) {
#ifdef __SWITCH__
        ImVec2 iconSize = ImVec2(20.0f, 20.0f);
        float posScale = 1.0f;
#elif defined(__WIIU__)
        ImVec2 iconSize = ImVec2(16.0f * 2, 16.0f * 2);
        float posScale = 2.0f;
#else
        ImVec2 iconSize = ImVec2(20.0f, 20.0f);
        float posScale = 1.5f;
#endif
        ImGui::SetCursorPos(ImVec2(5, 2.5f) * posScale);
        ImGui::Image(Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName("Game_Icon"), iconSize);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(25, 0) * posScale);
    }
}

void DrawGameMenu() {
    if (UIWidgets::BeginMenu("Starship")) {
        if (UIWidgets::MenuItem("Reset", "F4")) {
            gNextGameState = GSTATE_BOOT;
        }
#if !defined(__SWITCH__) && !defined(__WIIU__)

        if (UIWidgets::MenuItem("Toggle Fullscreen", "F11")) {
            Ship::Context::GetInstance()->GetWindow()->ToggleFullscreen();
        }
#endif

        if (UIWidgets::MenuItem("Quit")) {
            Ship::Context::GetInstance()->GetWindow()->Close();
        }
        ImGui::EndMenu();
    }
}

static const char* hudAspects[] = {
    "Expand", "Custom", "Original (4:3)", "Widescreen (16:9)", "Nintendo 3DS (5:3)", "16:10 (8:5)", "Ultrawide (21:9)"
};

static const char* radioCommBox[] = {
    "Original", "Expand"
};

static const char* vrFogModes[] = {
    "Off", "World Distance", "Stock N64"
};

void DrawEnhancementsMenu() {
    if (UIWidgets::BeginMenu("Enhancements")) {

        if (UIWidgets::BeginMenu("Gameplay")) {
            UIWidgets::CVarCheckbox("No Level of Detail (LOD)", "gDisableLOD", {
                .tooltip = "Disable Level of Detail (LOD) to avoid models using lower poly versions at a distance",
                .defaultValue = true
            });
            UIWidgets::CVarCheckbox("Character heads inside Arwings at all times", "gTeamFaces", {
                .tooltip = "Character heads are displayed inside Arwings in all cutscenes",
                .defaultValue = true
            });
            UIWidgets::CVarCheckbox("Use red radio backgrounds for enemies.", "gEnemyRedRadio");
            UIWidgets::CVarSliderInt("Cockpit Glass Opacity: %d", "gCockpitOpacity", 0, 255, 120);
            UIWidgets::CVarCheckbox("Skippable level intro", "gSkipLevelIntro", {
                .tooltip = "Press START (the controller menu button in VR) during a level's opening fly-in to "
                           "skip it, even on a fresh save. Off = vanilla, where you can only skip an intro after "
                           "clearing that planet.",
                .defaultValue = true
            });

            ImGui::EndMenu();
        }
        
        if (UIWidgets::BeginMenu("Fixes")) {
            UIWidgets::CVarCheckbox("Macbeth: Level ending cutscene camera fix", "gMaCameraFix", {
                .tooltip = "Fixes a camera bug found in the code of the game"
            });

            UIWidgets::CVarCheckbox("Sector Z: Spawn all actors", "gSzActorFix", {
                .tooltip = "Fixes a bug found in Sector Z, where only 10 of 12 available actors are spawned, this causes two 'Space Junk Boxes' to be missing from the level."
            });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Restoration")) {
            UIWidgets::CVarCheckbox("Sector Z: Missile cutscene bug", "gSzMissileBug", {
                .tooltip = "Restores the missile cutscene bug present in JP 1.0"
            });

            UIWidgets::CVarCheckbox("Beta: Restore beta bomb explosion", "gRestoreBetaBombExplosion", {
                .tooltip = "Restores the beta bomb explosion found inside the game"
            });

            UIWidgets::CVarCheckbox("Beta: Restore beta coin", "gRestoreBetaCoin", {
                .tooltip = "Restores the beta coin that got replaced with the gold ring"
            });

            UIWidgets::CVarCheckbox("Beta: Restore beta boost/brake gauge", "gRestoreBetaBoostGauge", {
                .tooltip = "Restores the beta boost gauge that was seen in some beta footage"
            });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("HUD")) {
            if (UIWidgets::CVarCombobox("Radio Communication Box", "gRadioCommBox.Selection", radioCommBox, 
            {
                .tooltip = "Which Aspect Ratio to use when drawing the Radio Communication Box",
                .defaultIndex = 0,
            })) {
                switch (CVarGetInteger("gRadioCommBox.Selection", 0)) {
                    case 0:
                        CVarSetInteger("gRadioCommBox.expand", 0);
                        break;
                    case 1:
                        CVarSetInteger("gRadioCommBox.expand", 1);
                        break;
                }
            }

            if (UIWidgets::CVarCombobox("HUD Aspect Ratio", "gHUDAspectRatio.Selection", hudAspects, 
            {
                .tooltip = "Which Aspect Ratio to use when drawing the HUD (Radar, gauges and radio messages)",
                .defaultIndex = 0,
            })) {
                CVarSetInteger("gHUDAspectRatio.Enabled", 1);
                switch (CVarGetInteger("gHUDAspectRatio.Selection", 0)) {
                    case 0:
                        CVarSetInteger("gHUDAspectRatio.Enabled", 0);
                        CVarSetInteger("gHUDAspectRatio.X", 0);
                        CVarSetInteger("gHUDAspectRatio.Y", 0);
                        break;
                    case 1:
                        if (CVarGetInteger("gHUDAspectRatio.X", 0) <= 0){
                            CVarSetInteger("gHUDAspectRatio.X", 1);
                        }
                        if (CVarGetInteger("gHUDAspectRatio.Y", 0) <= 0){
                            CVarSetInteger("gHUDAspectRatio.Y", 1);
                        }
                        break;
                    case 2:
                        CVarSetInteger("gHUDAspectRatio.X", 4);
                        CVarSetInteger("gHUDAspectRatio.Y", 3);
                        break;
                    case 3:
                        CVarSetInteger("gHUDAspectRatio.X", 16);
                        CVarSetInteger("gHUDAspectRatio.Y", 9);
                        break;
                    case 4:
                        CVarSetInteger("gHUDAspectRatio.X", 5);
                        CVarSetInteger("gHUDAspectRatio.Y", 3);
                        break;
                    case 5:
                        CVarSetInteger("gHUDAspectRatio.X", 8);
                        CVarSetInteger("gHUDAspectRatio.Y", 5);
                        break;
                    case 6:
                        CVarSetInteger("gHUDAspectRatio.X", 21);
                        CVarSetInteger("gHUDAspectRatio.Y", 9);
                        break;                    
                }
            }
            
            if (CVarGetInteger("gHUDAspectRatio.Selection", 0) == 1)
            {
                UIWidgets::CVarSliderInt("Horizontal: %d", "gHUDAspectRatio.X", 1, 100, 1);
                UIWidgets::CVarSliderInt("Vertical: %d", "gHUDAspectRatio.Y", 1, 100, 1);
            }

            ImGui::Dummy(ImVec2(ImGui::CalcTextSize("Nintendo 3DS (5:3)").x + 35, 0.0f));
            ImGui::EndMenu();
        }

        // VR options. The VR layer reads these CVars every frame, so changes apply live. Only useful
        // while running in VR (headset connected, or forced with --vr).
        if (UIWidgets::BeginMenu("VR")) {
            ImGui::TextDisabled("Star Fox 64 VR " VR_PORT_VERSION_STR);
            static const char* vrViewModes[] = { "Third Person", "First Person", "Cockpit", "Diorama", "Theater" };
            UIWidgets::CVarCombobox("View Mode", "gVRViewMode", vrViewModes, {
                .tooltip = "How the game is shown in VR. Third Person = the classic chase cam in stereo 3D; "
                           "First Person = ride at the pilot's seat with the Arwing ahead of you; Cockpit = "
                           "the game's own in-cockpit camera with the dashboard and glass; Diorama = the "
                           "level shrunk to a tabletop you lean around; Theater = flat screen floating in "
                           "front of you (most comfortable).",
                .defaultIndex = 0,
            });
            UIWidgets::CVarSliderFloat("World Scale (units/m)", "gVRWorldScale", 5.0f, 2000.0f, 25.0f, {
                .tooltip = "How big the world feels in Third Person. Smaller value = larger world. "
                           "First Person and Diorama have their own scale knobs.",
                .format = "%.0f",
                .step = 5.0f,
            });
            UIWidgets::CVarSliderFloat("Third Person Distance (m)", "gVRThirdPersonDist", -15.0f, 50.0f, 0.0f, {
                .tooltip = "Move the Third Person camera further behind the Arwing with positive values, or "
                           "closer with negative. 0 = the game's stock chase distance.",
                .format = "%.1f",
                .step = 0.5f,
            });
            UIWidgets::CVarSliderFloat("Eye Height (m)", "gVREyeHeight", -1.0f, 1.0f, 0.16f, {
                .tooltip = "Raise or lower the eye in Third Person. First Person has its own eye height.",
                .format = "%.2f",
                .step = 0.02f,
            });
            UIWidgets::CVarSliderFloat("First Person Forward (m)", "gVRFirstPersonFwd", -30.0f, 30.0f, 14.0f, {
                .tooltip = "How far the eye is pushed toward the Arwing in First Person. Needs to be large "
                           "enough to move you up into the ship, or the view looks like Third Person. Raise "
                           "until you're at the nose; if it moves the wrong way, use a negative value.",
                .format = "%.1f",
                .step = 0.5f,
            });
            UIWidgets::CVarSliderFloat("First Person World Scale (units/m)", "gVRFirstPersonScale", 5.0f, 500.0f,
                                       25.0f, {
                .tooltip = "How big the world feels in First Person. Separate from the main World Scale so it "
                           "won't change Third Person. Lower = larger world.",
                .format = "%.0f",
                .step = 5.0f,
            });
            UIWidgets::CVarSliderFloat("First Person Eye Height (m)", "gVRFirstPersonEyeHeight", -1.0f, 1.0f,
                                       0.0f, {
                .tooltip = "Raise or lower the eye in First Person.",
                .format = "%.2f",
                .step = 0.02f,
            });
            UIWidgets::CVarCheckbox("First Person Flip Cam", "gVRFlipCam", {
                .tooltip = "In First Person, roll the whole view with the ship - so barrel rolls spin you and "
                           "flying upside down puts you upside down. Turn off to keep the horizon level "
                           "(more comfortable). Cockpit always rolls with the ship; Third Person stays level.",
                .defaultValue = true,
            });
            UIWidgets::CVarCheckbox("Right Stick Turning", "gVRRightStickTurn", {
                .tooltip = "Steer left/right with the right stick's X axis too (motion controllers). Boost and "
                           "brake stay on its Y axis. While this is on, the flick-right-to-answer gesture is "
                           "disabled so a hard turn can't answer a call - use Y to answer instead.",
                .defaultValue = false,
            });
            UIWidgets::CVarSliderFloat("Diorama Distance (m)", "gVRDioramaDist", 0.1f, 3.0f, 0.25f, {
                .tooltip = "How far in front of you the tabletop sits (Diorama mode).",
                .format = "%.2f",
                .step = 0.01f,
            });
            UIWidgets::CVarSliderFloat("Diorama World Scale (units/m)", "gVRDioramaWorldScale", 20.0f, 2000.0f,
                                       800.0f, {
                .tooltip = "Size of the tabletop world (Diorama mode). Higher = smaller tabletop.",
                .format = "%.0f",
                .step = 5.0f,
            });
            UIWidgets::CVarSliderFloat("Diorama Height (m)", "gVRDioramaHeight", -1.5f, 1.0f, -0.16f, {
                .tooltip = "Raise or lower the tabletop to a comfortable height (Diorama mode).",
                .format = "%.2f",
                .step = 0.01f,
            });
            UIWidgets::CVarCheckbox("Cockpit Floor", "gVRCockpitFloor", {
                .tooltip = "Draw a solid floor under the pilot in Cockpit mode so you can't see the world "
                           "through the bottom of the cockpit when you look down. On-rails cockpit only.",
                .defaultValue = true,
            });
            UIWidgets::CVarSliderFloat("Cockpit Floor Height", "gVRCockpitFloorY", -50.0f, 20.0f, -6.0f, {
                .tooltip = "Raise or lower the Cockpit floor. Raise it until it sits just below your feet and "
                           "you can no longer see the world under the cockpit; lower it if it pokes into view.",
                .format = "%.0f",
                .step = 1.0f,
            });
            UIWidgets::CVarSliderFloat("Cockpit Floor Size", "gVRCockpitFloorSize", 10.0f, 200.0f, 42.0f, {
                .tooltip = "How far the Cockpit floor extends. Keep it small so it stays inside the cockpit; "
                           "raise it only if you can see the world past its edges.",
                .format = "%.0f",
                .step = 2.0f,
            });
            UIWidgets::CVarSliderFloat("Stereo Depth", "gVRStereo", 0.0f, 1.5f, 0.5f, {
                .tooltip = "Stereo separation strength. Lower is gentler / less eye strain.",
                .format = "%.2f",
                .step = 0.05f,
            });
            UIWidgets::CVarSliderFloat("HUD Size", "gVRHudScale", 0.2f, 1.2f, 0.35f, {
                .tooltip = "How much of your view the in-game HUD (radar, gauges, radio) fills. Smaller = "
                           "more central.",
                .format = "%.2f",
                .step = 0.05f,
            });
            UIWidgets::CVarSliderFloat("HUD Distance (m)", "gVRHudDist", 0.5f, 8.0f, 2.9f, {
                .format = "%.1f",
                .step = 0.1f,
            });
            UIWidgets::CVarCheckbox("Loop Cam (First Person)", "gVRLoopCam", {
                .tooltip = "Carry the view through loops and somersaults so you go over the top with the "
                           "ship. Off = the camera stays level and the world sweeps past instead.",
                .defaultValue = true,
            });
            UIWidgets::CVarSliderFloat("Cockpit Forward (m)", "gVRCockpitFwd", -5.0f, 20.0f, 0.0f, {
                .tooltip = "Cockpit view: push your seat forward into the cockpit (the stock cockpit camera "
                           "sits quite far back in VR).",
                .format = "%.2f",
                .step = 0.25f,
            });
            UIWidgets::CVarSliderFloat("Cockpit Height (m)", "gVRCockpitHeight", -2.0f, 5.0f, -0.15f, {
                .tooltip = "Cockpit view: raise your seat in small steps so the moving cockpit frame stops "
                           "blocking the view.",
                .format = "%.2f",
                .step = 0.05f,
            });
            UIWidgets::CVarCheckbox("Cockpit Glass", "gVRCockpitGlass", {
                .tooltip = "Show the canopy glass in Cockpit view. Turn off for a completely clear view.",
                .defaultValue = true,
            });
            UIWidgets::CVarCheckbox("Cockpit Comm Screen", "gVRCockpitComm", {
                .tooltip = "In Cockpit view, show the radio caller's portrait on a small dash panel while a "
                           "call is playing - the conversation happens in the cockpit instead of only on the "
                           "HUD. Use the sliders below to place it on your dash.",
                .defaultValue = true,
            });
            UIWidgets::CVarSliderFloat("Comm Screen Height", "gVRCockpitCommY", -12.0f, 12.0f, -2.0f, {
                .tooltip = "Raise or lower the comm panel on the dash (Cockpit view).",
                .format = "%.1f",
                .step = 0.25f,
            });
            UIWidgets::CVarSliderFloat("Comm Screen Forward", "gVRCockpitCommZ", -20.0f, 20.0f, 9.0f, {
                .tooltip = "Move the comm panel toward or away from you. If it isn't visible, sweep this "
                           "through its range while a call is playing; negative flips to the other side.",
                .format = "%.1f",
                .step = 0.5f,
            });
            UIWidgets::CVarSliderFloat("Comm Screen Size", "gVRCockpitCommSize", 0.5f, 12.0f, 2.6f, {
                .tooltip = "Size of the comm panel.",
                .format = "%.1f",
                .step = 0.1f,
            });
            UIWidgets::CVarCheckbox("Cutscenes in VR", "gVRCutscenes", {
                .tooltip = "Play cutscenes in full stereo instead of on the flat theater screen. The scripted "
                           "camera sweeps can be intense - off is the comfortable default.",
                .defaultValue = false,
            });
            UIWidgets::CVarCheckbox("Hide HUD", "gVRHideHud", {
                .tooltip = "Hide the 2D overlay (radar, gauges, rings, lives, boss health) for a clean "
                           "view. Radio messages and the aiming reticle stay.",
                .defaultValue = false,
            });
            UIWidgets::CVarCheckbox("HUD Locked to World", "gVRHudWorldLock", {
                .tooltip = "Pin the HUD plane to the room direction it was facing when enabled, so you can "
                           "turn your head and look around it. Off = the HUD rides your face.",
                .defaultValue = true,
            });
            UIWidgets::CVarSliderFloat("Menu Distance (m)", "gVRMenuDist", 1.0f, 8.0f, 3.2f, {
                .format = "%.1f",
                .step = 0.1f,
            });
            UIWidgets::CVarSliderFloat("Menu Size", "gVRMenuSize", 2.0f, 12.0f, 4.2f, {
                .format = "%.1f",
                .step = 0.2f,
            });
            UIWidgets::CVarSliderFloat("Settings Menu Opacity", "gVRImGuiOpacity", 0.3f, 1.0f, 1.0f, {
                .tooltip = "How solid this settings menu is on the VR panel. 1.0 = opaque (recommended for "
                           "readability).",
                .format = "%.2f",
                .step = 0.05f,
            });
            UIWidgets::CVarSliderFloat("Menu Pointer Speed", "gVRPointerSpeed", 0.5f, 3.0f, 1.0f, {
                .tooltip = "How fast the stick-driven pointer glides across this menu.",
                .format = "%.2f",
                .step = 0.05f,
            });
            UIWidgets::CVarCheckbox("World-Anchored Sky Dome", "gVRSkyDome", {
                .tooltip = "Replace the flat sky with a world-anchored 3D dome (blue sky + clouds on planets, "
                           "stars in space) that stays put as you turn your head. Turn off for the flat game sky.",
                .defaultValue = true,
            });
            UIWidgets::CVarCheckbox("Level Backdrops", "gVRLevelBackdrop", {
                .tooltip = "Layer the level's own 2D backdrop panorama over the sky dome (Sector Z's nebula, "
                           "Area 6's planet Venom, ...). Turn off for the clean dome sky if a level's panorama "
                           "reads too dark. Stages whose backdrop is the level itself always keep it.",
                .defaultValue = true,
            });
            UIWidgets::CVarSliderFloat("Sky Brightness", "gVRSkyBright", 0.2f, 2.0f, 1.0f, {
                .tooltip = "Brightness of the dome sky gradient. Raise if it looks too dark/foggy, lower if too "
                           "washed out.",
                .format = "%.2f",
                .step = 0.05f,
            });
            UIWidgets::CVarSliderFloat("Cloud Opacity", "gVRCloudAlpha", 0.0f, 3.0f, 1.0f, {
                .tooltip = "How solid the dome clouds are (planet levels). 0 = no clouds, higher = thicker.",
                .format = "%.2f",
                .step = 0.1f,
            });
            UIWidgets::CVarSliderFloat("Cloud Coverage", "gVRCloudCover", 0.0f, 1.0f, 1.0f, {
                .tooltip = "How much of the sky the dome clouds fill (planet levels). Lower = sparser, clearer sky.",
                .format = "%.2f",
                .step = 0.05f,
            });
            ImGui::SeparatorText("VR Rendering");
            UIWidgets::CVarCombobox("Fog (flying modes)", "gVRFogMode", vrFogModes, {
                .tooltip = "Distance fog in Third / First / Cockpit view. World Distance rebuilds the fog from "
                           "real distance so it reads correctly under the VR projection (and hides object "
                           "pop-in again). Stock N64 keeps the game's raw fog, which the per-eye projection "
                           "saturates - comparison only. Theater and Diorama always use the stock fog.",
                .defaultIndex = 1,
            });
            UIWidgets::CVarSliderFloat("Fog Start (m)", "gVRFogNear", 0.0f, 2000.0f, 150.0f, {
                .tooltip = "World Distance fog: how far out the fog begins, in meters at life size.",
                .format = "%.0f",
                .step = 25.0f,
            });
            UIWidgets::CVarSliderFloat("Fog Full (m)", "gVRFogFar", 50.0f, 5000.0f, 500.0f, {
                .tooltip = "World Distance fog: where the fog becomes solid. The game spawns/despawns objects "
                           "around 500 m, so values near that hide pop-in like the original fog did.",
                .format = "%.0f",
                .step = 25.0f,
            });
            UIWidgets::CVarCheckbox("Disable Backface Culling", "gVRDisableCulling", {
                .tooltip = "Draw both sides of every surface. Can fill culled back-face holes in VR, but on "
                           "this N64 engine it also lets back-faces overdraw front-faces so some geometry goes "
                           "missing - leave OFF unless you know you want it.",
                .defaultValue = false,
            });
            UIWidgets::CVarSliderFloat("Draw Distance (x)", "gVRDrawDistance", 1.0f, 8.0f, 1.0f, {
                .tooltip = "Pushes the far clip out so distant terrain and large objects aren't cut off. "
                           "Higher = see further; very high values can cause z-fighting far away.",
                .format = "%.1f",
                .step = 0.5f,
            });
            if (UIWidgets::CVarSliderFloat("Internal Resolution (x)", "gInternalResolution", 0.5f, 4.0f, 1.0f, {
                    .tooltip = "Supersampling: render at this multiple of native resolution then downscale for "
                               "a much sharper, cleaner image in the headset. 1.5-2x is a big upgrade; higher "
                               "costs performance.",
                    .format = "%.2f",
                    .step = 0.1f,
                })) {
                Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(
                    CVarGetFloat("gInternalResolution", 1.0f));
            }
            UIWidgets::CVarCheckbox("Mixed Reality (passthrough)", "gVRPassthrough", {
                .tooltip = "Show your real room behind the game using headset passthrough (Quest). Locks the "
                           "view to Diorama - the shrunk level sits on your table.",
            });
            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Accessibility")) {
            UIWidgets::CVarCheckbox("Disable Gorgon (Area 6 boss) screen flashes", "gDisableGorgonFlash", {
                .tooltip = "Gorgon flashes the screen repeatedly when firing its beam or when teleporting, which causes eye pain for some players and may be harmful to those with photosensitivity.",
                .defaultValue = false
            });
            UIWidgets::CVarCheckbox("Add outline to Arwing and Wolfen in radar", "gFighterOutlines", {
                .tooltip = "Increases visibility of ships in the radar.",
                .defaultValue = false
            });
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
}

void DrawCheatsMenu() {
    if (UIWidgets::BeginMenu("Cheats")) {
        UIWidgets::CVarCheckbox("Infinite Lives", "gInfiniteLives");
        UIWidgets::CVarCheckbox("Invincible", "gInvincible");
        UIWidgets::CVarCheckbox("Unbreakable Wings", "gUnbreakableWings");
        UIWidgets::CVarCheckbox("Infinite Bombs", "gInfiniteBombs");
        UIWidgets::CVarCheckbox("Infinite Boost/Brake", "gInfiniteBoost");
        UIWidgets::CVarCheckbox("Hyper Laser", "gHyperLaser");
        UIWidgets::CVarSliderInt("Laser Range Multiplier: %d%%", "gLaserRangeMult", 15, 800, 100,
            { .tooltip = "Changes how far your lasers fly." });
        UIWidgets::CVarCheckbox("Rapid-fire mode", "gRapidFire", {
                .tooltip = "Hold A to keep firing. Release A to start charging a shot."
            });
            if (CVarGetInteger("gRapidFire", 0) == 1) {
                ImGui::Dummy(ImVec2(22.0f, 0.0f));
                ImGui::SameLine();
                UIWidgets::CVarCheckbox("Hold L to Charge", "gLtoCharge", {
                    .tooltip = "If you prefer to not have auto-charge."
                });
            }
        UIWidgets::CVarCheckbox("Self destruct button", "gHit64SelfDestruct", {
                .tooltip = "Press Down on the D-PAD to instantly self destruct."
            });
        UIWidgets::CVarCheckbox("Start with Falco dead", "gHit64FalcoDead", {
                .tooltip = "Start the level with with Falco dead."
            });
        UIWidgets::CVarCheckbox("Start with Slippy dead", "gHit64SlippyDead", {
                .tooltip = "Start the level with with Slippy dead."
            });
        UIWidgets::CVarCheckbox("Start with Peppy dead", "gHit64PeppyDead", {
                .tooltip = "Start the level with with Peppy dead."
            });
        
        UIWidgets::CVarCheckbox("Score Editor", "gScoreEditor", { .tooltip = "Enable the score editor" });

        if (CVarGetInteger("gScoreEditor", 0) == 1) {
            UIWidgets::CVarSliderInt("Score: %d", "gScoreEditValue", 0, 999, 0,
                { .tooltip = "Increase or decrease the current mission score number" });
        }

        ImGui::EndMenu();
    }
}

static const char* debugInfoPages[6] = {
    "Object",
    "Check Surface",
    "Map",
    "Stage",
    "Effect",
    "Enemy",
};

static const char* logLevels[] = {
    "trace", "debug", "info", "warn", "error", "critical", "off",
};

void DrawDebugMenu() {
    if (UIWidgets::BeginMenu("Developer")) {
        if (UIWidgets::CVarCombobox("Log Level", "gDeveloperTools.LogLevel", logLevels, {
            .tooltip = "The log level determines which messages are printed to the "
                        "console. This does not affect the log file output",
            .defaultIndex = 1,
        })) {
            Ship::Context::GetInstance()->GetLogger()->set_level((spdlog::level::level_enum)CVarGetInteger("gDeveloperTools.LogLevel", 1));
        }

#ifdef __SWITCH__
        if (UIWidgets::CVarCombobox("Switch CPU Profile", "gSwitchPerfMode", SWITCH_CPU_PROFILES, {
            .tooltip = "Switches the CPU profile to a different one",
            .defaultIndex = (int)Ship::SwitchProfiles::STOCK
        })) {
            SPDLOG_INFO("Profile:: %s", SWITCH_CPU_PROFILES[CVarGetInteger("gSwitchPerfMode", (int)Ship::SwitchProfiles::STOCK)]);
            Ship::Switch::ApplyOverclock();
        }
#endif

        UIWidgets::WindowButton("Gfx Debugger", "gGfxDebuggerEnabled", GameUI::mGfxDebuggerWindow, {
            .tooltip = "Enables the Gfx Debugger window, allowing you to input commands, type help for some examples"
        });

        // UIWidgets::CVarCheckbox("Debug mode", "gEnableDebugMode", {
        //     .tooltip = "TBD"
        // });

        UIWidgets::CVarCheckbox("Level Selector", "gLevelSelector", {
            .tooltip = "Allows you to select any level from the main menu"
        });

        UIWidgets::CVarCheckbox("Skip Briefing", "gSkipBriefing", {
            .tooltip = "Allows you to skip the briefing sequence in level select"
        });

        UIWidgets::CVarCheckbox("Enable Expert Mode", "gForceExpertMode", {
            .tooltip = "Allows you to force expert mode"
        });

        UIWidgets::CVarCheckbox("SFX Jukebox", "gSfxJukebox", {
            .tooltip = "Press L in the Expert Sound options to play sound effects from the game"
        });

        UIWidgets::CVarCheckbox("Disable Starfield interpolation", "gDisableStarsInterpolation", {
            .tooltip = "Disable starfield interpolation to increase performance on slower CPUs"
        });
        UIWidgets::CVarCheckbox("Disable Gamma Boost (Needs reload)", "gGraphics.GammaMode", {
            .tooltip = "Disables the game's Built-in Gamma Boost. Useful for modders",
            .defaultValue = false
        });

        UIWidgets::CVarCheckbox("Spawner Mod", "gSpawnerMod", {
            .tooltip = "Spawn Scenery, Actors, Bosses, Sprites, Items, Effects and even Event Actors.\n"
                       "\n"
                       "Controls:\n"
                       "D-Pad left and right to set the object Id.\n"
                       "C-Right to change between spawn modes.\n"
                       "Analog stick sets the spawn position.\n"
                       "L-Trigger to spawn the object.\n"
                       "D-Pad UP to kill all objects.\n"
                       "D-Pad DOWN to freeze/unfreeze the ship speed.\n"
                       "WARNING: Spawning an object that's not loaded in memory will likely result in a crash."
        });

        UIWidgets::CVarCheckbox("Jump To Map", "gDebugJumpToMap", {
            .tooltip = "Press Z + R + C-UP to get back to the map"
        });

        UIWidgets::CVarCheckbox("L To Warp Zone", "gDebugWarpZone", {
            .tooltip = "Press L to get into the Warp Zone"
        });

        UIWidgets::CVarCheckbox("L to Level Complete", "gDebugLevelComplete", {
            .tooltip = "Press L to Level Complete"
        });

        UIWidgets::CVarCheckbox("L to All-Range mode", "gDebugJumpToAllRange", {
            .tooltip = "Press L to switch to All-Range mode"
        });

        UIWidgets::CVarCheckbox("Disable Collision", "gDebugNoCollision", {
            .tooltip = "Disable vehicle collision"
        });
        
        UIWidgets::CVarCheckbox("Speed Control", "gDebugSpeedControl", {
            .tooltip = "Arwing speed control. Use D-PAD Left and Right to Increase/Decrease the Arwing Speed, D-PAD Down to stop movement."
        });

        UIWidgets::CVarCheckbox("Debug Ending", "gDebugEnding", {
            .tooltip = "Jump to credits at the main menu"
        });

        UIWidgets::CVarCheckbox("Debug Pause", "gLToDebugPause", {
            .tooltip = "Press L to toggle Debug Pause"
        });
        if (CVarGetInteger("gLToDebugPause", 0)) {
            ImGui::Dummy(ImVec2(22.0f, 0.0f));
            ImGui::SameLine();
            UIWidgets::CVarCheckbox("Frame Advance", "gLToFrameAdvance", {
            .tooltip = "Pressing L again advances one frame instead"
        });
        }

        if (CVarGetInteger(StringHelper::Sprintf("gCheckpoint.%d.Set", gCurrentLevel).c_str(), 0)) {
            if (UIWidgets::Button("Clear Checkpoint")) {
                CVarClear(StringHelper::Sprintf("gCheckpoint.%d.Set", gCurrentLevel).c_str());
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        } else if (gPlayer != NULL && gGameState == GSTATE_PLAY) {
            if (UIWidgets::Button("Set Checkpoint")) {
                CVarSetInteger(StringHelper::Sprintf("gCheckpoint.%d.Set", gCurrentLevel).c_str(), 1);
                CVarSetInteger(StringHelper::Sprintf("gCheckpoint.%d.gSavedPathProgress", gCurrentLevel).c_str(), gGroundSurface);
                CVarSetFloat(StringHelper::Sprintf("gCheckpoint.%d.gSavedPathProgress", gCurrentLevel).c_str(), (-gPlayer->pos.z) - 250.0f);
                CVarSetInteger(StringHelper::Sprintf("gCheckpoint.%d.gSavedObjectLoadIndex", gCurrentLevel).c_str(), gObjectLoadIndex);
                Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        }

        UIWidgets::Spacer(0);

        UIWidgets::WindowButton("Stats", "gStatsEnabled", GameUI::mStatsWindow, {
            .tooltip = "Shows the stats window, with your FPS and frametimes, and the OS you're playing on"
        });
        UIWidgets::WindowButton("Console", "gConsoleEnabled", GameUI::mConsoleWindow, {
            .tooltip = "Enables the console window, allowing you to input commands, type help for some examples"
        });

        ImGui::EndMenu();
    }
}

void GameMenuBar::DrawElement() {
    if(ImGui::BeginMenuBar()){
        DrawMenuBarIcon();

        DrawGameMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawSettingsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawEnhancementsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawCheatsMenu();

        ImGui::SetCursorPosY(0.0f);

        ImGui::SetCursorPosY(0.0f);

        DrawDebugMenu();

        ImGui::EndMenuBar();
    }
}