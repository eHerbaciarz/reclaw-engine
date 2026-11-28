#include "../Util/MemStats.h"
#include "../Platform/Platform.h"
#include "../PortFeatures.h"
#include <map>
#include <cstdio>
#include "../Actor/ActorFactory.h"
#include "../UserInterface/HumanView.h"
#include "../Util/StartupTrace.h"
#include "../Util/PortSettings.h"
#include "../Events/Events.h"
#include "../Resource/Loaders/XmlLoader.h"
#include "../Resource/Loaders/PalLoader.h"
#include "../Resource/Loaders/WwdLoader.h"
#include "../Resource/Loaders/PcxLoader.h"
#include "../Resource/Loaders/PidLoader.h"
#include "../Resource/Loaders/WavLoader.h"
#include "../Events/EventMgr.h"
#include "../Graphics2D/Image.h"
#include "../Audio/Audio.h"
#include "../Video/Generic/IGenericVideo.h"

#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/PickupComponents/PickupComponent.h"
#include "../Actor/Components/LootComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include "../Actor/Components/ControllerComponents/ScoreComponent.h"
#include "../Actor/Components/ControllerComponents/LifeComponent.h"
#include "../Actor/Components/ControllerComponents/AmmoComponent.h"
#include "../Actor/Components/ControllableComponent.h"

#include "../Util/Converters.h"
#include "../Util/ClawLevelUtil.h"

#include "GameSaves.h"
#include "BaseGameLogic.h"
#include "Difficulty.h"
#include "../UserInterface/SecondScreen.h"
#include "GameSaves.h"
#include "HapticFeedback.h"
#ifdef __EMSCRIPTEN__
#include "SaveBridge.h"
#endif
#include "../Video/Native/MoviePlayer.h"
#include "../UserInterface/TitleScroll.h"
#include "../SaveState/SaveStateManager.h"
#include "../Race/Race.h"
#include "../BossModes/BossModes.h"
#include "../Platform/Platform.h"
#include "../UserInterface/Minimap.h"

#include "../Physics/ClawPhysics.h"

#include <algorithm>
#include <fstream>
//#include <thread>
#include <iostream>

//=================================================================================================
//
// BaseGameLogic implementation
//
//=================================================================================================

BaseGameLogic::BaseGameLogic()
{
    m_Lifetime = 0;
    m_pProcessMgr = NULL;
    m_LastActorId = 0;
    m_GameState = GameState_Initializing;
    m_HumanPlayersAttached = 0;
    m_AIPlayersAttached = 0;
    m_HumanGamesLoaded = 0;
    m_pActorFactory = NULL;
    m_Proxy = false;
    m_RenderDiagnostics = true;
    m_SelectedLevel = -1;
    m_bRunning = true;

    m_pGameSaveMgr.reset(new GameSaveMgr());

    //RegisterEngineScriptEvents();
    RegisterAllDelegates();
}

BaseGameLogic::~BaseGameLogic()
{
    while (!m_GameViews.empty())
    {
        m_GameViews.pop_front();
    }

    SAFE_DELETE(m_pProcessMgr);
    SAFE_DELETE(m_pActorFactory);

    // Destroy all actors
    for (auto &actorIter : m_ActorMap)
    {
        actorIter.second->Destroy();
    }
    m_ActorMap.clear();

    RemoveAllDelegates();
}


// Writes the saves file. 3DS: in a thread of its own (writing it to the SD
// card took ~100 ms: a stutter at every checkpoint flag); the text is made
// here, writes keep their order.
static void WriteSavesFile(TiXmlDocument& doc, const std::string& path)
{
#if defined(__3DS__)
    TiXmlPrinter printer;
    doc.Accept(&printer);
    struct Job { std::string path, text; };
    static SDL_mutex* s_pLock = SDL_CreateMutex();
    Job* job = new Job{ path, std::string("<?xml version=\"1.0\" ?>\n") + printer.CStr() };
    auto run = [](void* p) -> int {
        Job* j = (Job*)p;
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        SDL_LockMutex(s_pLock);
        const std::string tmp = j->path + ".tmp";
        if (FILE* f = fopen(tmp.c_str(), "wb"))
        {
            const bool ok = fwrite(j->text.data(), 1, j->text.size(), f) == j->text.size();
            fclose(f);
            if (ok) { std::remove(j->path.c_str()); std::rename(tmp.c_str(), j->path.c_str()); }
        }
        SDL_UnlockMutex(s_pLock);
        delete j;
        return 0;
    };
    if (SDL_Thread* t = SDL_CreateThread(run, "saves", job)) { SDL_DetachThread(t); return; }
    run(job);
#else
    doc.SaveFile(path.c_str());
#endif
}

bool BaseGameLogic::Initialize()
{
    m_pActorFactory = VCreateActorFactory();

    if (!m_pGameSaveMgr->IsSaveSupported()) {
        return m_pGameSaveMgr->Initialize(nullptr);
    }

#ifdef __EMSCRIPTEN__
    // Load saves from localStorage
    std::string jsonData = SaveBridge::LoadFromIndexedDB();
    if (!jsonData.empty()) {
        LOG("Loading saves from IndexedDB...");
        if (m_pGameSaveMgr->InitializeFromJson(jsonData)) {
            LOG("Successfully loaded saves from IndexedDB");
            return true;
        } else {
            LOG_WARNING("Failed to parse IndexedDB save data, starting fresh");
        }
    } else {
        LOG("No save data found in IndexedDB, starting fresh game");
    }

    // No save data found - initialize with just level 1
    LevelSave level1Save;
    level1Save.levelNumber = 1;
    level1Save.levelName = "La Roca";
    CheckpointSave startCheckpoint;
    startCheckpoint.checkpointIdx = 0;
    startCheckpoint.score = 0;
    startCheckpoint.health = 100;
    startCheckpoint.lives = Difficulty::StartLives();
    startCheckpoint.bulletCount = 10;
    startCheckpoint.magicCount = 5;
    startCheckpoint.dynamiteCount = 3;
    level1Save.checkpointMap[0] = startCheckpoint;
    m_pGameSaveMgr->AddLevelEntry(1, "La Roca");
    m_pGameSaveMgr->AddCheckpointSave(1, startCheckpoint);

    // Save initial state to IndexedDB
    SaveBridge::SaveToIndexedDB(m_pGameSaveMgr->ToJson());

    return true;
#else
    return ReloadSaves();
#endif
}

// The saves file of the chosen difficulty (Options > Difficulty)
bool BaseGameLogic::ReloadSaves()
{
#ifdef __EMSCRIPTEN__
    return true;
#else
    if (!m_pGameSaveMgr->IsSaveSupported()) {
        return m_pGameSaveMgr->Initialize(nullptr);
    }
    STARTUP_TRACE(std::string("saves: difficulty ") + Difficulty::Name(Difficulty::Get()));

    // Native platform: load from XML file
    std::string savesFilePath = g_pApp->GetGameConfig()->userDirectory + Difficulty::SavesFile(g_pApp->GetGameConfig()->savesFile);

    TiXmlDocument gameSaves(savesFilePath.c_str());
    gameSaves.LoadFile();
    bool startFresh = false;
    if (gameSaves.Error())
    {
        if (gameSaves.ErrorId() == TiXmlBase::TIXML_ERROR_OPENING_FILE)
        {
            // First launch: no saves yet (the web build starts the same way).
            LOG_WARNING("No saves file at " + savesFilePath + ", starting a new game");
        }
        else
        {
            // A damaged saves file must not keep the game from starting; keep
            // it next to the new one so progress can be recovered by hand.
            LOG_ERROR("Error while loading " + savesFilePath + ": " + std::string(gameSaves.ErrorDesc())
                      + " - moved to " + savesFilePath + ".bak, starting a new game");
            std::remove((savesFilePath + ".bak").c_str());
            std::rename(savesFilePath.c_str(), (savesFilePath + ".bak").c_str());
        }
        startFresh = true;
    }
    else if (!m_pGameSaveMgr->Initialize(gameSaves.RootElement()))
    {
        LOG_ERROR("Saves file " + savesFilePath + " contains no usable saves, starting a new game");
        startFresh = true;
    }

    if (startFresh)
    {
        if (!m_pGameSaveMgr->IsSaveSupported() || g_pApp->GetGlobalOptions()->loadAllLevelSaves)
        {
            return m_pGameSaveMgr->Initialize(nullptr);
        }
        m_pGameSaveMgr->ResetToNewGame();
        STARTUP_TRACE("saves: new game");
        TiXmlDocument newSaves;
        newSaves.LinkEndChild(m_pGameSaveMgr->ToXml());
        if (!newSaves.SaveFile(savesFilePath.c_str()))
        {
            LOG_WARNING("Could not create " + savesFilePath + " - progress will not be saved");
        }
        STARTUP_TRACE("saves: file written");
    }

    return true;
#endif
}

std::string BaseGameLogic::GetActorXml(uint32 actorId)
{
    StrongActorPtr pActor = MakeStrongPtr(VGetActor(actorId));
    if (pActor)
    {
        return pActor->ToXML();
    }
    else
    {
        LOG_ERROR("Could not find actor: " + ToStr(actorId));
    }

    return "";
}

bool BaseGameLogic::VEnterMenu(const char* xmlMenuResource)
{
    TiXmlElement* pXmlLevelRoot = nullptr;
    PidResourceLoader::ClearKept();   // (3DS: the level's pictures)
    
#ifdef __EMSCRIPTEN__
    // For WASM builds, try to load from individual files first for better lazy loading
    std::string localFilePath = "/Build_Release/" + std::string(xmlMenuResource);
    pXmlLevelRoot = XmlResourceLoader::LoadAndReturnRootXmlElement(localFilePath.c_str(), true);
    if (pXmlLevelRoot != NULL)
    {
        LOG("Loaded menu from individual file: " + localFilePath);
    }
    else
    {
        LOG("Individual file not found, falling back to resource cache: " + localFilePath);
    }
#endif

    // Fallback to resource cache if individual file loading failed or not WASM
    if (pXmlLevelRoot == NULL)
    {
        pXmlLevelRoot = XmlResourceLoader::LoadAndReturnRootXmlElement(xmlMenuResource);
    }
    
    if (pXmlLevelRoot == NULL)
    {
        LOG_ERROR("Could not load menu resource file: " + std::string(xmlMenuResource));
        return false;
    }

    for (auto pGameView : m_GameViews)
    {
        if (pGameView->VGetType() == GameView_Human)
        {
            shared_ptr<HumanView> pHumanView = static_pointer_cast<HumanView>(pGameView);
            const Uint32 t0 = SDL_GetTicks();
            if (!pHumanView->EnterMenu(pXmlLevelRoot))
            {
                return false;
            }
            LOG_WARNING(std::string("menu ") + xmlMenuResource + " built in " + std::to_string(SDL_GetTicks() - t0) + " ms");
        }
    }

    return true;
}

