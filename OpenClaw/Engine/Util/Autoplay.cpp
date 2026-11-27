// Scripted test runs for the desktop build (never on consoles).
//
//   RECLAW_AUTOPLAY=script.txt ./openclaw
//
// One command per line, '#' starts a comment. Time is game time (fixed
// logic steps), so a run gives the same result however fast the machine is.
//   wait MS               let the game run
//   level N [CHECKPOINT]  start level N (new game) from the menu
//   down KEY / up KEY     press / release a key (SDL key name: Left, Space, Left Ctrl, z ...)
//   tap KEY MS            press, wait MS, release
//   log MS                every MS ms log Claw: position, velocity, state, animation (0 = off)
//   watch NAME [MS]       every MS ms (default 250, 0 = off) log actors whose name has NAME
//                         within 800 px of Claw: position, visibility, alpha, animation
//   mark TEXT             a line in the log
//   cmd TEXT              console command (teleport X Y, catnip, ...)
//   shot NAME             save the next frame as NAME.bmp
//   custom FILE           start custom/FILE (.WWD)
//   damage NAME N         damage the actor nearest to Claw whose name has NAME
//   quit                  end the game
//
// Windows only: RECLAW_MIRROR_KEYS=1 - while the ReClaw window is NOT focused,
// it follows the keys held on the keyboard anyway (arrows, Space, Enter, Esc, left Ctrl /
// Alt / Shift, Z, X, 1-5), so the original CLAW.EXE in the focused window and
// ReClaw beside it get the same input - for side-by-side recordings.
#include "Autoplay.h"
#if !defined(__EMSCRIPTEN__) && !defined(__SWITCH__) && !defined(__vita__) && !defined(__3DS__) && !defined(__PSL1GHT__) && !defined(_EE) && !defined(__WIIU__) && !defined(_arch_dreamcast) && !defined(__ORBIS__) && !defined(__ANDROID__)
#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../GameApp/CommandHandler.h"
#include "../UserInterface/HumanView.h"
#include "../UserInterface/Console.h"
#include "../UserInterface/CustomLevels.h"
#include <strings.h>
#include "../Events/EventMgr.h"
#include "../Events/Events.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/PhysicsComponent.h"
#include "../Actor/Components/RenderComponent.h"
#include "../Actor/Components/EnemyAI/EnemyAIComponent.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "../Actor/Components/AnimationComponent.h"
#include "../Actor/Components/ControllableComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    std::vector<std::string> s_Lines;
    size_t s_Pc = 0;
    int s_State = -1;           // -1 not loaded, 0 off, 1 running
    int64_t s_WaitMs = 0;
    int64_t s_TimeMs = 0;
    uint32_t s_LogEvery = 0, s_LogAcc = 0;
    std::string s_Shot;
    struct PendingUp { SDL_Keycode key; int64_t at; };
    std::vector<PendingUp> s_Ups;

    void Load()
    {
        s_State = 0;
        const char *path = getenv("RECLAW_AUTOPLAY");
        if (!path || !*path) return;
        std::ifstream f(path);
        std::string l;
        while (std::getline(f, l))
        {
            size_t h = l.find('#');
            if (h != std::string::npos) l = l.substr(0, h);
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
            if (!l.empty()) s_Lines.push_back(l);
        }
        s_State = s_Lines.empty() ? 0 : 1;
        fprintf(stderr, "[autoplay] %zu commands from %s\n", s_Lines.size(), path);
    }

    void Key(SDL_Keycode k, bool down)
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.sym = k;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
        if (g_pApp) g_pApp->OnEvent(e);
    }

    SDL_Keycode KeyFrom(const std::string &name)
    {
        SDL_Keycode k = SDL_GetKeyFromName(name.c_str());
        if (k == SDLK_UNKNOWN) fprintf(stderr, "[autoplay] unknown key '%s'\n", name.c_str());
        return k;
    }

    std::string s_Watch;
    int s_WatchEvery = 0, s_WatchAcc = 0;

    void LogWatch()
    {
        if (s_Watch.empty() || !g_pApp || !g_pApp->GetGameLogic()) return;
        StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
        if (!pClaw) return;
        const Point c = pClaw->GetPositionComponent()->GetPosition();
        for (const auto &kv : g_pApp->GetGameLogic()->GetActorMap())
        {
            const StrongActorPtr &a = kv.second;
            if (!a || a->GetName().find(s_Watch) == std::string::npos || !a->GetPositionComponent()) continue;
            const Point p = a->GetPositionComponent()->GetPosition();
            if (fabs(p.x - c.x) > 800 || fabs(p.y - c.y) > 800) continue;
            auto r = MakeStrongPtr(a->GetComponent<ActorRenderComponent>());
            auto an = MakeStrongPtr(a->GetComponent<AnimationComponent>(AnimationComponent::g_Name));
            auto ai = MakeStrongPtr(a->GetComponent<EnemyAIComponent>(EnemyAIComponent::g_Name));
            fprintf(stderr, "[autoplay] t=%lld watch %s#%u pos=%.1f,%.1f vis=%d alpha=%d anim=%s%s\n",
                    (long long)s_TimeMs, a->GetName().c_str(), (unsigned)a->GetGUID(), p.x, p.y,
                    r ? (int)r->IsVisible() : -1, r ? r->GetAlpha() : -1,
                    an ? an->GetCurrentAnimationName().c_str() : "-",
                    ai ? (ai->IsDead() ? " dead" : (ai->IsPhased() ? " phased" : "")) : "");
        }
    }

    void LogClaw(const char *why)
    {
        if (!g_pApp || !g_pApp->GetGameLogic()) return;
        StrongActorPtr c = g_pApp->GetGameLogic()->GetClawActor();
        if (!c) { fprintf(stderr, "[autoplay] t=%lld %s no claw\n", (long long)s_TimeMs, why); return; }
        Point p, v;
        if (auto pc = MakeStrongPtr(c->GetComponent<PositionComponent>(PositionComponent::g_Name))) p = pc->GetPosition();
        int air = -1, climb = -1, ground = -1;
        if (auto ph = MakeStrongPtr(c->GetComponent<PhysicsComponent>(PhysicsComponent::g_Name)))
        { v = ph->GetVelocity(); air = ph->IsInAir(); climb = ph->IsClimbing(); ground = ph->IsOnGround(); }
        std::string anim = "?";
        if (auto ac = MakeStrongPtr(c->GetComponent<AnimationComponent>(AnimationComponent::g_Name)))
            if (ac->GetCurrentAnimation()) anim = ac->GetCurrentAnimationName();
        int st = -1, hp = -1;
        if (auto cc = MakeStrongPtr(c->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name))) st = cc->GetClawStateId();
        if (auto hc = MakeStrongPtr(c->GetComponent<HealthComponent>(HealthComponent::g_Name))) hp = hc->GetHealth();
        fprintf(stderr, "[autoplay] t=%lld %s pos=%.1f,%.1f vel=%.2f,%.2f st=%d air=%d climb=%d ground=%d hp=%d anim=%s\n",
                (long long)s_TimeMs, why, p.x, p.y, v.x, v.y, st, air, climb, ground, hp, anim.c_str());
    }

    void Run1(const std::string &line)
    {
        char cmd[32] = {0};
        sscanf(line.c_str(), "%31s", cmd);
        std::string rest = line.size() > strlen(cmd) ? line.substr(strlen(cmd)) : "";
        while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        if (!strcmp(cmd, "wait")) s_WaitMs = atoi(rest.c_str());
        else if (!strcmp(cmd, "level"))
        {
            int n = 1, cp = 0;
            sscanf(rest.c_str(), "%d %d", &n, &cp);
            IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Menu_LoadGame(n, cp == 0, cp)));
        }
        else if (!strcmp(cmd, "custom"))
        {
            // custom FILE.WWD: start a custom level from the custom/ folder
            bool found = false;
            for (const CustomLevels::Entry& e : CustomLevels::List())
            {
                if (strcasecmp(e.fileName.c_str(), rest.c_str()) != 0) continue;
                found = true;
                fprintf(stderr, "[autoplay] custom %s: '%s' base level %d start %d,%d\n", e.fileName.c_str(),
                        e.levelName.c_str(), e.baseLevel, e.startX, e.startY);
                if (e.baseLevel < 1 || e.baseLevel > 14) break;
                g_pApp->GetHumanView()->StartCustomLevel(e);
                break;
            }
            if (!found) fprintf(stderr, "[autoplay] custom level %s not found\n", rest.c_str());
        }
        else if (!strcmp(cmd, "down")) Key(KeyFrom(rest), true);
        else if (!strcmp(cmd, "up")) Key(KeyFrom(rest), false);
        else if (!strcmp(cmd, "tap"))
        {
            size_t sp = rest.rfind(' ');
            std::string k = sp == std::string::npos ? rest : rest.substr(0, sp);
            int ms = sp == std::string::npos ? 50 : atoi(rest.c_str() + sp + 1);
            SDL_Keycode kc = KeyFrom(k);
            Key(kc, true);
            s_Ups.push_back({kc, s_TimeMs + ms});
        }
        else if (!strcmp(cmd, "watch"))
        {
            std::istringstream in(rest); in >> s_Watch; s_WatchEvery = 250; in >> s_WatchEvery; s_WatchAcc = 0;
            if (!s_WatchEvery) s_Watch.clear();
            LogWatch();
        }
        else if (!strcmp(cmd, "log")) { s_LogEvery = atoi(rest.c_str()); s_LogAcc = 0; LogClaw("log"); }
        else if (!strcmp(cmd, "mark")) LogClaw(rest.c_str());
        else if (!strcmp(cmd, "cmd"))
        {
            if (g_pApp && g_pApp->GetHumanView())
                CommandHandler::HandleCommand(rest.c_str(), (void*)g_pApp->GetHumanView()->GetConsole().get());
        }
        else if (!strcmp(cmd, "shot")) s_Shot = rest;
        else if (!strcmp(cmd, "damage"))
        {
            // damage NAME N: damage the nearest actor whose name has NAME (from Claw)
            char name[128] = {0}; int n = 10;
            sscanf(rest.c_str(), "%127s %d", name, &n);
            StrongActorPtr c = g_pApp->GetGameLogic()->GetClawActor();
            StrongActorPtr best; double bestD = 1e18;
            for (const auto &kv : g_pApp->GetGameLogic()->GetActorMap())
            {
                const StrongActorPtr &a = kv.second;
                if (!a || !c || a->GetName().find(name) == std::string::npos || !a->GetPositionComponent()) continue;
                double d = (a->GetPositionComponent()->GetPosition() - c->GetPositionComponent()->GetPosition()).Length();
                if (d < bestD) { bestD = d; best = a; }
            }
            if (best)
                if (auto hc = MakeStrongPtr(best->GetComponent<HealthComponent>(HealthComponent::g_Name)))
                    hc->AddHealth(-n, DamageType_MeleeAttack, best->GetPositionComponent()->GetPosition(), c ? c->GetGUID() : INVALID_ACTOR_ID);
        }
        else if (!strcmp(cmd, "hurt"))
        {
            // hurt N [TYPE]: damage Claw by N (TYPE: DamageType number, default 0)
            int n = 10, type = 0;
            sscanf(rest.c_str(), "%d %d", &n, &type);
            StrongActorPtr c = g_pApp->GetGameLogic()->GetClawActor();
            if (c)
                if (auto hc = MakeStrongPtr(c->GetComponent<HealthComponent>(HealthComponent::g_Name)))
                    hc->AddHealth(-n, (DamageType)type, Point(0, 0), INVALID_ACTOR_ID);
        }
        else if (!strcmp(cmd, "quit"))
        {
            SDL_Event q; SDL_zero(q); q.type = SDL_QUIT; SDL_PushEvent(&q);
            s_State = 0;
        }
        else fprintf(stderr, "[autoplay] ? %s\n", line.c_str());
    }
}

