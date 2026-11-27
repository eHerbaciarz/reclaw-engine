#include "PowderKegComponent.h"
#include "PositionComponent.h"
#include "RenderComponent.h"
#include "AnimationComponent.h"
#include "ControllerComponents/HealthComponent.h"
#include "EnemyAI/EnemyAIComponent.h"

#include "../../GameApp/BaseGameApp.h"
#include "../../GameApp/BaseGameLogic.h"
#include "../../Events/Events.h"
#include "../../Events/EventMgr.h"

const char* PowderKegComponent::g_Name = "PowderKegComponent";

namespace
{
    // Gravity of a flying keg (the keg lying on the ground uses its XML value).
    // Measured on the original (La Roca): the keg goes ~100 px above Claw's
    // hands and lands ~180 px away after ~0.8 s - a high, short arc.
    const float kThrownGravityScale = 2.2f;
    // Minimum flight time before hitting something counts (the keg starts
    // right next to Claw and may touch the floor he stands on).
    const int kArmTimeMs = 60;
    // A sudden change of velocity this big (relative to the throw speed)
    // means the keg hit something solid.
    const double kImpactFactor = 0.45;
    const int kMaxFlightTimeMs = 5000;
}

PowderKegComponent::PowderKegComponent()
    :
    m_State(KegState_Idle),
    m_CarrierId(0),
    m_FlightTime(0)
{
}

bool PowderKegComponent::CanBeLifted() const
{
    if (m_State != KegState_Idle)
    {
        return false;
    }
    shared_ptr<HealthComponent> pHealth = MakeStrongPtr(m_pOwner->GetComponent<HealthComponent>(HealthComponent::g_Name));
    return !pHealth || pHealth->GetCurrentHealth() > 0;
}

void PowderKegComponent::MoveTo(const Point& position)
{
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetPosition(m_pOwner->GetGUID(), position);
    m_pOwner->GetPositionComponent()->SetPosition(position);
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Move_Actor(m_pOwner->GetGUID(), position)));
}

void PowderKegComponent::ShowFrame(int imageId)
{
    if (m_ShownFrame == imageId)
    {
        return;
    }
    m_ShownFrame = imageId;
    if (auto pRender = MakeStrongPtr(m_pOwner->GetComponent<ActorRenderComponent>()))
    {
        char name[16];
        snprintf(name, sizeof(name), "frame%03d", imageId);
        pRender->SetImage(name);
    }
}

void PowderKegComponent::OnLifted(uint32 carrierId)
{
    ShowFrame(1);
    m_State = KegState_Carried;
    m_CarrierId = carrierId;
    g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());
    FollowCarrier();
}

void PowderKegComponent::Throw(const Point& position, const Point& velocity, float gravityScale)
{
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    MoveTo(position);
    pPhysics->VActivate(m_pOwner->GetGUID());
    pPhysics->VSetGravityScale(m_pOwner->GetGUID(), gravityScale >= 0.0f ? gravityScale : kThrownGravityScale);
    pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), velocity);

    m_State = KegState_Thrown;
    m_FlightTime = 0;
    m_ThrowVelocity = velocity;
    m_LastVelocity = velocity;
}

void PowderKegComponent::Drop(const Point& position)
{
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    MoveTo(position);
    pPhysics->VActivate(m_pOwner->GetGUID());
    pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));

    m_State = KegState_Idle;
    m_CarrierId = 0;
    ShowFrame(1);
}

void PowderKegComponent::FollowCarrier()
{
    StrongActorPtr pCarrier = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(m_CarrierId));
    if (!pCarrier || !pCarrier->GetPositionComponent())
    {
        Drop(m_pOwner->GetPositionComponent()->GetPosition());
        return;
    }
    MoveTo(pCarrier->GetPositionComponent()->GetPosition() + GetCarryOffset());
}

void PowderKegComponent::Explode()
{
    m_State = KegState_Idle;
    shared_ptr<HealthComponent> pHealth = MakeStrongPtr(m_pOwner->GetComponent<HealthComponent>(HealthComponent::g_Name));
    if (pHealth && pHealth->GetCurrentHealth() > 0)
    {
        pHealth->AddHealth(-(pHealth->GetCurrentHealth() + 1000), DamageType_Explosion,
            m_pOwner->GetPositionComponent()->GetPosition(), m_CarrierId);
    }
    m_CarrierId = 0;
}

void PowderKegComponent::VUpdate(uint32 msDiff)
{
    if (m_State == KegState_Carried)
    {
        FollowCarrier();
        // turned on its side once Claw holds it above his head (after "lift")
        StrongActorPtr pCarrier = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(m_CarrierId));
        auto pAnim = pCarrier ? MakeStrongPtr(pCarrier->GetComponent<AnimationComponent>(AnimationComponent::g_Name)) : nullptr;
        const std::string anim = pAnim ? pAnim->GetCurrentAnimationName() : "";
        if (anim != "lift" && anim != "ducklift")
        {
            ShowFrame(14);
        }
        return;
    }
    if (m_State == KegState_Thrown)
    {
        // spins: 14, 16, 15, 17, 150 ms each
        static const int kSpin[] = { 14, 16, 15, 17 };
        ShowFrame(kSpin[(m_FlightTime / 150) % 4]);
    }
    if (m_State != KegState_Thrown)
    {
        return;
    }

    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    m_FlightTime += msDiff;

    Point velocity = pPhysics->VGetVelocity(m_pOwner->GetGUID());
    Point change(velocity.x - m_LastVelocity.x, velocity.y - m_LastVelocity.y);
    m_LastVelocity = velocity;

    if (m_FlightTime < kArmTimeMs)
    {
        return;
    }

    double throwSpeed = std::max(1.0, (double)m_ThrowVelocity.Length());
    if (change.Length() > kImpactFactor * throwSpeed ||
        velocity.Length() < 0.05 * throwSpeed ||
        m_FlightTime > kMaxFlightTimeMs)
    {
        Explode();
        return;
    }

    // Enemies do not collide with kegs in the physics world, so check them here.
    SDL_Rect keg = pPhysics->VGetAABB(m_pOwner->GetGUID(), true);
    for (const auto& kv : g_pApp->GetGameLogic()->GetActorMap())
    {
        const StrongActorPtr& pActor = kv.second;
        if (!pActor || pActor.get() == m_pOwner.get())
        {
            continue;
        }
        shared_ptr<EnemyAIComponent> pEnemy = MakeStrongPtr(pActor->GetComponent<EnemyAIComponent>(EnemyAIComponent::g_Name));
        if (!pEnemy || pEnemy->IsDead())
        {
            continue;
        }
        SDL_Rect enemy = pPhysics->VGetAABB(pActor->GetGUID(), true);
        if (SDL_HasIntersection(&keg, &enemy))
        {
            Explode();
            return;
        }
    }
}

void PowderKegComponent::VOnStateRestored()
{
    if (m_State == KegState_Carried)
    {
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());
        FollowCarrier();
    }
}
