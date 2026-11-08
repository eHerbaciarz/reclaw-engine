#ifndef HUMANVIEW_H_
#define HUMANVIEW_H_

#include "CheatCode.h"
#include <SDL2/SDL.h>
#include "../SharedDefines.h"
#include "../GameApp/BaseGameApp.h"
#include "../Process/ProcessMgr.h"
#include "Console.h"
#include "GameHUD.h"

#include "UserInterface.h"
#include "../Platform/Events/AppEvent.h"

typedef std::list<shared_ptr<IScreenElement>> ScreenElementList;
typedef std::list<shared_ptr<IGameView>> GameViewList;

class LevelData;
class Scene;
class CameraNode;
class Image;
class ScreenElementScoreScreen;
class HumanView : public IGameView
{
public:
    HumanView(SDL_Renderer* renderer);
    virtual ~HumanView();

    // Interface
    virtual void VOnRender(uint32 msDiff);
    virtual void VOnLostDevice();
    virtual GameViewType VGetType() { return GameView_Human; }
    virtual uint32 VGetId() const { return m_ViewId; }
    virtual void VOnAttach(uint32 viewId, uint32 actorId) { m_ViewId = viewId; m_ActorId = actorId; }

    virtual bool VOnEvent(SDL_Event& evt);
    virtual bool VOnGamepadEvent(const AppEvent& evt);
    virtual void VOnUpdate(uint32 msDiff);

    // Virtual methods to control layering of interface elements
    virtual void VPushElement(shared_ptr<IScreenElement> element);
    virtual void VRemoveElement(shared_ptr<IScreenElement> element);

    virtual void VSetControlledActor(uint32 actorId) { m_ActorId = actorId; }

    virtual void VSetCameraOffset(int32 offsetX, int32 offsetY);

    shared_ptr<CameraNode> GetCamera() const { return m_pCamera; }
    shared_ptr<Scene> GetScene() const { return m_pScene; }

    bool EnterMenu(TiXmlElement* pMenuData);
    void LoadScoreScreen(TiXmlElement* pScoreScreenRootElem);
    bool LoadGame(TiXmlElement* pLevelXmlElem, LevelData* pLevelData);

    void RegisterConsoleCommandHandler(void(*handler)(const char*, void*), void* userdata);

    shared_ptr<Console> GetConsole() const { return m_pConsole; }

    void SetRendering(bool rendering) { m_bRendering = rendering; }
    bool IsRendering() { return m_bRendering; }
    void SetPostponeRenderPresent(bool postpone) { m_bPostponeRenderPresent = postpone; }
    // Death / teleport effects: the level as it was before Claw moved, drawn
    // under the closing black bands (the screen buffers do not keep it).
    void CaptureEffectFrame();
    bool HasEffectFrame() const { return m_pEffectFrame != nullptr; }
    void DrawEffectFrame(SDL_Renderer* pRenderer);
    void DropEffectFrame();

    void SetCurrentLevelMusic(const std::string& music) { m_CurrentLevelMusic = music; }

shared_ptr<ScreenElementMenu> GetActiveMenu() { return m_pMenu ? m_pMenu : (m_pGameOverMenu ? m_pGameOverMenu : m_pIngameMenu); }
    // Options > Graphics > aspect classic: the level's 4:3 area (renderer's
    // scaled units; empty: the whole screen). Also sizes the camera to it.
    SDL_Rect ClassicArea();
    bool IsScoreScreenVisible();

protected:
    virtual bool VLoadGameDelegate(TiXmlElement* pLevelXmlElem, LevelData* pLevelData) { VPushElement(m_pScene); return true; }