bool Autoplay::Active() { if (s_State < 0) Load(); return s_State > 0; }

#ifdef _WIN32
static void MirrorKeys()
{
    static int s_On = -1;
    if (s_On < 0)
    {
        const char *e = getenv("RECLAW_MIRROR_KEYS");
        s_On = (e && *e && *e != '0') ? 1 : 0;
        if (s_On) fprintf(stderr, "[mirror] following the keyboard while unfocused\n");
    }
    if (!s_On || !g_pApp) return;
    static const struct { int vk; SDL_Keycode key; } kMap[] = {
        { VK_LEFT, SDLK_LEFT }, { VK_RIGHT, SDLK_RIGHT }, { VK_UP, SDLK_UP }, { VK_DOWN, SDLK_DOWN },
        { VK_SPACE, SDLK_SPACE }, { VK_LCONTROL, SDLK_LCTRL }, { VK_LMENU, SDLK_LALT },
        { VK_LSHIFT, SDLK_LSHIFT }, { 'Z', SDLK_z }, { 'X', SDLK_x }, { VK_RETURN, SDLK_RETURN }, { VK_ESCAPE, SDLK_ESCAPE },
        { '1', SDLK_1 }, { '2', SDLK_2 }, { '3', SDLK_3 }, { '4', SDLK_4 }, { '5', SDLK_5 },
    };
    static bool s_Down[sizeof(kMap) / sizeof(kMap[0])] = {};
    // Focused: SDL delivers the keys itself; let go of what the mirror held.
    const bool focused = SDL_GetKeyboardFocus() != nullptr;
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); ++i)
    {
        const bool down = !focused && (GetAsyncKeyState(kMap[i].vk) & 0x8000) != 0;
        if (down != s_Down[i]) { s_Down[i] = down; Key(kMap[i].key, down); }
    }
}
#endif