void BaseGameLogic::VPlayCutscene(const std::string& videoSource)
{
    LOG("Playing cutscene: " + videoSource);
    m_pActiveVideo = g_pApp->GetVideoModule()->CreateVideoPlayer();
    if (m_pActiveVideo)
    {
        if (m_pActiveVideo->Load(videoSource))
        {
            m_pActiveVideo->Play();
            VChangeState(GameState_Cutscene);
        }
        else
        {
            LOG_ERROR("Failed to load video: " + videoSource);
            m_pActiveVideo.reset();
        }
    }
}

// Trace builds: time spent in game processes [0], physics [1], game views
// and actors [2] (reported with the fps in BaseGameApp).
Uint64 g_PerfLogicTicks[4] = { 0, 0, 0, 0 };
Uint64 g_PerfRenderTicks[12] = { 0 };   // render parts (BaseGameApp's perf line)
int g_PerfTilesDrawn = 0;
std::map<std::string, int> g_PerfSoundPlays;
Uint64 g_PerfEventTicks = 0;   // event queue time   // sounds played (BaseGameApp's perf line)

// The level's own 4:3 loading screen from CLAW.REZ (localized: Polish text in
// the Polish release), drawn over the middle of the widescreen artwork from
// ASSETS.ZIP so the side bars are filled with matching graphics.
static shared_ptr<Image> s_pLoadingScreenOverlay;

static shared_ptr<Image> LoadOriginalLoadingScreen(const std::string& path)
{
    shared_ptr<ResourceCache> pRezCache = g_pApp->GetResourceCache();
    if (!pRezCache)
    {
        return nullptr;
    }
    Resource resource(path);
    shared_ptr<ResourceHandle> handle = pRezCache->GetHandle(&resource);
    if (!handle || !handle->GetDataBuffer())
    {
        return nullptr;
    }
    shared_ptr<PcxResourceExtraData> extraData = std::static_pointer_cast<PcxResourceExtraData>(handle->GetExtraData());
    if (!extraData)
    {
        extraData.reset(new PcxResourceExtraData());
        extraData->LoadImage(handle->GetDataBuffer(), handle->GetSize(), false, SDL_Color{ 0, 0, 0, 0 });
        if (!extraData->GetImage())
        {
            return nullptr;
        }
        handle->SetExtraData(extraData);
    }
    shared_ptr<Image> pImage = extraData->GetImage();
    // Only a classic 4:3 screen is useful as an overlay.
    if (!pImage || !pImage->GetTexture() || pImage->GetWidth() > 700)
    {
        return nullptr;
    }
    return pImage;
}

void RenderLoadingScreen(shared_ptr<Image> pBackground, SDL_Rect& renderRect, Point& scale, float progress)
{
    // While we are at it, eat incoming events
    SDL_Event evt;
    while (SDL_PollEvent(&evt))
    {
        g_pApp->OnEvent(evt);
    }

    SDL_Renderer* pRenderer = g_pApp->GetRenderer();

    // The layout below is computed in window pixels (as in the browser build,
    // which always renders at scale 1). Native configs may use a render scale
    // (Switch: 1.5), so draw unscaled and restore the scale afterwards.
    float savedScaleX = 1.0f, savedScaleY = 1.0f;
    SDL_RenderGetScale(pRenderer, &savedScaleX, &savedScaleY);
    SDL_RenderSetScale(pRenderer, 1.0f, 1.0f);

    // Clear screen with black (for widescreen letterboxing)
    SDL_SetRenderDrawColor(pRenderer, 0, 0, 0, 255);
    SDL_RenderClear(pRenderer);

    Point windowSize = g_pApp->GetWindowSize();
    const double DESIGN_HEIGHT = 480.0;
    double uniformScale = windowSize.y / DESIGN_HEIGHT;

    // Check if background is widescreen (wider than standard 640px)
    int bgWidth = pBackground->GetWidth();
    int bgHeight = pBackground->GetHeight();
    bool isWidescreenBackground = (bgWidth > 700);

    SDL_Rect backgroundDst;
    int contentOffsetX;

    if (isWidescreenBackground)
    {
        // Widescreen background: scale to fill height, center horizontally
        double bgScale = windowSize.y / (double)bgHeight;
        int scaledWidth = (int)(bgWidth * bgScale);
        int scaledHeight = (int)(bgHeight * bgScale);
        backgroundDst.x = (int)((windowSize.x - scaledWidth) / 2.0);
        backgroundDst.y = 0;
        backgroundDst.w = scaledWidth;
        backgroundDst.h = scaledHeight;
        // Center content in the middle of the screen
        contentOffsetX = (int)(windowSize.x / 2.0 - 640.0 * uniformScale / 2.0);
    }
    else
    {
        // Standard 4:3 background: center in widescreen
        const double DESIGN_WIDTH = 640.0;
        contentOffsetX = (int)((windowSize.x - DESIGN_WIDTH * uniformScale) / 2.0);
        backgroundDst.x = contentOffsetX;
        backgroundDst.y = 0;
        backgroundDst.w = (int)(DESIGN_WIDTH * uniformScale);
        backgroundDst.h = (int)(DESIGN_HEIGHT * uniformScale);
    }

    // Render background
    SDL_Rect backgroundSrc = { 0, 0, bgWidth, bgHeight };
    SDL_RenderCopy(pRenderer, pBackground->GetTexture(), &backgroundSrc, &backgroundDst);

    if (isWidescreenBackground && s_pLoadingScreenOverlay)
    {
        // The side edges fade into the widescreen artwork instead of ending
        // in a hard seam: the outer columns are drawn one by one with rising
        // opacity.
        SDL_Texture* pTexture = s_pLoadingScreenOverlay->GetTexture();
        const int srcW = s_pLoadingScreenOverlay->GetWidth();
        const int srcH = s_pLoadingScreenOverlay->GetHeight();
        const double sx = (640.0 * uniformScale) / srcW;
        const int dstH = (int)(DESIGN_HEIGHT * uniformScale);
        const int fade = std::min(32, srcW / 4);
        auto dstX = [&](int col) { return contentOffsetX + (int)(col * sx); };

        // The widescreen artwork has the English level name in the middle,
        // and long names reach under the faded edges (THE DARK WOODS...).
        // Under the edges, show the artwork from just outside the 4:3 area,
        // mirrored, so the fade goes into plain parchment.
        {
            const double bgScale = backgroundDst.h / (double)bgHeight;
            const int L = dstX(0), R = dstX(srcW);
            const int fw = dstX(fade) - L;
            const int fwArt = std::max(1, (int)(fw / bgScale + 0.5));
            const int artL = (int)((L - backgroundDst.x) / bgScale);
            const int artR = (int)((R - backgroundDst.x) / bgScale);
            SDL_Rect srcL = { std::max(0, artL - fwArt), 0, fwArt, bgHeight };
            SDL_Rect srcR = { std::min(bgWidth - fwArt, artR), 0, fwArt, bgHeight };
            SDL_Rect dstLeft = { L, backgroundDst.y, fw, backgroundDst.h };
            SDL_Rect dstRight = { R - fw, backgroundDst.y, fw, backgroundDst.h };
            SDL_RenderCopyEx(pRenderer, pBackground->GetTexture(), &srcL, &dstLeft, 0.0, NULL, SDL_FLIP_HORIZONTAL);
            SDL_RenderCopyEx(pRenderer, pBackground->GetTexture(), &srcR, &dstRight, 0.0, NULL, SDL_FLIP_HORIZONTAL);
        }

        SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
        SDL_Rect src = { fade, 0, srcW - 2 * fade, srcH };
        SDL_Rect dst = { dstX(fade), 0, dstX(srcW - fade) - dstX(fade), dstH };
        SDL_SetTextureAlphaMod(pTexture, 255);
        SDL_RenderCopy(pRenderer, pTexture, &src, &dst);
        for (int i = 0; i < fade; i++)
        {
            SDL_SetTextureAlphaMod(pTexture, (Uint8)(255 * (i + 1) / (fade + 1)));
            int cols[2] = { i, srcW - 1 - i };
            for (int col : cols)
            {
                SDL_Rect s1 = { col, 0, 1, srcH };
                SDL_Rect d1 = { dstX(col), 0, std::max(1, dstX(col + 1) - dstX(col)), dstH };
                SDL_RenderCopy(pRenderer, pTexture, &s1, &d1);
            }
        }
        SDL_SetTextureAlphaMod(pTexture, 255);
    }

    // Progress bar (centered within the 4:3 content area)
    int progressFullLength = (int)(640.0 * uniformScale / 2);
    int progressCurrLength = (int)((progressFullLength * progress) / 100.0f);
    int progressHeight = (int)(3 * uniformScale);
    int progressX = contentOffsetX + (int)(640.0 * uniformScale / 4);
    int progressY = (int)(DESIGN_HEIGHT * uniformScale * 0.75);

    SDL_Rect totalProgressBarRect = { progressX, progressY, progressFullLength, progressHeight };
    SDL_Rect remainingProgressBarRect = { progressX, progressY, progressCurrLength, progressHeight };

    SDL_Texture* pTotalProgressBar = Util::CreateSDLTextureRect(
        progressFullLength, progressHeight, COLOR_BLACK, pRenderer);
    SDL_Texture* pRemainingProgressBar = Util::CreateSDLTextureRect(
        remainingProgressBarRect.w, remainingProgressBarRect.h, COLOR_RED, pRenderer);

    SDL_RenderCopy(pRenderer, pTotalProgressBar, NULL, &totalProgressBarRect);
    SDL_RenderCopy(pRenderer, pRemainingProgressBar, NULL, &remainingProgressBarRect);

    Util::RenderForcePresent(pRenderer);

    SDL_DestroyTexture(pTotalProgressBar);
    SDL_DestroyTexture(pRemainingProgressBar);

    SDL_RenderSetScale(pRenderer, savedScaleX, savedScaleY);
}

#include "../Resource/ResourceMgr.h"

