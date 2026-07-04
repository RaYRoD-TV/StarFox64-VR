#include "Engine.h"
#include "ui/ImguiUI.h"
#include "StringHelper.h"

#include "extractor/GameExtractor.h"
#include "libultraship/src/Context.h"
#include "libultraship/src/controller/controldevice/controller/mapping/ControllerDefaultMappings.h"
#include "resource/type/ResourceType.h"
#include "resource/importers/AnimFactory.h"
#include "resource/importers/ColPolyFactory.h"
#include "resource/importers/EnvSettingsFactory.h"
#include "resource/importers/GenericArrayFactory.h"
#include "resource/importers/HitboxFactory.h"
#include "resource/importers/LimbFactory.h"
#include "resource/importers/MessageFactory.h"
#include "resource/importers/MessageLookupFactory.h"
#include "resource/importers/ObjectInitFactory.h"
#include "resource/importers/ScriptCommandFactory.h"
#include "resource/importers/ScriptFactory.h"
#include "resource/importers/SkeletonFactory.h"
#include "resource/importers/Vec3fFactory.h"
#include "resource/importers/Vec3sFactory.h"

#include "resource/importers/audio/AudioTableFactory.h"
#include "resource/importers/audio/BookFactory.h"
#include "resource/importers/audio/DrumFactory.h"
#include "resource/importers/audio/EnvelopeFactory.h"
#include "resource/importers/audio/InstrumentFactory.h"
#include "resource/importers/audio/LoopFactory.h"
#include "resource/importers/audio/SampleFactory.h"
#include "resource/importers/audio/SoundFontFactory.h"

#include "port/interpolation/FrameInterpolation.h"
#include "port/vr/vr.h"
#include <Fast3D/Fast3dWindow.h>
#include <DisplayListFactory.h>
#include <TextureFactory.h>
#include <MatrixFactory.h>
#include <BlobFactory.h>
#include <VertexFactory.h>
#include "audio/GameAudio.h"
#include "port/patches/DisplayListPatch.h"
#include "port/mods/PortEnhancements.h"

#include <Fast3D/interpreter.h>
#include <filesystem>
#if defined(ENABLE_VR) && defined(_WIN32)
#include <SDL2/SDL.h>
#include <imgui_impl_sdl2.h> // ImGui_ImplSDL2_SetGamepadMode - force the menu pad nav to (re)open the device
#endif

#ifdef __SWITCH__
#include <port/switch/SwitchImpl.h>
#endif

namespace fs = std::filesystem;

extern "C" {
bool prevAltAssets = false;
bool gEnableGammaBoost = true;
#include <sf64thread.h>
#include <macros.h>
#include "sf64audio_provisional.h"
void AudioThread_CreateNextAudioBuffer(int16_t* samples, uint32_t num_samples);
}

std::vector<uint8_t*> MemoryPool;
GameEngine* GameEngine::Instance;

GameEngine::GameEngine() {
    // Initialize context properties early to recognize paths properly for non-portable builds
    this->context = Ship::Context::CreateUninitializedInstance("Starship", "ship", "starship.cfg.json");

#ifdef __SWITCH__
    Ship::Switch::Init(Ship::PreInitPhase);
    Ship::Switch::Init(Ship::PostInitPhase);
#endif

    std::vector<std::string> archiveFiles;
    const std::string main_path = Ship::Context::GetPathRelativeToAppDirectory("sf64.o2r");
    const std::string assets_path = Ship::Context::LocateFileAcrossAppDirs("starship.o2r");

#ifdef _WIN32
    AllocConsole();
#endif

    if (std::filesystem::exists(main_path)) {
        archiveFiles.push_back(main_path);
    } else {
        if (ShowYesNoBox("Starship - Asset Extraction", "Please provide a Starfox 64 ROM.\n\nSupported Versions:\nUS 1.0\nUS 1.1\n\nAssets will be extracted into an O2R file.") == IDYES) {
            if(!GenAssetFile()){
                ShowMessage("Error", "An error occured, no O2R file was generated.\n\nExiting...");
                exit(1);
            } else {
                archiveFiles.push_back(main_path);
            }

            if (ShowYesNoBox("Extraction Complete", "ROM Extracted. Extract another?\n\n Starship supports JP and EU ROMs for voice replacement.\n Voice replacement ROM assets can also be installed in:\n Settings->Language->Install JP/EU Audio") == IDYES) {
                if(!GenAssetFile()){
                    ShowMessage("Error", "An error occured, no O2R file was generated.");
                }
            }
        } else {
            exit(1);
        }
    }

    if (std::filesystem::exists(assets_path)) {
        archiveFiles.push_back(assets_path);
    }

    if (const std::string patches_path = Ship::Context::GetPathRelativeToAppDirectory("mods");
        !patches_path.empty()) {
        if (!std::filesystem::exists(patches_path)) {
            std::filesystem::create_directories(patches_path);
        }

        if (std::filesystem::is_directory(patches_path)) {
            for (const auto& p : std::filesystem::recursive_directory_iterator(patches_path)) {
                const auto ext = p.path().extension().string();
                if (StringHelper::IEquals(ext, ".otr") || StringHelper::IEquals(ext, ".o2r")) {
                    archiveFiles.push_back(p.path().generic_string());
                }

                if (StringHelper::IEquals(ext, ".zip")) {
                    SPDLOG_WARN("Zip files should be only used for development purposes, not for distribution");
                    archiveFiles.push_back(p.path().generic_string());
                }
            }
        }
    }

    this->context->InitConfiguration();    // without this line InitConsoleVariables fails at Config::Reload()
    this->context->InitConsoleVariables(); // without this line the controldeck constructor failes in
                                           // ShipDeviceIndexMappingManager::UpdateControllerNamesFromConfig()

#if defined(ENABLE_VR) && defined(_WIN32)
    // VR: SDL_main decided whether to enable VR (--vr / --novr / headset auto-detect) and called
    // vr_request_enable(). OpenXR binds to the WGL context, so force the OpenGL backend before the
    // window is created (context->Init below reads Window.Backend.Id to choose the renderer).
    if (vr_is_requested()) {
        this->context->GetConfig()->SetInt("Window.Backend.Id", (int32_t)Ship::WindowBackend::FAST3D_SDL_OPENGL);
        this->context->GetConfig()->SetString("Window.Backend.Name", "OpenGL");
        SPDLOG_INFO("[VR] requested - forced OpenGL backend (OpenXR binds to WGL)");
        // Gamepad menu navigation - the mouse cursor isn't usable in the headset, so the menu must be
        // drivable with the controller. (LUS blocks game input itself while the menu is open with this on.)
        CVarSetInteger("gControlNav", 1);
        // VR steals OS focus to the compositor, so the desktop SDL window runs in the background. Without
        // this, SDL stops updating gamepad state for the unfocused window and the ImGui menu nav goes dead.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        CVarSave();
    }
#endif

    auto defaultMappings = std::make_shared<Ship::ControllerDefaultMappings>(
        // KeyboardKeyToButtonMappings - use built-in LUS defaults
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>>(),
        // KeyboardKeyToAxisDirectionMappings - use built-in LUS defaults
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>(),
        // SDLButtonToButtonMappings
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>>{
            { BTN_A, { SDL_CONTROLLER_BUTTON_A } },
            { BTN_B, { SDL_CONTROLLER_BUTTON_X } },
            { BTN_START, { SDL_CONTROLLER_BUTTON_START } },
            { BTN_CLEFT, { SDL_CONTROLLER_BUTTON_Y } },
            { BTN_CDOWN, { SDL_CONTROLLER_BUTTON_B } },
            { BTN_DUP, { SDL_CONTROLLER_BUTTON_DPAD_UP } },
            { BTN_DDOWN, { SDL_CONTROLLER_BUTTON_DPAD_DOWN } },
            { BTN_DLEFT, { SDL_CONTROLLER_BUTTON_DPAD_LEFT } },
            { BTN_DRIGHT, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT } },
            { BTN_R, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER } },
            { BTN_Z, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER } }
        },
        // SDLButtonToAxisDirectionMappings - use built-in LUS defaults
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GameControllerButton>>>(),
        // SDLAxisDirectionToButtonMappings
        std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>>{
            { BTN_CLEFT, { { SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1 } } },
            { BTN_CDOWN, { { SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1 } } },
            { BTN_CUP, { { SDL_CONTROLLER_AXIS_RIGHTY, -1 } } },
            { BTN_CRIGHT, { { SDL_CONTROLLER_AXIS_RIGHTX, 1 } } }
        },
        // SDLAxisDirectionToAxisDirectionMappings - use built-in LUS defaults
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, std::pair<SDL_GameControllerAxis, int32_t>>>>()
    );
    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>(), defaultMappings);

    this->context->InitResourceManager(archiveFiles, {}, 3); // without this line InitWindow fails in Gui::Init()
    this->context->InitConsole(); // without this line the GuiWindow constructor fails in ConsoleWindow::InitElement()

    auto window = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({}));

    auto audioChannelsSetting = Ship::Context::GetInstance()->GetConfig()->GetCurrentAudioChannelsSetting();
    this->context->Init(archiveFiles, {}, 3, { 32000, 1024, 1680, audioChannelsSetting }, window, controlDeck);