void Autoplay::Tick(uint32_t ms)
{
#ifdef _WIN32
    MirrorKeys();
#endif
    if (!Active()) return;
    s_TimeMs += ms;
    for (size_t i = 0; i < s_Ups.size();)
        if (s_Ups[i].at <= s_TimeMs) { Key(s_Ups[i].key, false); s_Ups.erase(s_Ups.begin() + i); }
        else ++i;
    if (s_LogEvery && (s_LogAcc += ms) >= s_LogEvery) { s_LogAcc = 0; LogClaw("log"); }
    if (s_WatchEvery && (s_WatchAcc += ms) >= s_WatchEvery) { s_WatchAcc = 0; LogWatch(); }
    if (s_WaitMs > 0) { s_WaitMs -= ms; return; }
    while (s_State > 0 && s_WaitMs <= 0 && s_Pc < s_Lines.size())
    {
        fprintf(stderr, "[autoplay] t=%lld > %s\n", (long long)s_TimeMs, s_Lines[s_Pc].c_str());
        Run1(s_Lines[s_Pc++]);
    }
}

void Autoplay::AfterRender(SDL_Renderer *r)
{
    if (s_Shot.empty() || !r) return;
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s && SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
        SDL_SaveBMP(s, (s_Shot + ".bmp").c_str());
    if (s) SDL_FreeSurface(s);
    s_Shot.clear();
}
#else
bool Autoplay::Active() { return false; }
void Autoplay::Tick(uint32_t) {}
void Autoplay::AfterRender(SDL_Renderer *) {}
#endif