#ifdef OPENCLAW_STARTUP_TRACE
#if !defined(__PS4__)
#include <malloc.h>
#endif
#if defined(__3DS__)
#include <3ds.h>
#endif
// Level loading steps with the memory in use (3DS / Vita: is it running out?)
static void TraceLevel(const char* step)
{
#if defined(__PS4__)
    struct { int uordblks; } mi = { 0 };   // (no mallinfo in the PS4 libc)
#else
    struct mallinfo mi = mallinfo();
#endif
    char line[200];
#if defined(__3DS__)
    snprintf(line, sizeof(line), "level: %s (heap used %d KB, free in app region %lu KB, linear free %lu KB)", step,
             (int)(mi.uordblks / 1024), (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024),
             (unsigned long)(linearSpaceFree() / 1024));
#else
    snprintf(line, sizeof(line), "level: %s (heap used %d KB)", step, (int)(mi.uordblks / 1024));
#endif
    STARTUP_TRACE(line);
    STARTUP_TRACE(std::string("  memory: ") + MemStats::Report());
}
#else
#define TraceLevel(step) do {} while (0)
#endif

bool BaseGameLogic::VLoadGame(const char* xmlLevelResource)
{
    // Save states belong to one level session.
    SaveStateManager::Get()->Reset();
    // The level map is rebuilt from the new level's collision tiles.
    Minimap::Reset();

    // Stop all audio
    g_pApp->GetAudio()->StopAllSounds();

    m_pPhysics.reset(CreateClawPhysics());

    {
        // Options > Graphics > filter changed: the pictures made again
        static int s_LoadedFilter = -1;
        if (s_LoadedFilter >= 0 && s_LoadedFilter != Image::Filter())
        {
            PidResourceLoader::ClearKept();
            g_pApp->GetResourceMgr()->VFlush();
        }
        s_LoadedFilter = Image::Filter();
    }

#if defined(__3DS__) || defined(__PSL1GHT__) || defined(RECLAW_PS2) || defined(RECLAW_DREAMCAST)
    // 3DS, PS3, PS2, Dreamcast memory: the menu's images and files (and the decoded pictures they
    // keep) leave the resource caches before the level is built.
    PidResourceLoader::ClearKept();
    WavResourceLoader::ClearKept();   // (all sounds were stopped above)
    g_pApp->GetResourceCache()->ClearKeptFiles();
    g_pApp->GetResourceMgr()->VFlush();
    TraceLevel("caches flushed");
#endif

    float loadingProgress = 0.0f;
    float lastProgress = 0.0f;

    std::string levelPath = "/LEVEL" + ToStr(m_pCurrentLevel->GetLevelNumber()) + "/*";

    // ============== LOADING SCREEN RENDERING ==============

    // Start rendering the loading screen
    Point windowSize = g_pApp->GetWindowSize();
    Point scale = g_pApp->GetScale();
    int targetWidth = (int)(windowSize.x / scale.x);
    int targetHeight = (int)(windowSize.y / scale.y);
    SDL_Rect backgroundRect = { 0, 0, targetWidth, targetHeight };
    // Background loading image
    std::string backgroundPath = "/LEVEL" + ToStr(m_pCurrentLevel->m_LeveNumber) + "/SCREENS/LOADING.PCX";
    shared_ptr<Image> pBackgroundImage = PcxResourceLoader::LoadAndReturnImage(backgroundPath.c_str());
    assert(pBackgroundImage != nullptr);
    assert(pBackgroundImage->GetTexture() != NULL);
    s_pLoadingScreenOverlay = LoadOriginalLoadingScreen(backgroundPath);

    RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, loadingProgress);

    // Preload the level's pictures and sounds - after the loading screen is
    // up (it used to happen before it: the menu froze for seconds on the
    // handhelds), with the bar moving.
    STARTUP_TRACE("level: preload");
#if !defined(__3DS__) && !defined(__PSL1GHT__) && !defined(RECLAW_PS2) && !defined(RECLAW_DREAMCAST)
    // (3DS: the level does not fit the small cache anyway - no preload;
    // PS3: all of the level's pictures took 73 MB - the game ran out of memory;
    // PS2 / Dreamcast: the level's sounds alone ran the memory out)
    {
        std::vector<std::string> files = g_pApp->GetResourceCache()->Match(levelPath);
        for (size_t i = 0; i < files.size(); ++i)
        {
            Resource resource(files[i]);
            g_pApp->GetResourceCache()->GetHandle(&resource);
            if (i % 40 == 0)
            {
                RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, 10.0f * i / files.size());
            }
        }
    }
#endif
    TraceLevel("preload done");

    // ============== LEVEL LOADING ==============

    WapWwd* pWwd = NULL;
    if (m_pCurrentLevel->IsCustomLevel())
    {
        // Custom level: the WWD comes from a file, the assets from the REZ.
        static WapWwd* s_pCustomWwd = NULL;
        if (s_pCustomWwd)
        {
            WAP_WwdDestroy(s_pCustomWwd);
            s_pCustomWwd = NULL;
        }
        std::ifstream file(m_pCurrentLevel->GetCustomWwdPath(), std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!data.empty())
        {
            s_pCustomWwd = WAP_WwdLoadFromData(data.data(), (uint32_t)data.size());
        }
        pWwd = s_pCustomWwd;
        if (pWwd == NULL)
        {
            LOG_ERROR("Could not load custom level: " + m_pCurrentLevel->GetCustomWwdPath());
            return false;
        }
    }
    else
    {
        pWwd = WwdResourceLoader::LoadAndReturnWwd(xmlLevelResource);
    }
    assert(pWwd != NULL);
    TraceLevel(pWwd ? "WWD loaded" : "WWD FAILED");

    TiXmlElement* pXmlLevelRoot = WwdToXml(pWwd, m_pCurrentLevel->GetLevelNumber());
    TraceLevel("WWD converted");
    if (pXmlLevelRoot == NULL)
    {
        LOG_ERROR("Could not load level resource file: " + std::string(xmlLevelResource));
        return false;
    }

    // Get level palette
    TiXmlElement* pLevelProperties = pXmlLevelRoot->FirstChildElement("LevelProperties");
    if (!pLevelProperties)
    {
        LOG_ERROR("Level does not have level properties node.");
        return false;
    }

    if (TiXmlElement* pLevelNameElem = pLevelProperties->FirstChildElement("LevelName"))
    {
        m_pCurrentLevel->m_LevelName = pLevelNameElem->GetText();
    }
    if (TiXmlElement* pLevelAuthorElem = pLevelProperties->FirstChildElement("Author"))
    {
        m_pCurrentLevel->m_LevelAuthor = pLevelAuthorElem->GetText();
    }
    if (TiXmlElement* pLevelCreatedDateElem = pLevelProperties->FirstChildElement("Created"))
    {
        m_pCurrentLevel->m_LevelCreatedDate = pLevelCreatedDateElem->GetText();
    }

    // Tile descriptions
    if (TiXmlElement* pTileDescRootElem = pLevelProperties->FirstChildElement("TileDescriptions"))
    {
        for (TiXmlElement* pTileDescElem = pTileDescRootElem->FirstChildElement("TileDescription");
            pTileDescElem; pTileDescElem = pTileDescElem->NextSiblingElement("TileDescription"))
        {

            // Maybe code more deffensively here, revisit in future probably.. right now I dont want to
            // add 10000 conditions to assert correct xml format

            // TileDescription will maybe be used by editor, it is not used directly by game
            //    but only to parse TileCollisionPrototype from it which is used by physics subsystem
            TileDescription tileDesc;
            tileDesc.tileId = std::stoi(pTileDescElem->FirstChildElement("TileId")->GetText());

            TiXmlElement* pTileSizeElem = pTileDescElem->FirstChildElement("Size");
            tileDesc.width = std::stoi(pTileSizeElem->Attribute("width"));
            tileDesc.height = std::stoi(pTileSizeElem->Attribute("height"));

            if (std::string(pTileDescElem->FirstChildElement("Type")->GetText()) == "single")
            {
                tileDesc.type = WAP_TILE_TYPE_SINGLE;
            }
            else
            {
                tileDesc.type = WAP_TILE_TYPE_DOUBLE;
                tileDesc.outsideAttrib = std::stoi(pTileDescElem->FirstChildElement("OutsideAttrib")->GetText());
            }
            tileDesc.insideAttrib = std::stoi(pTileDescElem->FirstChildElement("InsideAttrib")->GetText());

            TiXmlElement* pTileRectElem = pTileDescElem->FirstChildElement("TileRect");
            tileDesc.rect.left = std::stoi(pTileRectElem->Attribute("left"));
            tileDesc.rect.top = std::stoi(pTileRectElem->Attribute("top"));
            tileDesc.rect.right = std::stoi(pTileRectElem->Attribute("right"));
            tileDesc.rect.bottom = std::stoi(pTileRectElem->Attribute("bottom"));

            m_pCurrentLevel->m_TileDescriptionMap.insert(std::make_pair(tileDesc.tileId, tileDesc));

            // This structure is actually used in game in order to prevent recalculating the collision rects
            //    over and over again
            TileCollisionPrototype tileProto;
            tileProto.id = tileDesc.tileId;
            tileProto.width = tileDesc.width;
            tileProto.height = tileDesc.height;
            Util::ParseCollisionRectanglesFromTile(&tileProto, &tileDesc);

            m_pCurrentLevel->m_TileCollisionPrototypeMap.insert(std::make_pair(tileProto.id, tileProto));
        }
    }
    else
    {
        assert(false && "Tile descriptions element not found.");
    }

    loadingProgress = 10.0f;
    TraceLevel("tiles described");
    RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, loadingProgress);

    // Get number of actors to estimate loading progress
    int numActors = 0;
    for (TiXmlElement* pActorElem = pXmlLevelRoot->FirstChildElement("Actor");
        pActorElem != NULL;
        pActorElem = pActorElem->NextSiblingElement("Actor"), numActors++);

    // Leave 90% for actor's processing
    float actorToPercent = (100.0f - loadingProgress - 5.0f) / (float)numActors;

    std::string palettePath = pLevelProperties->FirstChildElement("Palette")->GetText();
    std::replace(palettePath.begin(), palettePath.end(), '\\', '/');
    g_pApp->SetCurrentPalette(PalResourceLoader::LoadAndReturnPal(palettePath.c_str()));

    uint32 clawId = -1;
    for (TiXmlElement* pActorElem = pXmlLevelRoot->FirstChildElement("Actor"), *pNextActorElem = nullptr;
        pActorElem != NULL;
        pActorElem = pNextActorElem)
    {
        pNextActorElem = pActorElem->NextSiblingElement("Actor");
        //LOG("Creating actor: " + std::string(pActorElem->Attribute("Type")));
        //if (std::string(pActorElem->Attribute("Type")) != "Plane") break;
        StrongActorPtr pActor;
        const char* pActorType = pActorElem->Attribute("Type");
        try
        {
            pActor = VCreateActor(pActorElem, NULL);
        }
        catch (const std::exception& e)
        {
            // One broken actor must not end the game: say which and go on
            LOG_ERROR(std::string("Exception while creating actor ") + (pActorType ? pActorType : "?") + ": " + e.what());
            Platform::FlushLog();
            continue;
        }
#ifdef OPENCLAW_STARTUP_TRACE
        {
            static int s_created = 0;
            if (++s_created % 250 == 0)
            {
                char step[64];
                snprintf(step, sizeof(step), "%d actors", s_created);
                TraceLevel(step);
            }
        }
#endif
        if (pActor)
        {
            shared_ptr<EventData_New_Actor> pNewActorEvent(new EventData_New_Actor(pActor->GetGUID()));
            IEventMgr::Get()->VQueueEvent(pNewActorEvent);

            // Get Claw's GUID
            if (pActor->GetName() == "Claw")
            {
                assert(clawId == -1 && "Multiple Captain Claws in this level - not supported at this time !");
                clawId = pActor->GetGUID();
            }
        }
        else
        {
            LOG_ERROR(std::string("Could not create actor ") + (pActorType ? pActorType : "?"));
            return false;
        }

#if defined(__3DS__)
        // 3DS memory: the actor's XML is not needed once it exists (the
        // level's XML is ~20 MB, more than is left for the actors).
        pXmlLevelRoot->RemoveChild(pActorElem);
#endif

        loadingProgress += actorToPercent;
        if ((loadingProgress - lastProgress) > 1.0f)
        {
            RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, loadingProgress);
            lastProgress = loadingProgress;
        }
    }

    // Load game save data
    // Custom levels start with new-game stats and never use the save game.
    CheckpointSave customStart;
    customStart.checkpointIdx = 0;
    customStart.score = 0;
    customStart.health = 100;
    customStart.lives = 6;
    customStart.bulletCount = 10;
    customStart.magicCount = 5;
    customStart.dynamiteCount = 3;
    // Boss practice / Boss Rush: their own stats, the save game is not used.
    if (BossModes::IsActive())
    {
        BossModes::FillStartStats(customStart);
    }
    const CheckpointSave* pCheckpointSave = (m_pCurrentLevel->IsCustomLevel() || BossModes::IsActive()) ? &customStart :
        m_pGameSaveMgr->GetCheckpointSave(m_pCurrentLevel->m_LeveNumber, m_pCurrentLevel->m_LoadedCheckpoint);
    if (pCheckpointSave == NULL)
    {
        // A level the save game has not reached (e.g. started from a test
        // script): fresh stats instead of a crash.
        LOG_WARNING("No checkpoint save for level " + ToStr(m_pCurrentLevel->m_LeveNumber) + ", fresh start");
        pCheckpointSave = &customStart;
    }

    loadingProgress = 95.0f;
    TraceLevel("actors created");
    if (Image::Filter() != 0)
        LOG_WARNING("Pixel filter " + ToStr(Image::Filter()) + ": " + ToStr(Image::FilteredCount()) + " pictures made with it so far");
    RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, loadingProgress);

