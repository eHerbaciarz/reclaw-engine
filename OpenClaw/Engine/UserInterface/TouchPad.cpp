#include "TouchPad.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Actor/Actor.h"
#include "../Actor/Components/AnimationComponent.h"
#include "../Actor/Components/Animation.h"
#include "../Actor/Components/RenderComponent.h"
#include "../Actor/Components/ControllerComponents/AmmoComponent.h"
#include "../Graphics2D/Image.h"
#include "../Platform/Events/AppEvent.h"

namespace
{
    // The controls, in the game's coordinates as fractions of the screen
    // height (h) from the left / right edge and the bottom.
    enum Control { DPad, Jump, Shoot, Sword, Lift, Weapon, Map, Pause, ControlCount };

    enum Anchor { Left, Right, Middle };

    struct Button
    {
        Control id;
        GamepadButton button;
        Anchor anchor;          // x counts from the left edge, the right edge or the middle
        float x, y, r;          // centre and radius, in screen heights
        const char* anims[6];   // Claw's animations for the icon (first found)
        int frame;              // frame shown when idle
    };

    // Xbox-style diamond: jump bottom (A), shoot right (B), sword left (X), lift top (Y)
    const Button kButtons[] = {
        { Jump,   GamepadButton::A,          Right, -0.25f, 0.84f, 0.095f, { "jump" }, 2 },
        { Shoot,  GamepadButton::B,          Right, -0.13f, 0.67f, 0.095f, { "pistol" }, 2 },
        { Sword,  GamepadButton::X,          Right, -0.37f, 0.67f, 0.095f, { "swipe" }, 2 },
        { Lift,   GamepadButton::Y,          Right, -0.25f, 0.50f, 0.080f, { "lift", "pickup", "liftup", "lift1", "lift2" }, 1 },
        { Weapon, GamepadButton::LeftBumper, Right, -0.10f, 0.45f, 0.060f, { "magic" }, 2 },
        // top middle: the HUD is in the corners
        { Map,    GamepadButton::Back,       Middle, -0.08f, 0.08f, 0.055f, { nullptr }, 0 },
        { Pause,  GamepadButton::Start,      Middle,  0.08f, 0.08f, 0.055f, { nullptr }, 0 },
    };
    const float kDPadX = 0.27f, kDPadY = 0.72f, kDPadR = 0.20f;

    struct Finger { Control control; GamepadButton dirs[2]; int nDirs; };

    std::map<SDL_FingerID, Finger> g_Fingers;
    bool g_Pressed[ControlCount] = {};
    int g_DirCount[4] = {};                 // fingers holding each D-pad direction
    Uint32 g_PressStart[ControlCount] = {};
    bool g_HiddenByController = false;
    bool g_TouchSeen = false;
    SDL_Texture* g_Circle = nullptr;
    SDL_Renderer* g_CircleRenderer = nullptr;
    int g_LastW = 0, g_LastH = 0;           // size of the game image (render target)

    // On-screen controls only on Android (the 3DS touch screen works the menu,
    // SecondScreen; a Steam Deck has its own buttons).
    bool HasTouch()
    {
#if defined(__ANDROID__)
        return true;
#else
        (void)g_TouchSeen;
        return false;
#endif
    }

    bool Playing()
    {
        if (!g_pApp || !g_pApp->GetGameLogic()) return false;
        return g_pApp->GetGameLogic()->GetGameState() == GameState_IngameRunning && !g_pApp->IsMenuActive();
    }

    void SendButton(GamepadButton b, bool down)
    {
        AppEvent e;
        e.type = down ? AppEventType::GamepadButtonDown : AppEventType::GamepadButtonUp;
        e.gamepadButton.gamepadIndex = 0;
        e.gamepadButton.button = b;
        e.gamepadButton.value = down ? 1.0f : 0.0f;
        g_pApp->OnAppEvent(e);
    }

    int DirIndex(GamepadButton b)
    {
        return b == GamepadButton::DPadLeft ? 0 : b == GamepadButton::DPadRight ? 1 : b == GamepadButton::DPadUp ? 2 : 3;
    }

