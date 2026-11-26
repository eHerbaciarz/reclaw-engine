#include "TitleScroll.h"
#include <map>
#include "../Util/StartupTrace.h"
#include "HumanView.h"
#include "../Video/Native/MoviePlayer.h"
#include "CustomLevels.h"
#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../GameApp/Difficulty.h"
#include "../GameApp/GameSaves.h"
#include "../GameApp/HapticFeedback.h"
#include "../GameApp/SaveBridge.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "../Events/EventMgr.h"
#include "../Events/Events.h"
#include "../Audio/Audio.h"
#include <SDL2/SDL_ttf.h>
#include "../Resource/Loaders/MidiLoader.h"
#include "../Resource/Loaders/WavLoader.h"
#include "../Resource/Loaders/XmlLoader.h"
#include "../Resource/Loaders/PcxLoader.h"
#include "../Graphics2D/Image.h"
#include "../Util/PrimeSearch.h"
#include "ScoreScreen/EndLevelScoreScreen.h"
#include "../Race/Race.h"
#include "../BossModes/BossModes.h"
#include "../Race/RaceUi.h"
#include "Lang.h"
#include "GameFont.h"
#include "../Graphics2D/Stereo3D.h"
#include "../GameApp/GameLanguages.h"
#include "Minimap.h"
#include "SecondScreen.h"
#include "Subtitles.h"
#include "CheatCode.h"
#include "../Platform/Platform.h"
#include "../Race/RaceLobby.h"
#include "../Scene/HUDSceneNode.h"
#include "TouchPad.h"
#include "GameHUD.h"
#include "../Graphics2D/CrtEffect.h"

const uint32 g_InvalidGameViewId = 0xFFFFFFFF;

HumanView::HumanView(SDL_Renderer* renderer)
    :
    m_bRendering(true),
    m_bPostponeRenderPresent(false),
    m_CursorFrameIdx(0),
    m_CursorAccumMs(0)
{
    STARTUP_TRACE("HumanView: begin");
    SDL_ShowCursor(SDL_DISABLE);
    m_pProcessMgr = new ProcessMgr();

    m_ViewId = INVALID_GAME_VIEW_ID;

    RegisterAllDelegates();
    STARTUP_TRACE("HumanView: delegates");

    if (renderer)
    {
        m_pScene.reset(new ScreenElementScene(renderer));
        m_pCamera.reset(new CameraNode(Point(0, 0), 0, 0));
        m_pHUD.reset(new ScreenElementHUD());
        STARTUP_TRACE("HumanView: scene");

        m_pScene->AddChild(INVALID_ACTOR_ID, m_pCamera);
        m_pScene->SetCamera(m_pCamera);

        //m_pConsole = unique_ptr<Console>(new Console(g_pApp->GetWindowSize().x, g_pApp->GetWindowSize().y / 2,
            //g_pApp->GetConsoleFont(), renderer, "console02.tga"));

        m_pConsole = unique_ptr<Console>(new Console(g_pApp->GetConsoleConfig(), renderer, g_pApp->GetWindow()));
        STARTUP_TRACE("HumanView: console");
    }
}

HumanView::~HumanView()
{
    RemoveAllDelegates();
    DropEffectFrame();

    while (!m_ScreenElements.empty())
    {
        m_ScreenElements.pop_front();
    }

    SAFE_DELETE(m_pProcessMgr);
}

void HumanView::RegisterConsoleCommandHandler(void(*handler)(const char*, void*), void* userdata)
{
    if (m_pConsole)
    {
        m_pConsole->SetCommandHandler(handler, (void*)m_pConsole.get());
    }
}

// "Pokonaj <boss>" / "Defeat <boss>" when a boss fight starts, as in the
// original: two lines in the game's own letters slide down from the top,
// stay a moment, then slide out to the right.
namespace BossTitle
{
    const int kInMs = 600, kHoldMs = 2200, kOutMs = 500;
    int g_Ms = -1;                              // -1: not shown
    std::string g_Lines[2];
    std::shared_ptr<Image> g_Images[2];

    bool IsShown() { return g_Ms >= 0; }

    void Start(int level)
    {
        std::string name;
        for (const auto& b : BossModes::Bosses()) if (b.level == level) name = b.name;
        if (name.empty()) return;
        g_Lines[0] = Lang::Tr("Pokonaj", "Defeat");
        g_Lines[1] = name;
        for (int i = 0; i < 2; ++i)
        {
            std::string up;
            for (char c : g_Lines[i]) up += (char)toupper((unsigned char)c);
            g_Images[i].reset();
            GameFont::Pixels px;
            if (GameFont::Covers(up) && GameFont::RenderPixels(up, GameFont::Look::Normal, -1, px))
            {
                px = GameFont::WithShadow(GameFont::Scale(px, 125), 3, 3, 200);
                g_Images[i] = GameFont::ToImage(px);
            }
        }
        g_Ms = 0;
    }

    void Update(uint32 msDiff)
    {
        if (g_Ms < 0) return;
        g_Ms += (int)msDiff;
        if (g_Ms > kInMs + kHoldMs + kOutMs) { g_Ms = -1; g_Images[0].reset(); g_Images[1].reset(); }
    }

    void Stop() { g_Ms = -1; g_Images[0].reset(); g_Images[1].reset(); }

    // viewport: the level's area in the screen's units
    void Render(SDL_Renderer* r, const SDL_Rect& viewport)
    {
        if (g_Ms < 0) return;
        const double unit = viewport.h / 480.0;     // menu letters: 640x480 units
        int w[2] = { 0, 0 }, h[2] = { 0, 0 };
        for (int i = 0; i < 2; ++i)
            if (g_Images[i]) { w[i] = (int)(g_Images[i]->GetWidth() * unit); h[i] = (int)(g_Images[i]->GetHeight() * unit); }
            else { w[i] = 0; h[i] = (int)(32 * unit); }
        const int gap = (int)(-6 * unit);
        const int blockH = h[0] + gap + h[1];
        const int targetY = (int)(viewport.h * 0.30) - blockH / 2;
        int y = targetY, dx = 0;
        if (g_Ms < kInMs)
        {
            const double f = g_Ms / (double)kInMs;
            const double e = 1.0 - (1.0 - f) * (1.0 - f);       // eases in
            y = (int)(-blockH + (targetY + blockH) * e);
        }
        else if (g_Ms > kInMs + kHoldMs)
        {
            const double f = (g_Ms - kInMs - kHoldMs) / (double)kOutMs;
            dx = (int)(f * f * (viewport.w + 50));
        }
        for (int i = 0; i < 2; ++i)
        {
            const int lineY = y + (i ? h[0] + gap : 0);
            if (g_Images[i] && g_Images[i]->GetTexture())
            {
                SDL_Rect dst = { viewport.w / 2 - w[i] / 2 + dx, lineY, w[i], h[i] };
                SDL_RenderCopy(r, g_Images[i]->GetTexture(), NULL, &dst);
            }
            else
            {
                const SDL_Color gold = { 246, 205, 86, 255 };
                RaceUi::DrawText(r, g_Lines[i], (int)(24 * unit), viewport.w / 2 + dx, lineY, 0, gold);
            }
        }
    }
}

void HumanView::VOnRender(uint32 msDiff)
{
    //PROFILE_CPU("HumanView Render");

    m_CurrentTick = SDL_GetTicks();
    if (m_CurrentTick == m_LastDraw)
    {
        return;
    }

    if (!m_bRendering)
    {
        return;
    }

    SDL_Renderer* renderer = g_pApp->GetRenderer();

    if (m_RunFullSpeed || true)
    {
        //PROFILE_CPU(".");

        // Nintendo 3DS 3D slider: the level is drawn twice, right eye first.
        const bool inLevel = m_pScene && m_pScene->VIsVisible() && !m_pCamera2 && !g_pApp->IsMenuActive();
        // (a picture kept for an effect: flat, the slider left as it is)
        const int eyes = (!m_bCapturingEffectFrame && Stereo3D::BeginFrame(inLevel)) ? 2 : 1;
        for (int pass = 0; pass < eyes; ++pass)
        {
            Stereo3D::SetEye(eyes == 2 ? (pass == 0 ? 1 : -1) : 0);
            // If we render out of bounds, render it as black color
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

#ifdef OPENCLAW_STARTUP_TRACE
            extern Uint64 g_PerfRenderTicks[12];
            const Uint64 hv0 = SDL_GetPerformanceCounter();
#endif
            SDL_RenderClear(renderer);
#ifdef OPENCLAW_STARTUP_TRACE
            g_PerfRenderTicks[0] += SDL_GetPerformanceCounter() - hv0;
            const Uint64 hv1 = SDL_GetPerformanceCounter();
            const Uint64 scene0 = g_PerfRenderTicks[1] + g_PerfRenderTicks[2] + g_PerfRenderTicks[3] + g_PerfRenderTicks[4] + g_PerfRenderTicks[5];
#endif

            // Sort screen elements
            m_ScreenElements.sort(SortBy_SharedPtr_Content<IScreenElement>());

            for (shared_ptr<IScreenElement>& screenElement : m_ScreenElements)
            {
                if (screenElement->VIsVisible())
                {
                    const bool isScene = m_pScene && screenElement.get() == static_cast<IScreenElement*>(m_pScene.get());
                    if (isScene && m_pCamera2)
                    {
                        RenderSplitScene(renderer);
                        continue;
                    }
                    // 3D: the HUD and other elements over the level stand out of the screen
                    const int pop = isScene ? 0 : Stereo3D::Offset(-0.6f);
                    // Options > Graphics > aspect classic: the level and its HUD in a
                    // 4:3 area in the middle (the camera as wide as it)
                    const bool isHud = dynamic_cast<ScreenElementHUD*>(screenElement.get()) != nullptr;
                    SDL_Rect area43 = ClassicArea();
                    const bool classic = (isScene || isHud) && area43.w > 0;
                    if (classic)
                    {
                        SDL_Rect vp = { area43.x + pop, area43.y, area43.w, area43.h };
                        SDL_RenderSetViewport(renderer, &vp);
                    }
                    else if (pop)
                    {
                        SDL_Rect vp = { pop, 0, (int)(g_pApp->GetWindowSize().x), (int)(g_pApp->GetWindowSize().y) };
                        SDL_RenderSetViewport(renderer, &vp);
                    }
                    screenElement->VOnRender(msDiff);
                    if (pop || classic) SDL_RenderSetViewport(renderer, NULL);
                    if (isScene && (Race::IsActive() || BossModes::IsActive() || Minimap::IsVisible() || BossTitle::IsShown()))
                    {
                        Point scale = g_pApp->GetScale();
                        SDL_Rect full = { 0, 0, (int)(g_pApp->GetWindowSize().x / scale.x), (int)(g_pApp->GetWindowSize().y / scale.y) };
                        if (area43.w > 0) { full.w = area43.w; full.h = area43.h; SDL_RenderSetViewport(renderer, &area43); }
                        // 3DS: the map is on the bottom screen (SecondScreen)
                        if (!SecondScreen::ShowsHud()) Minimap::Render(renderer, full, m_ActorId);
                        Race::RenderOverlay(renderer, 0, full);
                        BossModes::RenderOverlay(renderer, full);
                        BossTitle::Render(renderer, full);
                        if (area43.w > 0) SDL_RenderSetViewport(renderer, NULL);
                    }
                }
            }
            //LOG("SCREEN ELEMENTS: " + ToStr(m_ScreenElements.size()))

            g_pApp->GetGameLogic()->VRenderDiagnostics(renderer, m_pCamera);

            RenderLevelReveal(renderer);
            TitleScroll::RenderMenuUnroll(renderer);
            RenderToast(renderer);

            m_pConsole->OnRender(renderer);

            // Subtitles / the cheat code's message, over the game and the menus
            Subtitles::Render(renderer);

            // On-screen controls (touch screens), while playing
            TouchPad::Render(renderer);

            // Lazy-load cursor frames on first render (resource cache is guaranteed ready by then)
            if (m_CursorFrames.empty())
            {
                for (int i = 1; i <= 19; i++)
                {
                    char path[64];
                    snprintf(path, sizeof(path), "/states/menu/images/cursor/cursor%02d.pcx", i);
                    // PCX palette index 0 = magenta-pink (R=255, G=0, B=132) — use as color key
                    shared_ptr<Image> pFrame = PcxResourceLoader::LoadAndReturnImage(path, true, { 255, 0, 132, 0 });
                    if (pFrame)
                        m_CursorFrames.push_back(pFrame);
                }
            }

            // Draw the custom cursor only when a menu is active (main/loading menu
            // or the in-game quick menu) — IsMenuActive() is the shared source of
            // truth for that condition.
            bool showCursor = g_pApp->IsMenuActive();
    #ifdef __EMSCRIPTEN__
            // The mouse cursor only makes sense when the mouse is the active input.
            // Touch has no hovering pointer (cursor would stick at the last tap) and
            // gamepad navigates without one — so show the cursor only for mouse.
            // Self-corrects: moving the mouse brings it back on hybrid setups.
            if (!g_pApp->WasLastInputMouse()) showCursor = false;
    #endif
    #if defined(__ANDROID__)
            // A touch screen has no hovering pointer.
            showCursor = false;
    #endif
            if (showCursor && !m_CursorFrames.empty())
            {
                int mouseX, mouseY;
    #ifdef __EMSCRIPTEN__
                mouseX = g_pApp->GetPointerX();
                mouseY = g_pApp->GetPointerY();
    #else
                g_pApp->GetMouseState(&mouseX, &mouseY);
    #endif

                shared_ptr<Image> pCursor = m_CursorFrames[m_CursorFrameIdx];
                SDL_Rect destRect = { mouseX, mouseY, pCursor->GetWidth(), pCursor->GetHeight() };
                SDL_RenderCopy(renderer, pCursor->GetTexture(), NULL, &destRect);
            }

#ifdef OPENCLAW_STARTUP_TRACE
            {
                const Uint64 scene1 = g_PerfRenderTicks[1] + g_PerfRenderTicks[2] + g_PerfRenderTicks[3] + g_PerfRenderTicks[4] + g_PerfRenderTicks[5];
                g_PerfRenderTicks[6] += (SDL_GetPerformanceCounter() - hv1) - (scene1 - scene0);
            }
#endif
            if (eyes == 2 && pass == 0)
            {
                g_pApp->PresentFrame(false);
                Stereo3D::StoreRightEye(g_pApp->GetWindow());
            }
        }
        Stereo3D::SetEye(0);

        if (!m_bPostponeRenderPresent)
        {
            g_pApp->PresentFrame();
        }
    }
}

