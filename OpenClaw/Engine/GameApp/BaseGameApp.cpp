#include "../PortFeatures.h"
#include <map>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include "../Audio/Audio.h"
#include "../UserInterface/Lang.h"
#include "../Platform/Platform.h"
#include "../UserInterface/SecondScreen.h"
#include <fstream>

// A file named in config.xml without a folder: next to the binary / in the
// working directory when it is there, otherwise among the files bundled with
// the port (romfs:/, app0:/, the app's folder...).
static std::string ResolveBundledFile(const std::string& path)
{
  if (path.empty() || path.find(':') != std::string::npos || path[0] == '/') return path;
  if (std::ifstream(path).good()) return path;
  std::string bundled = Platform::BundlePath(path);
  if (std::ifstream(bundled).good()) return bundled;
  return path;
}
#include "../Events/EventMgr.h"
#include "../Events/EventMgrImpl.h"
#include "../Events/Events.h"
#include "../Graphics/GraphicsAdapter.h"
#include "../Graphics2D/Image.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceDirectory.h"
#include "StartupSplash.h"
#include "../Video/Native/MoviePlayer.h"
#include "../UserInterface/CustomLevels.h"
#include "../Resource/ResourceMgr.h"
#include "../UserInterface/HumanView.h"
#include "BaseGameLogic.h"


// Resource loaders
#include "../Resource/Loaders/AniLoader.h"
#include "../Resource/Loaders/DefaultLoader.h"
#include "../Resource/Loaders/MidiLoader.h"
#include "../Resource/Loaders/PalLoader.h"
#include "../Resource/Loaders/PcxLoader.h"
#include "../Resource/Loaders/PidLoader.h"
// Options > Graphics > filter changed: the level's pictures are made again
// before the next frame (a loading bar meanwhile), not in the middle of the
// menu's event handling.
static bool s_RefilterPending = false;
static void DrawRefilterProgress(int done, int total);
#include "../Resource/Loaders/PngLoader.h"
#include "../Resource/Loaders/WavLoader.h"
#include "../Resource/Loaders/WwdLoader.h"
#include "../Resource/Loaders/XmlLoader.h"


#include "BaseGameApp.h"
#include "../Race/RaceUi.h"
#if !defined(__EMSCRIPTEN__)
#include "../Audio/Native/SoundFontTrim.h"
#endif
#include "../Util/PortSettings.h"
#include "../Graphics2D/CrtEffect.h"
#if defined(__PSL1GHT__)
#include "../Platform/Input/Ps3Pad.h"
#endif
#include "../Util/StartupTrace.h"

#include "../Platform/Events/AppEvent.h"

#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef __3DS__
#include <3ds.h>
#endif

#ifdef __EMSCRIPTEN__
#include "../Platform/Video/WASM/WasmVideoPlatform.h"
#else
#include "../Platform/Video/Null/NullVideoPlatform.h"
#include "../Platform/Input/SdlGamepadInput.h"
#include "../UserInterface/TouchPad.h"
#include "../Resource/LooseSoundFiles.h"
#endif

#include <cctype>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

TiXmlElement *CreateDefaultDisplayConfig();
TiXmlElement *CreateDefaultAudioConfig();
TiXmlElement *CreateDefaultFontConfig();
TiXmlElement *CreateDefaultAssetsConfig();
TiXmlDocument CreateDefaultConfig();

BaseGameApp *g_pApp = NULL;

BaseGameApp::BaseGameApp() {
  g_pApp = this;

  m_pGame = NULL;
  m_pEventMgr = NULL;
  m_pWindow = NULL;
  m_pRenderer = NULL;
  m_pPalette = NULL;
  m_pAudio = NULL;
  m_pConsoleFont = NULL;
  m_pTouchManager = nullptr;
  m_graphicsAdapter = nullptr;
  m_videoModule = nullptr;
  m_IsRunning = false;
  m_QuitRequested = false;
  m_IsQuitting = false;

  // Initialize fixed timestep variables
  m_accumulator = 0.0;
  m_fixedTimestep = 1.0 / 60.0;  // 60 updates per second
  m_alpha = 0.0;
  m_logicTick = 0;
}

void BaseGameApp::OnAppEvent(const AppEvent &event) {
  // Stage 2a: keep existing engine behavior by translating platform events
  // back into SDL_Event and forwarding through the existing path.
  SDL_Event sdlEvent;
  SDL_memset(&sdlEvent, 0, sizeof(sdlEvent));

  switch (event.type) {
  case AppEventType::Quit: {
    sdlEvent.type = SDL_QUIT;
    OnEvent(sdlEvent);
    break;
  }

  case AppEventType::Resize: {
    sdlEvent.type = SDL_WINDOWEVENT;
    sdlEvent.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    sdlEvent.window.data1 = event.resize.width;
    sdlEvent.window.data2 = event.resize.height;
    OnEvent(sdlEvent);
    break;
  }

  case AppEventType::KeyDown:
  case AppEventType::KeyUp: {
    sdlEvent.type =
        (event.type == AppEventType::KeyDown) ? SDL_KEYDOWN : SDL_KEYUP;
    sdlEvent.key.repeat = event.key.repeat ? 1 : 0;
    sdlEvent.key.state =
        (event.type == AppEventType::KeyDown) ? SDL_PRESSED : SDL_RELEASED;
    sdlEvent.key.keysym.scancode =
        static_cast<SDL_Scancode>(event.key.scancode);
    sdlEvent.key.keysym.sym =
        SDL_GetKeyFromScancode(sdlEvent.key.keysym.scancode);
    sdlEvent.key.keysym.mod = 0;
    OnEvent(sdlEvent);
    break;
  }

  case AppEventType::MouseMove: {
    sdlEvent.type = SDL_MOUSEMOTION;
    sdlEvent.motion.x = static_cast<int>(event.mouseMove.x);
    sdlEvent.motion.y = static_cast<int>(event.mouseMove.y);
    OnEvent(sdlEvent);
    break;
  }

  case AppEventType::MouseDown:
  case AppEventType::MouseUp: {
    sdlEvent.type = (event.type == AppEventType::MouseDown)
                        ? SDL_MOUSEBUTTONDOWN
                        : SDL_MOUSEBUTTONUP;
    sdlEvent.button.button = event.mouseButton.button;
    sdlEvent.button.state =
        (event.type == AppEventType::MouseDown) ? SDL_PRESSED : SDL_RELEASED;
    sdlEvent.button.x = static_cast<int>(event.mouseButton.x);
    sdlEvent.button.y = static_cast<int>(event.mouseButton.y);
    OnEvent(sdlEvent);
    break;
  }

  case AppEventType::TouchStart:
  case AppEventType::TouchMove:
  case AppEventType::TouchEnd: {
    if (event.type == AppEventType::TouchStart)
      sdlEvent.type = SDL_FINGERDOWN;
    else if (event.type == AppEventType::TouchMove)
      sdlEvent.type = SDL_FINGERMOTION;
    else
      sdlEvent.type = SDL_FINGERUP;
    sdlEvent.tfinger.touchId = 0;
    sdlEvent.tfinger.fingerId = static_cast<SDL_FingerID>(event.touch.fingerId);
    sdlEvent.tfinger.x = event.touch.x;
    sdlEvent.tfinger.y = event.touch.y;
    sdlEvent.tfinger.dx = 0.0f;
    sdlEvent.tfinger.dy = 0.0f;
    sdlEvent.tfinger.pressure =
        (event.type == AppEventType::TouchEnd) ? 0.0f : 1.0f;
    OnEvent(sdlEvent);
    break;
  }

  // Gamepad events - route directly to game views (no SDL conversion needed)
  case AppEventType::GamepadConnected:
    LOG("Gamepad " + std::to_string(event.gamepadConnection.gamepadIndex) + " connected");
    break;

  case AppEventType::GamepadDisconnected:
    LOG("Gamepad " + std::to_string(event.gamepadConnection.gamepadIndex) + " disconnected");
    break;

  case AppEventType::GamepadButtonDown:
  case AppEventType::GamepadButtonUp:
  case AppEventType::GamepadAxis: {
    if (m_pGame) {
      for (auto iter = m_pGame->m_GameViews.rbegin();
           iter != m_pGame->m_GameViews.rend(); ++iter) {
        if ((*iter)->VOnGamepadEvent(event)) {
          break;
        }
      }
    }
    break;
  }

  default:
    break;
  }
}


// A plain loading screen while the game starts (the first start on handhelds
// takes several seconds): "ReClaw" and a progress bar, straight on the screen.
std::string g_StartupFailureHint;   // extra lines for the "failed to start" screen ('|' = new line)

static void ShowStartupProgress(SDL_Renderer* r, int percent)
{
  if (!r) return;
  SDL_Texture* prevTarget = SDL_GetRenderTarget(r);
  float sx = 1.0f, sy = 1.0f;
  SDL_RenderGetScale(r, &sx, &sy);
  SDL_SetRenderTarget(r, NULL);
  SDL_RenderSetScale(r, 1.0f, 1.0f);
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(r, &w, &h);
  SDL_SetRenderDrawColor(r, 14, 8, 4, 255);
  SDL_RenderClear(r);

  static SDL_Texture* s_pTitle = nullptr;
  static int s_TitleW = 0, s_TitleH = 0;
  if (!s_pTitle) {
    if (!TTF_WasInit()) TTF_Init();
    if (TTF_Font* font = TTF_OpenFont(Platform::SplashFont().c_str(), std::max(16, h / 12))) {
      if (SDL_Surface* sf = TTF_RenderUTF8_Blended(font, "ReClaw", SDL_Color{ 236, 196, 98, 255 })) {
        s_pTitle = SDL_CreateTextureFromSurface(r, sf);
        s_TitleW = sf->w; s_TitleH = sf->h;
        SDL_FreeSurface(sf);
      }
      TTF_CloseFont(font);
    }
  }
  if (s_pTitle) {
    SDL_Rect d = { (w - s_TitleW) / 2, h / 2 - s_TitleH, s_TitleW, s_TitleH };
    SDL_RenderCopy(r, s_pTitle, NULL, &d);
  }
  int barW = w / 3, barH = std::max(3, h / 120);
  SDL_Rect frame = { (w - barW) / 2, h / 2 + h / 12, barW, barH };
  SDL_SetRenderDrawColor(r, 60, 36, 12, 255);
  SDL_RenderFillRect(r, &frame);
  SDL_Rect fill = frame;
  fill.w = barW * std::max(0, std::min(100, percent)) / 100;
  SDL_SetRenderDrawColor(r, 236, 196, 98, 255);
  SDL_RenderFillRect(r, &fill);
  CrtEffect::BeforePresent(r);
  SDL_RenderPresent(r);
  SDL_PumpEvents();
  if (percent >= 100 && s_pTitle) { SDL_DestroyTexture(s_pTitle); s_pTitle = nullptr; }

  SDL_SetRenderTarget(r, prevTarget);
  SDL_RenderSetScale(r, sx, sy);
}

bool BaseGameApp::Initialize(int argc, char **argv) {
  RegisterEngineEvents();
  VRegisterGameEvents();

  // Initialization sequence
  STARTUP_TRACE("begin InitializeEventMgr");
  if (!InitializeEventMgr()) {
    STARTUP_TRACE("FAILED InitializeEventMgr");
    return false;
  }

  STARTUP_TRACE("begin InitializeDisplay");
  if (!InitializeDisplay(m_GameOptions)) {
    STARTUP_TRACE("FAILED InitializeDisplay");
    return false;
  }

  // Dedication screen (only in builds made with a splash text); stays up
  // while the rest of the game loads.
  StartupSplash::Show(m_pRenderer);
  ShowStartupProgress(m_pRenderer, 5);

  // 3DS bottom screen / Wii U GamePad: status screen.
  SecondScreen::Init();

  STARTUP_TRACE("begin InitializeAudio");
  if (!InitializeAudio(m_GameOptions)) {
    STARTUP_TRACE("FAILED InitializeAudio");
    return false;
  }

  STARTUP_TRACE("begin InitializeFont");
  if (!InitializeFont(m_GameOptions)) {
    STARTUP_TRACE("FAILED InitializeFont");
    return false;
  }

  ShowStartupProgress(m_pRenderer, 10);
  STARTUP_TRACE("begin InitializeResources");
  if (!InitializeResources(m_GameOptions)) {
    STARTUP_TRACE("FAILED InitializeResources");
    return false;
  }

  ShowStartupProgress(m_pRenderer, 25);
  STARTUP_TRACE("begin InitializeLocalization");
  if (!InitializeLocalization(m_GameOptions)) {
    STARTUP_TRACE("FAILED InitializeLocalization");
    return false;
  }

  STARTUP_TRACE("begin InitializeTouchManager");
  if (!InitializeTouchManager(m_GameOptions)) {
    STARTUP_TRACE("FAILED InitializeTouchManager");
    return false;
  }

  STARTUP_TRACE("begin InitializeGraphicsSystem");
  if (!InitializeGraphicsSystem()) {
    STARTUP_TRACE("FAILED InitializeGraphicsSystem");
    return false;
  }


  STARTUP_TRACE("begin GenericVideoModule");
  m_videoModule.reset(new GenericVideoModule());
  if (!m_videoModule->Initialize())
    return false;

  STARTUP_TRACE("begin ReadActorXmlPrototypes");
  if (!ReadActorXmlPrototypes(m_GameOptions)) {
    STARTUP_TRACE("FAILED ReadActorXmlPrototypes");
    return false;
  }


  // Level metadata is now lazy-loaded on-demand when levels are loaded
  // This improves WASM startup performance by avoiding parsing 13 XML files
  // The "reload levelmetadata" command still works for development

  STARTUP_TRACE("begin RegisterAllDelegates");
  RegisterAllDelegates();
  STARTUP_TRACE("begin VCreateGameAndView");

  m_pGame = VCreateGameAndView();
  if (!m_pGame) {
    LOG_ERROR("Failed to initialize game logic.");
    return false;
  }

  // Lazy loading architecture: Only load critical assets initially
  // Menu and UI assets load first, level assets load on-demand
  LOG("Loading critical assets (menu/UI)...");
  ShowStartupProgress(m_pRenderer, 40);
#if !defined(__PSL1GHT__)
  // (PS3: the menu's pictures load when shown - all of them up front took
  // ~57 MB, and the level then ran out of memory)
  STARTUP_TRACE("preload " "/STATES/MENU/*");
  m_pResourceMgr->VPreload("/STATES/MENU/*", NULL, ORIGINAL_RESOURCE);
#endif
  ShowStartupProgress(m_pRenderer, 85);
  STARTUP_TRACE("preload " "/GAME/FONTS/*");
  m_pResourceMgr->VPreload("/GAME/FONTS/*", NULL, ORIGINAL_RESOURCE);
  STARTUP_TRACE("preload " "/GAME/IMAGES/MENU/*");
  m_pResourceMgr->VPreload("/GAME/IMAGES/MENU/*", NULL, ORIGINAL_RESOURCE);

  // Load custom assets
  ShowStartupProgress(m_pRenderer, 92);
#if !defined(__3DS__) && !defined(__PSL1GHT__)
  // (3DS, PS3: not all 12 MB of ASSETS.ZIP up front - too little memory)
  STARTUP_TRACE("preload " "*");
  m_pResourceMgr->VPreload("*", NULL, CUSTOM_RESOURCE);
#endif

  LOG("Critical assets loaded. Level assets will load on-demand.");

  ShowStartupProgress(m_pRenderer, 100);
  STARTUP_TRACE("begin VPerformStartupTests");
  if (!VPerformStartupTests()) {
    LOG_ERROR("Failed to pass certain startup tests.");
    return false;
  }

  // Configure fixed timestep based on settings
  if (m_GlobalOptions.gameLogicFps > 0) {
    m_fixedTimestep = 1.0 / static_cast<double>(m_GlobalOptions.gameLogicFps);
  } else {
    LOG_WARNING("Invalid gameLogicFps value, using default 60 FPS");
    m_fixedTimestep = 1.0 / 60.0;
  }
#if defined(__3DS__) || defined(__PSL1GHT__)
  // 3DS, PS3: the game logic (physics, actors) runs 30 times a second - half the
  // work for the same game speed; logic_hz=60 in port_settings.txt for 60.
  {
    // With the GPU drawing there is time for 60 (a step costs ~8 ms): at 30
    // the sprites' frames and some movements (ladders) changed every other
    // screen refresh only.
    int defaultHz = 30;
#if defined(RECLAW_GPU)
    // (PS3 with the RSX: a step costs ~6 ms; at 30 the animations - Claw
    // coming back after dying - moved every other frame only)
    if (ReclawGpu_Active()) defaultHz = 60;
#endif
    int hz = PortSettings::GetInt("logic_hz", defaultHz);
    if (hz < 20 || hz > 60) hz = defaultHz;
    m_fixedTimestep = 1.0 / hz;
    LOG_WARNING("Game logic at " + ToStr(hz) + " Hz" + (hz != defaultHz ? " (logic_hz in port_settings.txt)" : ""));
  }
#endif

  STARTUP_TRACE("Initialize done");
  m_IsRunning = true;

  StartupSplash::Finish(m_pRenderer);

  return true;
}