    void HoldDir(GamepadButton b, bool down)
    {
        int& n = g_DirCount[DirIndex(b)];
        if (down) { if (n++ == 0) SendButton(b, true); }
        else if (n > 0 && --n == 0) SendButton(b, false);
    }

    // Game image size and where it sits in the window (as PresentFrame draws it).
    void Layout(float& outX, float& outY, float& outScale)
    {
        int winW = 0, winH = 0;
        SDL_Renderer* r = g_pApp->GetRenderer();
        SDL_GetRendererOutputSize(r, &winW, &winH);
        int w = g_LastW, h = g_LastH;
        if (w <= 0 || h <= 0) { w = winW; h = winH; }
        float fit = std::min((float)winW / w, (float)winH / h);
        outScale = fit;
        outX = (winW - w * fit) / 2;
        outY = (winH - h * fit) / 2;
    }

    // Finger position (0..1 of the window) in game coordinates.
    void ToGame(float fx, float fy, float& gx, float& gy)
    {
        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(g_pApp->GetRenderer(), &winW, &winH);
        float ox, oy, s;
        Layout(ox, oy, s);
        gx = (fx * winW - ox) / s;
        gy = (fy * winH - oy) / s;
    }

    void Centre(const Button& b, float& cx, float& cy, float& cr)
    {
        float h = (float)g_LastH, w = (float)g_LastW;
        cx = b.anchor == Right ? w + b.x * h : (b.anchor == Middle ? w / 2 + b.x * h : b.x * h);
        cy = b.y * h;
        cr = b.r * h;
    }