void HumanView::VOnUpdate(uint32 msDiff)
{
    m_pProcessMgr->UpdateProcesses(msDiff);

    // (game time: stops with the pause menu)
    if (g_pApp->GetGameLogic() && g_pApp->GetGameLogic()->GetGameState() == GameState_IngameRunning)
        BossTitle::Update(msDiff);
    else if (g_pApp->GetGameLogic() && g_pApp->GetGameLogic()->GetGameState() != GameState_IngamePaused)
        BossTitle::Stop();

    m_pConsole->OnUpdate(msDiff);

    for (shared_ptr<IScreenElement> &element : m_ScreenElements)
    {
        element->VOnUpdate(msDiff);
    }

    if (!m_CursorFrames.empty())
    {
        const uint32 CURSOR_FRAME_MS = 100;
        m_CursorAccumMs += msDiff;
        if (m_CursorAccumMs >= CURSOR_FRAME_MS)
        {
            m_CursorFrameIdx = (m_CursorFrameIdx + m_CursorAccumMs / CURSOR_FRAME_MS) % (int)m_CursorFrames.size();
            m_CursorAccumMs %= CURSOR_FRAME_MS;
        }
    }
}

bool HumanView::VOnEvent(SDL_Event& evt)
{
    // Secret code (Konami) typed while playing: arrows, Space (jump), Alt
    // (fire). The keys still do their normal job.
    if (evt.type == SDL_KEYDOWN && evt.key.repeat == 0 && !(m_pIngameMenu && m_pIngameMenu->VIsVisible()) &&
        g_pApp->GetGameLogic() && g_pApp->GetGameLogic()->GetGameState() == GameState_IngameRunning)
    {
        SDL_Keycode key = evt.key.keysym.sym;
        OnCheatKey(key == SDLK_UP ? CheatCode::Key::Up : key == SDLK_DOWN ? CheatCode::Key::Down :
                   key == SDLK_LEFT ? CheatCode::Key::Left : key == SDLK_RIGHT ? CheatCode::Key::Right :
                   key == SDLK_SPACE ? CheatCode::Key::Jump :
                   (key == SDLK_LALT || key == SDLK_RALT) ? CheatCode::Key::Fire : CheatCode::Key::Other);
    }
    // ...and in the main menu (the last two keys do not reach the menu)
    if (evt.type == SDL_KEYDOWN && evt.key.repeat == 0 && g_pApp->GetGameLogic() &&
        g_pApp->GetGameLogic()->GetGameState() == GameState_Menu)
    {
        SDL_Keycode key = evt.key.keysym.sym;
        if (OnMenuCheatKey(key == SDLK_UP ? CheatCode::Key::Up : key == SDLK_DOWN ? CheatCode::Key::Down :
                           key == SDLK_LEFT ? CheatCode::Key::Left : key == SDLK_RIGHT ? CheatCode::Key::Right :
                           (key == SDLK_SPACE || key == SDLK_RETURN) ? CheatCode::Key::Jump :
                           (key == SDLK_LALT || key == SDLK_RALT || key == SDLK_ESCAPE) ? CheatCode::Key::Fire : CheatCode::Key::Other))
        {
            return true;
        }
    }

    // First let console try to eat this event
    if (m_pConsole->OnEvent(evt))
    {
        //LOG("command eaten !");
        return true;
    }

    // Then screen layers in reverse order
    for (ScreenElementList::reverse_iterator i = m_ScreenElements.rbegin(); i != m_ScreenElements.rend(); ++i)
    {
        if ((*i)->VIsVisible())
        {
            if ((*i)->VOnEvent(evt))
            {
                return true;
            }
        }
    }

    

    switch (evt.type)
    {
        case SDL_KEYDOWN:
        {
            if (evt.key.repeat == 0)
            {
                bool ingameMenuVisible = m_pIngameMenu && m_pIngameMenu->VIsVisible();

                // Level map (Tab)
                if (evt.key.keysym.sym == SDLK_TAB &&
                    g_pApp->GetGameLogic()->GetGameState() == GameState_IngameRunning &&
                    !ingameMenuVisible)
                {
                    Minimap::Toggle();
                    return true;
                }

                if (SDL_GetScancodeFromKey(evt.key.keysym.sym) == SDL_SCANCODE_ESCAPE &&
                    g_pApp->GetGameLogic()->GetGameState() == GameState_IngameRunning &&
                    m_pIngameMenu &&
                    !ingameMenuVisible)
                {
                    m_pIngameMenu->VSetVisible(true);
                    return true;
                }

                // Don't feed new gameplay input to the player while the ingame menu
                // is open, otherwise the character keeps responding in the background.
                if (!ingameMenuVisible)
                {
                    return m_pKeyboardHandler->VOnKeyDown(evt.key.keysym.sym);
                }
                return false;
            }
            break;
        }
        case SDL_KEYUP:
        {
            if (evt.key.repeat == 0)
            {
                // Always forward key-up so a key held when the menu opened gets
                // cleared on release — prevents the player bolting when the menu closes.
                return m_pKeyboardHandler->VOnKeyUp(evt.key.keysym.sym);
            }
            break;
        }

        case SDL_UserTouchEvent:
        {
            if (m_pTouchHandler) {
                const Touch_Event &touchEvent = *((Touch_Event *)evt.user.data1);
                return m_pTouchHandler->VOnTouch(touchEvent);
            }
            break;
        }

        case SDL_MOUSEMOTION:
        {
            return m_pPointerHandler->VOnPointerMove(evt.motion);
        }
        case SDL_MOUSEBUTTONDOWN:
        {
            return m_pPointerHandler->VOnPointerButtonDown(evt.button);
        }
        case SDL_MOUSEBUTTONUP:
        {
            return m_pPointerHandler->VOnPointerButtonUp(evt.button);
        }
        default:
            return false;
    }

    return false;
}

bool HumanView::VOnGamepadEvent(const AppEvent& evt)
{
    switch (evt.type) {
        case AppEventType::GamepadButtonDown:
        {
            GameState gameState = g_pApp->GetGameLogic()->GetGameState();
            // Any active menu — quick menu, main menu, OR the game-over screen.
            bool menuActive = g_pApp->IsMenuActive();


            // Handle Start button for pause menu (like ESC key)
            if (evt.gamepadButton.button == GamepadButton::Start &&
                gameState == GameState_IngameRunning &&
                m_pIngameMenu &&
                !m_pIngameMenu->VIsVisible())
            {
                m_pIngameMenu->VSetVisible(true);
                return true;
            }

            // Secret code (Konami) typed while playing: D-pad, then the
            // bottom (jump: B on Nintendo, Cross on PlayStation) and right
            // (fire: A / Circle) buttons. The buttons still do their job.
            if (gameState == GameState_IngameRunning && !menuActive)
            {
                GamepadButton b = evt.gamepadButton.button;
                OnCheatKey(b == GamepadButton::DPadUp ? CheatCode::Key::Up :
                           b == GamepadButton::DPadDown ? CheatCode::Key::Down :
                           b == GamepadButton::DPadLeft ? CheatCode::Key::Left :
                           b == GamepadButton::DPadRight ? CheatCode::Key::Right :
                           b == GamepadButton::A ? CheatCode::Key::Jump :
                           b == GamepadButton::B ? CheatCode::Key::Fire : CheatCode::Key::Other);
            }

            // ...and in the main menu (Cross and Circle at the end of it do
            // not choose / go back there)
            if (gameState == GameState_Menu)
            {
                GamepadButton b = evt.gamepadButton.button;
                if (OnMenuCheatKey(b == GamepadButton::DPadUp ? CheatCode::Key::Up :
                                   b == GamepadButton::DPadDown ? CheatCode::Key::Down :
                                   b == GamepadButton::DPadLeft ? CheatCode::Key::Left :
                                   b == GamepadButton::DPadRight ? CheatCode::Key::Right :
                                   b == GamepadButton::A ? CheatCode::Key::Jump :
                                   b == GamepadButton::B ? CheatCode::Key::Fire : CheatCode::Key::Other))
                {
                    return true;
                }
            }

            // Score screen after a level: Cross / Circle / Start (A / B / +)
            // skip its animations, like Enter on a keyboard
            if (IsScoreScreenVisible())
            {
                GamepadButton b = evt.gamepadButton.button;
                if (b == GamepadButton::A || b == GamepadButton::B || b == GamepadButton::Start)
                {
                    SDL_Event key = {0};
                    key.type = SDL_KEYDOWN;
                    key.key.repeat = 0;
                    key.key.keysym.sym = SDLK_RETURN;
                    key.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    for (shared_ptr<IScreenElement>& e : m_ScreenElements)
                        if (dynamic_cast<ScreenElementScoreScreen*>(e.get()) && e->VOnEvent(key)) return true;
                }
            }

            // Handle gamepad input for menus (main menu, quick menu, or game-over)
            bool inMenu = (gameState == GameState_Menu || menuActive);

            if (inMenu)
            {
                // Convert gamepad buttons to SDL keyboard events for menu navigation
                SDL_Event sdlEvt = {0};
                sdlEvt.type = SDL_KEYDOWN;
                sdlEvt.key.repeat = 0;

                bool handled = false;
                switch (evt.gamepadButton.button)
                {
                    case GamepadButton::A:
                        sdlEvt.key.keysym.sym = SDLK_RETURN;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_RETURN;
                        handled = true;
                        break;
                    case GamepadButton::B:
                        sdlEvt.key.keysym.sym = SDLK_ESCAPE;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                        handled = true;
                        break;
                    case GamepadButton::DPadUp:
                        sdlEvt.key.keysym.sym = SDLK_UP;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_UP;
                        handled = true;
                        break;
                    case GamepadButton::DPadDown:
                        sdlEvt.key.keysym.sym = SDLK_DOWN;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_DOWN;
                        handled = true;
                        break;
                    case GamepadButton::DPadLeft:
                        sdlEvt.key.keysym.sym = SDLK_LEFT;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_LEFT;
                        handled = true;
                        break;
                    case GamepadButton::DPadRight:
                        sdlEvt.key.keysym.sym = SDLK_RIGHT;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                        handled = true;
                        break;
                    case GamepadButton::Start:
                        sdlEvt.key.keysym.sym = SDLK_RETURN;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_RETURN;
                        handled = true;
                        break;
                    // Block these buttons in menus (no menu function, prevent game actions)
                    case GamepadButton::X:
                    case GamepadButton::Y:
                    case GamepadButton::LeftBumper:
                    case GamepadButton::RightBumper:
                    case GamepadButton::LeftTrigger:
                    case GamepadButton::RightTrigger:
                        return true;  // Consume the event, do nothing
                    default:
                        break;
                }

                if (handled)
                {
                    // Send to visible menu
                    if (m_pIngameMenu && m_pIngameMenu->VIsVisible())
                    {
                        return m_pIngameMenu->VOnEvent(sdlEvt);
                    }
                    // For main menu, let it fall through to screen elements
                    for (auto& pScreenElement : m_ScreenElements)
                    {
                        if (pScreenElement->VIsVisible() && pScreenElement->VOnEvent(sdlEvt))
                        {
                            return true;
                        }
                    }
                    return true;
                }
            }

            // Level map (Select / Minus)
            if (!menuActive && evt.gamepadButton.button == GamepadButton::Back &&
                gameState == GameState_IngameRunning)
            {
                Minimap::Toggle();
                return true;
            }

            // Forward to gamepad handler for in-game controls (only if no menu active)
            if (!menuActive && m_pGamepadHandler2 && evt.gamepadButton.gamepadIndex == 1) {
                return m_pGamepadHandler2->VOnGamepadButtonDown(
                    evt.gamepadButton.button, evt.gamepadButton.value);
            }
            if (!menuActive && m_pGamepadHandler) {
                return m_pGamepadHandler->VOnGamepadButtonDown(
                    evt.gamepadButton.button, evt.gamepadButton.value);
            }
            return false;
        }

        case AppEventType::GamepadButtonUp:
        {
            // Always forward button-up (even while the menu is open) so a d-pad
            // direction released during the pause gets cleared — otherwise the
            // held-direction state sticks and the player moves on unpause. Mirrors
            // the keyboard key-up handling.
            if (m_pGamepadHandler2 && evt.gamepadButton.gamepadIndex == 1) {
                return m_pGamepadHandler2->VOnGamepadButtonUp(evt.gamepadButton.button);
            }
            if (m_pGamepadHandler) {
                return m_pGamepadHandler->VOnGamepadButtonUp(evt.gamepadButton.button);
            }
            return false;
        }

        case AppEventType::GamepadAxis:
        {
            // Handle analog stick for menu navigation
            GameState gameState = g_pApp->GetGameLogic()->GetGameState();
            bool menuActive = g_pApp->IsMenuActive();
            bool inMenu = (gameState == GameState_Menu || menuActive);

            if (inMenu && evt.gamepadAxis.axis == GamepadAxis::LeftStickY)
            {
                static float lastAxisValue = 0.0f;
                float value = evt.gamepadAxis.value;

                // Trigger menu navigation when crossing threshold
                const float threshold = 0.5f;
                bool wasOver = fabs(lastAxisValue) >= threshold;
                bool nowOver = fabs(value) >= threshold;

                if (nowOver && !wasOver)
                {
                    SDL_Event sdlEvt = {0};
                    sdlEvt.type = SDL_KEYDOWN;
                    sdlEvt.key.repeat = 0;

                    if (value < -threshold) {
                        sdlEvt.key.keysym.sym = SDLK_UP;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_UP;
                    } else {
                        sdlEvt.key.keysym.sym = SDLK_DOWN;
                        sdlEvt.key.keysym.scancode = SDL_SCANCODE_DOWN;
                    }

                    if (m_pIngameMenu && m_pIngameMenu->VIsVisible())
                    {
                        m_pIngameMenu->VOnEvent(sdlEvt);
                    }
                    else
                    {
                        for (auto& pScreenElement : m_ScreenElements)
                        {
                            if (pScreenElement->VIsVisible() && pScreenElement->VOnEvent(sdlEvt))
                            {
                                break;
                            }
                        }
                    }
                }
                lastAxisValue = value;
                return true;
            }

            // Forward to gamepad handler for in-game controls (only if no menu active)
            if (!menuActive && m_pGamepadHandler2 && evt.gamepadAxis.gamepadIndex == 1) {
                return m_pGamepadHandler2->VOnGamepadAxis(
                    evt.gamepadAxis.axis, evt.gamepadAxis.value);
            }
            if (!menuActive && m_pGamepadHandler) {
                return m_pGamepadHandler->VOnGamepadAxis(
                    evt.gamepadAxis.axis, evt.gamepadAxis.value);
            }
            return false;
        }

        default:
            return false;
    }
}