#if defined(__3DS__)
extern "C" void (*__stdio_exit_handler)(void);
#endif

void BaseGameApp::Terminate() {
  STARTUP_TRACE("Terminate");
  LOG("Terminating...");

  RemoveAllDelegates();

  // Shutdown graphics system
  ShutdownGraphicsSystem();

  if (m_videoPlatform) {
    m_videoPlatform->Shutdown();
    m_videoPlatform.reset();
  }
  m_appEventQueue.Clear();

#ifndef __EMSCRIPTEN__
  SdlGamepadInput::Shutdown();
#endif

  if (m_videoModule) {
    m_videoModule->Shutdown();
    m_videoModule.reset();
  }

  SAFE_DELETE(m_pGame);
  SDL_DestroyRenderer(m_pRenderer);
  SDL_DestroyWindow(m_pWindow);
  SAFE_DELETE(m_pAudio);
  SAFE_DELETE(m_pTouchManager);
  SAFE_DELETE(m_pEventMgr);
  SAFE_DELETE(m_pResourceMgr);
  if (m_pConsoleFont) {
    TTF_CloseFont(m_pConsoleFont);
    m_pConsoleFont = nullptr;
  }

  for (auto &actorProto : m_ActorXmlPrototypeMap) {
    delete actorProto.second;
  }
  m_ActorXmlPrototypeMap.clear();

  SaveGameOptions();
  Platform::FlushLog();
  Platform::CopyLogToUsb();
#if defined(__3DS__)
  // exit() closes every FILE still open; one of them was on a file system
  // already shut down (romfs), and closing it crashed the 3DS (Luma3DS dump:
  // _lseek_r in fclose in exit). The files are closed with the process.
  __stdio_exit_handler = nullptr;
#endif
}

#define STARTUP_TEST(condition, error)                                         \
  {                                                                            \
    if (!(condition)) {                                                        \
      LOG_ERROR((error));                                                      \
      bTestsOk = false;                                                        \
    }                                                                          \
  }

#define STARTUP_TEST_FILE_PRESENCE_IN_RESCACHE(filePath, resCacheName, error)  \
  {                                                                            \
    std::vector<std::string> matchedFiles =                                    \
        m_pResourceMgr->VMatch((filePath), (resCacheName));                    \
    STARTUP_TEST(matchedFiles.size() > 0, error);                              \
    if (bTestsOk) {                                                            \
      std::string filePathCopy = (filePath);                                   \
      std::transform(filePathCopy.begin(), filePathCopy.end(),                 \
                     filePathCopy.begin(), (int (*)(int))std::tolower);        \
      STARTUP_TEST(matchedFiles.size() == 1, "More than 1 file found");        \
      STARTUP_TEST(matchedFiles[0] == (filePathCopy), (error));                \
    }                                                                          \
  }

bool BaseGameApp::VPerformStartupTests() {
  bool bTestsOk = true;

#ifndef __EMSCRIPTEN__
  // Base SDL video, audio and events (not used in WASM build)
  STARTUP_TEST(SDL_WasInit(SDL_INIT_VIDEO),
               "SDL Video subsystem is unitialized");
  STARTUP_TEST(SDL_WasInit(SDL_INIT_AUDIO),
               "SDL Audio subsystem is unitialized");
  STARTUP_TEST(SDL_WasInit(SDL_INIT_EVENTS),
               "SDL Event subsystem is unitialized");
  STARTUP_TEST(m_pWindow != NULL, "SDL Window is NULL");
  STARTUP_TEST(m_pRenderer != NULL, "SDL Renderer is NULL");
#else
  // WASM build uses WebGL directly, no SDL initialization needed
  LOG("WASM build: Skipping SDL subsystem checks");
#endif

  // Game logic
  STARTUP_TEST(m_pGame != NULL, "Game Logic is NULL");

  // Game view
  STARTUP_TEST(GetHumanView() != NULL, "Human View is NULL");

  // Event manager
  STARTUP_TEST(IEventMgr::Get() != NULL, "Event manager is unitialized");

  // Audio manager
  STARTUP_TEST(m_pAudio != NULL, "Audio manager is unitialized");

  // Resources
  STARTUP_TEST(m_pResourceMgr->VHasResourceCache(ORIGINAL_RESOURCE),
               std::string(ORIGINAL_RESOURCE) + " is not part of ResourceMgr");
  STARTUP_TEST(m_pResourceMgr->VHasResourceCache(CUSTOM_RESOURCE),
               std::string(CUSTOM_RESOURCE) + " is not part of ResourceMgr");

  // Files located in my custom ASSETS.ZIP
  STARTUP_TEST_FILE_PRESENCE_IN_RESCACHE(
      "/ACTOR_PROTOTYPES/LEVEL1/LEVEL1_SOLDIER.XML", CUSTOM_RESOURCE,
      "/ACTOR_PROTOTYPES/LEVEL1/LEVEL1_SOLDIER.XML not found in: " +
          std::string(CUSTOM_RESOURCE));

  STARTUP_TEST_FILE_PRESENCE_IN_RESCACHE(
      "/ACTOR_PROTOTYPES/LEVEL1/LEVEL1_OFFICER.XML", CUSTOM_RESOURCE,
      "/ACTOR_PROTOTYPES/LEVEL1/LEVEL1_OFFICER.XML not found in: " +
          std::string(CUSTOM_RESOURCE));

  return bTestsOk;
}