    // The D-pad directions under a point (up to two: diagonals).
    int DirsAt(float gx, float gy, GamepadButton out[2])
    {
        float h = (float)g_LastH;
        float dx = gx - kDPadX * h, dy = gy - kDPadY * h;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < kDPadR * h * 0.18f) return 0;    // dead centre
        float a = std::atan2(-dy, dx) * 180.0f / 3.14159265f;   // 0 = right, 90 = up
        if (a < 0) a += 360.0f;
        int n = 0;
        if (a < 67.5f || a >= 292.5f) out[n++] = GamepadButton::DPadRight;
        if (a >= 112.5f && a < 247.5f) out[n++] = GamepadButton::DPadLeft;
        if (a >= 22.5f && a < 157.5f) out[n++] = GamepadButton::DPadUp;
        if (a >= 202.5f && a < 337.5f) out[n++] = GamepadButton::DPadDown;
        return n;
    }

    bool InDPad(float gx, float gy, float extra)
    {
        float h = (float)g_LastH;
        float dx = gx - kDPadX * h, dy = gy - kDPadY * h;
        return dx * dx + dy * dy <= (kDPadR * h * extra) * (kDPadR * h * extra);
    }

    const Button* ButtonAt(float gx, float gy)
    {
        const Button* best = nullptr;
        float bestD = 1e9f;
        for (const Button& b : kButtons)
        {
            float cx, cy, cr;
            Centre(b, cx, cy, cr);
            float d = std::sqrt((gx - cx) * (gx - cx) + (gy - cy) * (gy - cy));
            if (d <= cr * 1.25f && d < bestD) { best = &b; bestD = d; }
        }
        return best;
    }

    void FingerDown(SDL_FingerID id, float gx, float gy)
    {
        Finger f = { ControlCount, {}, 0 };
        if (InDPad(gx, gy, 1.3f) || (gx < g_LastW * 0.45f && !ButtonAt(gx, gy)))
        {
            f.control = DPad;
            f.nDirs = DirsAt(gx, gy, f.dirs);
            for (int i = 0; i < f.nDirs; ++i) HoldDir(f.dirs[i], true);
            g_Pressed[DPad] = true;
        }
        else if (const Button* b = ButtonAt(gx, gy))
        {
            f.control = b->id;
            if (!g_Pressed[b->id]) g_PressStart[b->id] = SDL_GetTicks();
            g_Pressed[b->id] = true;
            SendButton(b->button, true);
        }
        g_Fingers[id] = f;
    }

    void FingerMove(SDL_FingerID id, float gx, float gy)
    {
        auto it = g_Fingers.find(id);
        if (it == g_Fingers.end() || it->second.control != DPad) return;
        Finger& f = it->second;
        GamepadButton dirs[2];
        int n = DirsAt(gx, gy, dirs);
        for (int i = 0; i < f.nDirs; ++i)
        {
            if (std::find(dirs, dirs + n, f.dirs[i]) == dirs + n) HoldDir(f.dirs[i], false);
        }
        for (int i = 0; i < n; ++i)
        {
            if (std::find(f.dirs, f.dirs + f.nDirs, dirs[i]) == f.dirs + f.nDirs) HoldDir(dirs[i], true);
        }
        std::copy(dirs, dirs + n, f.dirs);
        f.nDirs = n;
    }

    void FingerUp(SDL_FingerID id)
    {
        auto it = g_Fingers.find(id);
        if (it == g_Fingers.end()) return;
        Finger f = it->second;
        g_Fingers.erase(it);
        if (f.control == DPad)
        {
            for (int i = 0; i < f.nDirs; ++i) HoldDir(f.dirs[i], false);
            bool other = false;
            for (auto& o : g_Fingers) if (o.second.control == DPad) other = true;
            g_Pressed[DPad] = other;
        }
        else if (f.control < ControlCount)
        {
            bool other = false;
            for (auto& o : g_Fingers) if (o.second.control == f.control) other = true;
            if (!other)
            {
                g_Pressed[f.control] = false;
                for (const Button& b : kButtons) if (b.id == f.control) SendButton(b.button, false);
            }
        }
    }

    //------------------------------------------------------------ drawing
    SDL_Texture* CircleTexture(SDL_Renderer* r)
    {
        if (g_Circle && g_CircleRenderer == r) return g_Circle;
        const int n = 128;
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_ABGR8888);
        if (!s) return nullptr;
        SDL_LockSurface(s);
        for (int y = 0; y < n; ++y)
        {
            Uint32* row = (Uint32*)((Uint8*)s->pixels + y * s->pitch);
            for (int x = 0; x < n; ++x)
            {
                float dx = x + 0.5f - n / 2.0f, dy = y + 0.5f - n / 2.0f;
                float d = std::sqrt(dx * dx + dy * dy) / (n / 2.0f);   // 0 centre .. 1 edge
                float fill = d < 1.0f ? 0.55f : 0.0f;
                float ring = std::max(0.0f, 1.0f - (float)std::fabs(d - 0.95f) / 0.05f);   // light rim
                float edge = std::min(1.0f, std::max(0.0f, (1.0f - d) * n / 2.0f)); // antialias
                float a = std::max(fill, ring) * edge;
                Uint8 c = (Uint8)(ring > fill ? 235 : 20);
                row[x] = (Uint32)c | ((Uint32)c << 8) | ((Uint32)c << 16) | ((Uint32)(a * 255) << 24);
            }
        }
        SDL_UnlockSurface(s);
        if (g_Circle) SDL_DestroyTexture(g_Circle);
        g_Circle = SDL_CreateTextureFromSurface(r, s);
        g_CircleRenderer = r;
        SDL_FreeSurface(s);
        if (g_Circle)
        {
            SDL_SetTextureBlendMode(g_Circle, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(g_Circle, SDL_ScaleModeLinear);
        }
        return g_Circle;
    }

    void DrawCircle(SDL_Renderer* r, float cx, float cy, float rad, Uint8 alpha, Uint8 tint = 255)
    {
        SDL_Texture* t = CircleTexture(r);
        if (!t) return;
        SDL_SetTextureAlphaMod(t, alpha);
        SDL_SetTextureColorMod(t, 255, tint, tint);
        SDL_Rect d = { (int)(cx - rad), (int)(cy - rad), (int)(rad * 2), (int)(rad * 2) };
        SDL_RenderCopy(r, t, NULL, &d);
    }

    // A frame of one of Claw's animations, fitted into the circle.
    void DrawClaw(SDL_Renderer* r, const Button& b, float cx, float cy, float rad, bool pressed)
    {
        StrongActorPtr claw = g_pApp->GetGameLogic()->GetClawActor();
        if (!claw) return;
        shared_ptr<AnimationComponent> anim = MakeStrongPtr(claw->GetComponent<AnimationComponent>(AnimationComponent::g_Name));
        shared_ptr<ActorRenderComponent> render = MakeStrongPtr(claw->GetComponent<ActorRenderComponent>(ActorRenderComponent::g_Name));
        if (!anim || !render) return;

        const char* name = nullptr;
        std::shared_ptr<Animation> a;
        if (b.id == Shoot)
        {
            // The weapon Claw has in hand
            shared_ptr<AmmoComponent> ammo = MakeStrongPtr(claw->GetComponent<AmmoComponent>(AmmoComponent::g_Name));
            AmmoType t = ammo ? ammo->GetActiveAmmoType() : AmmoType_Pistol;
            const char* weaponAnim = t == AmmoType_Magic ? "magic" : (t == AmmoType_Dynamite ? "postdynamite" : "pistol");
            a = anim->FindAnimation(weaponAnim);
        }
        for (int i = 0; !a && i < 6 && b.anims[i]; ++i) a = anim->FindAnimation(b.anims[i]);
        (void)name;
        if (!a || a->GetAnimFramesSize() == 0) return;

        const std::vector<AnimationFrame>& frames = a->GetAnimFrames();
        int idx = std::min(b.frame, (int)frames.size() - 1);
        if (pressed)
        {
            // Held: Claw does it (the animation plays, about 12 frames a second)
            idx = (int)(((SDL_GetTicks() - g_PressStart[b.id]) / 80) % frames.size());
        }
        shared_ptr<Image> img = MakeStrongPtr(render->GetImage(frames[idx].imageName));
        if (!img || !img->GetTexture()) return;
        float iw = (float)img->GetWidth(), ih = (float)img->GetHeight();
        float s = std::min(rad * 1.5f / iw, rad * 1.5f / ih);
        SDL_Rect d = { (int)(cx - iw * s / 2), (int)(cy - ih * s / 2), (int)(iw * s), (int)(ih * s) };
        SDL_SetTextureAlphaMod(img->GetTexture(), pressed ? 255 : 210);
        SDL_RenderCopy(r, img->GetTexture(), NULL, &d);
        SDL_SetTextureAlphaMod(img->GetTexture(), 255);
    }

    void DrawSymbol(SDL_Renderer* r, Control id, float cx, float cy, float rad)
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 246, 205, 86, 230);
        float u = rad * 0.32f;
        if (id == Pause)
        {
            SDL_Rect a = { (int)(cx - u), (int)(cy - u * 1.2f), (int)(u * 0.7f), (int)(u * 2.4f) };
            SDL_Rect b = { (int)(cx + u * 0.3f), (int)(cy - u * 1.2f), (int)(u * 0.7f), (int)(u * 2.4f) };
            SDL_RenderFillRect(r, &a);
            SDL_RenderFillRect(r, &b);
        }
        else if (id == Map)
        {
            // a folded map: three panels
            for (int i = 0; i < 3; ++i)
            {
                SDL_Rect p = { (int)(cx - u * 1.5f + i * u), (int)(cy - u * (i == 1 ? 0.9f : 1.2f)), (int)(u * 0.85f), (int)(u * 2.2f) };
                SDL_RenderFillRect(r, &p);
            }
        }
    }

    void DrawDPad(SDL_Renderer* r)
    {
        float h = (float)g_LastH;
        float cx = kDPadX * h, cy = kDPadY * h, rad = kDPadR * h;
        DrawCircle(r, cx, cy, rad, 120);
        // arrows: lit while held
        const GamepadButton dirs[4] = { GamepadButton::DPadLeft, GamepadButton::DPadRight, GamepadButton::DPadUp, GamepadButton::DPadDown };
        const float vx[4] = { -1, 1, 0, 0 }, vy[4] = { 0, 0, -1, 1 };
        for (int i = 0; i < 4; ++i)
        {
            bool on = g_DirCount[DirIndex(dirs[i])] > 0;
            float ax = cx + vx[i] * rad * 0.62f, ay = cy + vy[i] * rad * 0.62f;
            DrawCircle(r, ax, ay, rad * 0.27f, on ? 230 : 140, on ? 120 : 255);
            // a small triangle pointing outwards
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 246, 205, 86, on ? 255 : 200);
            float t = rad * 0.13f;
            for (int k = 0; k < (int)t; ++k)
            {
                float w = (t - k);
                float px = ax + vx[i] * (k - t / 2), py = ay + vy[i] * (k - t / 2);
                if (vx[i] != 0) SDL_RenderDrawLine(r, (int)px, (int)(py - w), (int)px, (int)(py + w));
                else SDL_RenderDrawLine(r, (int)(px - w), (int)py, (int)(px + w), (int)py);
            }
        }
    }
}