void HumanView::VOnLostDevice()
{

}

bool HumanView::EnterMenu(TiXmlElement* pMenuData)
{
    if (m_pMenu == nullptr)
    {
        m_pMenu.reset(new ScreenElementMenu(g_pApp->GetRenderer()));
        if (!m_pMenu->Initialize(pMenuData))
        {
            LOG_ERROR("Could not initialize ScreenElementMenu");
            return false;
        }
    }
    else
    {
        // The menu object is created once and reused. Initialize (which starts the
        // background music) only runs on first creation, so restart the menu music
        // explicitly when returning to the menu from gameplay.
        m_pMenu->PlayBackgroundMusic();
        // (items shown after the cheat code entered in a level)
        m_pMenu->RefreshActivePageVisibility();
    }

    m_ScreenElements.clear();
    VPushElement(m_pMenu);

    return true;
}

void HumanView::LoadScoreScreen(TiXmlElement* pScoreScreenRootElem)
{
    STARTUP_TRACE("score screen: dropping the level's picture");
    m_ScreenElements.clear();
    m_pScene.reset();
    m_pHUD.reset();
    STARTUP_TRACE("score screen: level's picture dropped");

    m_pCamera->SetParent(nullptr);
    Point defaultCameraPos(0, 0);
    m_pCamera->VSetPosition(defaultCameraPos);
    m_pCamera->SetTarget(nullptr);

    g_pApp->GetAudio()->StopAllSounds();
    STARTUP_TRACE("score screen: sounds stopped");

    LOG("Initializing score screen ...");
    shared_ptr<ScreenElementScoreScreen> pScoreScreen(new ScreenElementScoreScreen(g_pApp->GetRenderer()));
    if (pScoreScreen->Initialize(pScoreScreenRootElem))
    {
        LOG("Score screen initialized OK");
        pScoreScreen->SetCamera(m_pCamera);
        VPushElement(pScoreScreen);
        assert(m_ScreenElements.size() == 1);
    }
    else
    {
        LOG_ERROR("Failed to inizialize Score Screen !");
    }
}

bool HumanView::IsScoreScreenVisible()
{
    if (m_ScreenElements.empty()) return false;
    
    // Check if the only screen element is a score screen
    // ScreenElementScoreScreen stores score data and is the sole element during score screen display
    shared_ptr<IScreenElement> pElement = m_ScreenElements.front();
    
    // ScreenElementScoreScreen is a subclass of IScreenElement and Scene
    // Cast attempt: if it's valid, it's the score screen
    shared_ptr<ScreenElementScoreScreen> pScoreScreen = std::dynamic_pointer_cast<ScreenElementScoreScreen>(pElement);
    return (pScoreScreen != nullptr);
}

bool HumanView::LoadGame(TiXmlElement* pLevelXmlElem, LevelData* pLevelData)
{
    m_pScene->SortSceneNodesByZCoord();

    // Stop any currently playing music (e.g., menu music)
    g_pApp->GetAudio()->StopMusic();

    // Start playing background music
    m_CurrentLevelMusic = "/LEVEL" + ToStr(pLevelData->GetLevelNumber()) +
        "/MUSIC/PLAY.XMI";

    SoundInfo soundInfo(m_CurrentLevelMusic);
    soundInfo.isMusic = true;
    soundInfo.loops = -1;
    soundInfo.soundVolume = g_pApp->GetGameConfig()->musicVolume;

    IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Request_Play_Sound(soundInfo)));

    return VLoadGameDelegate(pLevelXmlElem, pLevelData);
}

void HumanView::VPushElement(shared_ptr<IScreenElement> element)
{
    m_ScreenElements.push_front(element);
}

void HumanView::VRemoveElement(shared_ptr<IScreenElement> element)
{
    m_ScreenElements.remove(element);
}

void HumanView::VSetCameraOffset(int32 offsetX, int32 offsetY)
{
    if (m_pCamera)
    {
        m_pCamera->SetCameraOffset(offsetX, offsetY);
    }
    else
    {
        LOG_WARNING("Trying to set offset to nonexistant camera");
    }
}

void HumanView::RegisterAllDelegates()
{
    IEventMgr* pEventMgr = IEventMgr::Get();
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::NewHUDElementDelegate), EventData_New_HUD_Element::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::ScoreUpdatedDelegate), EventData_Updated_Score::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::LivesUpdatedDelegate), EventData_Updated_Lives::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::HealthUpdatedDelegate), EventData_Updated_Health::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::AmmoUpdatedDelegate), EventData_Updated_Ammo::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::AmmoTypeUpdatedDelegate), EventData_Updated_Ammo_Type::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::PowerupUpdatedTimeDelegate), EventData_Updated_Powerup_Time::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::PowerupUpdatedStatusDelegate), EventData_Updated_Powerup_Status::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::RequestPlaySoundDelegate), EventData_Request_Play_Sound::sk_EventType);
    pEventMgr->VAddListener(MakeDelegate(this, &HumanView::RequestResetLevelDelegate), EventData_Request_Reset_Level::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::LoadGameDelegate), EventData_Menu_LoadGame::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetVolumeDelegate), EventData_Set_Volume::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SoundEnabledChangedDelegate), EventData_Sound_Enabled_Changed::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ClawDiedDelegate), EventData_Claw_Died::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::TeleportActorDelegate), EventData_Teleport_Actor::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::EnterMenuDelegate), EventData_Enter_Menu::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::FinishedLevelDelegate), EventData_Finished_Level::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ActorEnteredBossAreaDelegate), EventData_Entered_Boss_Area::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::BossFightStartedDelegate), EventData_Boss_Fight_Started::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::IngameMenuEndGameDelegate), EventData_IngameMenu_End_Game::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ResetSaveProgressDelegate), EventData_Reset_Save_Progress::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ExportSaveDataDelegate), EventData_Export_Save_Data::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ImportSaveDataDelegate), EventData_Import_Save_Data::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::StartNewGameDelegate), EventData_Start_New_Game::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::PlayMoviesDelegate), EventData_Menu_Play_Movies::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::LoadCustomLevelDelegate), EventData_Menu_Load_Custom_Level::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::StartRaceDelegate), EventData_Menu_Start_Race::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::BossModesDelegate), EventData_Menu_Boss_Modes::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::OpenLanguageDelegate), EventData_Menu_Open_Language::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::OpenCheatsDelegate), EventData_Menu_Open_Cheats::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetSubtitlesDelegate), EventData_Menu_Set_Subtitles::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetViewZoomDelegate), EventData_Menu_Set_View_Zoom::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetSmoothDelegate), EventData_Menu_Set_Smooth::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetAspectDelegate), EventData_Menu_Set_Aspect::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetFilterDelegate), EventData_Menu_Set_Filter::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetCrtDelegate), EventData_Menu_Set_Crt::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetFpsCounterDelegate), EventData_Menu_Set_Fps_Counter::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetMenuFontDelegate), EventData_Menu_Set_Menu_Font::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::SetDifficultyDelegate), EventData_Menu_Set_Difficulty::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::WorldFinishedLoadingDelegate), EventData_World_Finished_Loading::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ResetSaveProgressFromManageSavesDelegate), EventData_Reset_Save_Progress_From_Manage_Saves::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ToggleFullscreenDelegate), EventData_Menu_ToggleFullscreen::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(
        this, &HumanView::ToggleAspectRatioDelegate), EventData_Menu_ToggleAspectRatio::sk_EventType);

}

void HumanView::RemoveAllDelegates()
{
    IEventMgr* pEventMgr = IEventMgr::Get();
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::NewHUDElementDelegate), EventData_New_HUD_Element::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::ScoreUpdatedDelegate), EventData_Updated_Score::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::LivesUpdatedDelegate), EventData_Updated_Lives::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::HealthUpdatedDelegate), EventData_Updated_Health::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::AmmoUpdatedDelegate), EventData_Updated_Ammo::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::AmmoTypeUpdatedDelegate), EventData_Updated_Ammo_Type::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::PowerupUpdatedTimeDelegate), EventData_Updated_Powerup_Time::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::PowerupUpdatedStatusDelegate), EventData_Updated_Powerup_Status::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::RequestPlaySoundDelegate), EventData_Request_Play_Sound::sk_EventType);
    pEventMgr->VRemoveListener(MakeDelegate(this, &HumanView::RequestResetLevelDelegate), EventData_Request_Reset_Level::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::LoadGameDelegate), EventData_Menu_LoadGame::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetVolumeDelegate), EventData_Set_Volume::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SoundEnabledChangedDelegate), EventData_Sound_Enabled_Changed::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ClawDiedDelegate), EventData_Claw_Died::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::TeleportActorDelegate), EventData_Teleport_Actor::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::EnterMenuDelegate), EventData_Enter_Menu::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::FinishedLevelDelegate), EventData_Finished_Level::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ActorEnteredBossAreaDelegate), EventData_Entered_Boss_Area::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::BossFightStartedDelegate), EventData_Boss_Fight_Started::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ResetSaveProgressDelegate), EventData_Reset_Save_Progress::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ExportSaveDataDelegate), EventData_Export_Save_Data::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ImportSaveDataDelegate), EventData_Import_Save_Data::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::StartNewGameDelegate), EventData_Start_New_Game::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::PlayMoviesDelegate), EventData_Menu_Play_Movies::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::LoadCustomLevelDelegate), EventData_Menu_Load_Custom_Level::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::StartRaceDelegate), EventData_Menu_Start_Race::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::BossModesDelegate), EventData_Menu_Boss_Modes::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::OpenLanguageDelegate), EventData_Menu_Open_Language::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::OpenCheatsDelegate), EventData_Menu_Open_Cheats::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetSubtitlesDelegate), EventData_Menu_Set_Subtitles::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetViewZoomDelegate), EventData_Menu_Set_View_Zoom::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetSmoothDelegate), EventData_Menu_Set_Smooth::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetAspectDelegate), EventData_Menu_Set_Aspect::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetFilterDelegate), EventData_Menu_Set_Filter::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetCrtDelegate), EventData_Menu_Set_Crt::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetFpsCounterDelegate), EventData_Menu_Set_Fps_Counter::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetMenuFontDelegate), EventData_Menu_Set_Menu_Font::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::SetDifficultyDelegate), EventData_Menu_Set_Difficulty::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::WorldFinishedLoadingDelegate), EventData_World_Finished_Loading::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ResetSaveProgressFromManageSavesDelegate), EventData_Reset_Save_Progress_From_Manage_Saves::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ToggleFullscreenDelegate), EventData_Menu_ToggleFullscreen::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(
        this, &HumanView::ToggleAspectRatioDelegate), EventData_Menu_ToggleAspectRatio::sk_EventType);
}

//=====================================================================================================================
// Delegates
//=====================================================================================================================

void HumanView::NewHUDElementDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_New_HUD_Element> pCastEventData = static_pointer_cast<EventData_New_HUD_Element>(pEventData);
    if (m_pHUD)
    {
        m_pHUD->AddHUDElement(pCastEventData->GetHUDKey(), pCastEventData->GetHUDElement());
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

void HumanView::ScoreUpdatedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Updated_Score> pCastEventData = static_pointer_cast<EventData_Updated_Score>(pEventData);
    if (m_pHUD)
    {
        m_pHUD->UpdateScore(pCastEventData->GetNewScore());
        if (!pCastEventData->IsInitialScore())
        {
            if ((pCastEventData->GetOldScore() / 1000000) != (pCastEventData->GetNewScore() / 1000000))
            {
                shared_ptr<EventData_New_Life> pEvent(new EventData_New_Life(pCastEventData->GetActorId(), 1));
                IEventMgr::Get()->VQueueEvent(pEvent);

                // Play new life sound
                SoundInfo soundInfo(SOUND_GAME_EXTRA_LIFE);
                IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
                    new EventData_Request_Play_Sound(soundInfo)));
            }
        }
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