void BaseGameApp::StepLoop() {
  static uint32 lastTime = SDL_GetTicks();
  SDL_Event event;
  Touch_Event touchEvent;
  static int consecutiveLagSpikes = 0;

  if (m_IsRunning) {
    // PROFILE_CPU("MAINLOOP");

    uint32 now = SDL_GetTicks();
    double frameTime = (now - lastTime) / 1000.0; // Convert to seconds
    lastTime = now;

    // This occurs when recovering program from background or after load
    // We want to ignore these situations
    if (frameTime > 1.0) {
      consecutiveLagSpikes++;
      if (consecutiveLagSpikes > 10) {
        LOG_ERROR("Experiencing lag spikes, " + ToStr(consecutiveLagSpikes) +
                  " high latency frames in a row");
      }
      return;
    }
    consecutiveLagSpikes = 0;

    // Cap frame time to prevent spiral of death (max 250ms = 4 FPS)
    if (frameTime > 0.25) {
      frameTime = 0.25;
    }

    // Accumulate time for fixed timestep updates
    m_accumulator += frameTime;
    // At most 3 logic steps per frame: a slow frame (loading a picture, a
    // sound) must not pile up a dozen steps that make the next frames slow
    // too (seen on the PS Vita: 15 steps, 3 fps). The game runs a little
    // slower for that moment instead.
    if (m_accumulator > 3 * m_fixedTimestep) {
      m_accumulator = 3 * m_fixedTimestep;
    }

// Handle all input events
#ifdef __EMSCRIPTEN__
    // Debug: check if videoPlatform exists
    static bool debugOnce = true;
    if (debugOnce) {
      debugOnce = false;
      if (m_videoPlatform) {
      } else {
        EM_ASM({ console.log('[Debug] m_videoPlatform is NULL!'); });
      }
    }
    if (m_videoPlatform) {
      m_videoPlatform->PumpEvents();
    }

    AppEvent appEvent;
    while (m_appEventQueue.Poll(appEvent)) {
      OnAppEvent(appEvent);
    }
#else
    while (SDL_PollEvent(&event)) {
      // Controllers go through the same AppEvent path as the browser build so
      // HumanView/ActorController handle pads identically on every platform.
      // 3DS: the touch screen works the menu shown on it.
      if (SecondScreen::HandleTouch(event)) {
        continue;
      }
#if defined(__SWITCH__)
      // Switch: the first finger on the touch screen is the mouse (the sword
      // cursor follows it, a tap clicks), on the window's own pixels.
      if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP || event.type == SDL_FINGERMOTION) {
        static SDL_FingerID s_Finger = -1;
        static bool s_Logged = false;
        if (!s_Logged) { s_Logged = true; LOG("Touch screen: first touch"); }
        if (event.type == SDL_FINGERDOWN && s_Finger == -1) s_Finger = event.tfinger.fingerId;
        if (event.tfinger.fingerId == s_Finger && m_pWindow) {
          int ww = 0, wh = 0;
          SDL_GetWindowSize(m_pWindow, &ww, &wh);
          int x = (int)(event.tfinger.x * ww), y = (int)(event.tfinger.y * wh);
          SDL_WarpMouseInWindow(m_pWindow, x, y);
          SDL_Event m;
          SDL_zero(m);
          if (event.type == SDL_FINGERMOTION) {
            m.type = SDL_MOUSEMOTION;
            m.motion.windowID = SDL_GetWindowID(m_pWindow);
            m.motion.x = x; m.motion.y = y;
            m.motion.state = SDL_BUTTON_LMASK;
          } else {
            m.type = event.type == SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            m.button.windowID = SDL_GetWindowID(m_pWindow);
            m.button.button = SDL_BUTTON_LEFT;
            m.button.state = event.type == SDL_FINGERDOWN ? SDL_PRESSED : SDL_RELEASED;
            m.button.clicks = 1;
            m.button.x = x; m.button.y = y;
            if (event.type == SDL_FINGERUP) s_Finger = -1;
          }
          if (event.type == SDL_FINGERDOWN) {
            // the cursor moves there first, so the click lands on that item
            SDL_Event mv;
            SDL_zero(mv);
            mv.type = SDL_MOUSEMOTION;
            mv.motion.windowID = SDL_GetWindowID(m_pWindow);
            mv.motion.x = x; mv.motion.y = y;
            OnEvent(mv);
          }
          OnEvent(m);
        }
        continue;
      }
#endif
      // On-screen controls (touch screens) take the touches while playing.
      if (TouchPad::HandleEvent(event)) {
        continue;
      }
      AppEvent padEvent;
      bool hasPadEvent = false;
      if (SdlGamepadInput::Translate(event, padEvent, hasPadEvent)) {
        if (hasPadEvent) {
          if (padEvent.type == AppEventType::GamepadButtonDown) {
            TouchPad::OnControllerUsed();
          }
          OnAppEvent(padEvent);
        }
        continue;
      }
      OnEvent(event);
    }
#endif
#ifdef __PSL1GHT__
    // PS3: the pads straight from ioPad.
    SdlGamepadInput::PollDirect([](const AppEvent &e, void *self) {
      if (e.type == AppEventType::GamepadButtonDown) TouchPad::OnControllerUsed();
      static_cast<BaseGameApp *>(self)->OnAppEvent(e);
    }, this);
#endif

    // Handle all touch events
    if (m_pTouchManager) {
      m_pTouchManager->Update();
      while (m_pTouchManager->PollEvent(&touchEvent)) {
        OnEvent(touchEvent.sdlEvent);
      }
    }

    if (m_pGame) {
#ifdef OPENCLAW_STARTUP_TRACE
      // Performance in reclaw.log every 5 s: frames, logic and render time.
      static Uint64 s_perfStart = SDL_GetPerformanceCounter(), s_logicTicks = 0, s_renderTicks = 0;
      static int s_perfFrames = 0, s_perfUpdates = 0;
      static double s_worstMs = 0, s_worstLogicMs = 0, s_worstRenderMs = 0, s_worstLoadMs = 0;
      const Uint64 perfFreq = SDL_GetPerformanceFrequency();
      Uint64 perfT0 = SDL_GetPerformanceCounter();
#endif
      // Fixed timestep update loop - game logic runs at constant rate
      while (m_accumulator >= m_fixedTimestep) {
#ifdef OPENCLAW_STARTUP_TRACE
        s_perfUpdates++;
#endif
        // PROFILE_CPU("ONLY GAME UPDATE");
#ifdef OPENCLAW_STARTUP_TRACE
        extern Uint64 g_PerfEventTicks;
        const Uint64 perfEv0 = SDL_GetPerformanceCounter();
#endif
        IEventMgr::Get()->VUpdate(
            20); // Allow event queue to process for up to 20 ms
#ifdef OPENCLAW_STARTUP_TRACE
        g_PerfEventTicks += SDL_GetPerformanceCounter() - perfEv0;
#endif

        // Advance the logic-tick counter. Scene nodes stamp this when their
        // position changes, so rendering only interpolates actors that actually
        // moved on the current tick (stationary actors render flat, no shake).
        m_logicTick++;

        // Convert fixed timestep back to milliseconds for game logic
        uint32 fixedDeltaMs = static_cast<uint32>(m_fixedTimestep * 1000.0);
#ifdef OPENCLAW_STARTUP_TRACE
        // One step much longer than a frame: what it was spent on (reclaw.log).
        extern Uint64 g_PerfLogicTicks[4];
        const Uint64 stepT0 = SDL_GetPerformanceCounter();
        const Uint64 stepL0 = g_PerfLogicTicks[0], stepL1 = g_PerfLogicTicks[1], stepL2 = g_PerfLogicTicks[2], stepL3 = g_PerfLogicTicks[3];
        const int stepState = (int)m_pGame->GetGameState();
#endif
        m_pGame->VOnUpdate(fixedDeltaMs);
#ifdef OPENCLAW_STARTUP_TRACE
        {
          const Uint64 stepT1 = SDL_GetPerformanceCounter();
          const double k = 1000.0 / perfFreq;
          const double stepMs = (stepT1 - stepT0) * k, evMs = (stepT0 - perfEv0) * k;
          if (stepMs + evMs >= 40.0) {
            char slow[300];
            snprintf(slow, sizeof(slow), "slow step: %.0f ms (state %d -> %d): events %.0f, processes %.0f, physics %.0f, views %.0f, actors %.0f, other %.0f ms",
                     stepMs + evMs, stepState, (int)m_pGame->GetGameState(), evMs,
                     (g_PerfLogicTicks[0] - stepL0) * k, (g_PerfLogicTicks[1] - stepL1) * k, (g_PerfLogicTicks[3] - stepL3) * k,
                     ((g_PerfLogicTicks[2] - stepL2) - (g_PerfLogicTicks[3] - stepL3)) * k,
                     stepMs - ((g_PerfLogicTicks[0] - stepL0) + (g_PerfLogicTicks[1] - stepL1) + (g_PerfLogicTicks[2] - stepL2)) * k);
            STARTUP_TRACE(slow);
          }
        }
#endif

        m_accumulator -= m_fixedTimestep;
      }

      // Interpolation factor (0..1): how far this render frame sits between the
      // previous and next fixed logic tick. Actors are drawn at the lerp of their
      // previous/current positions by this alpha, so motion stays smooth even when
      // the display refreshes faster than the fixed logic rate.
      m_alpha = m_accumulator / m_fixedTimestep;
#ifdef OPENCLAW_STARTUP_TRACE
      Uint64 perfT1 = SDL_GetPerformanceCounter();
#endif

      // Render every frame. The main loop is driven by requestAnimationFrame, which
      // is already synced to the display's refresh rate — so rendering adapts to the
      // device automatically (60Hz, 165Hz, VRR, ...) with no artificial cap. Motion
      // smoothness comes from interpolation (m_alpha) rather than raw render count.
      uint32 fixedDeltaMs = static_cast<uint32>(m_fixedTimestep * 1000.0);
      if (s_RefilterPending) {
        s_RefilterPending = false;
        PidResourceLoader::Refilter(&DrawRefilterProgress);
      }
      for (auto &pGameView : m_pGame->m_GameViews) {
        // PROFILE_CPU("ONLY RENDER");
        pGameView->VOnRender(fixedDeltaMs);
      }
      SecondScreen::Update();
#ifdef OPENCLAW_STARTUP_TRACE
      {
        Uint64 perfT2 = SDL_GetPerformanceCounter();
        s_renderTicks += perfT2 - perfT1;
        s_logicTicks += perfT1 - perfT0;
        s_perfFrames++;
        {
          // the slowest frame: its logic, drawing and file loading (stutter hunting)
          extern Uint64 g_PerfFrameLoadTicks;
          const double ms = (double)(perfT2 - perfT0) * 1000.0 / perfFreq;
          if (ms > s_worstMs) {
            s_worstMs = ms;
            s_worstLogicMs = (double)(perfT1 - perfT0) * 1000.0 / perfFreq;
            s_worstRenderMs = (double)(perfT2 - perfT1) * 1000.0 / perfFreq;
            s_worstLoadMs = (double)g_PerfFrameLoadTicks * 1000.0 / perfFreq;
          }
          g_PerfFrameLoadTicks = 0;
        }
        double secs = (double)(perfT2 - s_perfStart) / perfFreq;
        if (secs >= 5.0) {
          extern Uint64 g_PerfLogicTicks[4];
          char line[700];
          const double perUpdate = 1000.0 / perfFreq / std::max(1, s_perfUpdates);
          snprintf(line, sizeof(line), "perf: %.1f fps, logic %.1f ms/frame (%.2f updates), render %.1f ms, worst %.0f ms;"
                   " per update: processes %.1f, physics %.1f, views+actors %.1f (views %.1f) ms",
                   s_perfFrames / secs, (double)s_logicTicks * 1000.0 / perfFreq / s_perfFrames,
                   (double)s_perfUpdates / s_perfFrames, (double)s_renderTicks * 1000.0 / perfFreq / s_perfFrames, s_worstMs,
                   g_PerfLogicTicks[0] * perUpdate, g_PerfLogicTicks[1] * perUpdate, g_PerfLogicTicks[2] * perUpdate,
                   g_PerfLogicTicks[3] * perUpdate);
          g_PerfLogicTicks[0] = g_PerfLogicTicks[1] = g_PerfLogicTicks[2] = g_PerfLogicTicks[3] = 0;
          STARTUP_TRACE(line);
          {
            // where the render time goes (ms per frame)
            extern Uint64 g_PerfRenderTicks[12];
            extern int g_PerfTilesDrawn;
            const double perFrame = 1000.0 / perfFreq / std::max(1, s_perfFrames);
            char parts[400];
            snprintf(parts, sizeof(parts), "perf render: clear %.1f, background %.1f, action %.1f, actors %.1f, foreground %.1f,"
                     " HUD %.1f, other screen parts %.1f, frame copy %.1f, screen (flip) %.1f ms; %d tiles/frame",
                     g_PerfRenderTicks[0] * perFrame, g_PerfRenderTicks[1] * perFrame, g_PerfRenderTicks[2] * perFrame,
                     g_PerfRenderTicks[3] * perFrame, g_PerfRenderTicks[4] * perFrame, g_PerfRenderTicks[5] * perFrame,
                     g_PerfRenderTicks[6] * perFrame, g_PerfRenderTicks[7] * perFrame, g_PerfRenderTicks[8] * perFrame,
                     g_PerfTilesDrawn / std::max(1, s_perfFrames));
            for (int k = 0; k < 12; ++k) g_PerfRenderTicks[k] = 0;
            g_PerfTilesDrawn = 0;
            STARTUP_TRACE(parts);
            // the sounds played in these 5 s (most often first)
            extern std::map<std::string, int> g_PerfSoundPlays;
            std::vector<std::pair<int, std::string>> plays;
            int total = 0;
            for (const auto &sp : g_PerfSoundPlays) { plays.push_back({ sp.second, sp.first }); total += sp.second; }
            std::sort(plays.rbegin(), plays.rend());
            extern Uint64 g_PerfEventTicks;
            char evs[64];
            snprintf(evs, sizeof(evs), "events %.2f ms/frame; ", g_PerfEventTicks * 1000.0 / perfFreq / std::max(1, s_perfFrames));
            g_PerfEventTicks = 0;
            std::string sounds = std::string("perf sounds: ") + evs + std::to_string(total) + " played;";
            for (size_t k = 0; k < plays.size() && k < 6; ++k)
              sounds += " " + plays[k].second + " x" + std::to_string(plays[k].first);
            g_PerfSoundPlays.clear();
            STARTUP_TRACE(sounds);
            extern Uint64 g_PerfLoadTicks;
            extern int g_PerfLoads;
            char loads[200];
            snprintf(loads, sizeof(loads), "perf loads: %d files, %.1f ms; worst frame %.0f ms: logic %.1f, drawing %.1f, of it loading files %.1f ms",
                     g_PerfLoads, g_PerfLoadTicks * 1000.0 / perfFreq, s_worstMs, s_worstLogicMs, s_worstRenderMs, s_worstLoadMs);
            g_PerfLoads = 0;
            g_PerfLoadTicks = 0;
            STARTUP_TRACE(loads);
          }
          // actor updates per logic step, the slowest component types and actors
          extern int g_PerfActorUpdates;
          extern int g_PerfSampledSteps;
          extern std::map<const char*, Uint64> g_PerfComponentTicks;
          extern std::map<std::string, Uint64> g_PerfActorTicks;
          {
            const double perSample = 1000.0 / perfFreq / std::max(1, g_PerfSampledSteps);
            {
              extern int g_PerfBodiesMoving, g_PerfBodiesAwake, g_PerfContacts, g_PerfPhysicsSteps;
              const int n = std::max(1, g_PerfPhysicsSteps);
              char phys[160];
              snprintf(phys, sizeof(phys), "perf physics: %d moving bodies (%d awake), %d contacts per step",
                       g_PerfBodiesMoving / n, g_PerfBodiesAwake / n, g_PerfContacts / n);
              g_PerfBodiesMoving = g_PerfBodiesAwake = g_PerfContacts = g_PerfPhysicsSteps = 0;
              STARTUP_TRACE(phys);
            }
            std::string top = "perf: " + std::to_string(g_PerfActorUpdates / std::max(1, s_perfUpdates)) + " actor updates/step; components:";
            std::vector<std::pair<Uint64, std::string>> v;
            for (auto& c : g_PerfComponentTicks) v.push_back({ c.second, c.first });
            std::sort(v.rbegin(), v.rend());
            for (size_t i = 0; i < v.size() && i < 6; ++i) { char b[96]; snprintf(b, sizeof(b), " %s %.2f", v[i].second.c_str(), v[i].first * perSample); top += b; }
            v.clear();
            for (auto& a : g_PerfActorTicks) v.push_back({ a.second, a.first });
            std::sort(v.rbegin(), v.rend());
            top += "; actors:";
            for (size_t i = 0; i < v.size() && i < 5; ++i) { char b[96]; snprintf(b, sizeof(b), " %s %.2f", v[i].second.c_str(), v[i].first * perSample); top += b; }
            snprintf(line, sizeof(line), "%s", top.c_str());
          }
          g_PerfComponentTicks.clear();
          g_PerfActorTicks.clear();
          g_PerfSampledSteps = 0;
          g_PerfActorUpdates = 0;
          STARTUP_TRACE(line);
          s_perfStart = perfT2; s_logicTicks = s_renderTicks = 0; s_perfFrames = s_perfUpdates = 0; s_worstMs = 0;
        }
      }
      {
        static int s_tracedFrames = 0;
        if (s_tracedFrames < 3) {
          ++s_tracedFrames;
          STARTUP_TRACE(s_tracedFrames == 1 ? "frame 1 rendered" : (s_tracedFrames == 2 ? "frame 2 rendered" : "frame 3 rendered"));
        }
      }
#endif

      // m_pGame->VRenderDiagnostics();
    }

    // Artificially decrease fps. Configurable from console
    Util::Sleep(m_DebugOptions.cpuDelayMs);
  }
}

void Loop(void *instance) {
  auto self = static_cast<BaseGameApp *>(instance);
  self->StepLoop();
#ifdef __EMSCRIPTEN__
  if (!self->m_IsRunning) {
    emscripten_cancel_main_loop();
  }
#endif
}

//=====================================================================================================================
// BaseGameApp::Run - Main game loop
//
//    Handle events -> update game -> render views
//=====================================================================================================================

int32 BaseGameApp::Run() {
  // Some systems (like web browsers) does not support infinite loops.
  // We need to return control after each loop steps.
#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop_arg(Loop, this, 0, 1);
  // Loop must call emscripten_cancel_main_loop() to exit
#elif defined(__SWITCH__)
  // appletMainLoop() returns false when the system asks the app to exit
  // (e.g. closing it from the HOME menu).
  while (m_IsRunning && appletMainLoop()) {
    Loop(this);
  }
#elif defined(__3DS__)
  while (m_IsRunning && aptMainLoop()) {
    Loop(this);
  }
#else
  while (m_IsRunning) {
    Loop(this);
  }
#endif
  Terminate();
  return 0;
}

void BaseGameApp::OnEvent(SDL_Event &event) {
  switch (event.type) {
  case SDL_QUIT:
  case SDL_APP_TERMINATING: {
    m_IsRunning = false;
    break;
  }

  case SDL_WINDOWEVENT: {
    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED /*||
                event.window.event == SDL_WINDOWEVENT_RESIZED*/)
            {
      // With a fixed RenderSize the game's resolution does not follow the window.
      if (!m_pRenderTarget)
        OnDisplayChange(event.window.data1, event.window.data2);
    } else if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
      VOnRestore();
    } else if (event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
      void VOnMinimized();
    }
#if defined(__PSL1GHT__)
    // PS3: SDL reports the system menu (PS button) as the window left / entered
    if (event.window.event == SDL_WINDOWEVENT_LEAVE) Ps3Pad::SetSystemMenu(true);
    else if (event.window.event == SDL_WINDOWEVENT_ENTER) Ps3Pad::SetSystemMenu(false);
#endif
    break;
  }

  case SDL_APP_LOWMEMORY: {
    LOG_WARNING("Running low on memory");
    break;
  }

  case SDL_APP_DIDENTERBACKGROUND: {
    LOG("Entered background");
    break;
  }

  case SDL_APP_DIDENTERFOREGROUND: {
    LOG("Entered foreground");
    break;
  }

  case SDL_KEYDOWN:
  case SDL_KEYUP:
  case SDL_TEXTEDITING:
  case SDL_TEXTINPUT:
  case SDL_MOUSEMOTION:
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
  case SDL_MOUSEWHEEL:
  case SDL_UserTouchEvent: {
    if (m_pGame) {
      for (GameViewList::reverse_iterator iter = m_pGame->m_GameViews.rbegin();
           iter != m_pGame->m_GameViews.rend(); ++iter) {
        (*iter)->VOnEvent(event);
      }
    }
    break;
  }
  case SDL_FINGERUP:
  case SDL_FINGERDOWN:
  case SDL_FINGERMOTION: {
    if (m_pTouchManager) {
      switch (event.type) {
      case SDL_FINGERUP:
        m_pTouchManager->OnFingerUp(event.tfinger);
        break;
      case SDL_FINGERDOWN:
        m_pTouchManager->OnFingerDown(event.tfinger);
        break;
      case SDL_FINGERMOTION:
        m_pTouchManager->OnFingerMotion(event.tfinger);
        break;
      }
    }
    break;
  }
  }
}

void BaseGameApp::OnDisplayChange(int newWidth, int newHeight) {
  LOG("Display changed. New Width-Height: " + ToStr(newWidth) + "-" +
      ToStr(newHeight));
}

void BaseGameApp::VOnRestore() { LOG("Window restored."); }

void BaseGameApp::VOnMinimized() { LOG("Window minimized"); }

bool BaseGameApp::LoadStrings(std::string language) { return true; }

std::string BaseGameApp::GetString(std::string stringId) { return ""; }

void BaseGameApp::SetWindowSize(int width, int height, double scale) {
  Point newScale;
  if (scale > 0.0) {
    newScale = Point(scale, scale);
  } else {
    newScale = Point(((double)width / 640.0), (double)height / 480.0);
  }

  SDL_SetWindowSize(m_pWindow, width, height);
  m_WindowSize.Set(width, height);

  SetScale(newScale);

  HumanView* pView = GetHumanView();
  if (pView) pView->GetCamera()->SetSize(width, height);

  // Don't reset camera scale - this was causing viewport misalignment when zoom changed
  // GetHumanView()->GetCamera()->SetScale(1, 1);
}

HumanView *BaseGameApp::GetHumanView() const {
  HumanView *pView = NULL;
  for (auto &view : m_pGame->m_GameViews) {
    if (view->VGetType() == GameView_Human) {
      pView = static_cast<HumanView *>(view.get());
      break;
    }
  }

  return pView;
}

bool BaseGameApp::IsMenuActive() const {
  if (!m_pGame) return false;
  GameState state = m_pGame->GetGameState();
  if (state == GameState_Menu || state == GameState_LoadingMenu) return true;
  // Quick menu stays in IngameRunning but shows the in-game menu element.
  HumanView *pView = GetHumanView();
  if (pView) {
    auto pMenu = pView->GetActiveMenu();
    if (pMenu && pMenu->VIsVisible()) return true;
  }
  return false;
}