#ifndef __SWITCH__
    Ship::Context::GetInstance()->GetLogger()->set_level(
        (spdlog::level::level_enum) CVarGetInteger("gDeveloperTools.LogLevel", 1));
    Ship::Context::GetInstance()->GetLogger()->set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");
#endif

    auto loader = context->GetResourceManager()->GetResourceLoader();
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAnimV0>(), RESOURCE_FORMAT_BINARY,
                                    "Animation", static_cast<uint32_t>(SF64::ResourceType::AnimData), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SF64::ResourceType::Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryLimbV0>(), RESOURCE_FORMAT_BINARY,
                                    "Limb", static_cast<uint32_t>(SF64::ResourceType::Limb), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryMessageV0>(), RESOURCE_FORMAT_BINARY,
                                    "Message", static_cast<uint32_t>(SF64::ResourceType::Message), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryMessageLookupV0>(),
                                    RESOURCE_FORMAT_BINARY, "MessageTable",
                                    static_cast<uint32_t>(SF64::ResourceType::MessageTable), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryEnvSettingsV0>(),
                                    RESOURCE_FORMAT_BINARY, "EnvSettings",
                                    static_cast<uint32_t>(SF64::ResourceType::Environment), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryObjectInitV0>(), RESOURCE_FORMAT_BINARY,
                                    "ObjectInit", static_cast<uint32_t>(SF64::ResourceType::ObjectInit), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryHitboxV0>(), RESOURCE_FORMAT_BINARY,
                                    "Hitbox", static_cast<uint32_t>(SF64::ResourceType::Hitbox), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryScriptV0>(), RESOURCE_FORMAT_BINARY,
                                    "Script", static_cast<uint32_t>(SF64::ResourceType::Script), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryScriptCMDV0>(), RESOURCE_FORMAT_BINARY,
                                    "ScriptCMD", static_cast<uint32_t>(SF64::ResourceType::ScriptCmd), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryColPolyV0>(), RESOURCE_FORMAT_BINARY,
                                    "ColPoly", static_cast<uint32_t>(SF64::ResourceType::ColPoly), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryVec3fV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vec3f", static_cast<uint32_t>(SF64::ResourceType::Vec3f), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryVec3sV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vec3s", static_cast<uint32_t>(SF64::ResourceType::Vec3s), 0);
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryGenericArrayV0>(),
                                    RESOURCE_FORMAT_BINARY, "GenericArray",
                                    static_cast<uint32_t>(SF64::ResourceType::GenericArray), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLVertexV0>(), RESOURCE_FORMAT_XML, "Vertex",
                                    static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLDisplayListV0>(), RESOURCE_FORMAT_XML,
                                    "DisplayList", static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);

    loader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY,
                                    "Blob", static_cast<uint32_t>(Ship::ResourceType::Blob), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAudioTableV0>(), RESOURCE_FORMAT_BINARY,
                                    "AudioTable", static_cast<uint32_t>(SF64::ResourceType::AudioTable), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAdpcmBookV0>(), RESOURCE_FORMAT_BINARY,
                                    "AdpcmBook", static_cast<uint32_t>(SF64::ResourceType::AdpcmBook), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryDrumV0>(), RESOURCE_FORMAT_BINARY,
                                    "Drum", static_cast<uint32_t>(SF64::ResourceType::Drum), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryEnvelopeV0>(), RESOURCE_FORMAT_BINARY,
                                    "Envelope", static_cast<uint32_t>(SF64::ResourceType::Envelope), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryInstrumentV0>(), RESOURCE_FORMAT_BINARY,
                                    "Instrument", static_cast<uint32_t>(SF64::ResourceType::Instrument), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinaryAdpcmLoopV0>(), RESOURCE_FORMAT_BINARY,
                                    "AdpcmLoop", static_cast<uint32_t>(SF64::ResourceType::AdpcmLoop), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinarySampleV1>(), RESOURCE_FORMAT_BINARY,
                                    "Sample", static_cast<uint32_t>(SF64::ResourceType::Sample), 1);
    
    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryXMLSampleV0>(), RESOURCE_FORMAT_XML,
                                    "Sample", static_cast<uint32_t>(SF64::ResourceType::Sample), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryBinarySoundFontV0>(), RESOURCE_FORMAT_BINARY,
                                    "SoundFont", static_cast<uint32_t>(SF64::ResourceType::SoundFont), 0);

    loader->RegisterResourceFactory(std::make_shared<SF64::ResourceFactoryXMLSoundFontV0>(), RESOURCE_FORMAT_XML,
                                    "SoundFont", static_cast<uint32_t>(SF64::ResourceType::SoundFont), 0);

    prevAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    gEnableGammaBoost = CVarGetInteger("gGraphics.GammaMode", 0) == 0;
    context->GetResourceManager()->SetAltAssetsEnabled(prevAltAssets);
}