void HumanView::LivesUpdatedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Updated_Lives> pCastEventData = static_pointer_cast<EventData_Updated_Lives>(pEventData);
    if (m_pHUD)
    {
        m_pHUD->UpdateLives(pCastEventData->GetNewLivesCount());
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

void HumanView::HealthUpdatedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Updated_Health> pCastEventData = static_pointer_cast<EventData_Updated_Health>(pEventData);
    if (m_pHUD)
    {
        m_pHUD->UpdateHealth(max(0, (int)pCastEventData->GetNewHealth()));

        // Haptic feedback on damage (health decreased, not initial health set)
        if (!pCastEventData->IsInitialHealth() &&
            pCastEventData->GetNewHealth() < pCastEventData->GetOldHealth())
        {
            HapticFeedback::Trigger(HapticPreset::Damage);
        }
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

void HumanView::AmmoUpdatedDelegate(IEventDataPtr pEventData)
{
    if (m_pHUD)
    {
        shared_ptr<EventData_Updated_Ammo> pCastEventData = static_pointer_cast<EventData_Updated_Ammo>(pEventData);

        // Validate ammo type first
        if (pCastEventData->GetAmmoType() < AmmoType_Pistol || pCastEventData->GetAmmoType() >= AmmoType_Max)
        {
            LOG_ERROR("Unknown ammo type: " + ToStr(pCastEventData->GetAmmoType()));
            return;
        }

        // TODO: This should be more generic
        if ((pCastEventData->GetAmmoType() == AmmoType_Pistol && m_pHUD->IsElementVisible("pistol")) ||
            (pCastEventData->GetAmmoType() == AmmoType_Magic && m_pHUD->IsElementVisible("magic")) ||
            (pCastEventData->GetAmmoType() == AmmoType_Dynamite && m_pHUD->IsElementVisible("dynamite")))
        {
            m_pHUD->UpdateAmmo(pCastEventData->GetAmmoCount());
        }
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

void HumanView::AmmoTypeUpdatedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Updated_Ammo_Type> pCastEventData = static_pointer_cast<EventData_Updated_Ammo_Type>(pEventData);
    if (pCastEventData->GetAmmoType() < AmmoType_Pistol || pCastEventData->GetAmmoType() >= AmmoType_Max)
    {
        LOG_ERROR("Unknown ammo type: " + ToStr(pCastEventData->GetAmmoType()));
        return;
    }

    if (m_pHUD)
    {
        // TODO: Redo this to be more general
        m_pHUD->SetElementVisible("pistol", false);
        m_pHUD->SetElementVisible("magic", false);
        m_pHUD->SetElementVisible("dynamite", false);

        if (pCastEventData->GetAmmoType() == AmmoType_Pistol) { m_pHUD->SetElementVisible("pistol", true); }
        else if (pCastEventData->GetAmmoType() == AmmoType_Magic) { m_pHUD->SetElementVisible("magic", true); }
        else if (pCastEventData->GetAmmoType() == AmmoType_Dynamite) { m_pHUD->SetElementVisible("dynamite", true); }

        // Play weapon select sound
        SoundInfo selectSound("/GAME/SOUNDS/SELECT.WAV");
        selectSound.soundVolume = 100;
        shared_ptr<EventData_Request_Play_Sound> pSoundEvent(new EventData_Request_Play_Sound(selectSound));
        IEventMgr::Get()->VTriggerEvent(pSoundEvent);
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

void HumanView::PowerupUpdatedTimeDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Updated_Powerup_Time> pCastEventData = static_pointer_cast<EventData_Updated_Powerup_Time>(pEventData);
    if (m_pHUD)
    {
        m_pHUD->UpdateStopwatchTime(pCastEventData->GetSecondsRemaining());
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}


void HumanView::PowerupUpdatedStatusDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Updated_Powerup_Status> pCastEventData = static_pointer_cast<EventData_Updated_Powerup_Status>(pEventData);
    if (m_pHUD)
    {
        bool hadPowerup = m_pHUD->IsElementVisible("stopwatch");

        if (!m_pHUD->SetElementVisible("stopwatch", !pCastEventData->IsPowerupFinished()))
        {
            LOG_ERROR("Could not set visibility to HUD element \"stopwatch\"");
        }

        if (!hadPowerup && m_pHUD->IsElementVisible("stopwatch"))
        {
            // Acquired powerup
            SoundInfo soundInfo("/GAME/MUSIC/POWERUP.XMI");
            soundInfo.isMusic = true;
            soundInfo.loops = -1;
            IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
                new EventData_Request_Play_Sound(soundInfo)));

            if (pCastEventData->GetPowerupType() == PowerupType_Invisibility)
            {

            }
        }
        else if (hadPowerup && !m_pHUD->IsElementVisible("stopwatch"))
        {
            // Lost powerup
            assert(!m_CurrentLevelMusic.empty());
            SoundInfo soundInfo(m_CurrentLevelMusic);
            soundInfo.isMusic = true;
            soundInfo.loops = -1;
            IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
                new EventData_Request_Play_Sound(soundInfo)));
        }
    }
    else
    {
        LOG_ERROR("HUD is unitialized");
    }
}

#include <cmath>
// TODO: Handle somehow volume of specific track
// Mix_VolumeChunk for sound
// Music has only 1 channel as far as I know so setting volume for music globally should be fine
void HumanView::RequestPlaySoundDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Request_Play_Sound> pCastEventData = static_pointer_cast<EventData_Request_Play_Sound>(pEventData);
    if (pCastEventData)
    {
        const SoundInfo* pSoundInfo = pCastEventData->GetSoundInfo();

        if ((pSoundInfo->soundToPlay.empty()) || (pSoundInfo->soundToPlay == "/GAME/SOUNDS/NULL.WAV"))
        {
            return;
        }

        if (!pSoundInfo->isMusic)
        {
            Subtitles::OnSoundPlayed(pSoundInfo->soundToPlay);
#ifdef OPENCLAW_STARTUP_TRACE
            extern std::map<std::string, int> g_PerfSoundPlays;
            g_PerfSoundPlays[pSoundInfo->soundToPlay]++;
#endif
        }

        if (pSoundInfo->isMusic) // Background music - instrumental
        {
#ifdef __EMSCRIPTEN__
            // Level/boss music are .XMI (XMIDI) files the browser can't decode. We
            // convert them to standard MIDI in-engine (libwap's WAP_XmiToMidiFromData,
            // same path desktop uses) and hand the raw MIDI bytes to the JS soundfont
            // synth (spessasynth). This means the repo ships NO game music — the XMI
            // comes from the user's own CLAW.REZ upload and is converted at runtime.
            // WAV music (MENUBED.WAV) falls through to the AudioWorklet below.
            {
                const std::string& path = pSoundInfo->soundToPlay;
                bool isXmi = path.size() >= 4 &&
                    (path.compare(path.size() - 4, 4, ".XMI") == 0 ||
                     path.compare(path.size() - 4, 4, ".xmi") == 0);
                if (isXmi)
                {
                    shared_ptr<MidiFile> pMidiFile = MidiResourceLoader::LoadAndReturnMidiFile(path.c_str());
                    if (pMidiFile && pMidiFile->data && pMidiFile->size > 0)
                    {
                        // Copy the converted MIDI bytes out of the WASM heap here (HEAPU8
                        // is in scope inside EM_ASM) and hand a Uint8Array to the player.
                        EM_ASM({
                            if (window.playLevelMidiBuffer) {
                                var bytes = HEAPU8.slice($0, $0 + $1);
                                window.playLevelMidiBuffer(bytes);
                            }
                        }, pMidiFile->data, (int)pMidiFile->size);
                    }
                    else
                    {
                        LOG_ERROR("Failed to convert XMI to MIDI: " + path);
                    }
                    return;
                }
            }
            // On WASM, WAV music (like MENUBED.WAV) is handled by the AudioWorklet;
            // fall through to the WAV path which routes it to window.musicSource.
#else
            // Native: XMI level/boss/powerup music is converted to MIDI by libwap
            // and synthesized by the audio system; anything else (the menu's
            // MENUBED.WAV) is a regular sample played on the music channel.
            const std::string& path = pSoundInfo->soundToPlay;
            bool isXmi = path.size() >= 4 &&
                (path.compare(path.size() - 4, 4, ".XMI") == 0 ||
                 path.compare(path.size() - 4, 4, ".xmi") == 0);
            if (isXmi)
            {
                shared_ptr<MidiFile> pMidiFile = MidiResourceLoader::LoadAndReturnMidiFile(path.c_str());
                if (pMidiFile && pMidiFile->data && pMidiFile->size > 0)
                {
                    g_pApp->GetAudio()->PlayMusic(pMidiFile->data, pMidiFile->size, pSoundInfo->loops != 0);
                }
                else
                {
                    LOG_ERROR("Failed to convert XMI to MIDI: " + path);
                }
            }
            else
            {
                shared_ptr<Mix_Chunk> pChunk = WavResourceLoader::LoadAndReturnSound(path.c_str());
                if (pChunk)
                {
                    g_pApp->GetAudio()->PlayMusicChunk(pChunk, pSoundInfo->loops != 0);
                }
                else
                {
                    LOG_ERROR("Failed to load music: " + path);
                }
            }
            return;
#endif
        }

        // Handle WAV files (including music files on WASM like MENUBED.WAV)
        {
            SoundProperties soundProperties;
            soundProperties.volume = pSoundInfo->soundVolume;
            soundProperties.loops = pSoundInfo->loops;

            Point soundSourcePos = pSoundInfo->soundSourcePosition;
            if (pSoundInfo->setPositionEffect)
            {
                assert(!soundSourcePos.IsZeroXY());
            }

            if (pSoundInfo->setDistanceEffect)
            {
                /*assert(!soundSourcePos.IsZeroXY());
                Point soundDistanceDelta = m_pCamera->GetCenterPosition() - soundSourcePos;
                float length = soundDistanceDelta.Length();
                float distanceRatio = length / (m_pCamera->GetWidth() / 2);
                int sdlDistance = min(distanceRatio * 255, (float)255);
                LOG("SDL DISTANCE: " + ToStr(sdlDistance));*/
            }

            bool play = true;
            if (!soundSourcePos.IsZeroXY())
            {
                // Split screen: heard from whichever player's view is closer.
                shared_ptr<CameraNode> pSoundCamera = m_pCamera;
                if (m_pCamera2 && (m_pCamera2->GetCenterPosition() - soundSourcePos).Length() <
                                  (m_pCamera->GetCenterPosition() - soundSourcePos).Length())
                {
                    pSoundCamera = m_pCamera2;
                }
                const float paddingPx = 150.0f;
                const float paddingRatio = paddingPx / (float)pSoundCamera->GetWidth();
                if (pSoundCamera->IntersectsWithPoint(soundSourcePos, 1.0f + paddingRatio))
                {
                    if (pSoundInfo->setDistanceEffect)
                    {
                        Point soundDistanceDelta = pSoundCamera->GetCenterPosition() - soundSourcePos;
                        double length = soundDistanceDelta.Length();

                        float distanceRatio = length / ((pSoundCamera->GetWidth() / 2) * (1.0f + paddingRatio));
                        //float distanceRatio = length / pSoundInfo->maxHearDistance;
                        int sdlDistance = min(distanceRatio * 150, (float)150);
                        /*LOG("SDL DISTANCE: " + ToStr(sdlDistance));
                        LOG("Length: " + ToStr(length));*/
                        soundProperties.distance = sdlDistance;

                        if (pSoundInfo->setPositionEffect)
                        {
                            double dot = soundDistanceDelta.y;
                            double det = soundDistanceDelta.x;
                            double angle = std::atan2(det, dot);
                            angle *= 180 / M_PI;
                            angle -= 180;

                            if (angle < 0) angle = fabs(angle) + 180;

                            soundProperties.angle = angle;
                        }
                    }

                    /*LOG("CenterPosition: " + pSoundCamera->GetCenterPosition().ToString());
                    LOG("SoundSourcePos: " + soundSourcePos.ToString());*/
                } else {
                    play = false;
                }
            }

            if (play)
            {
#ifdef __EMSCRIPTEN__
                // For WASM, use the new method that accepts the original path
                shared_ptr<Mix_Chunk> pSound = WavResourceLoader::LoadAndReturnSound(pSoundInfo->soundToPlay.c_str());
                if (pSound && pSound->abuf) {
                    float volume = (static_cast<float>(soundProperties.volume) / 100.0f);
                    g_pApp->GetAudio()->GetAudioSystem()->PlaySoundWithPath(pSoundInfo->soundToPlay, (const char*)pSound->abuf, pSound->alen, volume, soundProperties.loops);
                }
#else
                // For native builds, use the original method
                shared_ptr<Mix_Chunk> pSound = WavResourceLoader::LoadAndReturnSound(pSoundInfo->soundToPlay.c_str());
                if (pSound) {
                    g_pApp->GetAudio()->PlaySound(pSound.get(), soundProperties);
                }
#endif
            }
        }
    }
}

void HumanView::RequestResetLevelDelegate(IEventDataPtr pEventData)
{
    // This event actually has nothing but the information that someone requested level reset
    shared_ptr<EventData_Request_Reset_Level> pCastEventData = 
        static_pointer_cast<EventData_Request_Reset_Level>(pEventData);

    // Reset Graphical representation of level
    m_ScreenElements.clear();

    m_pScene.reset(new ScreenElementScene(g_pApp->GetRenderer()));
    //m_pCamera.reset(new CameraNode(Point(0, 0), 0, 0));
    m_pHUD.reset(new ScreenElementHUD());
    m_pScene->AddChild(INVALID_ACTOR_ID, m_pCamera);
    m_pScene->SetCamera(m_pCamera);
    //m_pCamera->SetSize(g_pApp->GetWindowSize().x, g_pApp->GetWindowSize().y);

    g_pApp->GetGameLogic()->VResetLevel();
}