    // Delegates
    void NewHUDElementDelegate(IEventDataPtr pEventData);
    void ScoreUpdatedDelegate(IEventDataPtr pEventData);
    void LivesUpdatedDelegate(IEventDataPtr pEventData);
    void HealthUpdatedDelegate(IEventDataPtr pEventData);
    void AmmoUpdatedDelegate(IEventDataPtr pEventData);
    void AmmoTypeUpdatedDelegate(IEventDataPtr pEventData);
    void PowerupUpdatedTimeDelegate(IEventDataPtr pEventData);
    void PowerupUpdatedStatusDelegate(IEventDataPtr pEventData);
    void RequestPlaySoundDelegate(IEventDataPtr pEventData);
    void RequestResetLevelDelegate(IEventDataPtr pEventData);
    void LoadGameDelegate(IEventDataPtr pEventData);
    void SetVolumeDelegate(IEventDataPtr pEventData);
    void SoundEnabledChangedDelegate(IEventDataPtr pEventData);
    void ClawDiedDelegate(IEventDataPtr pEventData);
    void TeleportActorDelegate(IEventDataPtr pEventData);
    void EnterMenuDelegate(IEventDataPtr pEventData);
    void FinishedLevelDelegate(IEventDataPtr pEventData);
    void ActorEnteredBossAreaDelegate(IEventDataPtr pEventData);
    void BossFightEndedDelegate(IEventDataPtr pEventData);
    void IngameMenuEndGameDelegate(IEventDataPtr pEventData);
    #ifndef __EMSCRIPTEN__
    void PersistSavesToFile();
    #endif
    void ResetSaveProgressDelegate(IEventDataPtr pEventData);
    void ExportSaveDataDelegate(IEventDataPtr pEventData);
    void ImportSaveDataDelegate(IEventDataPtr pEventData);
    void StartNewGameDelegate(IEventDataPtr pEventData);
    void PlayMoviesDelegate(IEventDataPtr pEventData);
    void LoadCustomLevelDelegate(IEventDataPtr pEventData);
    void WorldFinishedLoadingDelegate(IEventDataPtr pEventData);

    // Level start: the view is revealed from left to right like in the
    // original game. Pending until the first frame of the level is drawn.
    bool m_bLevelRevealPending = false;
    uint32 m_LevelRevealStart = 0;
    void RenderLevelReveal(SDL_Renderer* pRenderer);

public:
    // Short message at the top of the screen (quick save / load feedback).
    void ShowToast(const std::string& text);
    // Abort running screen effects (death fade, teleport fade) - save states.
    void CancelScreenEffects();

    // Split-screen race: left half follows player 1, right half player 2
    // (whose controller is the second pad).
    void EnableSplitScreen(uint32 secondActorId);
    void DisableSplitScreen();
    bool IsSplitScreen() const { return m_pCamera2 != nullptr; }
    // The camera that follows this actor (player 2 has his own).
    shared_ptr<CameraNode> GetCameraForActor(uint32 actorId);
    // Whether something is on screen in any view.
    bool IsPointInAnyView(const Point& point, float cameraScale);
    bool IsRectInAnyView(const SDL_Rect& rect);

protected:
    void RenderToast(SDL_Renderer* pRenderer);
    void RenderSplitScene(SDL_Renderer* pRenderer);
    void OpenCheatsDelegate(IEventDataPtr pEventData);
    void SetSubtitlesDelegate(IEventDataPtr pEventData);
    void SetViewZoomDelegate(IEventDataPtr pEventData);
    void SetSmoothDelegate(IEventDataPtr pEventData);
    void SetAspectDelegate(IEventDataPtr pEventData);
    void SetFilterDelegate(IEventDataPtr pEventData);
    void SetCrtDelegate(IEventDataPtr pEventData);
    void OnCheatKey(CheatCode::Key key);
    bool OnMenuCheatKey(CheatCode::Key key);
    void StartRaceDelegate(IEventDataPtr pEventData);
    void BossModesDelegate(IEventDataPtr pEventData);
    void OpenLanguageDelegate(IEventDataPtr pEventData);
    void StartModeLevel(int level);
    virtual void VCreateSecondController(uint32 actorId) { }
    virtual void VDestroySecondController() { }
    shared_ptr<CameraNode> m_pCamera2;
    uint32 m_SecondActorId = 0;
    shared_ptr<IGamepadHandler> m_pGamepadHandler2;
    std::string m_ToastText;
    uint32 m_ToastShownAt = 0;
    SDL_Texture* m_pToastTexture = nullptr;
    int m_ToastW = 0, m_ToastH = 0;
    void ResetSaveProgressFromManageSavesDelegate(IEventDataPtr pEventData);

    void ToggleFullscreenDelegate(IEventDataPtr pEventData);
    void ToggleAspectRatioDelegate(IEventDataPtr pEventData);

    uint32 m_ViewId;
    uint32 m_ActorId;

    ProcessMgr* m_pProcessMgr;

    uint64 m_CurrentTick;
    uint64 m_LastDraw;
    SDL_Texture* m_pEffectFrame = nullptr;
    bool m_bCapturingEffectFrame = false;
    bool m_RunFullSpeed;