#if defined(__3DS__)
    // 3DS: the level's pictures and sounds read now, while the loading screen
    // is up, as far as the memory allows. During play every picture or sound
    // used for the first time (an enemy dying, a shot) was read from the SD
    // card then: a stutter each time. They stay loaded for the level
    // (PidResourceLoader / WavResourceLoader keep them).
    {
        const std::string level = "/LEVEL" + ToStr(m_pCurrentLevel->GetLevelNumber());
        // (pictures are not preloaded: keeping them all ran the GPU memory out,
        // and they were not what stuttered)
        std::vector<std::string> images, sounds;
        // (the level's own first: its enemies; the game's folder holds cheat
        // and curse sounds too, so it comes last)
        for (const std::string& set : { level + "/SOUNDS/*", std::string("/CLAW/SOUNDS/*"), std::string("/GAME/SOUNDS/*") })
        {
            std::vector<std::string> m = g_pApp->GetResourceCache()->Match(set);
            sounds.insert(sounds.end(), m.begin(), m.end());
        }
        const size_t total = images.size() + sounds.size();
        size_t done = 0, imagesLoaded = 0, soundsLoaded = 0;
        bool imagesStopped = false, soundsStopped = false;
        for (const std::string& path : images)
        {
            ++done;
            if (path.size() < 4 || path.compare(path.size() - 4, 4, ".pid") != 0) continue;
            if (linearSpaceFree() < 10u * 1024 * 1024) { imagesStopped = true; break; }
            if (PidResourceLoader::LoadAndReturnImage(path.c_str(), g_pApp->GetCurrentPalette())) ++imagesLoaded;
            if (done % 60 == 0)
                RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, 95.0f + 4.0f * done / std::max<size_t>(1, total));
        }
        // Sounds: their files' bytes kept in memory (a few MB); decoded when
        // first played, from memory: 2-5 ms instead of 15-80 ms from the card.
        // (Decoding them all now needs 4-8 times as much: out of memory.)
        const size_t soundBytes = g_pApp->GetResourceCache()->KeepFiles(sounds, 6u * 1024 * 1024);
        soundsLoaded = soundBytes / 1024;
        RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, 99.0f);
        LOG_WARNING("Level preload: " + ToStr((int)imagesLoaded) + " of " + ToStr((int)images.size()) + " pictures" +
                    (imagesStopped ? " (stopped: GPU memory)" : "") + ", " + ToStr((int)sounds.size()) + " sound files, " +
                    ToStr((int)soundsLoaded) + " KB kept" + (soundsStopped ? "" : "") +
                    "; free: heap " + ToStr((int)Platform::FreeMemoryKB()) + " KB, linear " +
                    ToStr((int)(linearSpaceFree() / 1024)) + " KB");
        TraceLevel("pictures and sounds preloaded");
    }
#endif
#if defined(__PSL1GHT__)
    // PS3: the level's sound files read into memory now (from the HDD during
    // play: a stutter the first time each one was played); decoded from
    // memory when first played.
    {
        const std::string level = "/LEVEL" + ToStr(m_pCurrentLevel->GetLevelNumber());
        std::vector<std::string> sounds;
        for (const std::string& set : { level + "/SOUNDS/*", std::string("/CLAW/SOUNDS/*"), std::string("/GAME/SOUNDS/*") })
        {
            std::vector<std::string> m = g_pApp->GetResourceCache()->Match(set);
            sounds.insert(sounds.end(), m.begin(), m.end());
        }
        const size_t soundBytes = g_pApp->GetResourceCache()->KeepFiles(sounds, 24u * 1024 * 1024);
        LOG_WARNING("Level preload: " + ToStr((int)sounds.size()) + " sound files, " + ToStr((int)(soundBytes / 1024)) + " KB kept");
        TraceLevel("sounds preloaded");
    }
#endif

    // Load claw stats: Score, Health, Lives, Ammo: Bullets, Magic, Dynamite
    IEventMgr* pEventMgr = IEventMgr::Get();
    pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(clawId, PlayerStat_Score, pCheckpointSave->score, false)));
    pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(clawId, PlayerStat_Health, pCheckpointSave->health, false)));
    pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(clawId, PlayerStat_Lives, pCheckpointSave->lives, false)));
    pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(clawId, PlayerStat_Bullets, pCheckpointSave->bulletCount, false)));
    pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(clawId, PlayerStat_Magic, pCheckpointSave->magicCount, false)));
    pEventMgr->VQueueEvent(IEventDataPtr(new EventData_Modify_Player_Stat(clawId, PlayerStat_Dynamite, pCheckpointSave->dynamiteCount, false)));

    // Set claw to spawn location
    if (m_pCurrentLevel->IsCustomLevel())
    {
        m_CurrentSpawnPosition = Point(m_pCurrentLevel->m_CustomStartX, m_pCurrentLevel->m_CustomStartY);
    }
    else
    {
        m_CurrentSpawnPosition = GetSpawnPosition(m_pCurrentLevel->m_LeveNumber, m_pCurrentLevel->m_LoadedCheckpoint);
    }
    pEventMgr->VTriggerEvent(IEventDataPtr(new EventData_Teleport_Actor(clawId, m_CurrentSpawnPosition)));
    // Boss modes: the Captain starts right at the boss.
    BossModes::OnLevelLoaded(this, clawId, m_CurrentSpawnPosition);

    // Save possible pickup items
    for (auto &actorIter : m_ActorMap)
    {
        shared_ptr<TreasurePickupComponent> pTreasurePickupComponent =
            MakeStrongPtr(actorIter.second->GetComponent<TreasurePickupComponent>());
        if (pTreasurePickupComponent != nullptr)
        {
            PickupType treasurePickupType = pTreasurePickupComponent->GetPickupType();
            assert(treasurePickupType != PickupType_None);

            m_pCurrentLevel->m_TotalPickupsMap[treasurePickupType]++;
        }

        shared_ptr<LootComponent> pLootComponent =
            MakeStrongPtr(actorIter.second->GetComponent<LootComponent>());
        if (pLootComponent)
        {
            const std::vector<PickupType>* pLootList = pLootComponent->GetLoot();
            for (PickupType pickup : (*pLootList))
            {
                assert(pickup != PickupType_None);

                m_pCurrentLevel->m_TotalPickupsMap[pickup]++;
            }
        }
    }

    // Notify all human views
    for (auto pGameView : m_GameViews)
    {
        if (pGameView->VGetType() == GameView_Human)
        {
            shared_ptr<HumanView> pHumanView = static_pointer_cast<HumanView>(pGameView);
            pHumanView->LoadGame(pXmlLevelRoot, m_pCurrentLevel.get());
        }
    }

    // Race: player 2 (split screen) or the opponent's ghost (network).
    if (Race::IsActive())
    {
        m_pPhysics->VFlushPendingChanges();
        Race::OnLevelLoaded(this);
        m_pPhysics->VFlushPendingChanges();
    }

    loadingProgress = 100.0f;   
    RenderLoadingScreen(pBackgroundImage, backgroundRect, scale, loadingProgress);

    LOG("Level loaded !");
    LOG("Level name: " + m_pCurrentLevel->m_LevelName);
    LOG("Level author: " + m_pCurrentLevel->m_LevelAuthor);
    LOG("Level created date: " + m_pCurrentLevel->m_LevelCreatedDate);

    // TODO: This is a bit hacky but it helps with development (prevents entering cheats over and over again). It can be data driven.
    LOG("Loading startup ingame commands...");
    ExecuteStartupCommands(g_pApp->GetGameConfig()->startupCommandsFile);

    /*int skullsCount = m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Blue] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Red] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Purple] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Green];
    LOG("Skull count: " + ToStr(skullsCount));

    int crownsCount = m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Blue] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Red] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Purple] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Green];
    LOG("crownsCount count: " + ToStr(crownsCount));

    int geckoCount = m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Blue] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Red] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Purple] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Green];
    LOG("geckoCount count: " + ToStr(geckoCount));

    int coinsCount = m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Coins] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Default];
    LOG("coinsCount count: " + ToStr(coinsCount));

    int Rings = m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Blue] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Red] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Purple] +
        m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Green];
    LOG("Rings count: " + ToStr(Rings));*/

    TraceLevel("loaded, starting the world");
    { extern int g_ReclawDeDithered; STARTUP_TRACE(("pictures with dithering made see-through so far: " + std::to_string(g_ReclawDeDithered)).c_str()); }
    pEventMgr->VTriggerEvent(IEventDataPtr(new EventData_World_Finished_Loading()));
    TraceLevel("world started");

    SAFE_DELETE(pXmlLevelRoot);

    return true;
}