void HumanView::LoadGameDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_LoadGame> pCastEventData =
        static_pointer_cast<EventData_Menu_LoadGame>(pEventData);
    
    if (pCastEventData)
    {
        bool isNewGame = pCastEventData->GetIsNewGame();
        int levelNumber = pCastEventData->GetLevelNumber();
        int checkpointNumber = pCastEventData->GetCheckpointNumber();

        // Reset Graphical representation of level
        m_ScreenElements.clear();
        m_pMenu.reset();
        m_pScene.reset(new ScreenElementScene(g_pApp->GetRenderer()));
        m_pHUD.reset(new ScreenElementHUD());
        m_pScene->AddChild(INVALID_ACTOR_ID, m_pCamera);
        m_pScene->SetCamera(m_pCamera);

        // Go to menu after finishing last implemented level
        if (levelNumber > g_pApp->GetDebugOptions()->lastImplementedLevel)
        {
            IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Enter_Menu()));
        }
        else
        {
            shared_ptr<LevelData> pLevelData(new LevelData(levelNumber, isNewGame, checkpointNumber));
            g_pApp->GetGameLogic()->SetLevelData(pLevelData);

            g_pApp->GetGameLogic()->VChangeState(GameState_LoadingLevel);
        }
    }
}

void HumanView::EnterMenuDelegate(IEventDataPtr pEventData)
{
    if (g_pApp->GetGameLogic()->GetGameState() == GameState_Menu)
    {
        LOG_WARNING("Already in menu state. Skipping.");
        return;
    }

    Race::End();

    m_ScreenElements.clear();
    m_pScene.reset(new ScreenElementScene(g_pApp->GetRenderer()));
    m_pHUD.reset();
    m_pIngameMenu.reset();
    m_pGameOverMenu.reset();
    m_pCamera->SetParent(nullptr);

    g_pApp->GetAudio()->StopAllSounds();
    //g_pApp->GetGameLogic()->UnloadLevel();

    g_pApp->GetGameLogic()->VChangeState(GameState_LoadingMenu);
}

void HumanView::FinishedLevelDelegate(IEventDataPtr pEventData)
{
    
}

void HumanView::BossFightStartedDelegate(IEventDataPtr pEventData)
{
    if (!g_pApp->GetGameLogic() || !g_pApp->GetGameLogic()->GetCurrentLevelData()) return;
    BossTitle::Start(g_pApp->GetGameLogic()->GetCurrentLevelData()->GetLevelNumber());
}

void HumanView::ActorEnteredBossAreaDelegate(IEventDataPtr pEventData)
{
    m_pHUD->SetElementVisible("bossbar", true);

    assert(g_pApp->GetGameLogic()->GetCurrentLevelData() != nullptr);
    int currentLevel = g_pApp->GetGameLogic()->GetCurrentLevelData()->GetLevelNumber();

    std::string bossMusicPath = "LEVEL" + ToStr(currentLevel) + "/MUSIC/BOSS.XMI";

    SoundInfo soundInfo(bossMusicPath);
    soundInfo.isMusic = true;
    soundInfo.loops = -1;
    //soundInfo.soundVolume = g_pApp->GetGameConfig()->musicVolume * 3;
    g_pApp->GetAudio()->SetMusicVolume(g_pApp->GetGameConfig()->musicVolume);
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(soundInfo)));
}

void HumanView::BossFightEndedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Boss_Fight_Ended> pCastEventData =
        static_pointer_cast<EventData_Boss_Fight_Ended>(pEventData);

    if (!pCastEventData->GetIsBossDead())
    {
        SoundInfo soundInfo(m_CurrentLevelMusic);
        soundInfo.isMusic = true;
        soundInfo.loops = -1;
        //soundInfo.soundVolume = g_pApp->GetGameConfig()->musicVolume;
        g_pApp->GetAudio()->SetMusicVolume(g_pApp->GetGameConfig()->musicVolume);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(soundInfo)));
    }

    m_pHUD->SetElementVisible("bossbar", false);

    // Does music go back to original level one once the boss is defeated ?
    /*SoundInfo soundInfo(m_CurrentLevelMusic);
    soundInfo.isMusic = true;
    soundInfo.loops = -1;
    //soundInfo.soundVolume = g_pApp->GetGameConfig()->musicVolume;
    g_pApp->GetAudio()->SetMusicVolume(g_pApp->GetGameConfig()->musicVolume / 3);
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(soundInfo)));*/
}

void HumanView::SetVolumeDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Set_Volume> pCastEventData =
        static_pointer_cast<EventData_Set_Volume>(pEventData);

    if (pCastEventData)
    {
        if (pCastEventData->GetIsMusicVolume())
        {
            if (pCastEventData->GetIsDelta())
            {
                int currentVolume = g_pApp->GetAudio()->GetMusicVolume();
                int deltaVolume = pCastEventData->GetVolume();
                int newVolume = currentVolume + deltaVolume;
                
                LOG("MUSIC VOLUME DEBUG: current=" + ToStr(currentVolume) + 
                    ", delta=" + ToStr(deltaVolume) + 
                    ", new=" + ToStr(newVolume));
                
                // Clamp to 0-100 range
                newVolume = max(0, min(100, newVolume));
                
                LOG("MUSIC VOLUME DEBUG: after clamp=" + ToStr(newVolume));
                
                g_pApp->GetAudio()->SetMusicVolume(newVolume);
            }
            else
            {
                LOG("MUSIC VOLUME DEBUG: absolute value=" + ToStr(pCastEventData->GetVolume()));
                g_pApp->GetAudio()->SetMusicVolume(pCastEventData->GetVolume());
            }
        }
        else
        {
            if (pCastEventData->GetIsDelta())
            {
                int currentVolume = g_pApp->GetAudio()->GetSoundVolume();
                int deltaVolume = pCastEventData->GetVolume();
                int newVolume = currentVolume + deltaVolume;
                
                LOG("SOUND VOLUME DEBUG: current=" + ToStr(currentVolume) + 
                    ", delta=" + ToStr(deltaVolume) + 
                    ", new=" + ToStr(newVolume));
                
                // Clamp to 0-100 range
                newVolume = max(0, min(100, newVolume));
                
                LOG("SOUND VOLUME DEBUG: after clamp=" + ToStr(newVolume));
                
                g_pApp->GetAudio()->SetSoundVolume(newVolume);
            }
            else
            {
                LOG("SOUND VOLUME DEBUG: absolute value=" + ToStr(pCastEventData->GetVolume()));
                g_pApp->GetAudio()->SetSoundVolume(pCastEventData->GetVolume());
            }
        }
    }
}

void HumanView::SoundEnabledChangedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Sound_Enabled_Changed> pCastEventData =
        static_pointer_cast<EventData_Sound_Enabled_Changed>(pEventData);

    if (pCastEventData)
    {
        if (pCastEventData->GetIsMusic())
        {
            g_pApp->GetAudio()->SetMusicActive(pCastEventData->GetIsEnabled());
        }
        else
        {
            g_pApp->GetAudio()->SetSoundActive(pCastEventData->GetIsEnabled());
        }
    }
}

void HumanView::ClawDiedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Claw_Died> pCastEventData =
        static_pointer_cast<EventData_Claw_Died>(pEventData);

    // Strong haptic feedback on death
    HapticFeedback::Trigger(HapticPreset::Death);

    // Race: no game over and no full-screen death fade (the other player
    // keeps going); the Captain is simply put back at his checkpoint.
    if (Race::IsActive())
    {
        return;
    }

    if (pCastEventData->GetRemainingLives() < 0)
    {
        TiXmlElement* pXmlGameOverMenuRoot = XmlResourceLoader::LoadAndReturnRootXmlElement("GAME_OVER_MENU.XML");
        assert(pXmlGameOverMenuRoot != NULL);

        // Track as a member so GetActiveMenu()/IsMenuActive() recognize the
        // game-over screen — otherwise the touch overlay never switches to menu
        // mode and touch users can't leave the game-over screen.
        m_pGameOverMenu.reset(new ScreenElementMenu(g_pApp->GetRenderer()));
        DO_AND_CHECK(m_pGameOverMenu->Initialize(pXmlGameOverMenuRoot));

        m_ScreenElements.push_back(m_pGameOverMenu);
        m_pGameOverMenu->VSetVisible(true);

        IEventMgr::Get()->VAbortAllEvents();
    }
    else
    {
        StrongProcessPtr pDeathProcess(
            new DeathFadeInOutProcess(pCastEventData->GetDeathPosition(), 1450, 1450, 0, 0));
        m_pProcessMgr->AttachProcess(pDeathProcess);
        // Needs to be called here
        pDeathProcess->VOnInit();
    }
}

void HumanView::TeleportActorDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Teleport_Actor> pCastEventData =
        static_pointer_cast<EventData_Teleport_Actor>(pEventData);

    if (pCastEventData->GetHasScreenSfx())
    {
        StrongProcessPtr pTeleportSfxProcess(
            new TeleportFadeInOutProcess(1150, 1150));
        m_pProcessMgr->AttachProcess(pTeleportSfxProcess);
        // Needs to be called here
        pTeleportSfxProcess->VOnInit();
    }
}

void HumanView::IngameMenuEndGameDelegate(IEventDataPtr pEventData)
{
    IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Enter_Menu()));
}

#ifndef __EMSCRIPTEN__
// Native builds keep saves in an XML file (see BaseGameLogic); after a progress
// reset that file must be rewritten, otherwise the old saves return on restart.
void HumanView::PersistSavesToFile()
{
    shared_ptr<GameSaveMgr> pSaveMgr = g_pApp->GetGameLogic()->GetGameSaveMgr();
    if (!pSaveMgr->IsSaveSupported())
    {
        return;
    }
    TiXmlDocument saveGamesDoc;
    saveGamesDoc.LinkEndChild(pSaveMgr->ToXml());
    std::string savesFilePath = g_pApp->GetGameConfig()->userDirectory + Difficulty::SavesFile(g_pApp->GetGameConfig()->savesFile);
    if (!saveGamesDoc.SaveFile(savesFilePath.c_str()))
    {
        LOG_ERROR("Failed to write saves file: " + savesFilePath);
    }
}
#endif

void HumanView::ResetSaveProgressDelegate(IEventDataPtr pEventData)
{
#ifdef __EMSCRIPTEN__
    SaveBridge::DeleteSaveData();
#endif

    g_pApp->GetGameLogic()->GetGameSaveMgr()->ResetToNewGame();
#ifndef __EMSCRIPTEN__
    PersistSavesToFile();
#endif
    CheatCode::Lock();   // the cheats go with the saved games

    IEventMgr::Get()->VQueueEvent(IEventDataPtr(
        new EventData_Menu_SwitchPage("MenuPage_SinglePlayer_NewGame")));
}

void HumanView::ExportSaveDataDelegate(IEventDataPtr pEventData)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ window.exportSaveData(); });
#endif
}

void HumanView::ImportSaveDataDelegate(IEventDataPtr pEventData)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ window.importSaveData(); });
#endif
}

void HumanView::ResetSaveProgressFromManageSavesDelegate(IEventDataPtr pEventData)
{
#ifdef __EMSCRIPTEN__
    SaveBridge::DeleteSaveData();
#endif

    g_pApp->GetGameLogic()->GetGameSaveMgr()->ResetToNewGame();
#ifndef __EMSCRIPTEN__
    PersistSavesToFile();
#endif
    CheatCode::Lock();   // the cheats go with the saved games

    IEventMgr::Get()->VQueueEvent(IEventDataPtr(
        new EventData_Menu_SwitchPage("MenuPage_SinglePlayer_SaveData")));
}

// "Replay movies": the startup movies plus the story movies of every chapter
// finished so far (a level counts as finished when the next one is unlocked).
void HumanView::PlayMoviesDelegate(IEventDataPtr pEventData)
{
    if (!MoviePlayer::IsAvailable())
    {
        return;
    }
    shared_ptr<EventData_Menu_Play_Movies> pCastEventData =
        static_pointer_cast<EventData_Menu_Play_Movies>(pEventData);
    int movieIndex = pCastEventData->GetMovieIndex();
    int lastFinishedLevel = 0;
    shared_ptr<GameSaveMgr> pSaveMgr = g_pApp->GetGameLogic()->GetGameSaveMgr();
    for (int level = 1; level < LEVELS_COUNT; ++level)
    {
        if (pSaveMgr->HasLevelSave(level + 1))
        {
            lastFinishedLevel = level;
        }
    }
    g_pApp->GetAudio()->PauseMusic();
    g_pApp->GetAudio()->PauseAllSounds();
    if (movieIndex >= 0)
    {
        MoviePlayer::PlayMovie(g_pApp->GetRenderer(), movieIndex);
    }
    else
    {
        MoviePlayer::PlayReplay(g_pApp->GetRenderer(), lastFinishedLevel);
    }
    g_pApp->GetAudio()->ResumeAllSounds();
    g_pApp->GetAudio()->ResumeMusic();
}