    shared_ptr<ScreenElementMenu> m_pMenu;
    shared_ptr<ScreenElementScene> m_pScene;
    shared_ptr<ScreenElementHUD> m_pHUD;
    shared_ptr<CameraNode> m_pCamera;
    shared_ptr<Console> m_pConsole;
    shared_ptr<ScreenElementMenu> m_pIngameMenu;
    shared_ptr<ScreenElementMenu> m_pGameOverMenu;

    shared_ptr<IKeyboardHandler> m_pKeyboardHandler;
    shared_ptr<IPointerHandler> m_pPointerHandler;
    shared_ptr<ITouchHandler> m_pTouchHandler;
    shared_ptr<IGamepadHandler> m_pGamepadHandler;

    ScreenElementList m_ScreenElements;

    bool m_bRendering;
    bool m_bPostponeRenderPresent;

    std::string m_CurrentLevelMusic;

    std::vector<shared_ptr<Image>> m_CursorFrames;
    int m_CursorFrameIdx;
    uint32 m_CursorAccumMs;

private:
    void RegisterAllDelegates();
    void RemoveAllDelegates();
};

// TODO: Make generic way of making new special effects
// Should be constructed from 1 parameter - struct SpecialEffectDef
// This should contain all informations about transitions (could be like 20 transitions) -
// - struct SpecialEffectTransitionDef or struct SfxTransitionDef - duration, what to do in this
// transition etc.
//
// Right now I am only interested in death and teleport special effects so this is fine albeit
// unflexible

class SpecialEffectProcess : public Process
{
public:
    virtual ~SpecialEffectProcess() { VRestoreStates(); }

    virtual void VOnSuccess() override { VRestoreStates(); }
    virtual void VOnFail() override { VRestoreStates(); }
    virtual void VOnAbort() override { VRestoreStates(); }

    virtual void VRestoreStates();
    virtual void VRender(uint32 msDiff) = 0;
};

class DeathFadeInOutProcess : public SpecialEffectProcess
{
public:
    enum DeathFadeState
    {
        DeathFadeState_Started,
        DeathFadeState_FadingIn,
        DeathFadeState_FadingOut,
        DeathFadeState_Ended
    };

    DeathFadeInOutProcess(Point epicenter, int fadeInDuration, int fadeOutDuration, int startDelay, int endDelay);
    virtual ~DeathFadeInOutProcess();

    virtual void VOnInit() override;
    virtual void VOnUpdate(uint32 msDiff) override;
    virtual void VRender(uint32 msDiff) override;

private:
    Point m_Epicenter;
    int m_StartDelay;
    int m_FadeInDuration;
    int m_FadeOutDuration;
    int m_EndDelay;

    DeathFadeState m_DeathFadeState;
    int m_CurrentTime;

    Point m_FadeInSpeed;
    Point m_FadeOutSpeed;
};

class PrimeSearch;
class FadingLine
{
public:
    FadingLine(int length, Point fragmentSize, int fadeDelay, int fadeDuration, bool isFadingIn);
    ~FadingLine();

    void Update(uint32 msDiff);
    void Reset(int fadeDelay, bool isFadingIn);

    void Activate() { m_bIsActive = true; }
    bool IsDone() { return m_bIsDone; }

    void Render(SDL_Renderer* pRenderer, SDL_Texture* pFragmentTexture, Point& lineOffset, bool asRow);

private:
    int m_Length;
    Point m_FragmentSize;
    int m_FadeDuration;
    int m_FadeDelay;
    bool m_bIsFadingIn;

    int m_CurrentTime;
    int m_SingleFragmentFadeTime;

    bool m_bIsActive;
    bool m_bIsDone;
    int m_FragmentCount;
    unique_ptr<PrimeSearch> m_pPrimeSearch;
    std::vector<bool> m_FadedFragments;
};

class TeleportFadeInOutProcess : public SpecialEffectProcess
{
public:
    enum TeleportState
    {
        TeleportState_FadingIn,
        TeleportState_FadingOut,
    };

    TeleportFadeInOutProcess(int fadeInDuration, int fadeOutDuration);
    virtual ~TeleportFadeInOutProcess();

    virtual void VOnInit() override;
    virtual void VOnUpdate(uint32 msDiff) override;
    virtual void VRender(uint32 msDiff) override;

private:
    int m_FadeInDuration;
    int m_FadeOutDuration;
    Point m_FadeInSpeed;
    Point m_FadeOutSpeed;

    TeleportState m_TeleportState;
    int m_CurrentTime;

    Point m_FragmentSize;
    SDL_Texture* m_pFadingTexture;
    std::vector<shared_ptr<FadingLine>> m_Lines;
};

#endif