namespace TouchPad
{
    bool IsEnabled()
    {
        return HasTouch() && !g_HiddenByController;
    }

    void OnControllerUsed()
    {
        if (!g_Fingers.empty()) return;
        g_HiddenByController = true;
    }

    void ReleaseAll()
    {
        std::vector<SDL_FingerID> ids;
        for (auto& f : g_Fingers) ids.push_back(f.first);
        for (SDL_FingerID id : ids) FingerUp(id);
    }

    bool HandleEvent(const SDL_Event& evt)
    {
        bool finger = evt.type == SDL_FINGERDOWN || evt.type == SDL_FINGERUP || evt.type == SDL_FINGERMOTION;
        if (finger)
        {
            g_TouchSeen = true;
            if (evt.type == SDL_FINGERDOWN) g_HiddenByController = false;
        }
        if (!g_pApp || !HasTouch()) return false;

        if (!Playing())
        {
            // Menus take the touches (as mouse clicks); let go of what was held.
            if (!g_Fingers.empty()) ReleaseAll();
            return false;
        }
        // While playing, the mouse events SDL makes from touches would act as clicks.
        if ((evt.type == SDL_MOUSEBUTTONDOWN || evt.type == SDL_MOUSEBUTTONUP) && evt.button.which == SDL_TOUCH_MOUSEID) return true;
        if (evt.type == SDL_MOUSEMOTION && evt.motion.which == SDL_TOUCH_MOUSEID) return true;
        if (!finger || g_LastH <= 0) return finger;

        float gx, gy;
        ToGame(evt.tfinger.x, evt.tfinger.y, gx, gy);
        if (evt.type == SDL_FINGERDOWN) FingerDown(evt.tfinger.fingerId, gx, gy);
        else if (evt.type == SDL_FINGERMOTION) FingerMove(evt.tfinger.fingerId, gx, gy);
        else FingerUp(evt.tfinger.fingerId);
        return true;
    }

    void Render(SDL_Renderer* r)
    {
        if (!g_pApp || !IsEnabled() || !Playing()) return;
        // Size of what we draw into (the game image)
        int w = 0, h = 0;
        if (SDL_Texture* target = SDL_GetRenderTarget(r)) SDL_QueryTexture(target, NULL, NULL, &w, &h);
        else SDL_GetRendererOutputSize(r, &w, &h);
        float sx = 1.0f, sy = 1.0f;
        SDL_RenderGetScale(r, &sx, &sy);
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        g_LastW = w;
        g_LastH = h;

        DrawDPad(r);
        for (const Button& b : kButtons)
        {
            float cx, cy, cr;
            Centre(b, cx, cy, cr);
            bool on = g_Pressed[b.id];
            DrawCircle(r, cx, cy, cr, on ? 200 : 130, on ? 150 : 255);
            if (b.anims[0]) DrawClaw(r, b, cx, cy, cr, on);
            else DrawSymbol(r, b.id, cx, cy, cr);
        }
        SDL_RenderSetScale(r, sx, sy);
    }
}