bool BaseGameLogic::VLoadScoreScreen(const char* xmlScoreScreenResource)
{
    TiXmlElement* pScoreScreenRootElem = XmlResourceLoader::LoadAndReturnRootXmlElement(xmlScoreScreenResource);
    if (pScoreScreenRootElem == NULL)
    {
        LOG_ERROR("Failed to load score screen XML resource: " + std::string(xmlScoreScreenResource));
        return false;
    }
    STARTUP_TRACE("score screen: XML read");

    // Save game progress
    // When Claw finishes level, the saved checkpoint is (CurrentLevel + 1) level start

    // For convinience
    StrongActorPtr pClaw = GetClawActor();
    assert(pClaw != nullptr);

    auto pScoreComponent = MakeStrongPtr(pClaw->GetComponent<ScoreComponent>(ScoreComponent::g_Name));
    auto pHealthComponent = MakeStrongPtr(pClaw->GetComponent<HealthComponent>(HealthComponent::g_Name));
    auto pLifeComponent = MakeStrongPtr(pClaw->GetComponent<LifeComponent>(LifeComponent::g_Name));
    auto pAmmoComponent = MakeStrongPtr(pClaw->GetComponent<AmmoComponent>(AmmoComponent::g_Name));
    assert(pScoreComponent);
    assert(pHealthComponent);
    assert(pLifeComponent);
    assert(pAmmoComponent);

    CheckpointSave nextLevelCheckpoint;

    nextLevelCheckpoint.checkpointIdx = 0;
    nextLevelCheckpoint.score = pScoreComponent->GetScore();
    nextLevelCheckpoint.health = pHealthComponent->GetHealth();
    nextLevelCheckpoint.lives = pLifeComponent->GetLives();
    nextLevelCheckpoint.bulletCount = pAmmoComponent->GetRemainingAmmo(AmmoType_Pistol);
    nextLevelCheckpoint.magicCount = pAmmoComponent->GetRemainingAmmo(AmmoType_Magic);
    nextLevelCheckpoint.dynamiteCount = pAmmoComponent->GetRemainingAmmo(AmmoType_Dynamite);

    int finishedLevelNumber = m_pCurrentLevel->GetLevelNumber();
    int nextLevelNumber = m_pCurrentLevel->GetLevelNumber() + 1;

    // Unlock the next level (up to the last one); custom levels are never saved
    if (nextLevelNumber <= (int)LEVELS_COUNT && !m_pCurrentLevel->IsCustomLevel())
    {
        // Add level entry if it doesn't exist
        if (!m_pGameSaveMgr->HasLevelSave(nextLevelNumber)) {
            std::string levelName;
            switch (nextLevelNumber) {
                case 1: levelName = "La Roca"; break;
                case 2: levelName = "Battlements"; break;
                case 3: levelName = "Thief's Forest"; break;
                case 4: levelName = "Dark Woods"; break;
                case 5: levelName = "Town"; break;
                case 6: levelName = "Puerto De Lobo"; break;
                case 7: levelName = "Docks"; break;
                case 8: levelName = "Shipyards"; break;
                case 9: levelName = "Pirate's Cove"; break;
                case 10: levelName = "Cliffs"; break;
                case 11: levelName = "Caverns"; break;
                case 12: levelName = "Undersea Caves"; break;
                case 13: levelName = "Tiger Island"; break;
                case 14: levelName = "Temple"; break;
                default: levelName = "Unknown"; break;
            }
            m_pGameSaveMgr->AddLevelEntry(nextLevelNumber, levelName);
        }
        m_pGameSaveMgr->AddCheckpointSave(nextLevelNumber, nextLevelCheckpoint);

        // If not in testing mode
        if (m_pGameSaveMgr->IsSaveSupported() && !g_pApp->GetGlobalOptions()->loadAllLevelSaves)
        {
#ifdef __EMSCRIPTEN__
            // Save to IndexedDB
            SaveBridge::SaveToIndexedDB(m_pGameSaveMgr->ToJson());
            LOG("Level completion saved to IndexedDB");
#else
            TiXmlDocument saveGamesDoc;
            saveGamesDoc.LinkEndChild(m_pGameSaveMgr->ToXml());
            std::string savesFilePath = g_pApp->GetGameConfig()->userDirectory + Difficulty::SavesFile(g_pApp->GetGameConfig()->savesFile);
            WriteSavesFile(saveGamesDoc, savesFilePath);
#endif
        }
    }

    // Unload the finished level
    UnloadLevel();

    // Gather information about collected treasure items
    for (TiXmlElement* pScoreRowElem = pScoreScreenRootElem->FirstChildElement("ScoreRow");
        pScoreRowElem != NULL;
        pScoreRowElem = pScoreRowElem->NextSiblingElement("ScoreRow"))
    {
        std::string treasureTypeStr;
        DO_AND_CHECK(ParseAttributeFromXmlElem(&treasureTypeStr, "Treasure", pScoreRowElem));

        int pickedUpCount = 0;
        int totalCount = 0;
        // Better solution ?
        if (treasureTypeStr == "Skull")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Skull_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Skull_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Skull_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Skull_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Blue];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Skull_Purple];


        }
        else if (treasureTypeStr == "Crown")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crowns_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crowns_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crowns_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crowns_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Blue];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crowns_Purple];
        }
        else if (treasureTypeStr == "Gecko")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Geckos_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Geckos_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Geckos_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Geckos_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Blue];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Geckos_Purple];
        }
        else if (treasureTypeStr == "Scepter")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Scepters_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Scepters_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Scepters_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Scepters_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Scepters_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Scepters_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Scepters_Blue];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Scepters_Purple];
        }
        else if (treasureTypeStr == "Cross")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crosses_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crosses_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crosses_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Crosses_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crosses_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crosses_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crosses_Blue];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Crosses_Purple];
        }
        else if (treasureTypeStr == "Chalice")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Chalices_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Chalices_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Chalices_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Chalices_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Chalices_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Chalices_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Chalices_Blue];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Chalices_Purple];
        }
        else if (treasureTypeStr == "Ring")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Rings_Red];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Rings_Green];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Rings_Blue];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Rings_Purple];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Red];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Green];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Purple];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Rings_Purple];
        }
        else if (treasureTypeStr == "Goldbar")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Goldbars];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Goldbars];
        }
        else if (treasureTypeStr == "Coin")
        {
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Default];
            pickedUpCount += m_pCurrentLevel->m_LootedPickupsMap[PickupType_Treasure_Coins];

            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Default];
            totalCount += m_pCurrentLevel->m_TotalPickupsMap[PickupType_Treasure_Coins];
        }
        else
        {
            LOG_ERROR("Offending treasure type: " + treasureTypeStr);
            assert(false && "Unknown treasuretype");
        }

        /*LOG("[TOTAL] " + treasureTypeStr + ": " + ToStr(totalCount));
        LOG("[PICKED UP] " + treasureTypeStr + ": " + ToStr(pickedUpCount));*/

        DO_AND_CHECK(SetTiXmlNodeValue(pScoreRowElem, "ScoreRow.CountOfPickedUpScoreItems", pickedUpCount));
        DO_AND_CHECK(SetTiXmlNodeValue(pScoreRowElem, "ScoreRow.CountOfTotalScoreItemsInLevel", totalCount));
    }

    const CheckpointSave* pStartLevelSave = m_pCurrentLevel->IsCustomLevel() ? NULL :
        m_pGameSaveMgr->GetCheckpointSave(finishedLevelNumber, 0);
    int startLevelScore = pStartLevelSave ? pStartLevelSave->score : 0;
    int levelScoreCollected = nextLevelCheckpoint.score - startLevelScore;

    //<ScorePointsCollectedInLevel>0<ScorePointsCollectedInLevel>
    DO_AND_CHECK(SetTiXmlNodeValue(pScoreScreenRootElem, "FinishedLevelScreen.NextLevelNumber", nextLevelNumber));
    DO_AND_CHECK(SetTiXmlNodeValue(pScoreScreenRootElem, "FinishedLevelScreen.ScorePointsOnLevelStart", startLevelScore));
    DO_AND_CHECK(SetTiXmlNodeValue(pScoreScreenRootElem, "FinishedLevelScreen.ScorePointsCollectedInLevel", levelScoreCollected));

    STARTUP_TRACE("score screen: progress saved");
    // Load the ScoreScreen
    g_pApp->GetHumanView()->LoadScoreScreen(pScoreScreenRootElem);
    STARTUP_TRACE("score screen: shown");

    return true;
}

void BaseGameLogic::VSetProxy()
{
    m_Proxy = true;
}

StrongActorPtr BaseGameLogic::VCreateActor(const std::string& xmlActorResource, TiXmlElement* overrides)
{
    assert(m_pActorFactory);
    
    StrongActorPtr pActor = m_pActorFactory->CreateActor(xmlActorResource.c_str(), overrides);
    if (pActor)
    {
        m_ActorMap.insert(std::make_pair(pActor->GetGUID(), pActor));
        if (m_GameState == GameState_IngameRunning)
        {
            // Create event that actor was created
        }
        return pActor;
    }
    else
    {
        return StrongActorPtr();
    }
}

StrongActorPtr BaseGameLogic::VCreateActor(TiXmlElement* pActorRoot, TiXmlElement* overrides)
{
    assert(m_pActorFactory);

    StrongActorPtr pActor = m_pActorFactory->CreateActor(pActorRoot, overrides);
    if (pActor)
    {
        m_ActorMap.insert(std::make_pair(pActor->GetGUID(), pActor));
        if (m_GameState == GameState_IngameRunning)
        {
            // Create event that actor was created
        }
        return pActor;
    }
    else
    {
        return StrongActorPtr();
    }
}