// "Load custom level": pick a .WWD from the Custom folder and play it with the
// assets of the original level it was made for.
void HumanView::LoadCustomLevelDelegate(IEventDataPtr pEventData)
{
    CustomLevels::Entry level;
    if (!CustomLevels::Pick(g_pApp->GetRenderer(), level))
    {
        return;   // back to the menu page
    }
    if (level.baseLevel < 1 || level.baseLevel > 14)
    {
        LOG_ERROR("Custom level does not name its original level: " + level.path);
        RaceUi::ShowMessage(g_pApp->GetRenderer(), level.fileName,
            { Lang::Tr("Nie wiadomo, z którego poziomu gry ten poziom bierze grafiki",
                       "This level does not say which game level its graphics come from"),
              Lang::Tr("(brak ścieżek LEVELx w pliku .WWD).", "(no LEVELx paths in the .WWD file).") });
        return;
    }
    LOG_WARNING("Loading custom level " + level.path + " (assets of level " + ToStr(level.baseLevel) +
                ", start " + ToStr(level.startX) + "," + ToStr(level.startY) + ")");

    m_ScreenElements.clear();
    m_pMenu.reset();
    m_pScene.reset(new ScreenElementScene(g_pApp->GetRenderer()));
    m_pHUD.reset(new ScreenElementHUD());
    m_pScene->AddChild(INVALID_ACTOR_ID, m_pCamera);
    m_pScene->SetCamera(m_pCamera);

    shared_ptr<LevelData> pLevelData(new LevelData(level.baseLevel, true, 0));
    pLevelData->SetCustomLevel(level.path, level.startX, level.startY);
    g_pApp->GetGameLogic()->SetLevelData(pLevelData);
    g_pApp->GetGameLogic()->VChangeState(GameState_LoadingLevel);
}

// True: the press was part of the code's buttons after the arrows (eaten).
bool HumanView::OnMenuCheatKey(CheatCode::Key key)
{
    if (CheatCode::IsUnlocked()) return false;
    const bool done = CheatCode::Feed(key);
    if (done)
    {
        STARTUP_TRACE("cheat code entered in the menu");
        if (m_pMenu) m_pMenu->RefreshActivePageVisibility();
        return true;
    }
    return CheatCode::Progress() > 8;
}

void HumanView::OnCheatKey(CheatCode::Key key)
{
    if (CheatCode::Feed(key) && m_pIngameMenu)
    {
        m_pIngameMenu->SetMenuItemVisibleOnAllPages("CHEATS_BUTTON", true);
    }
}

void HumanView::OpenCheatsDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Open_Cheats> pEvent = static_pointer_cast<EventData_Menu_Open_Cheats>(pEventData);
    if (!pEvent->GetAction().empty())
    {
        // An item of the cheats page (its on / off pictures swap themselves).
        CheatCode::Apply(pEvent->GetAction());
        return;
    }
    // The pause menu's page in the game's letters; other releases keep the list.
    if (GameFont::Available())
    {
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Menu_SwitchPage("MenuPage_Cheats")));
        return;
    }
    CheatCode::RunCheatsScreen(g_pApp->GetRenderer());
}

void HumanView::SetSubtitlesDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Subtitles> pCast = static_pointer_cast<EventData_Menu_Set_Subtitles>(pEventData);
    if (pCast) Subtitles::SetEnabled(pCast->IsEnabled());
}

void HumanView::SetViewZoomDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_View_Zoom> pCast = static_pointer_cast<EventData_Menu_Set_View_Zoom>(pEventData);
    if (pCast) g_pApp->SetViewZoom(pCast->GetPercent());
}

void HumanView::SetSmoothDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Smooth> pCast = static_pointer_cast<EventData_Menu_Set_Smooth>(pEventData);
    if (pCast) g_pApp->SetSmooth(pCast->IsEnabled());
}

void HumanView::SetAspectDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Aspect> pCast = static_pointer_cast<EventData_Menu_Set_Aspect>(pEventData);
    if (pCast) BaseGameApp::SetAspectClassic(pCast->IsClassic());
}

void HumanView::SetFilterDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Filter> pCast = static_pointer_cast<EventData_Menu_Set_Filter>(pEventData);
    if (pCast) BaseGameApp::SetFilter(pCast->GetKind());
}

void HumanView::SetCrtDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Crt> pCast = static_pointer_cast<EventData_Menu_Set_Crt>(pEventData);
    if (pCast) CrtEffect::SetOn(pCast->IsEnabled());
}

void HumanView::SetFpsCounterDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Fps_Counter> pCast = static_pointer_cast<EventData_Menu_Set_Fps_Counter>(pEventData);
    if (pCast) CrtEffect::SetFpsCounter(pCast->IsEnabled());
}

void HumanView::SetDifficultyDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Difficulty> pCast = static_pointer_cast<EventData_Menu_Set_Difficulty>(pEventData);
    if (!pCast) return;
    Difficulty::Set(pCast->GetDifficulty());
    // Every difficulty has its own progress: load its saves file
    if (g_pApp->GetGameLogic()) g_pApp->GetGameLogic()->ReloadSaves();
    LOG_WARNING(std::string("Difficulty: ") + Difficulty::Name(Difficulty::Get()));
}

void HumanView::SetMenuFontDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Set_Menu_Font> pCast = static_pointer_cast<EventData_Menu_Set_Menu_Font>(pEventData);
    if (pCast) MenuFont::SetReclaw(pCast->IsReclaw());
}

void HumanView::StartRaceDelegate(IEventDataPtr pEventData)
{
    Race::Setup setup;
    if (!RaceLobby::Run(g_pApp->GetRenderer(), setup))
    {
        return;   // back to the menu page
    }
    Race::Configure(setup);
    LOG("Starting race on level " + ToStr(setup.level));
    StartModeLevel(setup.level);
}

// Main menu > Single Player > "Skrzyżuj szpady ponownie": the boss list
// (level 0), or the next boss level of the running boss mode.
void HumanView::BossModesDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Boss_Modes> pCast = static_pointer_cast<EventData_Menu_Boss_Modes>(pEventData);
    if (!pCast || g_pApp->GetGameLogic()->GetGameState() != GameState_Menu)
    {
        return;
    }
    int level = pCast->GetLevel();
    if (level <= 0)
    {
        STARTUP_TRACE("boss modes: list");
        level = BossModes::RunMenu(g_pApp->GetRenderer());
        STARTUP_TRACE(("boss modes: chosen level " + ToStr(level)).c_str());
    }
    if (level <= 0 || !BossModes::IsActive())
    {
        return;   // back to the menu page
    }
    LOG("Boss mode: loading level " + ToStr(level));
    StartModeLevel(level);
}

// Options > Language.
void HumanView::OpenLanguageDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Menu_Open_Language> pEvent =
        static_pointer_cast<EventData_Menu_Open_Language>(pEventData);
    if (!pEvent->GetCode().empty())
    {
        // An item of the Language page: saved for the next start, back to Options.
        GameLanguages::Choose(pEvent->GetCode());
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Menu_SwitchPage("MenuPage_Options")));
        return;
    }
    // The menu page in the game's letters; other releases keep the list.
    if (GameFont::Available())
    {
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Menu_SwitchPage("MenuPage_Options_Language")));
        return;
    }
    GameLanguages::RunMenu();
}

// Loads a level for a race or a boss mode (no save game).
void HumanView::StartModeLevel(int level)
{
    m_ScreenElements.clear();
    m_pMenu.reset();
    m_pScene.reset(new ScreenElementScene(g_pApp->GetRenderer()));
    m_pHUD.reset(new ScreenElementHUD());
    m_pScene->AddChild(INVALID_ACTOR_ID, m_pCamera);
    m_pScene->SetCamera(m_pCamera);

    shared_ptr<LevelData> pLevelData(new LevelData(level, true, 0));
    g_pApp->GetGameLogic()->SetLevelData(pLevelData);
    g_pApp->GetGameLogic()->VChangeState(GameState_LoadingLevel);
}

void HumanView::EnableSplitScreen(uint32 secondActorId)
{
    shared_ptr<SceneNode> pNode = m_pScene ? static_pointer_cast<SceneNode>(m_pScene->FindActor(secondActorId)) : nullptr;
    if (!pNode)
    {
        LOG_ERROR("Split screen: player 2 has no scene node");
        return;
    }
    Point window = g_pApp->GetWindowSize();
    m_pCamera->SetSize((uint32)(window.x / 2), (uint32)window.y);
    m_pCamera2.reset(new CameraNode(m_pCamera->GetPosition(), (uint32)(window.x / 2), (uint32)window.y));
    m_pCamera2->SetTarget(pNode);
    m_SecondActorId = secondActorId;
    VCreateSecondController(secondActorId);
    if (m_pHUD) m_pHUD->VSetVisible(false);
    SDL2HUDSceneNode::s_bHideAll = true;
    LOG("Split screen on");
}

void HumanView::DisableSplitScreen()
{
    if (!m_pCamera2 && m_SecondActorId == 0)
    {
        return;
    }
    VDestroySecondController();
    m_pGamepadHandler2.reset();
    m_pCamera2.reset();
    m_SecondActorId = 0;
    Point window = g_pApp->GetWindowSize();
    m_pCamera->SetSize((uint32)window.x, (uint32)window.y);
    if (m_pHUD) m_pHUD->VSetVisible(true);
    SDL2HUDSceneNode::s_bHideAll = false;
    LOG("Split screen off");
}

shared_ptr<CameraNode> HumanView::GetCameraForActor(uint32 actorId)
{
    if (m_pCamera2 && actorId == m_SecondActorId)
    {
        return m_pCamera2;
    }
    return m_pCamera;
}

bool HumanView::IsPointInAnyView(const Point& point, float cameraScale)
{
    if (m_pCamera && m_pCamera->IntersectsWithPoint(point, cameraScale)) return true;
    return m_pCamera2 && m_pCamera2->IntersectsWithPoint(point, cameraScale);
}

bool HumanView::IsRectInAnyView(const SDL_Rect& rect)
{
    if (m_pCamera)
    {
        SDL_Rect cameraRect = m_pCamera->GetCameraRect();
        if (SDL_HasIntersection(&rect, &cameraRect)) return true;
    }
    if (m_pCamera2)
    {
        SDL_Rect cameraRect = m_pCamera2->GetCameraRect();
        if (SDL_HasIntersection(&rect, &cameraRect)) return true;
    }
    return false;
}

// Options > Graphics > aspect classic: the 4:3 area of the level in the
// middle of the screen (in the renderer's scaled units), and the camera made
// as wide as it. Empty when the level fills the screen (widescreen, split
// screen, or the screen not wider than 4:3).
SDL_Rect HumanView::ClassicArea()
{
    SDL_Rect r = { 0, 0, 0, 0 };
    if (!m_pCamera) return r;
    const Point window = g_pApp->GetWindowSize();
    const Point scale = g_pApp->GetScale();
    int wantW = (int)window.x;
    if (BaseGameApp::AspectClassic() && !m_pCamera2 && window.y > 0)
    {
        const int w43 = (int)(window.y * 4 / 3) & ~1;
        if (w43 + 8 < window.x) wantW = w43;
    }
    if ((int)m_pCamera->GetWidth() != wantW)
    {
        m_pCamera->SetSize(wantW, m_pCamera->GetHeight());
    }
    if (wantW >= (int)window.x || scale.x <= 0 || scale.y <= 0) return r;
    r.w = (int)(wantW / scale.x);
    r.h = (int)(window.y / scale.y);
    r.x = (int)((window.x - wantW) / 2 / scale.x);
    r.y = 0;
    return r;
}

void HumanView::RenderSplitScene(SDL_Renderer* pRenderer)
{
    Point scale = g_pApp->GetScale();
    int fullW = (int)(g_pApp->GetWindowSize().x / scale.x);
    int fullH = (int)(g_pApp->GetWindowSize().y / scale.y);
    int halfW = fullW / 2;
    SDL_Rect views[2] = { { 0, 0, halfW, fullH }, { halfW, 0, fullW - halfW, fullH } };
    shared_ptr<CameraNode> cameras[2] = { m_pCamera, m_pCamera2 };

    for (int i = 0; i < 2; ++i)
    {
        SDL_RenderSetViewport(pRenderer, &views[i]);
        m_pScene->SetCamera(cameras[i]);
        m_pScene->OnRender();
        SDL_Rect local = { 0, 0, views[i].w, views[i].h };
        Minimap::Render(pRenderer, local, i == 0 ? m_ActorId : m_SecondActorId);
        Race::RenderOverlay(pRenderer, i, local);
    }
    SDL_RenderSetViewport(pRenderer, NULL);
    m_pScene->SetCamera(m_pCamera);

    // Divider between the halves
    SDL_SetRenderDrawColor(pRenderer, 20, 10, 0, 255);
    SDL_Rect divider = { halfW - 1, 0, 2, fullH };
    SDL_RenderFillRect(pRenderer, &divider);
}

void HumanView::ShowToast(const std::string& text)
{
    m_ToastText = text;
    m_ToastShownAt = SDL_GetTicks();
    if (m_pToastTexture)
    {
        SDL_DestroyTexture(m_pToastTexture);
        m_pToastTexture = nullptr;
    }
}

void HumanView::CancelScreenEffects()
{
    if (m_pProcessMgr)
    {
        m_pProcessMgr->AbortAllProcesses(true);
    }
}