bool BaseGameApp::LoadGameOptions(const char *inConfigFile) {
  TiXmlDocument m_XmlConfiguration;
  if (!m_XmlConfiguration.LoadFile(inConfigFile)) {
    LOG_WARNING("Configuration file: " + std::string(inConfigFile) +
                " not found - creating default configuration");
    m_XmlConfiguration = CreateAndReturnDefaultConfig(inConfigFile);
  }

  TiXmlElement *configRoot = m_XmlConfiguration.RootElement();
  if (configRoot == NULL) {
    LOG_ERROR("Could not load root element for config file");
    return false;
  }

  //-------------------------------------------------------------------------
  // Display
  //-------------------------------------------------------------------------
  TiXmlElement *displayElem = configRoot->FirstChildElement("Display");
  if (displayElem) {
    TiXmlElement *windowSizeElem = displayElem->FirstChildElement("Size");
    if (windowSizeElem) {
      windowSizeElem->Attribute("width", &m_GameOptions.windowWidth);
      windowSizeElem->Attribute("height", &m_GameOptions.windowHeight);
    }

    ParseValueFromXmlElem(&m_GameOptions.scale,
                          displayElem->FirstChildElement("Scale"));
    if (TiXmlElement *renderSizeElem = displayElem->FirstChildElement("RenderSize")) {
      renderSizeElem->Attribute("width", &m_GameOptions.renderWidth);
      renderSizeElem->Attribute("height", &m_GameOptions.renderHeight);
    }
    ParseValueFromXmlElem(&m_GameOptions.useVerticalSync,
                          displayElem->FirstChildElement("UseVerticalSync"));
    ParseValueFromXmlElem(&m_GameOptions.isFullscreen,
                          displayElem->FirstChildElement("IsFullscreen"));
    ParseValueFromXmlElem(
        &m_GameOptions.isFullscreenDesktop,
        displayElem->FirstChildElement("IsFullscreenDesktop"));
  }

#ifdef __EMSCRIPTEN__
  // Default to 4:3 (640x480) in windowed mode
  // Resolution changes dynamically when entering fullscreen based on screen aspect ratio
  m_GameOptions.windowWidth = 640;
  m_GameOptions.windowHeight = 480;
  m_GameOptions.scale = 1.0;
#endif

  //-------------------------------------------------------------------------
  // Audio
  //-------------------------------------------------------------------------
  TiXmlElement *audioElem = configRoot->FirstChildElement("Audio");
  if (audioElem) {
    ParseValueFromXmlElem(&m_GameOptions.frequency,
                          audioElem->FirstChildElement("Frequency"));
    ParseValueFromXmlElem(&m_GameOptions.soundChannels,
                          audioElem->FirstChildElement("SoundChannels"));
    ParseValueFromXmlElem(&m_GameOptions.mixingChannels,
                          audioElem->FirstChildElement("MixingChannels"));
    ParseValueFromXmlElem(&m_GameOptions.chunkSize,
                          audioElem->FirstChildElement("ChunkSize"));
    ParseValueFromXmlElem(&m_GameOptions.midiRpcServerPath,
                          audioElem->FirstChildElement("MusiscRpcServerPath"));
    ParseValueFromXmlElem(&m_GameOptions.soundVolume,
                          audioElem->FirstChildElement("SoundVolume"));
    ParseValueFromXmlElem(&m_GameOptions.musicVolume,
                          audioElem->FirstChildElement("MusicVolume"));
    ParseValueFromXmlElem(&m_GameOptions.soundOn,
                          audioElem->FirstChildElement("SoundOn"));
    ParseValueFromXmlElem(&m_GameOptions.musicOn,
                          audioElem->FirstChildElement("MusicOn"));
  }

  //-------------------------------------------------------------------------
  // Assets
  //-------------------------------------------------------------------------
  TiXmlElement *assetsElem = configRoot->FirstChildElement("Assets");
  if (assetsElem) {
    ParseValueFromXmlElem(&m_GameOptions.assetsFolder,
                          assetsElem->FirstChildElement("AssetsFolder"));
    DO_AND_CHECK(
        ParseValueFromXmlElem(&m_GameOptions.rezArchive,
                              assetsElem->FirstChildElement("RezArchive")));
    DO_AND_CHECK(
        ParseValueFromXmlElem(&m_GameOptions.customArchive,
                              assetsElem->FirstChildElement("CustomArchive")));
    DO_AND_CHECK(ParseValueFromXmlElem(
        &m_GameOptions.resourceCacheSize,
        assetsElem->FirstChildElement("ResourceCacheSize")));
#if defined(__3DS__)
    // Also for config.xml files written by older versions (24 MB)
    m_GameOptions.resourceCacheSize = std::min(m_GameOptions.resourceCacheSize, 16u);
#elif defined(__PSL1GHT__)
    // PS3: ~200 MB for everything (150 MB of cache left no room for the music)
    m_GameOptions.resourceCacheSize = std::min(m_GameOptions.resourceCacheSize, 40u);
#endif
    ParseValueFromXmlElem(&m_GameOptions.tempDir,
                          assetsElem->FirstChildElement("TempDir"));
    DO_AND_CHECK(ParseValueFromXmlElem(
        &m_GameOptions.savesFile, assetsElem->FirstChildElement("SavesFile")));
    if (TiXmlElement *langElem = assetsElem->FirstChildElement("Language"))
    {
      if (langElem->GetText()) m_GameOptions.language = langElem->GetText();
    }
  }

  //-------------------------------------------------------------------------
  // Font
  //-------------------------------------------------------------------------
  TiXmlElement *fontRootElem = configRoot->FirstChildElement("Font");
  if (fontRootElem) {
    for (TiXmlElement *fontElem = fontRootElem->FirstChildElement("Font");
         fontElem != NULL; fontElem = fontElem->NextSiblingElement("Font")) {
      if (fontElem->GetText()) {
        // next to the game or in its bundle (PS3: no working folder)
        std::string fontPath = ResolveBundledFile(
            m_GameOptions.assetsFolder + std::string(fontElem->GetText()));
        m_GameOptions.fontNames.push_back(fontPath.c_str());
      }
    }

    TiXmlElement *consoleFontElem =
        fontRootElem->FirstChildElement("ConsoleFont");
    if (consoleFontElem) {
      consoleFontElem->Attribute("size", (int *)&m_GameOptions.consoleFontSize);
      if (const char *fontName = consoleFontElem->Attribute("font")) {
        m_GameOptions.consoleFontName = ResolveBundledFile(m_GameOptions.assetsFolder + fontName);
      }
    }
  }

  //-------------------------------------------------------------------------
  // Console
  //-------------------------------------------------------------------------
  if (TiXmlElement *pConsoleRootElem =
          configRoot->FirstChildElement("Console")) {
    ParseValueFromXmlElem(
        &m_GameOptions.consoleConfig.backgroundImagePath,
        pConsoleRootElem->FirstChildElement("BackgroundImagePath"));
    ParseValueFromXmlElem(
        &m_GameOptions.consoleConfig.stretchBackgroundImage,
        pConsoleRootElem->FirstChildElement("StretchBackgroundImage"));
    ParseValueFromXmlElem(&m_GameOptions.consoleConfig.widthRatio,
                          pConsoleRootElem->FirstChildElement("WidthRatio"));
    ParseValueFromXmlElem(&m_GameOptions.consoleConfig.heightRatio,
                          pConsoleRootElem->FirstChildElement("HeightRatio"));
    ParseValueFromXmlElem(
        &m_GameOptions.consoleConfig.lineSeparatorHeight,
        pConsoleRootElem->FirstChildElement("LineSeparatorHeight"));
    ParseValueFromXmlElem(
        &m_GameOptions.consoleConfig.commandPromptOffsetY,
        pConsoleRootElem->FirstChildElement("CommandPromptOffsetY"));
    ParseValueFromXmlElem(
        &m_GameOptions.consoleConfig.consoleAnimationSpeed,
        pConsoleRootElem->FirstChildElement("ConsoleAnimationSpeed"));
    if (TiXmlElement *pElem =
            pConsoleRootElem->FirstChildElement("FontColor")) {
      int r, g, b;
      pElem->Attribute("r", &r);
      pElem->Attribute("g", &g);
      pElem->Attribute("b", &b);
      m_GameOptions.consoleConfig.fontColor.r = r;
      m_GameOptions.consoleConfig.fontColor.g = g;
      m_GameOptions.consoleConfig.fontColor.b = b;
    }
    ParseValueFromXmlElem(&m_GameOptions.consoleConfig.fontHeight,
                          pConsoleRootElem->FirstChildElement("FontHeight"));
    ParseValueFromXmlElem(&m_GameOptions.consoleConfig.leftOffset,
                          pConsoleRootElem->FirstChildElement("LeftOffset"));
    ParseValueFromXmlElem(&m_GameOptions.consoleConfig.commandPrompt,
                          pConsoleRootElem->FirstChildElement("CommandPrompt"));
    ParseValueFromXmlElem(&m_GameOptions.consoleConfig.fontPath,
                          pConsoleRootElem->FirstChildElement("FontPath"));

    m_GameOptions.consoleConfig.backgroundImagePath = ResolveBundledFile(
        m_GameOptions.assetsFolder +
        m_GameOptions.consoleConfig.backgroundImagePath);
    m_GameOptions.consoleConfig.fontPath = ResolveBundledFile(
        m_GameOptions.assetsFolder + m_GameOptions.consoleConfig.fontPath);
  } else {
    LOG_ERROR("Console configuration is missing.");
    return false;
  }
  //-------------------------------------------------------------------------
  // Global options
  //-------------------------------------------------------------------------
  if (TiXmlElement *pGlobalOptionsRootElem =
          configRoot->FirstChildElement("GlobalOptions")) {
    ParseValueFromXmlElem(
        &m_GlobalOptions.maxJumpSpeed,
        pGlobalOptionsRootElem->FirstChildElement("MaxJumpSpeed"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.maxFallSpeed,
        pGlobalOptionsRootElem->FirstChildElement("MaxFallSpeed"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.idleSoundQuoteIntervalMs,
        pGlobalOptionsRootElem->FirstChildElement("IdleSoundQuoteInterval"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.platformSpeedModifier,
        pGlobalOptionsRootElem->FirstChildElement("PlatformSpeedModifier"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.runSpeed,
        pGlobalOptionsRootElem->FirstChildElement("RunSpeed"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.powerupRunSpeed,
        pGlobalOptionsRootElem->FirstChildElement("PowerupRunSpeed"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.maxJumpHeight,
        pGlobalOptionsRootElem->FirstChildElement("MaxJumpHeight"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.powerupMaxJumpHeight,
        pGlobalOptionsRootElem->FirstChildElement("PowerupMaxJumpHeight"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.startLookUpOrDownTime,
        pGlobalOptionsRootElem->FirstChildElement("StartLookUpOrDownTime"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.freezeTime,
        pGlobalOptionsRootElem->FirstChildElement("FreezeTime"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.maxLookUpOrDownDistance,
        pGlobalOptionsRootElem->FirstChildElement("MaxLookUpOrDownDistance"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.lookUpOrDownSpeed,
        pGlobalOptionsRootElem->FirstChildElement("LookUpOrDownSpeed"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.scoreScreenPalPath,
        pGlobalOptionsRootElem->FirstChildElement("ScoreScreenPalPath"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.clawRunningSpeed,
        pGlobalOptionsRootElem->FirstChildElement("ClawRunningSpeed"));
    /*ParseValueFromXmlElem(&m_GlobalOptions.springBoardSpringHeight,
        pGlobalOptionsRootElem->FirstChildElement("SpringBoardSpringHeight"));*/
    ParseValueFromXmlElem(
        &m_GlobalOptions.springBoardSpringSpeed,
        pGlobalOptionsRootElem->FirstChildElement("SpringBoardSpringSpeed"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.clawMinFallHeight,
        pGlobalOptionsRootElem->FirstChildElement("ClawMinFallHeight"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.loadAllLevelSaves,
        pGlobalOptionsRootElem->FirstChildElement("LoadAllLevelSaves"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.showPosition,
        pGlobalOptionsRootElem->FirstChildElement("ShowPosition"));
    ParseValueFromXmlElem(
        &m_GlobalOptions.gameLogicFps,
        pGlobalOptionsRootElem->FirstChildElement("GameLogicFps"));
  }

  //-------------------------------------------------------------------------
  // Control options
  //-------------------------------------------------------------------------
  if (TiXmlElement *pControlOptionsRootElem =
          configRoot->FirstChildElement("ControlOptions")) {
    ParseValueFromXmlElem(
        &m_ControlOptions.useAlternateControls,
        pControlOptionsRootElem->FirstChildElement("UseAlternateControls"));
    if (TiXmlElement *pTouchScreenOptionsRootElem =
            pControlOptionsRootElem->FirstChildElement("TouchScreen")) {
      ParseValueFromXmlElem(
          &m_ControlOptions.touchScreen.enable,
          pTouchScreenOptionsRootElem->FirstChildElement("Enable"));
      ParseValueFromXmlElem(
          &m_ControlOptions.touchScreen.distanceThreshold,
          pTouchScreenOptionsRootElem->FirstChildElement("DistanceThreshold"));
      ParseValueFromXmlElem(
          &m_ControlOptions.touchScreen.timeThreshold,
          pTouchScreenOptionsRootElem->FirstChildElement("TimeThreshold"));
    }
  }

  //-------------------------------------------------------------------------
  // Debug options
  //-------------------------------------------------------------------------
  if (TiXmlElement *pDebugOptionsRootElem =
          configRoot->FirstChildElement("DebugOptions")) {
    ParseValueFromXmlElem(
        &m_DebugOptions.bSkipBossFightIntro,
        pDebugOptionsRootElem->FirstChildElement("SkipBossFightIntro"));
    ParseValueFromXmlElem(&m_DebugOptions.bSkipMenu,
                          pDebugOptionsRootElem->FirstChildElement("SkipMenu"));
    ParseValueFromXmlElem(&m_DebugOptions.cpuDelayMs,
                          pDebugOptionsRootElem->FirstChildElement("CpuDelay"));
    ParseValueFromXmlElem(
        &m_DebugOptions.lastImplementedLevel,
        pDebugOptionsRootElem->FirstChildElement("LastImplementedLevel"));
#ifndef __EMSCRIPTEN__
    // Older config.xml files on the SD card still say 13; level 14 is playable now.
    if (m_DebugOptions.lastImplementedLevel < 14) {
      m_DebugOptions.lastImplementedLevel = 14;
    }
#endif
    ParseValueFromXmlElem(
        &m_DebugOptions.skipMenuToLevel,
        pDebugOptionsRootElem->FirstChildElement("SkipMenuToLevel"));
  }

  return true;
}

void BaseGameApp::GetMouseState(int *x, int *y) {
  int mx = 0, my = 0;
  SDL_GetMouseState(&mx, &my);
  if (m_pRenderTarget && m_pWindow && m_GameOptions.renderWidth > 0 && m_GameOptions.renderHeight > 0) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(m_pWindow, &ww, &wh);
    if (ww > 0 && wh > 0 && (ww != m_GameOptions.renderWidth || wh != m_GameOptions.renderHeight)) {
      // the frame is shown fitted (letterboxed) in the window, see PresentFrame
      double fit = std::min((double)ww / m_GameOptions.renderWidth, (double)wh / m_GameOptions.renderHeight);
      double ox = (ww - m_GameOptions.renderWidth * fit) / 2, oy = (wh - m_GameOptions.renderHeight * fit) / 2;
      mx = (int)((mx - ox) / fit);
      my = (int)((my - oy) / fit);
    }
  }
  if (x) *x = mx;
  if (y) *y = my;
}

#if defined(RECLAW_GPU)
// The level's scale at the normal zoom when the GPU draws: 3DS: the game's
// 480x288 on the 400x240 screen; PS3: the software picture's size (960x540
// on a 1080p TV, 854x480 otherwise) on the TV.
static double GpuBaseScale() {
#if defined(RECLAW_PS3_GPU)
  int tvW = 1280, tvH = 720, w = 854, h = 480;
  ReclawPS3_TvSize(&tvW, &tvH);
  ReclawPS3_GameSize(&w, &h);
  return h > 0 ? (double)tvH / h : 1.5;
#else
  return 400.0 / 480.0;
#endif
}
#endif

// The level's scale at the normal zoom (set when the display starts).
static double s_ViewBaseScale = 0.0;

bool BaseGameApp::ViewZoomSupported() {
#if defined(RECLAW_GPU)
  if (ReclawGpu_Active()) return true;
#endif
#if defined(RECLAW_SDL_SMOOTH)
  return s_ViewBaseScale > 0.0;
#else
  return false;
#endif
}

bool BaseGameApp::SmoothSupported() {
#if defined(RECLAW_GPU)
  if (ReclawGpu_Active()) return true;
#endif
#if defined(RECLAW_SDL_SMOOTH)
  return true;
#else
  return false;
#endif
}

bool BaseGameApp::Smooth() {
  return PortSettings::GetInt("smooth", 1) != 0;
}

void BaseGameApp::SetSmooth(bool on) {
  PortSettings::SetInt("smooth", on ? 1 : 0);
#if defined(RECLAW_GPU)
  if (ReclawGpu_Active()) {
    ReclawGpu_SetSmooth(on ? 1 : 0);
    LOG_WARNING(std::string("Pictures ") + (on ? "smoothed" : "sharp"));
    return;
  }
#endif
#if defined(RECLAW_SDL_SMOOTH)
  Image::SetSmooth(on);
  LOG_WARNING(std::string("Pictures ") + (on ? "smoothed" : "sharp"));
#endif
}

bool BaseGameApp::AspectClassic() {
  return PortSettings::GetInt("aspect_classic", 0) != 0;
}

void BaseGameApp::SetAspectClassic(bool classic) {
  PortSettings::SetInt("aspect_classic", classic ? 1 : 0);
  LOG_WARNING(std::string("Aspect ") + (classic ? "classic (4:3)" : "widescreen"));
}

int BaseGameApp::Filter() {
  // Pixel filters: hidden from the menu until they are tuned; filters_test=1
  // in port_settings.txt turns them on for trying (filter=1..3).
  if (PortSettings::GetInt("filters_test", 0) == 0) return 0;
  const int k = PortSettings::GetInt("filter", 0);
  return (k >= 0 && k <= 3) ? k : 0;
}

static void DrawRefilterProgress(int done, int total) {
  SDL_Renderer *r = g_pApp->GetRenderer();
  float sx = 1.0f, sy = 1.0f;
  SDL_RenderGetScale(r, &sx, &sy);
  SDL_RenderSetScale(r, 1.0f, 1.0f);
  const Point window = g_pApp->GetWindowSize();
  const int W = (int)window.x, H = (int)window.y;
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);
  const int bw = W / 2, bh = std::max(6, H / 36);
  SDL_Rect frame = { (W - bw) / 2, H / 2 - bh / 2, bw, bh };
  SDL_SetRenderDrawColor(r, 236, 196, 98, 255);
  SDL_RenderDrawRect(r, &frame);
  SDL_Rect fill = { frame.x + 2, frame.y + 2, (int)((long long)(bw - 4) * done / std::max(1, total)), bh - 4 };
  SDL_RenderFillRect(r, &fill);
  Util::RenderForcePresent(r);
  SDL_RenderSetScale(r, sx, sy);
}

void BaseGameApp::SetFilter(int kind) {
  PortSettings::SetInt("filter", kind);
  Image::SetFilter(Filter());
  s_RefilterPending = true;
  LOG_WARNING("Pixel filter " + ToStr(Filter()));
}

int BaseGameApp::ViewZoom() {
  const int z = PortSettings::GetInt("view_zoom", 100);
  return z >= 95 ? 100 : (z >= 85 ? 90 : (z >= 75 ? 80 : 70));
}

void BaseGameApp::SetViewZoom(int percent) {
  PortSettings::SetInt("view_zoom", percent);
  if (ViewZoomSupported() && s_ViewBaseScale > 0.0) {
    // (the menus draw at scale 1 and put this scale back after each frame;
    // the level's camera takes the new size at once)
    m_GameOptions.scale = s_ViewBaseScale * ViewZoom() / 100.0;
    if (HumanView *pView = GetHumanView()) {
      if (shared_ptr<CameraNode> pCamera = pView->GetCamera()) {
        pCamera->SetScale((float)m_GameOptions.scale, (float)m_GameOptions.scale);
        pCamera->SetSize(pCamera->GetWidth(), pCamera->GetHeight());
      }
    }
    LOG_WARNING("View zoom " + ToStr(ViewZoom()) + "%");
  }
}

void BaseGameApp::PresentFrame(bool present) {
#if defined(RECLAW_GPU)
  // GPU drawing (3DS, PS3): the game's frame is the screen itself
  if (m_pRenderTarget && m_pRenderTarget == ReclawGpu_GameFrame()) {
    if (present) { CrtEffect::BeforePresent(m_pRenderer); SDL_RenderPresent(m_pRenderer); }
    return;
  }
#endif
  if (!m_pRenderTarget) {
    if (present) { CrtEffect::BeforePresent(m_pRenderer); SDL_RenderPresent(m_pRenderer); }
    return;
  }
  float scaleX = 1.0f, scaleY = 1.0f;
  SDL_RenderGetScale(m_pRenderer, &scaleX, &scaleY);

  SDL_SetRenderTarget(m_pRenderer, NULL);
  SDL_RenderSetScale(m_pRenderer, 1.0f, 1.0f);
  int outW = 0, outH = 0;
  SDL_GetRendererOutputSize(m_pRenderer, &outW, &outH);
  // Keep the aspect ratio; letterbox if the screen differs.
  double fit = std::min((double)outW / m_GameOptions.renderWidth,
                        (double)outH / m_GameOptions.renderHeight);
  SDL_Rect dst;
  dst.w = (int)(m_GameOptions.renderWidth * fit + 0.5);
  dst.h = (int)(m_GameOptions.renderHeight * fit + 0.5);
  dst.x = (outW - dst.w) / 2;
  dst.y = (outH - dst.h) / 2;
#ifdef OPENCLAW_STARTUP_TRACE
  extern Uint64 g_PerfRenderTicks[12];
  const Uint64 pf0 = SDL_GetPerformanceCounter();
#endif
  // (the frame covers the whole screen: nothing to clear around it)
  if (dst.x > 0 || dst.y > 0 || dst.w < outW || dst.h < outH) {
    SDL_SetRenderDrawColor(m_pRenderer, 0, 0, 0, 255);
    SDL_RenderClear(m_pRenderer);
  }
  SDL_RenderCopy(m_pRenderer, m_pRenderTarget, NULL, &dst);
#ifdef OPENCLAW_STARTUP_TRACE
  const Uint64 pf1 = SDL_GetPerformanceCounter();
  g_PerfRenderTicks[7] += pf1 - pf0;
#endif
  if (present) { CrtEffect::BeforePresent(m_pRenderer); SDL_RenderPresent(m_pRenderer); }
#ifdef OPENCLAW_STARTUP_TRACE
  g_PerfRenderTicks[8] += SDL_GetPerformanceCounter() - pf1;
#endif

  SDL_SetRenderTarget(m_pRenderer, m_pRenderTarget);
  SDL_RenderSetScale(m_pRenderer, scaleX, scaleY);
}

void BaseGameApp::SaveGameOptions(const char *outConfigFile) {
  LOG_ERROR("Not implemented yet!");
  return;
}

//=====================================================================================================================
// Private implementations
//=====================================================================================================================

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::RegisterEngineEvents
//---------------------------------------------------------------------------------------------------------------------
void BaseGameApp::RegisterEngineEvents() {
  /*REGISTER_EVENT(EventData_Environment_Loaded);
  REGISTER_EVENT(EventData_New_Actor);
  REGISTER_EVENT(EventData_Move_Actor);
  REGISTER_EVENT(EventData_Destroy_Actor);
  REGISTER_EVENT(EventData_Request_New_Actor);
  REGISTER_EVENT(EventData_Network_Player_Actor_Assignment);
  REGISTER_EVENT(EventData_Attach_Actor);
  REGISTER_EVENT(EventData_Collideable_Tile_Created);
  REGISTER_EVENT(EventData_Start_Climb);
  REGISTER_EVENT(EventData_Actor_Fire);
  REGISTER_EVENT(EventData_Actor_Attack);
  REGISTER_EVENT(EventData_New_HUD_Element);
  REGISTER_EVENT(EventData_New_Life);
  REGISTER_EVENT(EventData_Updated_Score);
  REGISTER_EVENT(EventData_Updated_Lives);
  REGISTER_EVENT(EventData_Updated_Health);
  REGISTER_EVENT(EventData_Updated_Ammo);
  REGISTER_EVENT(EventData_Updated_Ammo_Type);
  REGISTER_EVENT(EventData_Request_Change_Ammo_Type);
  REGISTER_EVENT(EventData_Teleport_Actor);*/
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::InitializeDisplay
//
// Initializes SDL2 main game window and creates SDL2 renderer
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::InitializeDisplay(GameOptions &gameOptions) {
  LOG(">>>>> Initializing display...");

  STARTUP_TRACE("SDL_Init");
  if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0) {
    LOG_ERROR("Failed to initialize SDL2 library. Error: %s" +
              std::string(SDL_GetError()));
    return false;
  }

  STARTUP_TRACE("SDL_CreateWindow");
  Uint32 windowFlags = SDL_WINDOW_SHOWN;
#ifdef __WIIU__
  // The game on the TV; the GamePad shows the status screen (SecondScreen).
  windowFlags |= SDL_WINDOW_WIIU_TV_ONLY;
#endif
  m_pWindow = SDL_CreateWindow(VGetGameTitle(), SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, gameOptions.windowWidth,
                               gameOptions.windowHeight, windowFlags);
  if (m_pWindow == NULL) {
    LOG_ERROR("Failed to create main window");
    return false;
  }

  if (gameOptions.isFullscreen) {
    SDL_SetWindowFullscreen(m_pWindow, SDL_WINDOW_FULLSCREEN);
    SDL_GetWindowSize(m_pWindow, &m_GameOptions.windowWidth,
                      &m_GameOptions.windowHeight);
  } else if (gameOptions.isFullscreenDesktop) {
    SDL_SetWindowFullscreen(m_pWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_GetWindowSize(m_pWindow, &m_GameOptions.windowWidth,
                      &m_GameOptions.windowHeight);
  }

  m_WindowSize.Set(gameOptions.windowWidth, gameOptions.windowHeight);

#ifdef __EMSCRIPTEN__
  // Get actual canvas dimensions from JavaScript (may differ from config defaults)
  // This ensures menus render correctly from the first frame
  int canvasWidth = EM_ASM_INT({
    var canvas = document.getElementById('canvas');
    return canvas ? canvas.width : 640;
  });
  int canvasHeight = EM_ASM_INT({
    var canvas = document.getElementById('canvas');
    return canvas ? canvas.height : 480;
  });
  if (canvasWidth > 0 && canvasHeight > 0) {
    m_WindowSize.Set(canvasWidth, canvasHeight);
    LOG("Using canvas dimensions: " + ToStr(canvasWidth) + "x" + ToStr(canvasHeight));
  }
#endif

  uint32 rendererFlags = SDL_RENDERER_ACCELERATED;
  if (gameOptions.useVerticalSync) {
    rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
  }

  STARTUP_TRACE("SDL_CreateRenderer");
  m_pRenderer = SDL_CreateRenderer(m_pWindow, -1, rendererFlags);
  if (m_pRenderer == NULL) {
    // Without a GPU renderer (Nintendo 3DS: SDL draws in software) the
    // accelerated / vsync request finds no driver; take whatever there is.
    LOG_WARNING(std::string("No accelerated renderer (") + SDL_GetError() + "), trying any");
    m_pRenderer = SDL_CreateRenderer(m_pWindow, -1, 0);
  }
  if (m_pRenderer == NULL) {
    LOG_ERROR("Failed to create SDL2 Renderer. Error: %s" +
              std::string(SDL_GetError()));
    return false;
  }

  STARTUP_TRACE("renderer created");

#if defined(__ANDROID__) || defined(OPENCLAW_PORTMASTER) || defined(OPENCLAW_STEAMDECK)
  // Phones, tablets, Linux handhelds and the Steam Deck: the same 480 lines
  // tall view as the other ports, as wide as the screen's aspect ratio
  // (Steam Deck 1280x800: 768x480 scaled by 5/3; square or 4:3 screens:
  // 640x480, letterboxed if needed).
  if (gameOptions.renderWidth <= 0 || gameOptions.renderHeight <= 0) {
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(m_pRenderer, &outW, &outH);
    if (outW > 0 && outH > 0) {
      if (outH > outW) std::swap(outW, outH);   // landscape
      gameOptions.renderHeight = 480;
      gameOptions.renderWidth = std::max(640, (480 * outW / outH) & ~1);
    }
  }
#endif

#if defined(__PSL1GHT__) || defined(__PS3__)
#if defined(RECLAW_PS3_GPU)
  if (ReclawGpu_Active()) {
    // The RSX draws straight into the TV's picture (1920x1080 / 1280x720),
    // the level at the software picture's view scaled up (sharp, every
    // picture scaled by the RSX); menus and movies at the TV's size.
    int tvW = 1280, tvH = 720;
    ReclawPS3_TvSize(&tvW, &tvH);
    gameOptions.renderWidth = 0;
    gameOptions.renderHeight = 0;
    s_ViewBaseScale = GpuBaseScale();
    gameOptions.scale = s_ViewBaseScale * ViewZoom() / 100.0;
    m_WindowSize.Set(tvW, tvH);
    m_pRenderTarget = ReclawGpu_GameFrame();
    SDL_SetRenderTarget(m_pRenderer, m_pRenderTarget);
    LOG_WARNING("PS3: drawing with the RSX at " + ToStr(tvW) + "x" + ToStr(tvH) + ", level scale " +
                ToStr(gameOptions.scale));
  } else
#endif
  // PS3: the game draws in software at the size of its frame (960x540 on
  // a 1080p TV, 854x480 otherwise; platform/ps3/compat/ps3_sdl_screen.cpp)
  if (gameOptions.renderWidth <= 0 || gameOptions.renderHeight <= 0) {
    int outW = 854, outH = 480;
    ReclawPS3_GameSize(&outW, &outH);
    gameOptions.renderWidth = outW;
    gameOptions.renderHeight = outH;
  }
#endif
#if defined(__3DS__)
  // 3DS: the game draws into an ARGB8888 frame (SDL's fast software blits
  // need it; the screen itself is RGBA8888), converted once per frame.
  // The game sees 480x288 (1.2x the 400x240 screen, 44% more of the level),
  // shrunk to the screen once per frame; <RenderSize> in config.xml
  // overrides it (400x240 = 1:1, fastest).
  // Full resolution (480x288). port_settings.txt "render_scale=" 50..99
  // draws smaller (e.g. 75: 360x216 at scale 0.75 - the same view of the
  // level, fewer pixels - stretched to the screen).
  int renderPct3DS = std::max(50, std::min(100, PortSettings::GetInt("render_scale", 100)));
#if defined(RECLAW_3DS_GPU)
  if (ReclawGpu_Active()) {
    // The GPU draws: no frame in memory, the game draws straight onto the
    // 400x240 screen at scale 400/480 (the same 480x288 of the level; the
    // GPU scales every picture).
    gameOptions.renderWidth = 0;
    gameOptions.renderHeight = 0;
    s_ViewBaseScale = GpuBaseScale();
    gameOptions.scale = s_ViewBaseScale * ViewZoom() / 100.0;
    renderPct3DS = 100;
    // The game draws into "its frame" (the screen with a view of its own):
    // movies, the title scroll and other screens that select no target
    // and draw by the screen's size get scale 1, as with the frame in memory.
    m_pRenderTarget = ReclawGpu_GameFrame();
    SDL_SetRenderTarget(m_pRenderer, m_pRenderTarget);
    LOG_WARNING("3DS: drawing with the GPU, the level at 480x288 on the 400x240 screen");
  } else
#endif
  if (gameOptions.renderWidth <= 0 || gameOptions.renderHeight <= 0) {
    gameOptions.renderWidth = 480 * renderPct3DS / 100;
    gameOptions.renderHeight = 288 * renderPct3DS / 100;
  } else {
    renderPct3DS = 100;   // a <RenderSize> of one's own: drawn 1:1
  }
  const Uint32 renderTargetFormat = SDL_PIXELFORMAT_ARGB8888;
#elif defined(__PSL1GHT__)
  // PS3 (software): the frame's own format, so the pictures (ARGB8888) and the
  // copy of the whole frame to the screen use SDL's fast blits.
  const Uint32 renderTargetFormat = SDL_PIXELFORMAT_ARGB8888;
#else
  const Uint32 renderTargetFormat = SDL_PIXELFORMAT_RGBA8888;
#endif
#ifndef __EMSCRIPTEN__
  if (gameOptions.renderWidth > 0 && gameOptions.renderHeight > 0) {
    m_pRenderTarget = SDL_CreateTexture(m_pRenderer, renderTargetFormat,
                                        SDL_TEXTUREACCESS_TARGET,
                                        gameOptions.renderWidth, gameOptions.renderHeight);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    if (m_pRenderTarget) {
      // Smooth final upscale (like the browser scaling its canvas); the game's
      // own sprites keep nearest-neighbour sampling inside the target.
#if defined(__3DS__) || defined(__PSL1GHT__)
      // 3DS, PS3 (software): plain sampling, the smooth one costs several ms a frame
      SDL_SetTextureScaleMode(m_pRenderTarget, SDL_ScaleModeNearest);
#if defined(__PSL1GHT__)
      // copied without blending (the frame is opaque anyway)
      SDL_SetTextureBlendMode(m_pRenderTarget, SDL_BLENDMODE_NONE);
#endif
#else
      SDL_SetTextureScaleMode(m_pRenderTarget, SDL_ScaleModeLinear);
#endif
    }
#endif
    if (m_pRenderTarget && SDL_SetRenderTarget(m_pRenderer, m_pRenderTarget) == 0) {
      // From now on the engine sees a window of RenderSize at scale 1.
      gameOptions.scale = 1.0;
#if defined(__3DS__)
      if (renderPct3DS < 100) {
        gameOptions.scale = renderPct3DS / 100.0;   // the level drawn smaller
        Image::SetScaledRenderer(m_pRenderer, (float)gameOptions.scale);
      }
#endif
      m_WindowSize.Set(gameOptions.renderWidth, gameOptions.renderHeight);
      LOG_WARNING("Rendering at " + ToStr(gameOptions.renderWidth) + "x" +
                  ToStr(gameOptions.renderHeight) + ", scaled to the window");
    } else {
      LOG_WARNING("Render target unavailable (" + std::string(SDL_GetError()) +
                  "), rendering directly");
      if (m_pRenderTarget) {
        SDL_DestroyTexture(m_pRenderTarget);
        m_pRenderTarget = nullptr;
      }
    }
  }
#endif
#if defined(RECLAW_SDL_SMOOTH)
  // Switch, Vita: the game draws straight onto the screen at its scale;
  // Options > view zoom shows more of the level (a smaller scale).
  if (!m_pRenderTarget && gameOptions.scale > 0.0) {
    s_ViewBaseScale = gameOptions.scale;
    gameOptions.scale = s_ViewBaseScale * ViewZoom() / 100.0;
  }
#endif
  SDL_RenderSetScale(m_pRenderer, (float)gameOptions.scale,
                     (float)gameOptions.scale);
#if defined(RECLAW_GPU) || defined(RECLAW_SDL_SMOOTH)
  if (SmoothSupported()) SetSmooth(Smooth());
#endif
  Image::SetFilter(Filter());

  // Stage 2a: install WASM-first platform event pipeline.
  if (!m_videoPlatform) {
#ifdef __EMSCRIPTEN__
    m_videoPlatform.reset(new WasmVideoPlatform());
#else
    m_videoPlatform.reset(new NullVideoPlatform());
#endif
    m_videoPlatform->SetEventQueue(&m_appEventQueue);
    m_videoPlatform->Init(gameOptions.windowWidth, gameOptions.windowHeight);
  }

#ifndef __EMSCRIPTEN__
  SdlGamepadInput::Initialize();
#endif

  LOG("Display successfully initialized.");

  return true;
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::InitializeAudio
//
// Initializes SDL Mixer as audio device
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::InitializeAudio(GameOptions &gameOptions) {
  LOG(">>>>> Initializing audio...");

  m_pAudio = new Audio();
  if (!m_pAudio->Initialize(gameOptions)) {
    LOG_ERROR("Failed to initialize SDL Mixer audio subsystem");
#if defined(__3DS__)
    // Homebrew sound on the 3DS needs the DSP firmware dumped once.
    if (std::string(SDL_GetError()).find("dspfirm") != std::string::npos) {
      g_StartupFailureHint = "Sound needs the 3DS DSP firmware: run the DSP1 homebrew once|"
                             "(it creates sdmc:/3ds/dspfirm.cdc), then start ReClaw again.";
    }
#endif
    return false;
  }

  LOG("Audio successfully initialized.");

  return true;
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::InitializeResources
//
// Register CLAW.REZ resource file as resource cache for assets the game is
// going to use
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::InitializeResources(GameOptions &gameOptions) {
  LOG(">>>>> Initializing resource cache...");

  if (gameOptions.rezArchive.empty()) {
    LOG_ERROR("No specified assets resource files in configuration.");
    return false;
  }

  std::string rezArchivePath =
      gameOptions.assetsFolder + gameOptions.rezArchive;

  // Either CLAW.REZ or a folder with its unpacked contents (e.g. "Assets").
  IResourceFile *rezArchive = nullptr;
  if (ResourceDirectory::LooksLikeAssetsFolder(rezArchivePath)) {
    rezArchive = new ResourceDirectory(rezArchivePath);
  } else {
    rezArchive = new ResourceRezArchive(rezArchivePath);
  }
  std::shared_ptr<ResourceCache> m_pResourceCache{new ResourceCache(
      gameOptions.resourceCacheSize, rezArchive, ORIGINAL_RESOURCE)};
  if (!m_pResourceCache->Init()) {
    LOG_ERROR("Failed to initialize resource cachce from resource file: " +
              std::string(rezArchivePath));
    return false;
  }

  m_pResourceCache->RegisterLoader(DefaultResourceLoader::Create());
  m_pResourceCache->RegisterLoader(XmlResourceLoader::Create());
  m_pResourceCache->RegisterLoader(WwdResourceLoader::Create());
  m_pResourceCache->RegisterLoader(PalResourceLoader::Create());
  m_pResourceCache->RegisterLoader(PidResourceLoader::Create());
  m_pResourceCache->RegisterLoader(AniResourceLoader::Create());
  m_pResourceCache->RegisterLoader(WavResourceLoader::Create());
  m_pResourceCache->RegisterLoader(MidiResourceLoader::Create());
  m_pResourceCache->RegisterLoader(PcxResourceLoader::Create());

  std::string customArchivePath =
      ResolveBundledFile(gameOptions.assetsFolder + gameOptions.customArchive);

  IResourceFile *pCustomArchive = new ResourceZipArchive(customArchivePath);
  std::shared_ptr<ResourceCache> pCustomCache{
      new ResourceCache(
#if defined(__3DS__)
          // 3DS: little memory; the port's own files are read when needed
          8,
#elif defined(__PSL1GHT__)
          24,
#else
          50,
#endif
          pCustomArchive, CUSTOM_RESOURCE)};
  if (!pCustomCache->Init()) {
    LOG_ERROR("Failed to initialize resource cachce from resource file: " +
              customArchivePath);
    return false;
  }

  pCustomCache->RegisterLoader(DefaultResourceLoader::Create());
  pCustomCache->RegisterLoader(XmlResourceLoader::Create());
  pCustomCache->RegisterLoader(WavResourceLoader::Create());
  pCustomCache->RegisterLoader(PcxResourceLoader::Create());
  pCustomCache->RegisterLoader(PngResourceLoader::Create());

#ifndef __EMSCRIPTEN__
  {
    // Digital releases keep the sounds in a "Sounds" folder next to CLAW.REZ.
    std::string rezDir = rezArchivePath;
    size_t slash = rezDir.find_last_of('/');
    rezDir = (slash == std::string::npos) ? std::string(".") : rezDir.substr(0, slash);
    std::vector<std::string> roots = {rezDir + "/Sounds", rezDir + "/SOUNDS", rezDir + "/sounds",
                                      rezDir + "/Assets", rezDir + "/ASSETS"};
#ifndef __EMSCRIPTEN__
    for (const std::string& folder : Platform::GameDataFolders(0, nullptr))
    {
      roots.push_back(folder + "Sounds");
      roots.push_back(folder + "Assets");
    }
#endif
    // Data already comes from an unpacked folder: its sounds are served directly.
    if (ResourceDirectory::LooksLikeAssetsFolder(rezArchivePath)) roots.clear();
    LooseSoundFiles::SetSearchRoots(roots);

    // Cutscenes: CLAW.FEC (or MOVIES/CLAW.FEC, MOVIES/*.SMK) next to the game data.
    std::vector<std::string> movieRoots = {rezDir};
#ifndef __EMSCRIPTEN__
    for (std::string folder : Platform::GameDataFolders(0, nullptr))
    {
      if (!folder.empty() && folder.back() == '/') folder.pop_back();
      movieRoots.push_back(folder);
    }
#endif
    MoviePlayer::SetSearchRoots(movieRoots, gameOptions.userDirectory);
    // Custom levels: a "Custom" folder in the same places.
    // Custom levels: reclaw/custom/ (or a "Custom" folder next to the game data).
    std::vector<std::string> customRoots = movieRoots;
    customRoots.insert(customRoots.begin(), Platform::RootDir());
    CustomLevels::SetSearchRoots(customRoots);
  }
#endif

  m_pResourceMgr = new ResourceMgrImpl();
  // Custom cache first so ASSETS.ZIP can override CLAW.REZ assets
  m_pResourceMgr->VAddResourceCache(pCustomCache);
  m_pResourceMgr->VAddResourceCache(m_pResourceCache);

  // Language of the port's own texts follows the REZ (or the config).
  Lang::Detect(gameOptions.language);

#if (defined(__PSL1GHT__) || defined(__3DS__)) && !defined(__EMSCRIPTEN__)
  // First start: which instruments the game's music uses (for the small soundfont).
#if defined(__3DS__)
  if (Platform::LevelMusicOn())
#endif
  SoundFontTrim::CollectUsage();
  {
    // First start: the small soundfont made now, with a progress screen
    // (in the background it took the game's CPU time on the 3DS).
    Uint32 lastDraw = 0;
    SoundFontTrim::MakeSmallNow([this, &lastDraw](int done, int total) {
      SDL_PumpEvents();
      const Uint32 now = SDL_GetTicks();
      if (lastDraw && now - lastDraw < 250) return;
      lastDraw = now;
      if (m_pRenderTarget) SDL_SetRenderTarget(m_pRenderer, m_pRenderTarget);
      SDL_SetRenderDrawColor(m_pRenderer, 0, 0, 0, 255);
      SDL_RenderClear(m_pRenderer);
      const Point size = GetWindowSize();
      const int px = std::max(12, (int)(size.y / 16));
      const int pct = total > 0 ? done * 100 / total : 0;
      RaceUi::DrawText(m_pRenderer, Lang::Tr("Przygotowanie muzyki", "Preparing the music"), px, (int)size.x / 2, (int)size.y / 2 - px * 2, 0);
      RaceUi::DrawText(m_pRenderer, std::to_string(pct) + "%", px, (int)size.x / 2, (int)size.y / 2, 0);
      RaceUi::DrawText(m_pRenderer, Lang::Tr("tylko przy pierwszym uruchomieniu", "only at the first start"), std::max(10, px * 2 / 3),
                       (int)size.x / 2, (int)size.y / 2 + px * 2, 0);
      PresentFrame(true);
    });
    RaceUi::FreeTextCache();
  }
#endif

  LOG("Resource cache successfully initialized");

  return true;
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::InitializeFont
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::InitializeFont(GameOptions &gameOptions) {

  if (TTF_Init() < 0) {
    LOG_ERROR("Failed to initialize SDL TTF font subsystem");
    return false;
  }

  m_pConsoleFont = TTF_OpenFont(ResolveBundledFile(gameOptions.consoleFontName).c_str(),
                                gameOptions.consoleFontSize);
  if (m_pConsoleFont == NULL) {
    LOG_ERROR("Failed to load TTF font");
    return false;
  }

  LOG("Font successfully initialized...");

  return true;
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::InitializeLocalization
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::InitializeLocalization(GameOptions &gameOptions) {
  return true;
}

bool BaseGameApp::InitializeTouchManager(GameOptions &gameOptions) {
  auto &touchScreenConfig = GetControlOptions()->touchScreen;
  if (touchScreenConfig.enable) {

    m_pTouchManager = new TouchManager{};

    LOG("Touch resolver successfully initialized...");
  }

  return true;
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::ReadActorXmlPrototypes
//
//     Reads XML documents containing various actor prototypes which are then
//     used to instantiate concrete actors
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::ReadActorXmlPrototypes(GameOptions &gameOptions) {

  // In case of reload
  if (!m_ActorXmlPrototypeMap.empty()) {
    LOG_TRACE("Detected reload of actor prototypes !");
    m_ActorXmlPrototypeMap.clear();
  }

  std::vector<std::string> xmlActorPrototypeFiles =
      m_pResourceMgr->VMatch("/ACTOR_PROTOTYPES/*.XML");

  for (const std::string &protoFile : xmlActorPrototypeFiles) {
    TiXmlElement *pActorProtoElem =
        XmlResourceLoader::LoadAndReturnRootXmlElement(protoFile.c_str());
    std::string protoName;
    if (!ParseAttributeFromXmlElem(&protoName, "ActorPrototypeName",
                                   pActorProtoElem)) {
      LOG_ERROR(protoFile +
                " is missing ActorPrototypeName attribute in its root node !");
    } else {
      // LOG(protoFile + ": " + protoName);
      ActorPrototype actorProto = StringToEnum_ActorPrototype(protoName);

      // Create our own pointer
      TiXmlNode *pDuplicateNode = pActorProtoElem->Clone();
      assert(pDuplicateNode);
      TiXmlElement *pActorProtoElemDuplicate = pDuplicateNode->ToElement();
      assert(pActorProtoElemDuplicate);

      auto iter = m_ActorXmlPrototypeMap.insert(
          std::make_pair(actorProto, pActorProtoElemDuplicate));
      if (!iter.second) {
        LOG_WARNING("Multi " + EnumToString_ActorPrototype(actorProto) +
                    " actor prototype definitions! Fix ASSETS!");
        std::string typeName;
        if (ParseAttributeFromXmlElem(&typeName, "Type", pActorProtoElem)) {
          LOG_WARNING(typeName + " type won't be used!");
        }
      }
    }
  }

  bool loadedAllRequired = true;

  // When I provide specific purpose API, I should be very dilligent
  for (int actorPrototypeIdx = ActorPrototype_Start + 1;
       actorPrototypeIdx < ActorPrototype_Max; actorPrototypeIdx++) {
    auto findIt =
        m_ActorXmlPrototypeMap.find(ActorPrototype(actorPrototypeIdx));
    if (findIt == m_ActorXmlPrototypeMap.end()) {
      LOG_ERROR("Actor prototype: \"" +
                EnumToString_ActorPrototype(ActorPrototype(actorPrototypeIdx)) +
                std::string("\" was not found !"));
      loadedAllRequired = false;
    }
  }

  if (loadedAllRequired) {
    LOG("Actor prototypes loaded successfully.");
  } else {
    LOG_ERROR("Some of the actor prototypes were not loaded.");
  }

  return loadedAllRequired;
}

bool BaseGameApp::ReadLevelMetadata(GameOptions &gameOptions) {
  static const std::string LEVEL_METADATA_ARCHIVE_FOLDER = "/LEVEL_METADATA";

  // In case of reload
  if (!m_LevelMetadataMap.empty()) {
    LOG_TRACE("Detected reload of level metadata !");
    m_LevelMetadataMap.clear();
  }

  std::vector<std::string> xmlLevelMetadataFiles =
      m_pResourceMgr->VMatch(LEVEL_METADATA_ARCHIVE_FOLDER + "/*.XML");
  for (const std::string &metadataFile : xmlLevelMetadataFiles) {
    TiXmlElement *pRootElem =
        XmlResourceLoader::LoadAndReturnRootXmlElement(metadataFile.c_str());
    if (pRootElem == NULL) {
      LOG_ERROR("Failed to parse level metadata file: " + metadataFile);
      return false;
    }

    shared_ptr<LevelMetadata> pLevelMetadata =
        shared_ptr<LevelMetadata>(new LevelMetadata);

    // LevelMetadata.LevelNumber
    if (!ParseValueFromXmlElem(&pLevelMetadata->levelNumber,
                               pRootElem->FirstChildElement("LevelNumber"))) {
      LOG_ERROR("Missing \"LevelNumber\" element in metadata file: " +
                metadataFile);
      return false;
    }

    // LevelMetadata.LevelName
    if (!ParseValueFromXmlElem(&pLevelMetadata->levelName,
                               pRootElem->FirstChildElement("LevelName"))) {
      LOG_ERROR("Missing \"LevelName\" element in metadata file: " +
                metadataFile);
      return false;
    }

    // LevelMetadata.LogicsToActorPrototypes.*
    TiXmlElement *pLogicToProtoRootElem =
        pRootElem->FirstChildElement("LogicsToActorPrototypes");
    if (pLogicToProtoRootElem == NULL) {
      LOG_ERROR(
          "Missing \"LogicsToActorPrototypes\" element in metadata file: " +
          metadataFile);
      return false;
    }

    for (TiXmlElement *pLogicToProtoElem =
             pLogicToProtoRootElem->FirstChildElement("LogicToActorPrototype");
         pLogicToProtoElem != NULL;
         pLogicToProtoElem =
             pLogicToProtoElem->NextSiblingElement("LogicToActorPrototype")) {
      std::string logic;
      std::string protoStr;

      if (!ParseAttributeFromXmlElem(&logic, "logic", pLogicToProtoElem)) {
        LOG_ERROR("Missing \"logic\" in LogicToActorPrototype element in "
                  "metadata file: " +
                  metadataFile);
        return false;
      }
      if (!ParseAttributeFromXmlElem(&protoStr, "actorPrototype",
                                     pLogicToProtoElem)) {
        LOG_ERROR("Missing \"actorPrototype\" in LogicToActorPrototype element "
                  "in metadata file: " +
                  metadataFile);
        return false;
      }

      ActorPrototype proto = StringToEnum_ActorPrototype(protoStr);
      pLevelMetadata->logicToActorPrototypeMap.insert(
          std::make_pair(logic, proto));
    }

    // LevelMetadata.ClawSpawnPositions.*
    TiXmlElement *pSpawnPositionsRootElem =
        pRootElem->FirstChildElement("ClawSpawnPositions");
    if (pSpawnPositionsRootElem == NULL) {
      LOG_ERROR("Missing \"ClawSpawnPositions\" element in metadata file: " +
                metadataFile);
      return false;
    }

    for (TiXmlElement *pSpawnPositionElem =
             pSpawnPositionsRootElem->FirstChildElement("ClawSpawnPosition");
         pSpawnPositionElem != NULL;
         pSpawnPositionElem =
             pSpawnPositionElem->NextSiblingElement("ClawSpawnPosition")) {
      std::string spawnNumberStr;
      int spawnNumber;
      Point spawnPosition;

      if (!ParseAttributeFromXmlElem(&spawnNumberStr, "spawnNumber",
                                     pSpawnPositionElem)) {
        LOG_ERROR("Missing \"spawnNumber\" in ClawSpawnPosition element in "
                  "metadata file: " +
                  metadataFile);
        return false;
      }

      spawnNumber = std::stoi(spawnNumberStr);

      if (!ParseValueFromXmlElem(&spawnPosition, pSpawnPositionElem, "x",
                                 "y")) {
        LOG_ERROR("Missing spawn positon x and y in ClawSpawnPosition element "
                  "in metadata file: " +
                  metadataFile + " for spawnNumber: " + ToStr(spawnNumber));
        return false;
      }

      pLevelMetadata->checkpointNumberToSpawnPositionMap.insert(
          std::make_pair(spawnNumber, spawnPosition));
    }

    // LevelMetadata.TopLadderEnds.*
    TiXmlElement *pTopLadderEndsRootElem =
        pRootElem->FirstChildElement("TopLadderEnds");
    if (pTopLadderEndsRootElem == NULL) {
      LOG_ERROR("Missing \"TopLadderEnds\" element in metadata file: " +
                metadataFile);
      return false;
    }

    for (TiXmlElement *pTopLadderEndElem =
             pTopLadderEndsRootElem->FirstChildElement("TopLadderEnd");
         pTopLadderEndElem != NULL;
         pTopLadderEndElem =
             pTopLadderEndElem->NextSiblingElement("TopLadderEnd")) {
      std::string tileIdStr;
      int tileId;
      Point offset;

      if (!ParseAttributeFromXmlElem(&tileIdStr, "tileId", pTopLadderEndElem)) {
        LOG_ERROR(
            "Missing \"tileId\" in TopLadderEnd element in metadata file: " +
            metadataFile);
        return false;
      }

      tileId = std::stoi(tileIdStr);

      if (!ParseValueFromXmlElem(&offset, pTopLadderEndElem, "offsetX",
                                 "offsetY")) {
        LOG_ERROR("Missing ladder positon x and y in TopLadderEnd element in "
                  "metadata file: " +
                  metadataFile + " for tileId: " + ToStr(tileId));
        return false;
      }

      pLevelMetadata->tileIdToTopLadderEndMap.insert(
          std::make_pair(tileId, offset));
    }

    // LevelMetadata.TileDeathEffect
    TiXmlElement *pTileDeathEffectElem =
        pRootElem->FirstChildElement("TileDeathEffect");
    if (pTileDeathEffectElem == NULL) {
      LOG_ERROR("Missing \"TileDeathEffect\" element in metadata file: " +
                metadataFile);
      return false;
    }

    if (!ParseAttributeFromXmlElem(&pLevelMetadata->tileDeathEffectType, "type",
                                   pTileDeathEffectElem)) {
      LOG_ERROR(
          "Missing \"type\" in TileDeathEffect element in metadata file: " +
          metadataFile);
      return false;
    }

    if (pLevelMetadata->tileDeathEffectType != "NONE") {
      if (!ParseValueFromXmlElem(&pLevelMetadata->tileDeathEffectOffset,
                                 pTileDeathEffectElem, "offsetX", "offsetY")) {
        LOG_ERROR("Missing death effect positon x and y in TileDeathEffect "
                  "element in metadata file: " +
                  metadataFile +
                  " for type: " + pLevelMetadata->tileDeathEffectType);
        return false;
      }
    }

    m_LevelMetadataMap.insert(
        std::make_pair(pLevelMetadata->levelNumber, pLevelMetadata));
  }

  if (m_LevelMetadataMap.empty()) {
    LOG_ERROR("Failed to load any level metadatas from \"" +
              LEVEL_METADATA_ARCHIVE_FOLDER + "\" archive folder !");
    return false;
  }

  return true;
}

bool BaseGameApp::LoadSingleLevelMetadata(int levelNumber) {
  // Check if already loaded
  if (m_LevelMetadataMap.find(levelNumber) != m_LevelMetadataMap.end()) {
    return true; // Already loaded
  }

  // Build metadata file path
  std::string metadataFile = "/LEVEL_METADATA/LEVEL" + ToStr(levelNumber) + ".XML";

  // Load and parse XML
  TiXmlElement *pRootElem =
      XmlResourceLoader::LoadAndReturnRootXmlElement(metadataFile.c_str());
  if (pRootElem == NULL) {
    LOG_ERROR("Failed to parse level metadata file: " + metadataFile);
    return false;
  }

  shared_ptr<LevelMetadata> pLevelMetadata =
      shared_ptr<LevelMetadata>(new LevelMetadata);

  // LevelMetadata.LevelNumber
  if (!ParseValueFromXmlElem(&pLevelMetadata->levelNumber,
                             pRootElem->FirstChildElement("LevelNumber"))) {
    LOG_ERROR("Missing \"LevelNumber\" element in metadata file: " +
              metadataFile);
    return false;
  }

  // LevelMetadata.LevelName
  if (!ParseValueFromXmlElem(&pLevelMetadata->levelName,
                             pRootElem->FirstChildElement("LevelName"))) {
    LOG_ERROR("Missing \"LevelName\" element in metadata file: " +
              metadataFile);
    return false;
  }

  // LevelMetadata.LogicsToActorPrototypes.*
  TiXmlElement *pLogicToProtoRootElem =
      pRootElem->FirstChildElement("LogicsToActorPrototypes");
  if (pLogicToProtoRootElem == NULL) {
    LOG_ERROR(
        "Missing \"LogicsToActorPrototypes\" element in metadata file: " +
        metadataFile);
    return false;
  }

  for (TiXmlElement *pLogicToProtoElem =
           pLogicToProtoRootElem->FirstChildElement("LogicToActorPrototype");
       pLogicToProtoElem != NULL;
       pLogicToProtoElem =
           pLogicToProtoElem->NextSiblingElement("LogicToActorPrototype")) {
    std::string logic;
    std::string protoStr;

    if (!ParseAttributeFromXmlElem(&logic, "logic", pLogicToProtoElem)) {
      LOG_ERROR("Missing \"logic\" in LogicToActorPrototype element in "
                "metadata file: " +
                metadataFile);
      return false;
    }
    if (!ParseAttributeFromXmlElem(&protoStr, "actorPrototype",
                                   pLogicToProtoElem)) {
      LOG_ERROR("Missing \"actorPrototype\" in LogicToActorPrototype element "
                "in metadata file: " +
                metadataFile);
      return false;
    }

    ActorPrototype proto = StringToEnum_ActorPrototype(protoStr);
    pLevelMetadata->logicToActorPrototypeMap.insert(
        std::make_pair(logic, proto));
  }

  // LevelMetadata.ClawSpawnPositions.*
  TiXmlElement *pSpawnPositionsRootElem =
      pRootElem->FirstChildElement("ClawSpawnPositions");
  if (pSpawnPositionsRootElem == NULL) {
    LOG_ERROR("Missing \"ClawSpawnPositions\" element in metadata file: " +
              metadataFile);
    return false;
  }

  for (TiXmlElement *pSpawnPositionElem =
           pSpawnPositionsRootElem->FirstChildElement("ClawSpawnPosition");
       pSpawnPositionElem != NULL;
       pSpawnPositionElem =
           pSpawnPositionElem->NextSiblingElement("ClawSpawnPosition")) {
    std::string spawnNumberStr;
    int spawnNumber;
    Point spawnPosition;

    if (!ParseAttributeFromXmlElem(&spawnNumberStr, "spawnNumber",
                                   pSpawnPositionElem)) {
      LOG_ERROR("Missing \"spawnNumber\" in ClawSpawnPosition element in "
                "metadata file: " +
                metadataFile);
      return false;
    }

    spawnNumber = std::stoi(spawnNumberStr);

    if (!ParseValueFromXmlElem(&spawnPosition, pSpawnPositionElem, "x",
                               "y")) {
      LOG_ERROR("Missing spawn positon x and y in ClawSpawnPosition element "
                "in metadata file: " +
                metadataFile + " for spawnNumber: " + ToStr(spawnNumber));
      return false;
    }

    pLevelMetadata->checkpointNumberToSpawnPositionMap.insert(
        std::make_pair(spawnNumber, spawnPosition));
  }

  // LevelMetadata.TopLadderEnds.*
  TiXmlElement *pTopLadderEndsRootElem =
      pRootElem->FirstChildElement("TopLadderEnds");
  if (pTopLadderEndsRootElem == NULL) {
    LOG_ERROR("Missing \"TopLadderEnds\" element in metadata file: " +
              metadataFile);
    return false;
  }

  for (TiXmlElement *pTopLadderEndElem =
           pTopLadderEndsRootElem->FirstChildElement("TopLadderEnd");
       pTopLadderEndElem != NULL;
       pTopLadderEndElem =
           pTopLadderEndElem->NextSiblingElement("TopLadderEnd")) {
    std::string tileIdStr;
    int tileId;
    Point offset;

    if (!ParseAttributeFromXmlElem(&tileIdStr, "tileId", pTopLadderEndElem)) {
      LOG_ERROR(
          "Missing \"tileId\" in TopLadderEnd element in metadata file: " +
          metadataFile);
      return false;
    }

    tileId = std::stoi(tileIdStr);

    if (!ParseValueFromXmlElem(&offset, pTopLadderEndElem, "offsetX",
                               "offsetY")) {
      LOG_ERROR("Missing ladder positon x and y in TopLadderEnd element in "
                "metadata file: " +
                metadataFile + " for tileId: " + ToStr(tileId));
      return false;
    }

    pLevelMetadata->tileIdToTopLadderEndMap.insert(
        std::make_pair(tileId, offset));
  }

  // LevelMetadata.TileDeathEffect
  TiXmlElement *pTileDeathEffectElem =
      pRootElem->FirstChildElement("TileDeathEffect");
  if (pTileDeathEffectElem == NULL) {
    LOG_ERROR("Missing \"TileDeathEffect\" element in metadata file: " +
              metadataFile);
    return false;
  }

  if (!ParseAttributeFromXmlElem(&pLevelMetadata->tileDeathEffectType, "type",
                                 pTileDeathEffectElem)) {
    LOG_ERROR(
        "Missing \"type\" in TileDeathEffect element in metadata file: " +
        metadataFile);
    return false;
  }

  if (pLevelMetadata->tileDeathEffectType != "NONE") {
    if (!ParseValueFromXmlElem(&pLevelMetadata->tileDeathEffectOffset,
                               pTileDeathEffectElem, "offsetX", "offsetY")) {
      LOG_ERROR("Missing death effect positon x and y in TileDeathEffect "
                "element in metadata file: " +
                metadataFile +
                " for type: " + pLevelMetadata->tileDeathEffectType);
      return false;
    }
  }

  // Store in map
  m_LevelMetadataMap.insert(
      std::make_pair(pLevelMetadata->levelNumber, pLevelMetadata));

  LOG("Lazy-loaded metadata for level " + ToStr(levelNumber));

  return true;
}

const shared_ptr<LevelMetadata>
BaseGameApp::GetLevelMetadata(int levelNumber) const {
  // Check if already loaded
  auto findIt = m_LevelMetadataMap.find(levelNumber);
  if (findIt != m_LevelMetadataMap.end()) {
    return findIt->second; // Already loaded, return cached
  }

  // Lazy-load the metadata for this level
  LOG("Requesting metadata for level " + ToStr(levelNumber));

  // Need to cast away const because we're modifying m_LevelMetadataMap
  BaseGameApp* mutableThis = const_cast<BaseGameApp*>(this);
  if (!mutableThis->LoadSingleLevelMetadata(levelNumber)) {
    LOG_ERROR("Failed to lazy-load level metadata for level: " + ToStr(levelNumber));
    return NULL;
  }

  // Now it should be in the map
  findIt = m_LevelMetadataMap.find(levelNumber);
  if (findIt == m_LevelMetadataMap.end()) {
    LOG_ASSERT("LevelMetadata still not found after lazy-load for level: " + ToStr(levelNumber));
    return NULL;
  }

  return findIt->second;
}

//---------------------------------------------------------------------------------------------------------------------
// BaseGameApp::InitializeEventMgr
//---------------------------------------------------------------------------------------------------------------------
bool BaseGameApp::InitializeEventMgr() {
  m_pEventMgr = new EventMgr("BaseGameApp Event Mgr", true);
  if (!m_pEventMgr) {
    LOG_ERROR("Failed to create EventMgr.");
    return false;
  }

  return true;
}

Point BaseGameApp::GetScale() {
  float scaleX, scaleY;
  uint32 windowFlags = GetWindowFlags();
  Point scale(1.0, 1.0);

  SDL_RenderGetScale(m_pRenderer, &scaleX, &scaleY);

  scale.Set((double)scaleX, (double)scaleY);

  return scale;
}

void BaseGameApp::SetScale(Point scale) {
  SDL_RenderSetScale(m_pRenderer, (float)scale.x, (float)scale.y);
}

uint32 BaseGameApp::GetWindowFlags() { return SDL_GetWindowFlags(m_pWindow); }

class TiXmlMergeVisitor : public TiXmlVisitor {
public:
  TiXmlMergeVisitor(TiXmlElement *pParentRootElem)
      : m_pParentRootElem(pParentRootElem) {}

  virtual bool VisitEnter(const TiXmlElement &elem,
                          const TiXmlAttribute *pAttribute) override {
    std::string elemPath = GeTiXmlElementElementPath(&elem);
    /*LOG("Visiting element: " + std::string(elem.Value()) + " with path: " +
    elemPath); if (pAttribute != NULL)
    {
        LOG("Attribute value: " + std::string(pAttribute->Name()));
    }*/

    if (TiXmlElement *pParentElemToBeModified =
            GetTiXmlElementFromPath(m_pParentRootElem, elemPath)) {
      // Check for attributes and text to be changed
      UpdateTiXmlElementAttributes(pParentElemToBeModified, &elem);
      UpdateTiXmlElementText(pParentElemToBeModified, &elem);
    } else {
      // Parent does not contain this element, so add it with all its
      // descendants

      // First get parented node to which we will add the new one
      elemPath = elemPath.substr(0, elemPath.find_last_of("."));
      TiXmlElement *pParentElemToWhichAdd =
          GetTiXmlElementFromPath(m_pParentRootElem, elemPath);
      assert(pParentElemToWhichAdd != NULL);

      // Clone and add
      TiXmlElement *pChildElemCopy = elem.Clone()->ToElement();
      pParentElemToWhichAdd->LinkEndChild(pChildElemCopy);

      // We just added the whole subtree
      return false;
    }

    return true;
  }

  virtual bool VisitExit(const TiXmlElement &elem) override {
    /*if (elem.Parent() == NULL)
    {
        m_pParentRootElem->Print(stdout, -1);
        LOG("Visiting DONE");
    }*/

    return true;
  }

private:
  std::string GeTiXmlElementElementPath(const TiXmlElement *pElem) {
    assert(pElem != NULL);
    std::string path = pElem->Value();

    while (pElem->Parent() && pElem->Parent()->ToElement()) {
      pElem = pElem->Parent()->ToElement();
      path.insert(0, std::string(pElem->Value()) + ".");
    }

    return path;
  }

  int UpdateTiXmlElementAttributes(TiXmlElement *updateThis,
                                   const TiXmlElement *withThis) {
    int numModifiedAttributes = 0;
    for (const TiXmlAttribute *pNewAttr = withThis->FirstAttribute();
         pNewAttr != NULL; pNewAttr = pNewAttr->Next()) {
      updateThis->SetAttribute(pNewAttr->Name(), pNewAttr->Value());
      // LOG("Updated parent's [" + std::string(pNewAttr->Name()) + "] attribute
      // with value [" + std::string(pNewAttr->Value()) + "]");
      numModifiedAttributes++;
    }

    return numModifiedAttributes;
  }

  bool UpdateTiXmlElementText(TiXmlElement *updateThis,
                              const TiXmlElement *withThis) {
    if (withThis->GetText() == NULL) {
      return false;
    }

    return SetTiXmlElementText(withThis->GetText(), updateThis);
  }

  TiXmlElement *m_pParentRootElem;
};

// Remark: Caller is getting a NEW copy of the prototype -> caller is
// responsible for freeing this copy !
TiXmlElement *BaseGameApp::GetActorPrototypeElem(ActorPrototype proto) {
  auto findIt = m_ActorXmlPrototypeMap.find(proto);
  if (findIt == m_ActorXmlPrototypeMap.end()) {
    LOG_ERROR("Failed to find ActorPrototype: " + ToStr((int)proto));
  }
  assert(findIt != m_ActorXmlPrototypeMap.end());

  TiXmlElement *pCopy = findIt->second->Clone()->ToElement();
  assert(pCopy != NULL);

  TiXmlElement *pRootElem = pCopy;
  assert(pRootElem != NULL);

  // If this is derived XML, load its parent and apply its changes
  if (pRootElem->Attribute("Parent") != NULL) {
    ActorPrototype parentProto =
        StringToEnum_ActorPrototype(pRootElem->Attribute("Parent"));
    TiXmlElement *pParentRootElem = GetActorPrototypeElem(parentProto);
    assert(pParentRootElem != NULL);

    // Merge changes from child to parent (child contains only delta changes)
    // IE. Child applies changes to Parent
    // * New attributes are added
    // * Existing attributes are overwritten
    // * New nodes are added
    // * Existing node text is overwritten
    TiXmlMergeVisitor mergeVisitor(pParentRootElem);
    pRootElem->Accept(&mergeVisitor);

    SAFE_DELETE(pRootElem);

    // pParentRootElem->Print(stdout, -1);

    return pParentRootElem;
  }

  return pCopy;
}

//=====================================================================================================================
// Events
//=====================================================================================================================

void BaseGameApp::RegisterAllDelegates() {
  IEventMgr::Get()->VAddListener(
      MakeDelegate(this, &BaseGameApp::QuitGameDelegate),
      EventData_Quit_Game::sk_EventType);
}

void BaseGameApp::RemoveAllDelegates() {
  IEventMgr::Get()->VRemoveListener(
      MakeDelegate(this, &BaseGameApp::QuitGameDelegate),
      EventData_Quit_Game::sk_EventType);
}

void BaseGameApp::QuitGameDelegate(IEventDataPtr pEventData) {
  Terminate();
  exit(0);
}

//=====================================================================================================================
// XML config management
//=====================================================================================================================

TiXmlElement *CreateDefaultDisplayConfig() {
  TiXmlElement *display = new TiXmlElement("Display");

  XML_ADD_2_PARAM_ELEMENT("Size", "width", ToStr(1280).c_str(), "height",
                          ToStr(768).c_str(), display);
  XML_ADD_TEXT_ELEMENT("Scale", "1", display);
  XML_ADD_TEXT_ELEMENT("UseVerticalSync", "true", display);
  XML_ADD_TEXT_ELEMENT("IsFullscreen", "false", display);
  XML_ADD_TEXT_ELEMENT("IsFullscreenDesktop", "false", display);

  return display;
}

TiXmlElement *CreateDefaultAudioConfig() {
  TiXmlElement *audio = new TiXmlElement("Audio");

  XML_ADD_TEXT_ELEMENT("Frequency", "44100", audio);
  XML_ADD_TEXT_ELEMENT("SoundChannels", "1", audio);
  XML_ADD_TEXT_ELEMENT("MixingChannels", "24", audio);
  XML_ADD_TEXT_ELEMENT("ChunkSize", "2048", audio);
  XML_ADD_TEXT_ELEMENT("SoundVolume", "50", audio);
  XML_ADD_TEXT_ELEMENT("MusicVolume", "50", audio);
  XML_ADD_TEXT_ELEMENT("MusicRpcServerPath", "MidiProc.exe", audio);

  return audio;
}

TiXmlElement *CreateDefaultFontConfig() {
  TiXmlElement *font = new TiXmlElement("Font");

  XML_ADD_TEXT_ELEMENT("Font", "clacon.ttf", font);
  XML_ADD_2_PARAM_ELEMENT("ConsoleFont", "font", "clacon.ttf", "size", "20",
                          font);

  return font;
}

TiXmlElement *CreateDefaultAssetsConfig() {
  TiXmlElement *assets = new TiXmlElement("Assets");

  XML_ADD_TEXT_ELEMENT("RezArchive", "CLAW.REZ", assets);
  XML_ADD_TEXT_ELEMENT("ResourceCacheSize", "50", assets);
  XML_ADD_TEXT_ELEMENT("TempDir", ".", assets);
  XML_ADD_TEXT_ELEMENT("SavesFile", "SAVES.XML", assets);

  return assets;
}

TiXmlElement *CreateDefaultConsoleConfig() {
  TiXmlElement *pConsoleConfig = new TiXmlElement("Console");

  // Assume that the default constructor has default values set
  ConsoleConfig defaultConfig;

  XML_ADD_TEXT_ELEMENT("BackgroundImagePath",
                       defaultConfig.backgroundImagePath.c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("StretchBackgroundImage",
                       ToStr(defaultConfig.stretchBackgroundImage).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("WidthRatio", ToStr(defaultConfig.widthRatio).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("HeightRatio", ToStr(defaultConfig.heightRatio).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("LineSeparatorHeight",
                       ToStr(defaultConfig.lineSeparatorHeight).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("CommandPromptOffsetY",
                       ToStr(defaultConfig.commandPromptOffsetY).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("ConsoleAnimationSpeed",
                       ToStr(defaultConfig.consoleAnimationSpeed).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("FontPath", defaultConfig.fontPath.c_str(),
                       pConsoleConfig);

  TiXmlElement *pColorElem = new TiXmlElement("FontColor");
  pColorElem->SetAttribute("r", defaultConfig.fontColor.r);
  pColorElem->SetAttribute("g", defaultConfig.fontColor.g);
  pColorElem->SetAttribute("b", defaultConfig.fontColor.b);
  pConsoleConfig->LinkEndChild(pColorElem);

  XML_ADD_TEXT_ELEMENT("FontHeight", ToStr(defaultConfig.fontHeight).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("LeftOffset", ToStr(defaultConfig.leftOffset).c_str(),
                       pConsoleConfig);
  XML_ADD_TEXT_ELEMENT("CommandPrompt", defaultConfig.commandPrompt.c_str(),
                       pConsoleConfig);

  return pConsoleConfig;
}

TiXmlDocument
BaseGameApp::CreateAndReturnDefaultConfig(const char *inConfigFile) {
  TiXmlDocument xmlConfig;

  //----- [Configuration]
  TiXmlElement *root = new TiXmlElement("Configuration");
  xmlConfig.LinkEndChild(root);

  root->LinkEndChild(CreateDefaultDisplayConfig());
  root->LinkEndChild(CreateDefaultAudioConfig());
  root->LinkEndChild(CreateDefaultFontConfig());
  root->LinkEndChild(CreateDefaultAssetsConfig());
  root->LinkEndChild(CreateDefaultConsoleConfig());

  xmlConfig.SaveFile(inConfigFile);

  return xmlConfig;
}

void BaseGameApp::RegisterTouchRecognizers(ITouchHandler &touchHandler) {
  if (m_pTouchManager) {
    m_pTouchManager->RemoveAllRecognizers();
    m_pTouchManager->AddRecognizers(touchHandler.VRegisterRecognizers());
  }
}

std::shared_ptr<ResourceCache> BaseGameApp::GetResourceCache() const {
  return m_pResourceMgr->VGetResourceCacheFromName(ORIGINAL_RESOURCE);
}

// Graphics system implementation
bool BaseGameApp::InitializeGraphicsSystem() {
  LOG("Initializing graphics system...");

  m_graphicsAdapter.reset(new GraphicsAdapter());

#ifdef __EMSCRIPTEN__
  // WASM: No SDL renderer needed
  if (!m_graphicsAdapter->Initialize()) {
    LOG_WARNING("Failed to initialize new graphics system");
    return false;
  }
#else
  // Non-WASM: Use existing SDL renderer
  if (!m_graphicsAdapter->Initialize(m_pRenderer)) {
    LOG_WARNING(
        "Failed to initialize new graphics system, falling back to SDL2");
    return false;
  }
#endif

  LOG("Graphics system initialized successfully");
  LOG("Active renderer: " + m_graphicsAdapter->GetRendererName());

  // Log detailed status
  std::string rendererStatus = m_graphicsAdapter->GetRendererStatus();
  LOG(rendererStatus);

  // Check renderer status
  if (m_graphicsAdapter->IsUsingWebGL()) {
    LOG("📱 Using WebGL renderer - Good compatibility");
  } else {
    LOG("⚠️  No graphics system available");
  }

  return true;
}

void BaseGameApp::ShutdownGraphicsSystem() {
  if (m_graphicsAdapter) {
    LOG("Shutting down graphics system...");
    m_graphicsAdapter->Shutdown();
    m_graphicsAdapter.reset();
    LOG("Graphics system shutdown complete");
  }
}