bool GameEngine::GenAssetFile(bool exitOnFail) {
    auto extractor = new GameExtractor();

    if (!extractor->SelectGameFromUI()) {
        ShowMessage("Error", "No ROM selected.\n\nExiting...");
        if (exitOnFail) {
            exit(1);
        } else {
            return false;
        }
    }

    auto game = extractor->ValidateChecksum();
    if (!game.has_value()) {
        ShowMessage("Unsupported ROM", "The provided ROM is not supported.\n\nCheck the readme for a list of supported versions.");
        if (exitOnFail) {
            exit(1);
        } else {
            return false;
        }
    }

    ShowMessage(("Starship - Extraction - Found " + game.value()).c_str(), "The extraction process will now begin.\n\nThis may take a few minutes.", SDL_MESSAGEBOX_INFORMATION);

    return extractor->GenerateOTR();
}

void GameEngine::Create() {
    const auto instance = Instance = new GameEngine();
    instance->AudioInit();
    DisplayListPatch::Run();
    GameUI::SetupGuiElements();
#if defined(__SWITCH__) || defined(__WIIU__)
    CVarRegisterInteger("gControlNav", 1); // always enable controller nav on switch/wii u
    osSetTime(0);
#endif
    PortEnhancements_Init();
}

void GameEngine::Destroy() {
    PortEnhancements_Exit();
    AudioExit();
    for (auto ptr : MemoryPool) {
        free(ptr);
    }
    MemoryPool.clear();
#ifdef __SWITCH__
    Ship::Switch::Exit();
#endif
}

void GameEngine::StartFrame() const {
    using Ship::KbScancode;
    const int32_t dwScancode = this->context->GetWindow()->GetLastScancode();
    this->context->GetWindow()->SetLastScancode(-1);

    switch (dwScancode) {
        case KbScancode::LUS_KB_TAB: {
            // Toggle HD Assets
            CVarSetInteger("gEnhancements.Mods.AlternateAssets", !CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0));
            break;
        }
        case KbScancode::LUS_KB_F4: {
            gNextGameState = GSTATE_BOOT;
            break;
        }
        default:
            break;
    }
}

#if 0
// Values for 44100 hz
#define SAMPLES_HIGH 752
#define SAMPLES_LOW 720
#else
// Values for 32000 hz
#define SAMPLES_HIGH 560
#define SAMPLES_LOW 528

#endif

#define MAX_NUM_AUDIO_CHANNELS 6

extern "C" u16 audBuffer = 0;
#include <sf64audio_provisional.h>

extern "C" volatile s32 gAudioTaskCountQ;
int frames = 0;
extern "C" int countermin = 0;

extern "C" unsigned short samples_high = SAMPLES_HIGH;
extern "C" unsigned short samples_low = SAMPLES_LOW;

void GameEngine::HandleAudioThread() {
#ifdef PIPE_DEBUG
    std::ofstream outfile("audio.bin", std::ios::binary | std::ios::app);
#endif
    while (audio.running) {
        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            while (!audio.processing && audio.running) {
                audio.cv_to_thread.wait(Lock);
            }
            if (!audio.running) {
                break;
            }
        }

        // gVIsPerFrame = 2;

#define AUDIO_FRAMES_PER_UPDATE (gVIsPerFrame > 0 ? gVIsPerFrame : 1)
#define MAX_AUDIO_FRAMES_PER_UPDATE 5 // Compile-time constant with max value of gVIsPerFrame

        std::unique_lock<std::mutex> Lock(audio.mutex);
        int samples_left = AudioPlayerBuffered();
        u32 num_audio_samples = samples_left < AudioPlayerGetDesiredBuffered() ? (((samples_high))) : (((samples_low)));

        frames++;

        if (frames > 60) {
            countermin++;
        }

        const int32_t num_audio_channels = GetNumAudioChannels();

        s16 audio_buffer[SAMPLES_HIGH * MAX_NUM_AUDIO_CHANNELS * MAX_AUDIO_FRAMES_PER_UPDATE] = { 0 };
        for (int i = 0; i < AUDIO_FRAMES_PER_UPDATE; i++) {
            AudioThread_CreateNextAudioBuffer(audio_buffer + i * (num_audio_samples * num_audio_channels),
                                              num_audio_samples);
        }
#ifdef PIPE_DEBUG
        if (outfile.is_open()) {
            outfile.write(reinterpret_cast<char*>(audio_buffer),
                          num_audio_samples * (sizeof(int16_t) * num_audio_channels * AUDIO_FRAMES_PER_UPDATE));
        }
#endif
        AudioPlayerPlayFrame((u8*) audio_buffer,
                             num_audio_samples * (sizeof(int16_t) * num_audio_channels * AUDIO_FRAMES_PER_UPDATE));
        
        audio.processing = false;
        audio.cv_from_thread.notify_one();
    }