void BaseGameLogic::VDestroyActor(const uint32 actorId)
{
    // Trigger actor destroyed event prior removing it here

    auto findIter = m_ActorMap.find(actorId);
    if (findIter != m_ActorMap.end())
    {
        //LOG("Destroying: " + ToStr(actorId));
        findIter->second->Destroy();
        m_ActorMap.erase(findIter);
    }
}

WeakActorPtr BaseGameLogic::VGetActor(const uint32 actorId)
{
    auto findIter = m_ActorMap.find(actorId);
    if (findIter != m_ActorMap.end())
    {
        return findIter->second;
    }

    return WeakActorPtr();
}

void BaseGameLogic::VModifyActor(const uint32 actorId, TiXmlElement* overrides)
{
    assert(m_pActorFactory);

    auto findIter = m_ActorMap.begin();
    if (findIter != m_ActorMap.end())
    {
        m_pActorFactory->ModifyActor(findIter->second, overrides);
    }
}

void BaseGameLogic::VOnUpdate(uint32 msDiff)
{
    m_Lifetime += msDiff;

    // TODO: This is code duplication, should think of better way
    if (!m_bRunning)
    {
        for (auto &pGameView : m_GameViews)
        {
            pGameView->VOnUpdate(msDiff);
        }

        return;
    }

    switch (m_GameState)
    {
        case GameState_Initializing:
        {
#ifdef ANDROID
            VChangeState(GameState_LoadingLevel);
#else
            if (g_pApp->GetDebugOptions()->bSkipMenu)
            {
                VChangeState(GameState_LoadingLevel);
            }
            else
            {
#ifdef __EMSCRIPTEN__
                // Try to play intro movie
                VPlayCutscene("videos/INTRO.mp4");
#else
                // Original Smacker movies from the CD (CLAW.FEC), if present.
                if (MoviePlayer::IsAvailable())
                {
                    g_pApp->GetAudio()->StopMusic();
                    g_pApp->GetAudio()->StopAllSounds();
                    MoviePlayer::PlayStartupMovies(g_pApp->GetRenderer());
                }
                // Title map unrolling, then the menu (from the game's own data).
                TitleScroll::Play(g_pApp->GetRenderer());
#endif
                // If it failed/skipped immediately, it will fall through or we check state
                if (m_GameState == GameState_Initializing)
                {
                    VChangeState(GameState_Menu);
                }
            }
#endif
            break;
        }

        case GameState_LoadingMenu:
        {
            VChangeState(GameState_Menu);
            break;
        }

        case GameState_Menu:
        {
            // Boss modes: next boss / back to the boss list.
            BossModes::OnMenu();
            break;
        }

        case GameState_LoadingLevel:
        {
            VChangeState(GameState_IngameRunning);
            break;
        }

        case GameState_LoadingScoreScreen:
        {
            VChangeState(GameState_ScoreScreen);
            break;
        }

        case GameState_ScoreScreen:
        {
            break;
        }

        case GameState_IngameRunning:
        {
            // Race: countdown, network, finish (save states are off in races).
            Race::Update(this, msDiff);
            // Boss practice / Boss Rush: stopwatch, results, next boss.
            BossModes::Update(this, msDiff);

            // Quick save / quick load / rewind happen between steps.
            m_bSkipWorldStep = (Race::IsActive() || BossModes::IsActive() || !Platform::SupportsSaveStates()) ? false : SaveStateManager::Get()->OnGameStep(this, msDiff);
            if (m_bSkipWorldStep)
            {
                break;
            }

#ifdef OPENCLAW_STARTUP_TRACE
            Uint64 perfA = SDL_GetPerformanceCounter();
#endif
            if (m_pProcessMgr)
            {
                m_pProcessMgr->UpdateProcesses(msDiff);
            }
#ifdef OPENCLAW_STARTUP_TRACE
            Uint64 perfB = SDL_GetPerformanceCounter();
            g_PerfLogicTicks[0] += perfB - perfA;
            struct PhysicsTimer { Uint64 t0 = SDL_GetPerformanceCounter(); ~PhysicsTimer() { g_PerfLogicTicks[1] += SDL_GetPerformanceCounter() - t0; } } physicsTimer;
#endif
            
            if (m_pPhysics)
            {
                //PROFILE_CPU("PHYSICS");
                // TODO: Add config to choose between fixed physics timestep and variable
                if (true)
                {
                    m_pPhysics->VOnUpdate(msDiff);
                    m_pPhysics->VSyncVisibleScene();
                }
                else
                {
                    static uint32 timeSinceLastUpdate = 0;
                    const uint32 updateInterval = 1000 / 120;

                    timeSinceLastUpdate += msDiff;
                    if (timeSinceLastUpdate >= updateInterval)
                    {
                        //PROFILE_CPU("PHYSICS");
                        //LOG(ToStr(timeSinceLastUpdate));
                        m_pPhysics->VOnUpdate(timeSinceLastUpdate);
                        m_pPhysics->VSyncVisibleScene();

                        timeSinceLastUpdate = 0;
                    }
                }
                break;
            }

            break;
        }

        case GameState_Cutscene:
        {
            if (m_pActiveVideo)
            {
                m_pActiveVideo->Update(msDiff / 1000.0f);
                if (!m_pActiveVideo->IsPlaying())
                {
                    LOG("Cutscene finished.");
                    m_pActiveVideo.reset();
                    VChangeState(GameState_Menu); // For now, always go to menu
                }
            }
            else
            {
                VChangeState(GameState_Menu);
            }
            break;
        }

        default:
        {
            LOG_ERROR("Unknown state: " + ToStr(m_GameState));
            break;
        }
    }

#ifdef OPENCLAW_STARTUP_TRACE
    extern bool g_PerfSampleStep;
    extern int g_PerfSampledSteps;
    static int s_StepNo = 0;
    g_PerfSampleStep = (m_GameState == GameState_IngameRunning) && (++s_StepNo % 8 == 0);
    if (g_PerfSampleStep) g_PerfSampledSteps++;
    Uint64 perfViews = SDL_GetPerformanceCounter();
    struct ViewsTimer { Uint64 t0; ~ViewsTimer() { g_PerfLogicTicks[2] += SDL_GetPerformanceCounter() - t0; } } viewsTimer{ perfViews };
#endif
    // Update all game views
    for (auto &pGameView : m_GameViews)
    {
        pGameView->VOnUpdate(msDiff);
    }
#ifdef OPENCLAW_STARTUP_TRACE
    g_PerfLogicTicks[3] += SDL_GetPerformanceCounter() - perfViews;   // views alone
#endif

    // Limit update to max 100 times / second
    static int msAccumulation = 0;
    msAccumulation += msDiff;
    if (m_bSkipWorldStep)
    {
        m_bSkipWorldStep = false;
        msAccumulation = 0;
    }
    else if (msAccumulation >= 5)
    {
        // Update all game actors. Handhelds and the PS3 (its PPU: ~15 ms a step
        // for all of them): scenery and pickups far from the screen wait - most
        // of a level's ~1700 actors; updating them all cost the PS Vita a third
        // of each step (PortFeatures.h).
#if defined(RECLAW_PAUSE_FAR_SCENERY)
        HumanView* pView = (m_GameState == GameState_IngameRunning) ? g_pApp->GetHumanView() : nullptr;
        shared_ptr<CameraNode> pCam = (pView && !Race::IsActive()) ? pView->GetCamera() : nullptr;
        SDL_Rect nearRect = { 0, 0, 0, 0 };
        if (pCam)
        {
            nearRect = pCam->GetCameraRect();
#if defined(RECLAW_PAUSE_FAR_ENEMIES)
            // half a screen around the view
            nearRect.x -= nearRect.w / 2; nearRect.y -= nearRect.h / 2; nearRect.w *= 2; nearRect.h *= 2;
#else
            nearRect.x -= nearRect.w; nearRect.y -= nearRect.h; nearRect.w *= 3; nearRect.h *= 3;
#endif
        }
        auto isFar = [&](Actor* pActor) {
            shared_ptr<PositionComponent> pPos = pActor->GetPositionComponent();
            if (!pPos) return false;
            const Point& p = pPos->GetPosition();
            return p.x < nearRect.x || p.y < nearRect.y || p.x > nearRect.x + nearRect.w || p.y > nearRect.y + nearRect.h;
        };
#endif
        for (auto &actorIter : m_ActorMap)
        {
#if defined(RECLAW_PAUSE_FAR_SCENERY)
            Actor* pActor = actorIter.second.get();
            bool pausable = pCam && pActor->IsPausableWhenFar();
#if defined(RECLAW_PAUSE_FAR_ENEMIES)
            // (enemies: their bodies switched off too, or a walking one would
            // keep sliding; back within reach they carry on where they were)
            pausable = pausable || (pCam && pActor->IsEnemyPausableWhenFar());
#endif
            if (pausable)
            {
                const bool far = isFar(pActor);
                if (far != pActor->m_bPausedFar)
                {
                    if (m_pPhysics) m_pPhysics->VSetFarPaused(pActor->GetGUID(), far);
                    pActor->m_bPausedFar = far;
                }
                if (far) continue;
            }
            else if (pActor->m_bPausedFar)
            {
                // (a race started, or the view is gone: everything runs again)
                if (m_pPhysics) m_pPhysics->VSetFarPaused(pActor->GetGUID(), false);
                pActor->m_bPausedFar = false;
            }
#endif
            actorIter.second->Update(msAccumulation);
        }
        msAccumulation = 0;
    }
}