// Gold text with a dark outline, like the menu, for 1.5 s.
void HumanView::RenderToast(SDL_Renderer* pRenderer)
{
    const uint32 kToastMs = 1500;
    if (m_ToastText.empty())
    {
        return;
    }
    uint32 elapsed = SDL_GetTicks() - m_ToastShownAt;
    if (elapsed >= kToastMs)
    {
        m_ToastText.clear();
        if (m_pToastTexture)
        {
            SDL_DestroyTexture(m_pToastTexture);
            m_pToastTexture = nullptr;
        }
        return;
    }

    if (!m_pToastTexture)
    {
        if (!TTF_WasInit() && TTF_Init() != 0) return;
        static TTF_Font* s_pFont = nullptr;
        if (!s_pFont)
        {
            s_pFont = TTF_OpenFont(Platform::SplashFont().c_str(), 22);
            if (!s_pFont) { m_ToastText.clear(); return; }
        }
        SDL_Color gold = { 236, 196, 98, 255 }, edge = { 30, 15, 2, 255 };
        TTF_SetFontOutline(s_pFont, 2);
        SDL_Surface* back = TTF_RenderUTF8_Blended(s_pFont, m_ToastText.c_str(), edge);
        TTF_SetFontOutline(s_pFont, 0);
        SDL_Surface* front = TTF_RenderUTF8_Blended(s_pFont, m_ToastText.c_str(), gold);
        if (back && front)
        {
            SDL_Rect dst = { 2, 2, front->w, front->h };
            SDL_BlitSurface(front, NULL, back, &dst);
            m_pToastTexture = SDL_CreateTextureFromSurface(pRenderer, back);
            m_ToastW = back->w;
            m_ToastH = back->h;
        }
        if (back) SDL_FreeSurface(back);
        if (front) SDL_FreeSurface(front);
        if (!m_pToastTexture) { m_ToastText.clear(); return; }
    }

    int w = 0, h = 0;
    if (SDL_Texture* pTarget = SDL_GetRenderTarget(pRenderer)) SDL_QueryTexture(pTarget, NULL, NULL, &w, &h);
    else SDL_GetRendererOutputSize(pRenderer, &w, &h);
    float sx = 1, sy = 1;
    SDL_RenderGetScale(pRenderer, &sx, &sy);
    SDL_RenderSetScale(pRenderer, 1, 1);
    // fade out during the last 400 ms
    uint8_t alpha = elapsed > kToastMs - 400 ? (uint8_t)(255 * (kToastMs - elapsed) / 400) : 255;
    SDL_SetTextureAlphaMod(m_pToastTexture, alpha);
    SDL_Rect dst = { (w - m_ToastW) / 2, h / 8, m_ToastW, m_ToastH };
    SDL_RenderCopy(pRenderer, m_pToastTexture, NULL, &dst);
    SDL_RenderSetScale(pRenderer, sx, sy);
}

void HumanView::WorldFinishedLoadingDelegate(IEventDataPtr pEventData)
{
    m_bLevelRevealPending = true;
}

// The original game opens every level by uncovering the view from the left
// edge to the right (~1.5 s) while the game already runs.
void HumanView::RenderLevelReveal(SDL_Renderer* pRenderer)
{
    const uint32 kRevealMs = 1500;
    // The reveal advances by at most 33 ms per frame, so the slow first frames
    // of a level (pictures and sounds still loading) do not make it jump.
    static uint32 s_LastTick = 0, s_Elapsed = 0;
    const uint32 now = SDL_GetTicks();
    if (m_bLevelRevealPending)
    {
        m_bLevelRevealPending = false;
        m_LevelRevealStart = now ? now : 1;
        s_LastTick = now;
        s_Elapsed = 0;
    }
    if (m_LevelRevealStart == 0)
    {
        return;
    }
    s_Elapsed += std::min<uint32>(now - s_LastTick, 33);
    s_LastTick = now;
    uint32 elapsed = s_Elapsed;
    if (elapsed >= kRevealMs)
    {
        m_LevelRevealStart = 0;
        return;
    }

    int w = 0, h = 0;
    if (SDL_Texture* pTarget = SDL_GetRenderTarget(pRenderer))
    {
        SDL_QueryTexture(pTarget, NULL, NULL, &w, &h);
    }
    else
    {
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
    }
    float scaleX = 1.0f, scaleY = 1.0f;
    SDL_RenderGetScale(pRenderer, &scaleX, &scaleY);
    SDL_RenderSetScale(pRenderer, 1.0f, 1.0f);

    int revealed = (int)((int64_t)w * elapsed / kRevealMs);
    SDL_Rect cover = { revealed, 0, w - revealed, h };
    SDL_SetRenderDrawColor(pRenderer, 0, 0, 0, 255);
    SDL_RenderFillRect(pRenderer, &cover);

    SDL_RenderSetScale(pRenderer, scaleX, scaleY);
}

void HumanView::StartNewGameDelegate(IEventDataPtr pEventData)
{
    IEventMgr::Get()->VQueueEvent(IEventDataPtr(
        new EventData_Menu_SwitchPage("MenuPage_SinglePlayer_NewGame")));
}

void HumanView::ToggleFullscreenDelegate(IEventDataPtr pEventData)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ window.toggleFullscreen(); });
#endif
}

void HumanView::ToggleAspectRatioDelegate(IEventDataPtr pEventData)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ window.toggleAspectRatio(); });
#endif
}

//=================================================================================================
// 
// class SpecialEffectProcess
//
//=================================================================================================

void HumanView::CaptureEffectFrame()
{
    SDL_Renderer* pRenderer = g_pApp->GetRenderer();
    if (!pRenderer || !m_pScene) return;
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(pRenderer, &w, &h);
    if (w <= 0 || h <= 0) return;
    if (m_pEffectFrame)
    {
        int tw = 0, th = 0;
        SDL_QueryTexture(m_pEffectFrame, NULL, NULL, &tw, &th);
        if (tw != w || th != h) DropEffectFrame();
    }
    if (!m_pEffectFrame)
        m_pEffectFrame = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!m_pEffectFrame) { STARTUP_TRACE("effect: no texture for the level picture"); return; }
    SDL_Texture* previous = SDL_GetRenderTarget(pRenderer);
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(pRenderer, &sx, &sy);
    if (SDL_SetRenderTarget(pRenderer, m_pEffectFrame) != 0) { DropEffectFrame(); return; }
    // the same view as the screen's (a target starts at scale 1: on the PS3
    // the level came out half-size in a corner, shrinking as it closed)
    const Point scale = g_pApp->GetScale();
    SDL_RenderSetScale(pRenderer, (float)scale.x, (float)scale.y);
    SDL_RenderSetViewport(pRenderer, NULL);
    const bool wasRendering = m_bRendering, wasPostponed = m_bPostponeRenderPresent;
    m_bRendering = true;
    m_bPostponeRenderPresent = true;
    m_bCapturingEffectFrame = true;
    VOnRender(0);
    m_bCapturingEffectFrame = false;
    m_bRendering = wasRendering;
    m_bPostponeRenderPresent = wasPostponed;
    SDL_SetRenderTarget(pRenderer, previous);
    SDL_RenderSetScale(pRenderer, sx, sy);
    STARTUP_TRACE("effect: level picture kept");
}

void HumanView::DrawEffectFrame(SDL_Renderer* pRenderer)
{
    if (!m_pEffectFrame) return;
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(pRenderer, &sx, &sy);
    SDL_RenderSetScale(pRenderer, 1.0f, 1.0f);
    SDL_RenderSetViewport(pRenderer, NULL);
    SDL_SetTextureBlendMode(m_pEffectFrame, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(pRenderer, m_pEffectFrame, NULL, NULL);
    SDL_RenderSetScale(pRenderer, sx, sy);
}

void HumanView::DropEffectFrame()
{
    if (m_pEffectFrame) SDL_DestroyTexture(m_pEffectFrame);
    m_pEffectFrame = nullptr;
}

// The level drawn again under a full-screen effect (death, teleport), not
// shown yet: the effect's black bands go over a whole new frame. Drawing
// them over whatever the screen buffer held left the frames of two
// buffers (PS3, Switch...) alternating: a jerky, lopsided closing box.
static void DrawLevelUnderEffect(SDL_Renderer* pRenderer, bool closing)
{
    HumanView* pView = g_pApp->GetHumanView();
    if (!pView) return;
    if (closing && pView->HasEffectFrame())
    {
        // the moment Claw died / stepped into the warp (he has already been
        // moved to where he comes back)
        SDL_SetRenderDrawColor(pRenderer, 0, 0, 0, 255);
        SDL_RenderClear(pRenderer);
        pView->DrawEffectFrame(pRenderer);
    }
    else
    {
        if (!closing) pView->DropEffectFrame();
        const bool wasRendering = pView->IsRendering();
        pView->SetRendering(true);
        pView->SetPostponeRenderPresent(true);
        pView->VOnRender(0);
        pView->SetRendering(wasRendering);
    }
    const Point scale = g_pApp->GetScale();
    SDL_RenderSetScale(pRenderer, (float)scale.x, (float)scale.y);
    SDL_RenderSetViewport(pRenderer, NULL);
    SDL_SetRenderDrawColor(pRenderer, 0, 0, 0, 255);
}

static Uint32 g_EffectShownAt = 0;   // when the last effect frame was on the screen

static bool EffectShownJustNow()
{
    return g_EffectShownAt != 0 && SDL_GetTicks() - g_EffectShownAt < 8;
}

static void FillBlack(SDL_Renderer* pRenderer, const SDL_Rect& r)
{
    if (r.w > 0 && r.h > 0) SDL_RenderFillRect(pRenderer, &r);
}

void SpecialEffectProcess::VRestoreStates()
{
    if (g_pApp && g_pApp->GetHumanView()) g_pApp->GetHumanView()->DropEffectFrame();
    if (g_pApp && g_pApp->GetGameLogic())
    {
        g_pApp->GetGameLogic()->SetRunning(true);
    }
    if (g_pApp && g_pApp->GetHumanView())
    {
        g_pApp->GetHumanView()->SetPostponeRenderPresent(false);
        g_pApp->GetHumanView()->SetRendering(true);
    }
}

//=================================================================================================
// 
// class DeathFadeInOutProcess
//
//=================================================================================================

DeathFadeInOutProcess::DeathFadeInOutProcess(Point epicenter, int fadeInDuration, int fadeOutDuration, int startDelay, int endDelay)
    :
    SpecialEffectProcess(),
    m_Epicenter(epicenter),
    m_FadeInDuration(fadeInDuration),
    m_FadeOutDuration(fadeOutDuration),
    m_StartDelay(startDelay),
    m_EndDelay(endDelay),
    m_DeathFadeState(DeathFadeState_Started),
    m_CurrentTime(0)
{
    m_Epicenter.Set(epicenter.x, epicenter.y);
}

DeathFadeInOutProcess::~DeathFadeInOutProcess()
{

}

void DeathFadeInOutProcess::VOnInit()
{
    Process::VOnInit();

    assert(g_pApp);
    assert(g_pApp->GetGameLogic());
    assert(g_pApp->GetHumanView());
    assert(g_pApp->GetHumanView()->GetCamera());

    // Recalc epicenter to screen-local coordinates
    Point windowSize = g_pApp->GetWindowSizeScaled();
    shared_ptr<CameraNode> pCamera = g_pApp->GetHumanView()->GetCamera();
    Point cameraPos = pCamera->GetPosition();

    // (the camera's position is the level point at the top left corner;
    // the screen's units are the level's - SDL scales them)
    m_Epicenter.Set(m_Epicenter.x - cameraPos.x, m_Epicenter.y - cameraPos.y);

    // The level as it is now (Claw is moved back to the checkpoint later)
    g_pApp->GetHumanView()->CaptureEffectFrame();

    // Stop game logic update during fade in/out
    g_pApp->GetGameLogic()->SetRunning(false);

    // Also we will render as we want to
    g_pApp->GetHumanView()->SetRendering(false);
    g_pApp->GetHumanView()->SetPostponeRenderPresent(true);

    // Calculate graphics fade speed
    m_FadeInSpeed.x = (windowSize.x / (double)m_FadeInDuration) / 2.0;
    m_FadeInSpeed.y = (windowSize.y / (double)m_FadeInDuration) / 2.0;

    m_FadeOutSpeed.x = (windowSize.x / (double)m_FadeOutDuration) / 2.0;
    m_FadeOutSpeed.y = (windowSize.y / (double)m_FadeOutDuration) / 2.0;
}

void DeathFadeInOutProcess::VOnUpdate(uint32 msDiff)
{
    m_CurrentTime += msDiff;

    switch (m_DeathFadeState)
    {
        case DeathFadeState_Started:
        {
            if (m_CurrentTime >= m_StartDelay)
            {
                SoundInfo soundInfo(SOUND_GAME_DEATH_FADE_IN_SOUND);
                IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
                    new EventData_Request_Play_Sound(soundInfo)));

                m_CurrentTime = 0;
                m_DeathFadeState = DeathFadeState_FadingIn;
            }
            break;
        }

        case DeathFadeState_FadingIn:
        {
            if (m_CurrentTime >= m_FadeInDuration)
            {
                SoundInfo soundInfo(SOUND_GAME_DEATH_FADE_OUT_SOUND);
                IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
                    new EventData_Request_Play_Sound(soundInfo)));

                m_CurrentTime = 0;
                m_DeathFadeState = DeathFadeState_FadingOut;
            }
            break;
        }

        case DeathFadeState_FadingOut:
        {
            if (m_CurrentTime >= m_FadeOutDuration)
            {
                m_CurrentTime = 0;
                m_DeathFadeState = DeathFadeState_Ended;
            }
            break;
        }

        case DeathFadeState_Ended:
        {
            if (m_CurrentTime >= m_EndDelay)
            {
                Succeed();

                // This should not be here, since it does not know that Claw died here...
                StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
                assert(pClaw != nullptr);

                IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Claw_Respawned(pClaw->GetGUID())));
            }
            break;
        }
        default:
            LOG_ERROR("Unknown DeathFadeState: " + ToStr((int)m_DeathFadeState));
            break;
    }

    //LOG("State: " + ToStr((int)m_DeathFadeState));

    VRender(msDiff);
}