#ifdef PIPE_DEBUG
    outfile.close();
#endif
}

void GameEngine::StartAudioFrame() {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.processing = true;
    }
    audio.cv_to_thread.notify_one();
}

void GameEngine::EndAudioFrame() {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        while (audio.processing) {
            audio.cv_from_thread.wait(Lock);
        }
    }
}

void GameEngine::AudioInit() {
    if (!audio.running) {
        audio.running = true;
        audio.thread = std::thread(HandleAudioThread);
    }
}

void GameEngine::AudioExit() {
    {
        std::unique_lock lock(audio.mutex);
        audio.running = false;
    }
    audio.cv_to_thread.notify_all();
    // Wait until the audio thread quit
    audio.thread.join();
}

#if defined(ENABLE_VR) && defined(_WIN32)
extern "C" GameState gGameState;    // src/engine/fox_game.c; GSTATE_PLAY == 7 (sf64thread.h)
extern "C" s32 VrGame_IsCinematic(void); // src/engine/fox_play.c; 1 during scripted/cutscene camera
extern "C" s32 VrMenu_IsOpen(void);      // src/engine/vr_menu.c; 1 while the native VR options overlay is up
extern "C" Gfx* build_sky_dome_vr(void); // src/engine/vr_skydome.c; rebuilds the 3D sky dome DL for this frame
extern "C" Gfx* gVrSkyDomeGfx;           // the built dome DL (null until built); passed to RunVrEye

// Feed ImGui's menu gamepad navigation directly from the SDL controllers, INDEPENDENT of OS window
// focus. In VR the headset compositor holds focus, so the desktop SDL window is "background" and ImGui's
// SDL2 backend auto-read (SDL_GameControllerGetButton) returns 0 for every button -> menu renders but
// gets no nav input. SDL_GameControllerUpdate() force-refreshes the pad state regardless of focus, then we
// push the exact ImGuiKey_Gamepad* nav events ImGui needs. Co-exists with the auto-read (same keys agree).
static void VrFeedImGuiGamepadNav() {
    SDL_GameControllerUpdate(); // refresh pad state even when the SDL window is not the focused window
    // Open + read ALL game controllers and MERGE their input, so whichever device is actually being
    // held drives the menu (a phantom/virtual first pad can't starve nav).
    SDL_GameController* pads[8];
    int padCount = 0;
    for (int i = 0, n = SDL_NumJoysticks(); i < n && padCount < 8; i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* gc = SDL_GameControllerOpen(i); // ref-counted; same handles ControlDeck holds
            if (gc) {
                pads[padCount++] = gc;
            }
        }
    }
    if (padCount == 0) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    auto btn = [&](ImGuiKey key, SDL_GameControllerButton b) {
        bool down = false;
        for (int p = 0; p < padCount; p++) {
            down |= SDL_GameControllerGetButton(pads[p], b) != 0;
        }
        io.AddKeyEvent(key, down);
    };
    auto axis = [&](ImGuiKey key, SDL_GameControllerAxis a, int lo, int hi) {
        float best = 0.0f;
        for (int p = 0; p < padCount; p++) {
            float v = (float)(SDL_GameControllerGetAxis(pads[p], a) - lo) / (float)(hi - lo);
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            if (v > best) {
                best = v;
            }
        }
        io.AddKeyAnalogEvent(key, best > 0.1f, best);
    };
    const int dz = 8000; // stick dead zone (SDL's suggested value)
    btn(ImGuiKey_GamepadDpadLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    btn(ImGuiKey_GamepadDpadRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    btn(ImGuiKey_GamepadDpadUp, SDL_CONTROLLER_BUTTON_DPAD_UP);
    btn(ImGuiKey_GamepadDpadDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    btn(ImGuiKey_GamepadFaceDown, SDL_CONTROLLER_BUTTON_A); // activate / select
    btn(ImGuiKey_GamepadFaceRight, SDL_CONTROLLER_BUTTON_B); // cancel / back
    btn(ImGuiKey_GamepadFaceLeft, SDL_CONTROLLER_BUTTON_X);
    btn(ImGuiKey_GamepadFaceUp, SDL_CONTROLLER_BUTTON_Y);
    btn(ImGuiKey_GamepadL1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    btn(ImGuiKey_GamepadR1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    axis(ImGuiKey_GamepadLStickLeft, SDL_CONTROLLER_AXIS_LEFTX, -dz, -32768);
    axis(ImGuiKey_GamepadLStickRight, SDL_CONTROLLER_AXIS_LEFTX, +dz, +32767);
    axis(ImGuiKey_GamepadLStickUp, SDL_CONTROLLER_AXIS_LEFTY, -dz, -32768);
    axis(ImGuiKey_GamepadLStickDown, SDL_CONTROLLER_AXIS_LEFTY, +dz, +32767);
}

// Motion controllers drive the ImGui menu too: map their state onto ImGui's gamepad nav keys. The
// left-stick CLICK feeds ImGuiKey_GamepadBack - libultraship's menubar toggle - so the settings menu
// can be opened and closed from inside the headset; with the menu open, the left stick navigates and
// A / right trigger activates, B / left trigger backs out.
static void VrFeedImGuiFromVrControllers(bool menuOpen) {
    if (!vr_controllers_active()) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    unsigned vb = vr_controller_buttons();
    io.AddKeyEvent(ImGuiKey_GamepadBack, (vb & VR_BTN_LSTICK) != 0); // menubar toggle (see Gui::StartFrame)
    if (!menuOpen) {
        return;
    }
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (vb & (VR_BTN_A | VR_BTN_RTRIGGER)) != 0);  // activate
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (vb & (VR_BTN_B | VR_BTN_LTRIGGER)) != 0); // cancel / back
    float ls[2];
    vr_controller_stick(0, ls);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, ls[0] < -0.1f, ls[0] < 0.0f ? -ls[0] : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, ls[0] > 0.1f, ls[0] > 0.0f ? ls[0] : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, ls[1] > 0.1f, ls[1] > 0.0f ? ls[1] : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, ls[1] < -0.1f, ls[1] < 0.0f ? -ls[1] : 0.0f);
}

// Compute the mouse position ImGui should use, INDEPENDENT of OS window focus/hover, so the software
// cursor (io.MouseDrawCursor) always renders on the VR menu panel. ImGui's SDL2 backend only updates
// io.MousePos while the window is focused or hovered; in VR the headset compositor usually holds focus,
// so the moment the OS cursor drifts off the unfocused game window the backend pushes a mouse-leave
// (MousePos = -FLT_MAX) and ImGui stops drawing the cursor. Read the global mouse and clamp it into the
// window rect so the cursor pins to the panel edge instead of vanishing. With multi-viewports enabled
// ImGui expects ABSOLUTE desktop coordinates; without, window-local.
static bool VrComputeImGuiMousePos(float* outX, float* outY) {
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    if (win == nullptr) {
        return false;
    }
    int gx = 0, gy = 0, wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    SDL_GetWindowPosition(win, &wx, &wy);
    SDL_GetWindowSize(win, &ww, &wh);
    int cx = gx, cy = gy; // clamp into the window rect, in global coords
    if (cx < wx) {
        cx = wx;
    } else if (ww > 0 && cx > wx + ww - 1) {
        cx = wx + ww - 1;
    }
    if (cy < wy) {
        cy = wy;
    } else if (wh > 0 && cy > wy + wh - 1) {
        cy = wy + wh - 1;
    }
    const bool multiViewport = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    *outX = (float)(multiViewport ? cx : cx - wx);
    *outY = (float)(multiViewport ? cy : cy - wy);
    return true;
}
#endif

void GameEngine::RunCommands(Gfx* Commands, const std::vector<std::unordered_map<Mtx*, MtxF>>& mtx_replacements) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());

    if (wnd == nullptr) {
        return;
    }

    auto interpreter = wnd->GetInterpreterWeak().lock().get();

    // Process window events for resize, mouse, keyboard events
    wnd->HandleEvents();

    interpreter->mInterpolationIndex = 0;