void BaseGameLogic::VChangeState(GameState newState)
{
    if (newState == GameState_Menu)
    {
        // Unload level if applicable
        UnloadLevel();
#if defined(__3DS__) || defined(RECLAW_PS2) || defined(RECLAW_DREAMCAST)
        // 3DS (PS2, Dreamcast: memory): the level's pictures (textures in the GPU's memory) and files
        // leave the caches before the menu's are made (going back to the menu
        // ran out of GPU memory: the game quit)
        g_pApp->GetResourceCache()->ClearKeptFiles();
        g_pApp->GetResourceMgr()->VFlush();
        TraceLevel("level unloaded, caches flushed");
#endif
        if (!VEnterMenu("MENU.XML"))
        {
            LOG_ERROR("Failed to enter menu");
            g_pApp->Terminate();
            exit(1);
        }
    }
    else if (newState == GameState_LoadingLevel)
    {
        // The bottom screen (3DS): just the map while the level loads.
        SecondScreen::ShowBackgroundOnly();
        // In case of debugging
        if (m_pCurrentLevel == nullptr)
        {
            m_pCurrentLevel = ClawLevelUtil::GetDebugLoadLevelData();
        }

        int levelNumber = m_pCurrentLevel->GetLevelNumber();
        assert(levelNumber >= 0 && levelNumber <= 14);

        std::string levelName = "LEVEL" + ToStr(levelNumber);
        std::string wwdLevelPath = "/" + levelName + "/WORLDS/WORLD.WWD";

        // Lazy loading: Preload level-specific assets on-demand
        LOG("Loading assets for " + levelName + "...");
        std::string levelAssetPath = "/" + levelName + "/*";
#if defined(RECLAW_PS2) || defined(RECLAW_DREAMCAST)
        // (PS2, Dreamcast: the whole level does not fit in memory - it ran out
        // in the level's sounds; files are read when used)
        (void)levelAssetPath;
#else
        g_pApp->GetResourceCache()->Preload(levelAssetPath, NULL);
#endif
        LOG("Level assets loaded for " + levelName);

        // Load saved level file, e.g. LEVEL1.xml
        if (!VLoadGame(wwdLevelPath.c_str()))
        {
            LOG_ERROR("Could not load level");
            exit(1);
        }
    }
    else if (newState == GameState_ScoreScreen)
    {
        // Dummy for testing
        if (m_pCurrentLevel == nullptr)
        {
            m_pCurrentLevel.reset(new LevelData(1, false, 2));
        }

        std::string finishedLevelXmlDescPath = "/FINISHED_LEVEL_SCENES/LEVEL" + ToStr(m_pCurrentLevel->m_LeveNumber) + ".XML";

        if (!VLoadScoreScreen(finishedLevelXmlDescPath.c_str()))
        {
            LOG_ERROR("Could not load score screen");
            exit(1);
        }
    }

    // Convert GameState enum to readable string
    const char* stateNames[] = {
        "Invalid",
        "Initializing",
        "LoadingMenu",
        "Menu",
        "LoadingLevel",
        "IngameRunning",
        "IngamePaused",
        "LoadingScoreScreen",
        "ScoreScreen",
        "Cutscene"
    };

    std::string stateName = (newState >= 0 && newState < 10) ? stateNames[newState] : "Unknown";
    LOG("Changing game state to: " + stateName);
    m_GameState = newState;
}

void BaseGameLogic::VRenderDiagnostics(SDL_Renderer* pRenderer, shared_ptr<CameraNode> pCamera)
{
    if (m_RenderDiagnostics && m_pPhysics)
    {
        m_pPhysics->VRenderDiagnostics(pRenderer, pCamera);
    }
}

void BaseGameLogic::VAddView(shared_ptr<IGameView> pView, uint32 actorId)
{
    int viewId = static_cast<int>(m_GameViews.size());
    m_GameViews.push_back(pView);
    pView->VOnAttach(viewId, actorId);
}

void BaseGameLogic::VRemoveView(shared_ptr<IGameView> pView)
{
    m_GameViews.remove(pView);
}

ActorFactory* BaseGameLogic::VCreateActorFactory()
{
    return new ActorFactory();
}

void BaseGameLogic::RequestDestroyActorDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Destroy_Actor> pCastEventData = static_pointer_cast<EventData_Destroy_Actor>(pEventData);
    VDestroyActor(pCastEventData->GetActorId());
    if (m_pPhysics != nullptr)
    {
        m_pPhysics->VRemoveActor(pCastEventData->GetActorId());
    }
}

void BaseGameLogic::MoveActorDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Move_Actor> pCastEventData = static_pointer_cast<EventData_Move_Actor>(pEventData);

    StrongActorPtr pActor = MakeStrongPtr(VGetActor(pCastEventData->GetActorId()));
    if (pActor != nullptr)
    {
        shared_ptr<PositionComponent> pPositionComponent = pActor->GetPositionComponent();
        assert(pPositionComponent != nullptr);

        pPositionComponent->SetPosition(pCastEventData->GetMove());
    }
}

void BaseGameLogic::RequestNewActorDelegate(IEventDataPtr pEventData)
{

}

std::map<CollisionType, FixtureType> g_CollisionToFixtureTypeMap =
{
    { CollisionType_None, FixtureType_None },
    { CollisionType_Solid, FixtureType_Solid },
    { CollisionType_Ground, FixtureType_Ground },
    { CollisionType_Climb, FixtureType_Climb },
    { CollisionType_Death, FixtureType_Death }
};

static FixtureType CollisonToFixtureType(CollisionType collisionType)
{
    auto findIt = g_CollisionToFixtureTypeMap.find(collisionType);
    assert(findIt != g_CollisionToFixtureTypeMap.end());

    return findIt->second;
}

// Helper function
void BaseGameLogic::CreateSinglePhysicsTile(int x, int y, const TileCollisionPrototype& proto)
{
    for (const auto &tileCollisionRect : proto.collisionRectangles)
    {
        Point position = Point(x + tileCollisionRect.collisionRect.x,
            y + tileCollisionRect.collisionRect.y);
        Point size = Point(tileCollisionRect.collisionRect.w, tileCollisionRect.collisionRect.h);
#if !defined(__EMSCRIPTEN__)
        static const bool s_dump = getenv("RECLAW_LADDERS") != NULL;
        if (s_dump && tileCollisionRect.collisionType == CollisionType_Climb)
            fprintf(stderr, "[ladder] %d %d %d %d\n", (int)position.x, (int)position.y, (int)size.x, (int)size.y);
#endif

        m_pPhysics->VAddStaticGeometry(
            position, 
            size, 
            tileCollisionRect.collisionType, 
            CollisonToFixtureType(tileCollisionRect.collisionType));
    }

    Point topLadderOffset;
    if (ClawLevelUtil::TryGetTopLadderInfo(m_pCurrentLevel->GetLevelNumber(), proto.id, topLadderOffset))
    {
        Point position = Point(x, y) + topLadderOffset;
        Point size(64, 10);

        m_pPhysics->VAddStaticGeometry(position, size, CollisionType_Ground, FixtureType_TopLadderGround);
    }
}

void BaseGameLogic::CollideableTileCreatedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Collideable_Tile_Created> pCastEventData = static_pointer_cast<EventData_Collideable_Tile_Created>(pEventData);

    //-------------------------------------------------------------------------
    // (1) First get all info about the tile, its position etc we want to create
    //-------------------------------------------------------------------------

    int tileId = pCastEventData->GetTileId();
    int tileX = pCastEventData->GetPositionX();
    int tileY = pCastEventData->GetPositionY();
    int numTiles = pCastEventData->GetTilesCount();
    assert(numTiles >= 1);

    auto findIt = m_pCurrentLevel->m_TileCollisionPrototypeMap.find(tileId);
    if (findIt == m_pCurrentLevel->m_TileCollisionPrototypeMap.end())
    {
        LOG_ERROR("Unknown tile! Id = " + ToStr(tileId));
        assert(false && "Unknown tile");
    }

    const TileCollisionPrototype& tileProto = findIt->second;

    //-------------------------------------------------------------------------
    // (2) Create the tile in the physics world appropriately
    //    - If its single tile, create it simply from prototype
    //    - If its multiple same tiles, check if we can merge them, if yes, 
    //      merge and create, if not, create them separately
    //-------------------------------------------------------------------------

    if (numTiles == 1)
    {
        CreateSinglePhysicsTile(tileX, tileY, tileProto);
    }
    else
    {
        // Multiple tiles in a row, check if we can marge them
        bool bCanBeMerged = true;

        int countAttrTiles = 0;
        TileCollisionRectangle mergedRect;
        for (const TileCollisionRectangle& colRect : tileProto.collisionRectangles)
        {
            if (colRect.collisionType != CollisionType_None)
            {
                countAttrTiles++;
                mergedRect = colRect;

                if (colRect.collisionRect.w != tileProto.width)
                {
                    bCanBeMerged = false;
                }
            }
        }

        if (countAttrTiles != 1)
        {
            bCanBeMerged = false;
        }

        // If they can be merged, merge them
        if (bCanBeMerged)
        {
            assert(mergedRect.collisionRect.x == 0);
            assert(mergedRect.collisionRect.w == tileProto.width);
            assert(mergedRect.collisionRect.h > 0);

            Point mergedPosition(tileX + mergedRect.collisionRect.x, tileY + mergedRect.collisionRect.y);
            Point mergedSize(tileProto.width * numTiles, mergedRect.collisionRect.h);

            m_pPhysics->VAddStaticGeometry(
                mergedPosition, 
                mergedSize, 
                mergedRect.collisionType,
                CollisonToFixtureType(mergedRect.collisionType));
        }
        else
        {
            // If not, create them one by one
            for (int tileNumIdx = 0; tileNumIdx < numTiles; tileNumIdx++)
            {
                CreateSinglePhysicsTile(tileX, tileY, tileProto);
                tileX += tileProto.width;
            }
        }
    }
}

void BaseGameLogic::CreateStaticGeometryDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Add_Static_Geometry> pCastEventData =
        static_pointer_cast<EventData_Add_Static_Geometry>(pEventData);
}

void BaseGameLogic::ItemPickedUpDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Item_Picked_Up> pCastEventData =
        static_pointer_cast<EventData_Item_Picked_Up>(pEventData);

    //LOG("Picked up: " + ActorTemplates::EnumToString_PickupTypeToImageSet(pCastEventData->GetPickupType()));
    m_pCurrentLevel->m_LootedPickupsMap[pCastEventData->GetPickupType()]++;

    // Haptic feedback for pickups
    HapticFeedback::Trigger(HapticPreset::Pickup);
}

void BaseGameLogic::FinishedLevelDelegate(IEventDataPtr pEventData)
{
    // The last level done: the Boss Rush ("Paruj i kontruj") opens.
    if (m_pCurrentLevel && !m_pCurrentLevel->IsCustomLevel() && !BossModes::IsActive() &&
        m_pCurrentLevel->GetLevelNumber() == LEVELS_COUNT)
    {
        BossModes::MarkGameCompleted();
    }
    VChangeState(GameState_LoadingScoreScreen);
}

void BaseGameLogic::ActorEnteredBossAreaDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Entered_Boss_Area> pCastEventData =
        static_pointer_cast<EventData_Entered_Boss_Area>(pEventData);

    auto findIt = m_ActorMap.find(pCastEventData->GetControllerId());
    assert(findIt != m_ActorMap.end());

    StrongActorPtr pClaw = findIt->second;

    shared_ptr<ClawControllableComponent> pClawComponent = 
        MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>());
    assert(pClawComponent != nullptr);

    pClawComponent->SetFrozen(true);

    // Remembered for the boss practice mode ("Skrzyżuj szpady ponownie").
    if (m_pCurrentLevel && !m_pCurrentLevel->IsCustomLevel() && !Race::IsActive() && !BossModes::IsActive())
    {
        BossModes::MarkBossReached((int)m_pCurrentLevel->GetLevelNumber());
    }
}