void DeathFadeInOutProcess::VRender(uint32 msDiff)
{
    SDL_Renderer* pRenderer = g_pApp->GetRenderer();
    // two game steps in one frame (catching up): one picture is enough
    // (each one waits for the screen; drawing both made the effect late)
    if (EffectShownJustNow()) return;
    DrawLevelUnderEffect(pRenderer, m_DeathFadeState != DeathFadeState_FadingOut && m_DeathFadeState != DeathFadeState_Ended);

    Point windowSize = g_pApp->GetWindowSizeScaled();
    // Classic 4:3: the effect closes over the level's area, not the screen
    // (the black sides are drawn anyway); the speeds were worked out for
    // the whole width.
    SDL_Rect area = g_pApp->GetHumanView() ? g_pApp->GetHumanView()->ClassicArea() : SDL_Rect{ 0, 0, 0, 0 };
    double areaShare = 1.0;
    if (area.w > 0 && windowSize.x > 0)
    {
        areaShare = area.w / windowSize.x;
        windowSize.Set(area.w, area.h);
        SDL_RenderSetViewport(pRenderer, &area);
    }
    
    /*g_pApp->GetHumanView()->SetRendering(true);
    g_pApp->GetHumanView()->VOnRender(msDiff);
    g_pApp->GetHumanView()->SetRendering(false);*/

    // Render fade in/outs according to current state
    int referenceTime = 0;
    Point *fadeSpeed = &m_FadeInSpeed;
    if (m_DeathFadeState == DeathFadeState_FadingIn)
    {
        referenceTime = m_CurrentTime;
    }
    else if (m_DeathFadeState == DeathFadeState_FadingOut)
    {
        referenceTime = m_FadeOutDuration - m_CurrentTime;
        fadeSpeed = &m_FadeOutSpeed;
    }

    // As in the original: a black circle closes on where Claw died, and
    // opens again from the middle (the camera is on Claw, back at the
    // checkpoint). Drawn as one black span left and right of the circle per row.
    const double duration = (m_DeathFadeState == DeathFadeState_FadingOut) ? m_FadeOutDuration : m_FadeInDuration;
    double f = duration > 0 ? referenceTime / duration : 0.0;
    f = f < 0 ? 0 : (f > 1 ? 1 : f);
    const double W = windowSize.x, H = windowSize.y;
    double ex = W / 2, ey = H / 2;
    if (m_DeathFadeState != DeathFadeState_FadingOut && m_DeathFadeState != DeathFadeState_Ended)
    {
        double px = m_Epicenter.x, py = m_Epicenter.y;
        if (px >= 0 && px <= W && py >= 0 && py <= H) { ex = px; ey = py; }
    }
    // radius 0 when closed; at the start it reaches the farthest corner
    const double fx = std::max(ex, W - ex), fy = std::max(ey, H - ey);
    const double rMax = sqrt(fx * fx + fy * fy) + 1.0;
    const double r = rMax * (1.0 - f);
    SDL_SetRenderDrawColor(pRenderer, 0, 0, 0, 255);
    static std::vector<SDL_Rect> s_Spans;
    s_Spans.clear();
    const int rows = (int)H;
    for (int y = 0; y < rows; ++y)
    {
        const double dy = (y + 0.5) - ey;
        if (fabs(dy) >= r)
        {
            s_Spans.push_back(SDL_Rect{ 0, y, (int)W, 1 });
            continue;
        }
        const double dx = sqrt(r * r - dy * dy);
        const int left = (int)(ex - dx), right = (int)ceil(ex + dx);
        if (left > 0) s_Spans.push_back(SDL_Rect{ 0, y, left, 1 });
        if (right < (int)W) s_Spans.push_back(SDL_Rect{ right, y, (int)W - right, 1 });
    }
    if (!s_Spans.empty()) SDL_RenderFillRects(pRenderer, s_Spans.data(), (int)s_Spans.size());
    (void)areaShare;

    if (area.w > 0) SDL_RenderSetViewport(pRenderer, NULL);
    Util::RenderForcePresent(pRenderer);
    g_EffectShownAt = SDL_GetTicks();
}

//=================================================================================================
// 
// class FadingLine - helper that represents line on the screen that is fading in or out
//
//=================================================================================================

FadingLine::FadingLine(int length, Point fragmentSize, int fadeDelay, int fadeDuration, bool isFadingIn)
    :
    m_Length(length),
    m_FragmentSize(fragmentSize),
    m_FadeDelay(fadeDelay),
    m_FadeDuration(fadeDuration),
    m_bIsFadingIn(isFadingIn),
    m_CurrentTime(0),
    m_SingleFragmentFadeTime(0),
    m_bIsActive(false),
    m_bIsDone(false),
    m_FragmentCount(0)
{
    m_FragmentCount = (length / (int)fragmentSize.x) + 1;
    m_SingleFragmentFadeTime = fadeDuration / m_FragmentCount;
    m_pPrimeSearch.reset(new PrimeSearch(m_FragmentCount));

    for (int fragIdx = 0; fragIdx < m_FragmentCount; fragIdx++)
    {
        m_FadedFragments.push_back(!m_bIsFadingIn);
    }

    LOG("length: " + ToStr(m_Length) + ", fadeDelay: " + ToStr(m_FadeDelay) + 
        ", fadeDuration: " + ToStr(m_FadeDuration) + ", fragmentCount: " + ToStr(m_FragmentCount) + 
        ", isFadingIn: " + ToStr(m_bIsFadingIn));
}

FadingLine::~FadingLine()
{

}

void FadingLine::Update(uint32 msDiff)
{
    if (m_bIsDone)
    {
        return;
    }

    m_CurrentTime += msDiff;
    if (!m_bIsActive)
    {
        if (m_CurrentTime >= m_FadeDelay)
        {
            m_bIsActive = true;
            //m_CurrentTime = m_CurrentTime - m_FadeDelay;
            LOG("Activated");
            m_CurrentTime = 0;
        }
        else
        {
            return;
        }
    }

    if (m_CurrentTime >= m_SingleFragmentFadeTime)
    {
        // TODO: Handle possible overflow
        //m_CurrentTime -= m_SingleFragmentFadeTime;
        m_CurrentTime = 0;
        int fadedElemIdx = m_pPrimeSearch->GetNext();
        if (fadedElemIdx == -1)
        {
            m_bIsDone = true;
            return;
        }

        m_FadedFragments[fadedElemIdx] = m_bIsFadingIn;
    }
}

void FadingLine::Reset(int fadeDelay, bool isFadingIn)
{
    m_bIsActive = false;
    m_bIsDone = false;
    m_bIsFadingIn = isFadingIn;
    m_FadeDelay = fadeDelay;

    m_FadedFragments.clear();
    for (int fragIdx = 0; fragIdx < m_FragmentCount; fragIdx++)
    {
        m_FadedFragments.push_back(!m_bIsFadingIn);
    }

    m_pPrimeSearch.reset(new PrimeSearch(m_FragmentCount));
}

void FadingLine::Render(SDL_Renderer* pRenderer, SDL_Texture* pFragmentTexture, Point& lineOffset, bool asRow)
{
    assert(pRenderer != NULL);
    assert(pFragmentTexture != NULL);

    int fragIdx = 0;
    for (bool shouldRender : m_FadedFragments)
    {
        if (!shouldRender)
        {
            fragIdx++;
            //LOG("Should NOT render");
            continue;
        }
        else
        {
            //LOG("Should render")
        }

        SDL_Rect renderRect;
        renderRect.w = (int)m_FragmentSize.x;
        renderRect.y = (int)m_FragmentSize.y;
        if (asRow)
        {
            renderRect.x = (int)lineOffset.x + fragIdx * (int)m_FragmentSize.x;
            renderRect.y = (int)lineOffset.y;
        }
        else
        {
            renderRect.x = (int)lineOffset.x;
            renderRect.y = (int)lineOffset.y + fragIdx * (int)m_FragmentSize.y;
        }

        SDL_RenderCopy(pRenderer, pFragmentTexture, NULL, &renderRect);

        fragIdx++;
    }
}

//=================================================================================================
// 
// class TeleportFadeInOutProcess
//
//=================================================================================================

TeleportFadeInOutProcess::TeleportFadeInOutProcess(int fadeInDuration, int fadeOutDuration)
    :
    SpecialEffectProcess(),
    m_FadeInDuration(fadeInDuration),
    m_FadeOutDuration(fadeOutDuration),
    m_TeleportState(TeleportState_FadingIn),
    m_CurrentTime(0)
{
}

TeleportFadeInOutProcess::~TeleportFadeInOutProcess()
{

}

void TeleportFadeInOutProcess::VOnInit()
{
    SpecialEffectProcess::VOnInit();

    assert(g_pApp);
    assert(g_pApp->GetGameLogic());
    assert(g_pApp->GetHumanView());
    assert(g_pApp->GetHumanView()->GetCamera());

    // (the level before the warp: captured by the game logic before it
    // moved Claw; ClawGameLogic::TeleportActorDelegate)
    if (!g_pApp->GetHumanView()->HasEffectFrame()) g_pApp->GetHumanView()->CaptureEffectFrame();

    // Stop game logic update during fade in/out
    g_pApp->GetGameLogic()->SetRunning(false);
    g_pApp->GetHumanView()->SetPostponeRenderPresent(true);
    g_pApp->GetHumanView()->SetRendering(false);

    Point windowSize = g_pApp->GetWindowSizeScaled();

    // Calculate graphics fade speed
    m_FadeInSpeed.x = (windowSize.x / (double)m_FadeInDuration) / 2.0;
    m_FadeInSpeed.y = (windowSize.y / (double)m_FadeInDuration) / 2.0;

    m_FadeOutSpeed.x = (windowSize.x / (double)m_FadeOutDuration) / 2.0;
    m_FadeOutSpeed.y = (windowSize.y / (double)m_FadeOutDuration) / 2.0;

    /*m_FragmentSize.Set(8, 8);

    m_pFadingTexture = Util::CreateSDLTextureRect(
        (int)m_FragmentSize.x, (int)m_FragmentSize.y, COLOR_BLACK, g_pApp->GetRenderer());
    assert(m_pFadingTexture != NULL);
    if (m_pFadingTexture == NULL)
    {
        LOG_ERROR("Could not create fading texture for TeleportFadeInOutProcess");
       Fail();
    }

    int numLines = (int)(windowSize.y / m_FragmentSize.y);
    int msPerLine = (m_FadeInDuration / numLines) + 1;
    for (int lineIdx = 0; lineIdx < numLines; lineIdx++)
    {
        int fadeDelay = (m_FadeInDuration - (lineIdx * msPerLine)) / 2;
        LOG("FadeDelay: " + ToStr(fadeDelay));
        shared_ptr<FadingLine> pLine(new FadingLine((int)windowSize.x, m_FragmentSize, fadeDelay, msPerLine, true));
        m_Lines.push_back(pLine);
    }*/
}

void TeleportFadeInOutProcess::VOnUpdate(uint32 msDiff)
{
    m_CurrentTime += msDiff;

    switch (m_TeleportState)
    {
        case TeleportState_FadingIn:
            /*for (shared_ptr<FadingLine> pLine : m_Lines)
            {
                pLine->Update(msDiff);
            }*/

            if (m_CurrentTime >= m_FadeInDuration)
            {
                m_CurrentTime = 0;
                m_TeleportState = TeleportState_FadingOut;

                /*for (shared_ptr<FadingLine> pLine : m_Lines)
                {
                    pLine->Reset(false);
                }*/

                //Succeed();
            }
            break;

        case TeleportState_FadingOut:
            if (m_CurrentTime >= m_FadeOutDuration)
            {
                Succeed();
            }
            break;

        default:
            LOG_ERROR("Unknown TeleportState: " + ToStr((int)m_TeleportState));
            break;
    }

    VRender(msDiff);
}

void TeleportFadeInOutProcess::VRender(uint32 msDiff)
{
    SDL_Renderer* pRenderer = g_pApp->GetRenderer();
    // two game steps in one frame (catching up): one picture is enough
    // (each one waits for the screen; drawing both made the effect late)
    if (EffectShownJustNow()) return;
    DrawLevelUnderEffect(pRenderer, m_TeleportState == TeleportState_FadingIn);

    Point windowSize = g_pApp->GetWindowSizeScaled();
    // Classic 4:3: the effect closes over the level's area, not the screen
    // (the black sides are drawn anyway); the speeds were worked out for
    // the whole width.
    SDL_Rect area = g_pApp->GetHumanView() ? g_pApp->GetHumanView()->ClassicArea() : SDL_Rect{ 0, 0, 0, 0 };
    double areaShare = 1.0;
    if (area.w > 0 && windowSize.x > 0)
    {
        areaShare = area.w / windowSize.x;
        windowSize.Set(area.w, area.h);
        SDL_RenderSetViewport(pRenderer, &area);
    }

    // Render fade in/outs according to current state
    int referenceTime = 0;
    Point *fadeSpeed = &m_FadeInSpeed;
    if (m_TeleportState == TeleportState_FadingIn)
    {
        referenceTime = m_CurrentTime;
    }
    else if (m_TeleportState == TeleportState_FadingOut)
    {
        referenceTime = m_FadeOutDuration - m_CurrentTime;
        fadeSpeed = &m_FadeOutSpeed;
    }

    referenceTime = max(0, referenceTime);

    // Left -> right rect
    int currentWidth = (int)((double)referenceTime * fadeSpeed->x * areaShare);
    //LOG("Current width: " + ToStr(currentWidth));
    SDL_Rect leftRect = { 0, 0, currentWidth, (int)windowSize.y };
    FillBlack(pRenderer, leftRect);

    // Right -> left rect
    SDL_Rect rightRect = { (int)windowSize.x - currentWidth, 0, currentWidth, (int)windowSize.y };
    FillBlack(pRenderer, rightRect);


    if (area.w > 0) SDL_RenderSetViewport(pRenderer, NULL);
    Util::RenderForcePresent(pRenderer);
    g_EffectShownAt = SDL_GetTicks();
}