#if defined(ENABLE_VR) && defined(_WIN32)
    const bool vrActive = vr_is_requested() && vr_is_active();
    if (vr_is_requested() && !vrActive) {
        // Booting: advance the OpenXR session and close any frame it begins (safe no-op otherwise) so
        // the active loop starts clean next frame; then fall through to the flat render so the desktop
        // shows the game while VR spins up.
        vr_begin_frame();
        vr_submit();
    }
    if (vrActive) {
        // One GUI cycle per game tick keeps input + the controller device handler alive. When the ImGui
        // menu is open, render it once into a stable offscreen FBO and present it on the head-locked
        // panel so it's usable in the headset; otherwise render the stereo / panel frame(s).
        auto gui = wnd->GetGui();
        const bool menuOpen = gui->GetMenuOrMenubarVisible();
        float vrMouseX = 0.0f, vrMouseY = 0.0f;
        bool vrMouseValid = false;
        static bool sVrMenuNavArmed = false;
        // Every frame (menu open or not) so the left-stick click can TOGGLE the menu from the headset.
        VrFeedImGuiFromVrControllers(menuOpen);
        if (menuOpen) {
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            // The OS cursor isn't visible inside the headset, so have ImGui draw its own software cursor
            // into the menu texture -> it shows on the panel and tracks the mouse.
            ImGui::GetIO().MouseDrawCursor = true;
            // Focus-independent pad + mouse feeds: refresh + push nav events before StartDraw's
            // ImGui::NewFrame consumes them.
            VrFeedImGuiGamepadNav();
            vrMouseValid = VrComputeImGuiMousePos(&vrMouseX, &vrMouseY);
            if (vrMouseValid) {
                ImGui::GetIO().AddMousePosEvent(vrMouseX, vrMouseY);
            }
            if (!sVrMenuNavArmed) {
                // On menu open, force ImGui's SDL2 backend to (re)scan + open ALL controllers and MERGE
                // their input, so the real pad in hand always drives nav even when a phantom/virtual
                // device sits at index 0.
                ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);
                sVrMenuNavArmed = true;
            }
        } else if (sVrMenuNavArmed) {
            ImGui::GetIO().MouseDrawCursor = false;
            sVrMenuNavArmed = false;
        }
        gui->StartDraw();
        // Post-NewFrame override: ImGui::NewFrame (inside StartDraw) resolves the event queue with
        // last-write-wins, and the SDL2 backend's own events (unclamped global pos when focused, or the
        // window-leave -FLT_MAX) are queued AFTER our pre-StartDraw feed. Writing io.MousePos directly
        // here makes our clamped position authoritative for hit-testing and the software cursor.
        if (menuOpen && vrMouseValid) {
            ImGui::GetIO().MousePos = ImVec2(vrMouseX, vrMouseY);
        }
        interpreter->StartFrame();
        // Stereo per-eye render for the in-world view modes (Third/First Person, Diorama) while the game
        // is actually being FLOWN; Theater, every non-gameplay screen (title, menus, map, game over), the
        // ENDING sequence, and cutscenes render the flat frame once onto the head-locked panel. Cutscenes
        // (VrGame_IsCinematic: level intros, level-complete fly-bys, warp entries, boss set-pieces, the
        // ship going down) use scripted camera sweeps that read as sickness in stereo, so they go to the
        // comfortable virtual screen.
        const bool inPlay = (gGameState == GSTATE_PLAY);
        // Cutscenes and the native VR options overlay both render on the flat panel: cutscenes for comfort,
        // the overlay so it sits on a stable readable screen instead of the in-world HUD plane.
        const bool cinematic = inPlay ? (VrGame_IsCinematic() != 0 || VrMenu_IsOpen() != 0) : true;
        const bool stereo = inPlay && !cinematic && (vr_get_view_mode() != VR_VIEW_THEATER);
        static const std::unordered_map<Mtx*, MtxF> kEmptyMtx;
        const size_t steps = mtx_replacements.empty() ? 1 : mtx_replacements.size();
        // The sky dome is drawn game-side now (fox_display.c Vr_DrawSkyDome, as real world geometry through
        // the game camera), so the interpreter's separate dome pass is unused - pass nullptr.
        Gfx* const skyDome = nullptr;
        if (menuOpen) {
            uint32_t winW = 0, winH = 0;
            int32_t winX = 0, winY = 0;
            interpreter->GetDimensions(&winW, &winH, &winX, &winY);
            // Render the menu ONCE into its stable offscreen FBO.
            vr_menu_render_begin((int)winW, (int)winH); // bind + clear the private FBO
            gui->EndDraw();                             // render ImGui INTO that FBO
            vr_menu_apply_opacity();                    // gVRImGuiOpacity -> see-through panel (no-op if opaque)
            // CRITICAL: pace the SAME number of OpenXR frames as gameplay. One vr_begin_frame per tick
            // would run game logic at the headset's refresh instead of its native rate, speeding the
            // game up and garbling the audio. The menu panel (already rendered) is re-presented each
            // paced frame; the live stereo world renders behind it so the game doesn't black out.
            for (size_t s = 0; s < steps; s++) {
                const auto& mtx = mtx_replacements.empty() ? kEmptyMtx : mtx_replacements[s];
                vr_begin_frame();
                if (stereo) {
                    const int eyes = vr_eye_count();
                    for (int e = 0; e < eyes; e++) {
                        interpreter->RunVrEye(Commands, mtx, vr_eye_viewproj(e), vr_sky_viewproj(e),
                                              vr_hud_viewproj(e), vr_full2d_viewproj(e), skyDome,
                                              vr_eye_width(e), vr_eye_height(e));
                        vr_submit_eye_texture(e, interpreter->GetVrFbTextureId(), vr_eye_width(e),
                                              vr_eye_height(e));
                    }
                }
                vr_menu_render_present((int)winW, (int)winH); // present the menu texture on the panel
                vr_submit();
                interpreter->mInterpolationIndex++;
            }
            vr_menu_mirror_desktop((int)winW, (int)winH); // mirror to the flatscreen ONCE (no per-step flicker)
        } else {
            // Render ONE OpenXR frame per frame-interpolation step. Each step re-locates the head pose
            // (vr_begin_frame -> xrWaitFrame), paced by the headset, so the HMD runs at its native
            // refresh with smooth head tracking while game logic stays at its native rate.
            for (size_t s = 0; s < steps; s++) {
                const auto& mtx = mtx_replacements.empty() ? kEmptyMtx : mtx_replacements[s];
                vr_begin_frame();
                if (stereo) {
                    const int eyes = vr_eye_count();
                    for (int e = 0; e < eyes; e++) {
                        interpreter->RunVrEye(Commands, mtx, vr_eye_viewproj(e), vr_sky_viewproj(e),
                                              vr_hud_viewproj(e), vr_full2d_viewproj(e), skyDome,
                                              vr_eye_width(e), vr_eye_height(e));
                        vr_submit_eye_texture(e, interpreter->GetVrFbTextureId(), vr_eye_width(e),
                                              vr_eye_height(e));
                    }
                } else {
                    vr_set_panel_mode(true);
                    interpreter->RunVrPanel(Commands, mtx, vr_overlay_width(), vr_overlay_height());
                    vr_submit_panel_texture(interpreter->GetVrFbTextureId(), vr_overlay_width(),
                                            vr_overlay_height());
                }
                vr_submit();
                interpreter->mInterpolationIndex++;
            }
            gui->EndDraw();
            // Menu closed: mirror the rendered VR frame onto the flat window as the LAST fb0 write before
            // the swap, so the desktop shows the game instead of flickering between stale back-buffers
            // (the game renders into the OpenXR swapchains, never the live fb0). AFTER gui->EndDraw() so
            // the GUI composite can't overwrite it; BEFORE EndFrame()'s SwapBuffers.
            {
                uint32_t mW = 0, mH = 0;
                int32_t mX = 0, mY = 0;
                interpreter->GetDimensions(&mW, &mH, &mX, &mY);
                const int sw = stereo ? vr_eye_width(0) : vr_overlay_width();
                const int sh = stereo ? vr_eye_height(0) : vr_overlay_height();
                vr_mirror_game_desktop(interpreter->GetVrFbTextureId(), sw, sh, (int)mW, (int)mH);
            }
        }
        interpreter->EndFrame(); // present/swap the desktop window
    } else