void BaseGameLogic::BossFightStartedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Boss_Fight_Started> pCastEventData =
        static_pointer_cast<EventData_Boss_Fight_Started>(pEventData);

    auto findIt = m_ActorMap.find(pCastEventData->GetControllerId());
    assert(findIt != m_ActorMap.end());

    StrongActorPtr pClaw = findIt->second;

    shared_ptr<ClawControllableComponent> pClawComponent =
        MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>());
    assert(pClawComponent != nullptr);

    pClawComponent->SetFrozen(false);
}

void BaseGameLogic::IngameMenuEndLifeDelegate(IEventDataPtr pEventData)
{
    StrongActorPtr pClaw = GetClawActor();
    assert(pClaw != nullptr);

    shared_ptr<HealthComponent> pClawHealthComponent = MakeStrongPtr(pClaw->GetComponent<HealthComponent>());
    assert(pClawHealthComponent != nullptr);

    pClawHealthComponent->SetCurrentHealth(-1);
}

void BaseGameLogic::WorldFinishedLoadingDelegate(IEventDataPtr pEventData)
{
    for (const auto& actorPair : m_ActorMap)
    {
        actorPair.second->OnWorldFinishedLoading();
    }
}

StrongActorPtr BaseGameLogic::GetClawActor()
{
    StrongActorPtr claw = MakeStrongPtr(m_pClawActor);
    if (claw) {
        return claw;
    }

    for (auto &actorIter : m_ActorMap)
    {
        if (actorIter.second->GetName() == "Claw")
        {
            m_pClawActor = actorIter.second;
            return actorIter.second;
        }
    }

    return nullptr;
}

StrongActorPtr BaseGameLogic::FindActorByName(const std::string& name, bool bIsUnique)
{
    StrongActorPtr pFoundActor = nullptr;
    for (auto &actorIter : m_ActorMap)
    {
        if (actorIter.second->GetName() == name)
        {
            if (pFoundActor != nullptr && bIsUnique)
            {
                LOG_ASSERT("Actor with name: \"" + name + "\" was marked as unique but occured more than once !");
            }
            pFoundActor = actorIter.second;
        }
    }

    return pFoundActor;
}

ActorList BaseGameLogic::FindActorByName(const std::string& name)
{
    ActorList actorList;

    for (auto &actorIter : m_ActorMap)
    {
        if (actorIter.second->GetName() == name)
        {
            actorList.push_back(actorIter.second.get());
        }
    }

    return actorList;
}

void BaseGameLogic::UnloadLevel()
{
    SaveStateManager::Get()->Reset();

    // We cannot do this inside event update loop
    assert(IEventMgr::Get()->VIsUpdating() == false);

    // Handle all pending events before reset
    IEventMgr::Get()->VUpdate(IEventMgr::kINFINITE);

    // We need to create a copy here because the m_ActorMap will be modified by future events will
    // will arise
    ActorMap actorMapCopy = m_ActorMap;

    for (const auto &actor : actorMapCopy)
    {
        shared_ptr<EventData_Destroy_Actor> pEvent(new EventData_Destroy_Actor(actor.second->GetGUID()));
        IEventMgr::Get()->VTriggerEvent(pEvent);
    }

    // These events have to ensure that the map will be in fact empty
    assert(m_ActorMap.empty());

    // Process any pending events which could have arose from deleting all actors
    IEventMgr::Get()->VUpdate(IEventMgr::kINFINITE);

    //m_pCurrentLevel.reset();

    m_pPhysics.reset();
}

void BaseGameLogic::VResetLevel()
{
    // Handle all pending events before reset
    IEventMgr::Get()->VUpdate(IEventMgr::kINFINITE);

    for (auto &actorIter : m_ActorMap)
    {
        shared_ptr<EventData_Destroy_Actor> pEvent(new EventData_Destroy_Actor(actorIter.second->GetGUID()));
        IEventMgr::Get()->VTriggerEvent(pEvent);
    }

    assert(m_ActorMap.empty());
    assert(m_pClawActor.expired());

    // Process any pending events which could have arose from deleting all actors
    IEventMgr::Get()->VUpdate(IEventMgr::kINFINITE);

    // Reset physics. TODO: is replacing pointer which is shared between multiple classes OK like this ?
    m_pPhysics.reset(CreateClawPhysics());

    // Load new level
    VChangeState(GameState_LoadingLevel);
}

void BaseGameLogic::CheckpointReachedDelegate(IEventDataPtr pEventData)
{
    shared_ptr<EventData_Checkpoint_Reached> pCastEventData =
        static_pointer_cast<EventData_Checkpoint_Reached>(pEventData);

    if (!pCastEventData) return;

    // Race: every Captain has his own checkpoint and nothing is saved.
    if (Race::IsActive())
    {
        Race::SetSpawn(pCastEventData->GetActorId(), pCastEventData->GetSpawnPoint());
        return;
    }

    // Boss modes: the Captain always comes back at the boss, nothing is saved.
    if (BossModes::IsActive())
    {
        return;
    }

    // Update current spawn position
    m_CurrentSpawnPosition = pCastEventData->GetSpawnPoint();

    // Only save if this is a save checkpoint (not just a respawn point)
    if (!pCastEventData->IsSaveCheckpoint()) {
        return;
    }

    if (m_pCurrentLevel && m_pCurrentLevel->IsCustomLevel()) {
        return;   // custom levels keep their checkpoints in memory only
    }

    if (!m_pCurrentLevel) {
        LOG_WARNING("CheckpointReachedDelegate: No current level");
        return;
    }

    uint32 levelNumber = m_pCurrentLevel->GetLevelNumber();
    uint32 checkpointNumber = pCastEventData->GetSaveCheckpointNumber();

    LOG("Checkpoint " + ToStr(checkpointNumber) + " reached on level " + ToStr(levelNumber));

    // Get player stats for the save
    StrongActorPtr pClawActor = GetClawActor();
    if (!pClawActor) {
        LOG_WARNING("CheckpointReachedDelegate: No Claw actor found");
        return;
    }

    auto pHealthComponent = MakeStrongPtr(pClawActor->GetComponent<HealthComponent>(HealthComponent::g_Name));
    auto pScoreComponent = MakeStrongPtr(pClawActor->GetComponent<ScoreComponent>(ScoreComponent::g_Name));
    auto pLifeComponent = MakeStrongPtr(pClawActor->GetComponent<LifeComponent>(LifeComponent::g_Name));
    auto pAmmoComponent = MakeStrongPtr(pClawActor->GetComponent<AmmoComponent>(AmmoComponent::g_Name));

    if (!pHealthComponent || !pScoreComponent || !pLifeComponent || !pAmmoComponent) {
        LOG_WARNING("CheckpointReachedDelegate: Missing player components");
        return;
    }

    // Create checkpoint save with current player state
    CheckpointSave checkpointSave;
    checkpointSave.checkpointIdx = checkpointNumber;
    checkpointSave.score = pScoreComponent->GetScore();
    checkpointSave.health = pHealthComponent->GetHealth();
    checkpointSave.lives = pLifeComponent->GetLives();
    checkpointSave.bulletCount = pAmmoComponent->GetRemainingAmmo(AmmoType_Pistol);
    checkpointSave.magicCount = pAmmoComponent->GetRemainingAmmo(AmmoType_Magic);
    checkpointSave.dynamiteCount = pAmmoComponent->GetRemainingAmmo(AmmoType_Dynamite);

    // Add the checkpoint save
    m_pGameSaveMgr->AddCheckpointSave(levelNumber, checkpointSave);

    // Persist to storage
    if (m_pGameSaveMgr->IsSaveSupported() && !g_pApp->GetGlobalOptions()->loadAllLevelSaves)
    {
#ifdef __EMSCRIPTEN__
        SaveBridge::SaveToIndexedDB(m_pGameSaveMgr->ToJson());
        LOG("Checkpoint saved to IndexedDB");
#else
        TiXmlDocument saveGamesDoc;
        saveGamesDoc.LinkEndChild(m_pGameSaveMgr->ToXml());
        std::string savesFilePath = g_pApp->GetGameConfig()->userDirectory + Difficulty::SavesFile(g_pApp->GetGameConfig()->savesFile);
        WriteSavesFile(saveGamesDoc, savesFilePath);
        LOG("Checkpoint saved to file");
#endif
    }
}

//=====================================================================================================================
// Private
//=====================================================================================================================

void BaseGameLogic::RegisterAllDelegates()
{
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::CollideableTileCreatedDelegate), EventData_Collideable_Tile_Created::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::RequestDestroyActorDelegate), EventData_Destroy_Actor::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::CreateStaticGeometryDelegate), EventData_Add_Static_Geometry::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::ItemPickedUpDelegate), EventData_Item_Picked_Up::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::FinishedLevelDelegate), EventData_Finished_Level::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::MoveActorDelegate), EventData_Move_Actor::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::ActorEnteredBossAreaDelegate), EventData_Entered_Boss_Area::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::BossFightStartedDelegate), EventData_Boss_Fight_Started::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::IngameMenuEndLifeDelegate), EventData_IngameMenu_End_Life::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::WorldFinishedLoadingDelegate), EventData_World_Finished_Loading::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &BaseGameLogic::CheckpointReachedDelegate), EventData_Checkpoint_Reached::sk_EventType);
}

void BaseGameLogic::RemoveAllDelegates()
{
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::CollideableTileCreatedDelegate), EventData_Collideable_Tile_Created::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::RequestDestroyActorDelegate), EventData_Destroy_Actor::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::CreateStaticGeometryDelegate), EventData_Add_Static_Geometry::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::ItemPickedUpDelegate), EventData_Item_Picked_Up::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::FinishedLevelDelegate), EventData_Finished_Level::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::MoveActorDelegate), EventData_Move_Actor::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::ActorEnteredBossAreaDelegate), EventData_Entered_Boss_Area::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::BossFightStartedDelegate), EventData_Boss_Fight_Started::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::IngameMenuEndLifeDelegate), EventData_IngameMenu_End_Life::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::WorldFinishedLoadingDelegate), EventData_World_Finished_Loading::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &BaseGameLogic::CheckpointReachedDelegate), EventData_Checkpoint_Reached::sk_EventType);
}

void BaseGameLogic::ExecuteStartupCommands(const std::string& startupCommandsFile)
{
    std::ifstream commandsFile(startupCommandsFile);
    if (commandsFile.is_open())
    {
        std::string line;
        while (std::getline(commandsFile, line))
        {
            CommandHandler::HandleCommand(line.c_str(), (void*)g_pApp->GetHumanView()->GetConsole().get());
        }
    }
}