#endif
    {
        for (const auto& m : mtx_replacements) {
            wnd->DrawAndRunGraphicsCommands(Commands, m);
            interpreter->mInterpolationIndex++;
        }
    }

    bool curAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    if (prevAltAssets != curAltAssets) {
        prevAltAssets = curAltAssets;
        Ship::Context::GetInstance()->GetResourceManager()->SetAltAssetsEnabled(curAltAssets);
        gfx_texture_cache_clear();
    }
}

void GameEngine::ProcessGfxCommands(Gfx* commands) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());

    if (wnd == nullptr) {
        return;
    }

    if(gEnableGammaBoost) {
        wnd->EnableSRGBMode();
    }
    wnd->SetRendererUCode(UcodeHandlers::ucode_f3dex);

    std::vector<std::unordered_map<Mtx*, MtxF>> mtx_replacements;
    int target_fps = GameEngine::Instance->GetInterpolationFPS();
    static int last_fps;
    static int last_update_rate;
    static int time;
    int fps = target_fps;
    int original_fps = 60 / gVIsPerFrame;

    if (target_fps == 20 || original_fps > target_fps) {
        fps = original_fps;
    }

    if (last_fps != fps || last_update_rate != gVIsPerFrame) {
        time = 0;
    }

    // time_base = fps * original_fps (one second)
    int next_original_frame = fps;

    while (time + original_fps <= next_original_frame) {
        time += original_fps;
        if (time != next_original_frame) {
            mtx_replacements.push_back(FrameInterpolation_Interpolate((float) time / next_original_frame));
        } else {
            mtx_replacements.emplace_back();
        }
    }

    time -= fps;

    if (wnd != nullptr) {
        wnd->SetTargetFps(fps);
        wnd->SetMaximumFrameLatency(CVarGetInteger("gRenderParallelization", 1) ? 2 : 1);
    }

    // When the gfx debugger is active, only run with the final mtx
    if (GfxDebuggerIsDebugging()) {
        mtx_replacements.clear();
        mtx_replacements.emplace_back();
    }

    RunCommands(commands, mtx_replacements);

    last_fps = fps;
    last_update_rate = gVIsPerFrame;
}

uint32_t GameEngine::GetInterpolationFPS() {
#if defined(ENABLE_VR) && defined(_WIN32)
    // VR: target the headset's refresh so the per-eye render loop paces to the HMD (via xrWaitFrame)
    // while game logic stays at its native rate. Using the monitor refresh here would desync game speed.
    if (vr_is_active()) {
        int hz = vr_display_refresh_hz();
        return (uint32_t)(hz >= 30 ? hz : 72);
    }
#endif
    if (CVarGetInteger("gMatchRefreshRate", 1)) { // default ON: run at the monitor's full refresh rate
        return Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();

    } else if (CVarGetInteger("gVsyncEnabled", 1) ||
               !Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync()) {
        return std::min<uint32_t>(Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate(),
                                  CVarGetInteger("gInterpolationFPS", 60));
    }

    return CVarGetInteger("gInterpolationFPS", 60);
}

uint32_t GameEngine::GetInterpolationFrameCount()
{
	return ceil((float)GetInterpolationFPS() / (60.0f / gVIsPerFrame));
}

extern "C" uint32_t GameEngine_GetInterpolationFrameCount() {
	return GameEngine::GetInterpolationFrameCount();
}

void GameEngine::ShowMessage(const char* title, const char* message, SDL_MessageBoxFlags type) {
#if defined(__SWITCH__)
    SPDLOG_ERROR(message);
#else
    SDL_ShowSimpleMessageBox(type, title, message, nullptr);
    SPDLOG_ERROR(message);
#endif
}

int GameEngine::ShowYesNoBox(const char* title, const char* box) {
    int ret;
#ifdef _WIN32
    ret = MessageBoxA(nullptr, box, title, MB_YESNO | MB_ICONQUESTION);
#elif defined(__SWITCH__)
    SPDLOG_ERROR(box);
    return IDYES;
#else
    SDL_MessageBoxData boxData = { 0 };
    SDL_MessageBoxButtonData buttons[2] = { { 0 } };

    buttons[0].buttonid = IDYES;
    buttons[0].text = "Yes";
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonid = IDNO;
    buttons[1].text = "No";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    boxData.numbuttons = 2;
    boxData.flags = SDL_MESSAGEBOX_INFORMATION;
    boxData.message = box;
    boxData.title = title;
    boxData.buttons = buttons;
    SDL_ShowMessageBox(&boxData, &ret);
#endif
    return ret;
}

bool GameEngine::HasVersion(SF64Version ver){
    auto versions = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions();
    return std::find(versions.begin(), versions.end(), ver) != versions.end();
}

extern "C" bool GameEngine_HasVersion(SF64Version ver) {
    return GameEngine::HasVersion(ver);
}

extern "C" uint32_t GameEngine_GetSampleRate() {
    auto player = Ship::Context::GetInstance()->GetAudio()->GetAudioPlayer();
    if (player == nullptr) {
        return 0;
    }

    if (!player->IsInitialized()) {
        return 0;
    }

    return player->GetSampleRate();
}

extern "C" uint32_t GameEngine_GetSamplesPerFrame() {
    return SAMPLES_PER_FRAME;
}

// End

Fast::Interpreter* GameEngine_GetInterpreter() {
    return static_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow())
             ->GetInterpreterWeak()
             .lock()
             .get();
}

extern "C" float GameEngine_GetAspectRatio() {
#if defined(ENABLE_VR) && defined(_WIN32)
    // In VR the frame is rendered to a fixed-aspect eye/panel target, NOT the desktop window. 2D
    // widescreen sizing (HUD edge anchoring, background extension) must match that target's aspect,
    // or the game - which builds its display list while the interpreter still holds the window size -
    // sizes the 2D to the window and leaves gaps on the panel/eye.
    if (vr_is_active()) {
        if (vr_get_view_mode() != VR_VIEW_THEATER && vr_eye_width(0) > 0 && vr_eye_height(0) > 0) {
            return (float)vr_eye_width(0) / (float)vr_eye_height(0);
        }
        return (float)vr_overlay_width() / (float)vr_overlay_height();
    }
#endif
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.aspect_ratio;
}

// Final teardown on quit. vr_shutdown() releases OpenXR (a live session/instance keeps runtime threads
// alive past window close, which can leave the process - and its console - running). No-op when VR is
// off (all handles NULL-guarded).
extern "C" void GameEngine_TerminateVr(void) {
    vr_shutdown();
}

// Whether gamepad-shaped game input is currently blocked (the ImGui menu is open with controller
// navigation on, or something registered an input block). The VR motion-controller merge feeds the
// N64 pad directly - below libultraship's ControlDeck, which only blocks its own devices - so the
// merge checks this itself to match: menu open -> the sticks drive the menu, not the ship.
extern "C" bool GameEngine_GameInputBlocked(void) {
    auto ctx = Ship::Context::GetInstance();
    if (ctx == nullptr || ctx->GetControlDeck() == nullptr) {
        return false;
    }
    return ctx->GetControlDeck()->GamepadGameInputBlocked();
}

extern "C" uint32_t GameEngine_GetGameVersion() {
    return 0x00000001;
}

static const char* sOtrSignature = "__OTR__";

extern "C" uint8_t GameEngine_OTRSigCheck(const char* data) {
    if (data == nullptr) {
        return 0;
    }
    return strncmp(data, sOtrSignature, strlen(sOtrSignature)) == 0;
}

extern "C" void GameEngine_GetTextureInfo(const char* path, int32_t* width, int32_t* height, float* scale, bool* custom) {
    if(GameEngine_OTRSigCheck(path) != 1){
        *custom = false;
        return;
    }
    std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(path));
    *width = tex->Width;
    *height = tex->Height;
    *scale = tex->VPixelScale;
    *custom = tex->Flags & (1 << 0);
}

extern "C" float __cosf(float angle) throw() {
    return cosf(angle);
}

extern "C" float __sinf(float angle) throw() {
    return sinf(angle);
}

extern "C" float SIN_DEG(float angle) {
    return __sinf(M_DTOR * angle);
}
extern "C" float COS_DEG(float angle) {
    return __cosf(M_DTOR * angle);
}

struct TimedEntry {
    uint64_t duration;
    TimerAction action;
    int32_t* address;
    int32_t value;
    bool active;
};

std::vector<TimedEntry> gTimerTasks;

uint64_t Timer_GetCurrentMillis() {
    return SDL_GetTicks();
}

extern "C" s32 Timer_CreateTask(u64 time, TimerAction action, s32* address, s32 value) {
    const auto millis = Timer_GetCurrentMillis();
    TimedEntry entry = {
        .duration = millis + CYCLES_TO_MSEC_PC(time),
        .action = action,
        .address = address,
        .value = value,
        .active = true,
    };

    gTimerTasks.push_back(entry);

    return gTimerTasks.size() - 1;
}

extern "C" void Timer_Increment(int32_t* address, int32_t value) {
    *address += value;
}

extern "C" void Timer_SetValue(int32_t* address, int32_t value) {
    *address = value;
}

void Timer_CompleteTask(TimedEntry& task) {
    if (task.action != nullptr) {
        task.action(task.address, task.value);
    }
    task.active = false;
}

extern "C" void Timer_Update() {

    if (gTimerTasks.empty()) {
        return;
    }

    const auto millis = Timer_GetCurrentMillis();

    for (auto& task : gTimerTasks) {
        if (task.active && millis >= task.duration) {
            Timer_CompleteTask(task);
        }
    }
}

// Gets the width of the main ImGui window
extern "C" uint32_t OTRGetCurrentWidth() {
    return GameEngine::Instance->context->GetWindow()->GetWidth();
}

// Gets the height of the main ImGui window
extern "C" uint32_t OTRGetCurrentHeight() {
    return GameEngine::Instance->context->GetWindow()->GetHeight();
}

extern "C" float OTRGetHUDAspectRatio() {
    if (CVarGetInteger("gHUDAspectRatio.Enabled", 0) == 0 || CVarGetInteger("gHUDAspectRatio.X", 0) == 0 || CVarGetInteger("gHUDAspectRatio.Y", 0) == 0) {
        return GameEngine_GetAspectRatio();
    }
    return ((float)CVarGetInteger("gHUDAspectRatio.X", 1) / (float)CVarGetInteger("gHUDAspectRatio.Y", 1));
}

extern "C" float OTRGetDimensionFromLeftEdge(float v) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 - interpreter->mNativeDimensions.height / 2 * interpreter->mCurDimensions.aspect_ratio + (v));
}

extern "C" float OTRGetDimensionFromRightEdge(float v) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 + interpreter->mNativeDimensions.height / 2 * interpreter->mCurDimensions.aspect_ratio -
            (interpreter->mNativeDimensions.width - v));
}

extern "C" float OTRGetDimensionFromLeftEdgeForcedAspect(float v, float aspectRatio) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 - interpreter->mNativeDimensions.height / 2 * (aspectRatio > 0 ? aspectRatio : interpreter->mCurDimensions.aspect_ratio) + (v));
}

extern "C" float OTRGetDimensionFromRightEdgeForcedAspect(float v, float aspectRatio) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 + interpreter->mNativeDimensions.height / 2 * (aspectRatio > 0 ? aspectRatio : interpreter->mCurDimensions.aspect_ratio) -
            (interpreter->mNativeDimensions.width - v));
}

extern "C" float OTRGetDimensionFromLeftEdgeOverride(float v) {
    return OTRGetDimensionFromLeftEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" float OTRGetDimensionFromRightEdgeOverride(float v) {
    return OTRGetDimensionFromRightEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

// Gets the width of the current render target area
extern "C" uint32_t OTRGetGameRenderWidth() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.width;
}

// Gets the height of the current render target area
extern "C" uint32_t OTRGetGameRenderHeight() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.height;
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v) {
    return ((int) floorf(OTRGetDimensionFromLeftEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v) {
    return ((int) ceilf(OTRGetDimensionFromRightEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdgeForcedAspect(float v, float aspectRatio) {
    return ((int) floorf(OTRGetDimensionFromLeftEdgeForcedAspect(v, aspectRatio)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdgeForcedAspect(float v, float aspectRatio) {
    return ((int) ceilf(OTRGetDimensionFromRightEdgeForcedAspect(v, aspectRatio)));
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdgeOverride(float v) {
    return OTRGetRectDimensionFromLeftEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" int16_t OTRGetRectDimensionFromRightEdgeOverride(float v) {
    return OTRGetRectDimensionFromRightEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" int32_t OTRConvertHUDXToScreenX(int32_t v) {
    auto interpreter = GameEngine_GetInterpreter();
    float gameAspectRatio = interpreter->mCurDimensions.aspect_ratio;
    int32_t gameHeight = interpreter->mCurDimensions.height;
    int32_t gameWidth = interpreter->mCurDimensions.width;
    float hudAspectRatio = 4.0f / 3.0f;
    int32_t hudHeight = gameHeight;
    int32_t hudWidth = hudHeight * hudAspectRatio;
    float hudScreenRatio = (hudWidth / 320.0f);
    float hudCoord = v * hudScreenRatio;
    float gameOffset = (gameWidth - hudWidth) / 2;
    float gameCoord = hudCoord + gameOffset;
    float gameScreenRatio = (320.0f / gameWidth);
    float screenScaledCoord = gameCoord * gameScreenRatio;
    int32_t screenScaledCoordInt = screenScaledCoord;
    return screenScaledCoordInt;
}

extern "C" void* GameEngine_Malloc(size_t size) {
    MemoryPool.push_back(new uint8_t[size]);
    return (void*) MemoryPool.back();
}

extern "C" void GameEngine_Free(void* ptr) {
    for (auto it = MemoryPool.begin(); it != MemoryPool.end(); ++it) {
        if (*it == ptr) {
            free(ptr);
            MemoryPool.erase(it);
            break;
        }
    }
}
