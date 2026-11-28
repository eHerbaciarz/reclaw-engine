#include "LeapAIStateComponent.h"
#include "../PhysicsComponent.h"
#include "EnemyAIComponent.h"
#include "../../Actor.h"
#include "../AnimationComponent.h"
#include "../PositionComponent.h"
#include "../RenderComponent.h"
#include "../ControllableComponent.h"
#include "../ControllerComponents/HealthComponent.h"
#include "../../ActorTemplates.h"
#include "../../../Util/Util.h"

#include "../../../GameApp/BaseGameLogic.h"
#include "../../../GameApp/BaseGameApp.h"
#include "../../../Events/EventMgr.h"
#include "../../../Events/Events.h"

const char* LeapAIStateComponent::g_Name = "LeapAIStateComponent";

namespace
{
    const double kGravity = 2000.0;     // CLAW.EXE objects (0x4082e0)
}

LeapAIStateComponent::LeapAIStateComponent()
    :
    BaseEnemyAIStateComponent("LeapState"),
    m_MinDistance(20),
    m_MaxDistance(20),
    m_Chance(15),
    m_CheckEveryMs(500),
    m_CooldownMs(2000),
    m_CheckMs(0),
    m_CooldownLeftMs(0),
    m_bWantLeap(false),
    m_PlannedX(0.0),
    m_Vx(0), m_Vy(0), m_Duration(0), m_Time(0), m_TargetX(0)
{
}

bool LeapAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_Mode, pData->FirstChildElement("Mode"));
    ParseValueFromXmlElem(&m_Animation, pData->FirstChildElement("Animation"));
    ParseValueFromXmlElem(&m_Sound, pData->FirstChildElement("Sound"));
    ParseValueFromXmlElem(&m_MinDistance, pData->FirstChildElement("MinDistance"));
    ParseValueFromXmlElem(&m_MaxDistance, pData->FirstChildElement("MaxDistance"));
    ParseValueFromXmlElem(&m_Chance, pData->FirstChildElement("Chance"));
    ParseValueFromXmlElem(&m_CheckEveryMs, pData->FirstChildElement("CheckEveryMs"));
    ParseValueFromXmlElem(&m_CooldownMs, pData->FirstChildElement("CooldownMs"));
    ParseValueFromXmlElem(&m_ClawMinRange, pData->FirstChildElement("ClawMinRange"));
    ParseValueFromXmlElem(&m_ClawMaxRange, pData->FirstChildElement("ClawMaxRange"));
    ParseValueFromXmlElem(&m_OnClawAttack, pData->FirstChildElement("OnClawAttack"));
    if (m_MaxDistance < m_MinDistance) m_MaxDistance = m_MinDistance;
    return true;
}

bool LeapAIStateComponent::FindTarget(double& outX)
{
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return false;
    }
    if (auto pCtrl = MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name)))
    {
        if (pCtrl->IsDying()) return false;
    }
    const Point me = m_pPositionComponent->GetPosition();
    const Point claw = pClaw->GetPositionComponent()->GetPosition();
    if (fabs(claw.y - me.y) > 50 || fabs(claw.x - me.x) > m_ClawMaxRange || fabs(claw.x - me.x) < m_ClawMinRange)
    {
        return false;
    }
    const double side = claw.x < me.x ? -1.0 : 1.0;     // where Claw is, seen from the boss
    const int d = m_MinDistance + (m_MaxDistance > m_MinDistance ? rand() % (m_MaxDistance - m_MinDistance + 1) : 0);
    double x;
    if (m_Mode == "Over")
    {
        // CLAW.EXE (Raux, 0x481ba0): over a Claw who stands on the ground,
        // landing 20 px past him, inside the boss's own range
        auto pClawPhysics = pClaw->GetPhysicsComponent();
        if (pClawPhysics && (!pClawPhysics->IsOnGround() || pClawPhysics->IsInAir()))
        {
            return false;
        }
        x = claw.x + side * d;
    }
    else
    {
        // "Away": d px from Claw, on the boss's side
        x = claw.x - side * d;
    }
    // stays on the boss's own floor (its patrol path)
    if (auto pPatrol = MakeStrongPtr(m_pOwner->GetComponent<PatrolEnemyAIStateComponent>(PatrolEnemyAIStateComponent::g_Name)))
    {
        const int l = pPatrol->GetLeftPatrolBorder(), r = pPatrol->GetRightPatrolBorder();
        if (l < r && (x < l || x > r))
        {
            // a shorter flip that still keeps the minimum distance
            const double clamped = std::max((double)l, std::min((double)r, x));
            if (m_Mode == "Over" || fabs(clamped - claw.x) < m_MinDistance)
            {
                return false;
            }
            x = clamped;
        }
    }
    // no wall in the way
    RaycastResult ray = g_pApp->GetGameLogic()->VGetGamePhysics()->VRayCast(
        Point(me.x, me.y - 20), Point(x, me.y - 20), CollisionFlag_Solid);
    if (ray.foundIntersection)
    {
        return false;
    }
    outX = x;
    return fabs(x - me.x) > (m_Mode == "Over" ? 10.0 : 60.0);
}

bool LeapAIStateComponent::VCanEnter()
{
    return m_IsActive || (m_bWantLeap && !m_pEnemyAIComponent->IsDead());
}

void LeapAIStateComponent::VUpdate(uint32 msDiff)
{
    if (!m_IsActive)
    {
        m_CooldownLeftMs = std::max(0, m_CooldownLeftMs - (int)msDiff);
        BaseEnemyAIStateComponent* pCurrent = m_pEnemyAIComponent->GetCurrentState();
        if (!pCurrent || m_CooldownLeftMs > 0 || m_pEnemyAIComponent->IsDead() ||
            (pCurrent->VGetStateType() != EnemyAIState_Patrolling &&
             pCurrent->VGetStateType() != EnemyAIState_MeleeAttacking))
        {
            m_CheckMs = 0;
            return;
        }
        bool roll;
        if (m_OnClawAttack)
        {
            // a new blow by Claw, facing the boss
            bool attacking = false;
            StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
            auto pCtrl = pClaw ? MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name)) : nullptr;
            if (pCtrl && pCtrl->IsMeleeAttacking())
            {
                const bool bossRight = m_pPositionComponent->GetX() > pClaw->GetPositionComponent()->GetX();
                attacking = (pCtrl->GetFacing() == Direction_Right) == bossRight;
            }
            roll = attacking && !m_ClawWasAttacking;
            m_ClawWasAttacking = attacking;
        }
        else
        {
            if ((m_CheckMs += msDiff) < m_CheckEveryMs)
            {
                return;
            }
            m_CheckMs = 0;
            roll = true;
        }
        double x;
        if (roll && rand() % 100 < m_Chance && FindTarget(x))
        {
            m_PlannedX = x;
            m_bWantLeap = true;
            m_pEnemyAIComponent->EnterBestState(true);
            m_bWantLeap = false;
        }
        return;
    }

    // flying on the parabola (set every step; the body does not collide meanwhile)
    m_Time = std::min(m_Duration, m_Time + msDiff / 1000.0);
    Point pos(m_Start.x + m_Vx * m_Time, m_Start.y + m_Vy * m_Time + 0.5 * kGravity * m_Time * m_Time);
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    pPhysics->VSetPosition(m_pOwner->GetGUID(), pos);
    pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
    if (m_Time >= m_Duration)
    {
        pPhysics->VSetPosition(m_pOwner->GetGUID(), Point(m_TargetX, m_Start.y));
        m_CooldownLeftMs = m_CooldownMs;
        m_IsActive = false;      // let the best state take over again
        m_pEnemyAIComponent->EnterBestState(true);
    }
}

void LeapAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    m_Start = m_pPositionComponent->GetPosition();
    m_TargetX = m_PlannedX;
    // lowest launch speed for the distance (as 0x481ba0 / 0x43e2e0)
    const double dx = m_TargetX - m_Start.x;
    const double dist = fabs(dx);
    const double v2 = kGravity * dist;
    const double t2 = 2.0 * v2 / (kGravity * kGravity);
    m_Duration = sqrt(std::max(0.01, t2));
    m_Vx = dx / m_Duration;
    m_Vy = -0.5 * kGravity * m_Duration;
    m_Time = 0.0;

    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (pClaw)
    {
        // faces Claw when landing (Over) / while flipping (Away)
        const double clawX = pClaw->GetPositionComponent()->GetX();
        const bool faceRight = (m_Mode == "Over") ? (m_TargetX < clawX) : (clawX > m_Start.x);
        m_pRenderComponent->SetMirrored(faceRight);
    }
    if (!m_Animation.empty())
    {
        m_pAnimationComponent->SetAnimation(m_Animation);
    }
    if (!m_Sound.empty())
    {
        SoundInfo sound(m_Sound);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(sound)));
    }
}

void LeapAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    if (m_IsActive)
    {
        // interrupted (hit, fight over): land where it is now
        m_CooldownLeftMs = m_CooldownMs;
    }
    m_IsActive = false;
}

//=====================================================================================================================
// StepAIStateComponent
//=====================================================================================================================

const char* StepAIStateComponent::g_Name = "StepAIStateComponent";

StepAIStateComponent::StepAIStateComponent()
    : BaseEnemyAIStateComponent("StepState")
{
}

bool StepAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_Animation, pData->FirstChildElement("Animation"));
    ParseValueFromXmlElem(&m_NearRange, pData->FirstChildElement("NearRange"));
    ParseValueFromXmlElem(&m_BackChance, pData->FirstChildElement("BackChance"));
    ParseValueFromXmlElem(&m_BackMin, pData->FirstChildElement("BackMin"));
    ParseValueFromXmlElem(&m_BackMax, pData->FirstChildElement("BackMax"));
    ParseValueFromXmlElem(&m_MidRange, pData->FirstChildElement("MidRange"));
    ParseValueFromXmlElem(&m_AheadChance, pData->FirstChildElement("AheadChance"));
    ParseValueFromXmlElem(&m_AheadMin, pData->FirstChildElement("AheadMin"));
    ParseValueFromXmlElem(&m_AheadMax, pData->FirstChildElement("AheadMax"));
    ParseValueFromXmlElem(&m_Speed, pData->FirstChildElement("Speed"));
    ParseValueFromXmlElem(&m_CheckEveryMs, pData->FirstChildElement("CheckEveryMs"));
    return true;
}

bool StepAIStateComponent::Plan()
{
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return false;
    }
    const Point me = m_pPositionComponent->GetPosition();
    const Point claw = pClaw->GetPositionComponent()->GetPosition();
    const double dx = fabs(claw.x - me.x);
    if (fabs(claw.y - me.y) > 50 || dx > m_MidRange)
    {
        return false;
    }
    const double toward = claw.x < me.x ? -1.0 : 1.0;
    const int roll = rand() % 100;
    double move;
    if (dx <= m_NearRange)
    {
        if (roll >= m_BackChance) return false;
        move = -toward * (m_BackMin + rand() % (std::max(0, m_BackMax - m_BackMin) + 1));
    }
    else
    {
        if (roll >= m_AheadChance) return false;
        move = toward * (m_AheadMin + rand() % (std::max(0, m_AheadMax - m_AheadMin) + 1));
    }
    double x = me.x + move;
    // inside its own patrol range, and floor under the spot (0x421ad0)
    if (auto pPatrol = MakeStrongPtr(m_pOwner->GetComponent<PatrolEnemyAIStateComponent>(PatrolEnemyAIStateComponent::g_Name)))
    {
        const int l = pPatrol->GetLeftPatrolBorder(), r = pPatrol->GetRightPatrolBorder();
        if (l < r && (x < l || x > r)) return false;
    }
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    if (pPhysics->VRayCast(Point(me.x, me.y - 20), Point(x, me.y - 20), CollisionFlag_Solid).foundIntersection) return false;
    if (!pPhysics->VRayCast(Point(x, me.y), Point(x, me.y + 120), CollisionFlag_Solid | CollisionFlag_Ground).foundIntersection) return false;
    m_TargetX = x;
    m_Dir = move < 0 ? -1.0 : 1.0;
    m_FaceRight = toward > 0;
    return true;
}

void StepAIStateComponent::VUpdate(uint32 msDiff)
{
    if (!m_IsActive)
    {
        BaseEnemyAIStateComponent* pCurrent = m_pEnemyAIComponent->GetCurrentState();
        if (!pCurrent || m_pEnemyAIComponent->IsDead() ||
            (pCurrent->VGetStateType() != EnemyAIState_Patrolling &&
             pCurrent->VGetStateType() != EnemyAIState_MeleeAttacking))
        {
            m_CheckMs = 0;
            return;
        }
        if ((m_CheckMs += msDiff) < m_CheckEveryMs)
        {
            return;
        }
        m_CheckMs = 0;
        if (Plan())
        {
            m_bWant = true;
            m_pEnemyAIComponent->EnterBestState(true);
            m_bWant = false;
        }
        return;
    }

    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    Point vel = pPhysics->VGetVelocity(m_pOwner->GetGUID());
    pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(m_Dir * m_Speed / 75.0, vel.y));
    m_pRenderComponent->SetMirrored(m_FaceRight);
    m_TimeLeft -= (int)msDiff;
    const double x = m_pPositionComponent->GetX();
    if ((m_Dir > 0 && x >= m_TargetX) || (m_Dir < 0 && x <= m_TargetX) || m_TimeLeft <= 0)
    {
        pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, vel.y));
        m_IsActive = false;
        m_pEnemyAIComponent->EnterBestState(true);
    }
}

void StepAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    const double dist = fabs(m_TargetX - m_pPositionComponent->GetX());
    m_TimeLeft = (int)(dist / m_Speed * 1000.0) + 300;
    m_pRenderComponent->SetMirrored(m_FaceRight);
    if (!m_Animation.empty())
    {
        m_pAnimationComponent->SetAnimation(m_Animation);
    }
}

void StepAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    if (m_IsActive)
    {
        g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
    }
    m_IsActive = false;
}

//=====================================================================================================================
// WolvingtonMovesAIStateComponent
//=====================================================================================================================

const char* WolvingtonMovesAIStateComponent::g_Name = "WolvingtonMovesAIStateComponent";

WolvingtonMovesAIStateComponent::WolvingtonMovesAIStateComponent()
    : BaseEnemyAIStateComponent("WolvingtonMovesState")
{
}

bool WolvingtonMovesAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_NearRange, pData->FirstChildElement("NearRange"));
    ParseValueFromXmlElem(&m_CloseRange, pData->FirstChildElement("CloseRange"));
    ParseValueFromXmlElem(&m_FarRange, pData->FirstChildElement("FarRange"));
    ParseValueFromXmlElem(&m_LevelRange, pData->FirstChildElement("LevelRange"));
    ParseValueFromXmlElem(&m_BlowChance, pData->FirstChildElement("BlowChance"));
    ParseValueFromXmlElem(&m_MagicChance, pData->FirstChildElement("MagicChance"));
    ParseValueFromXmlElem(&m_RunChance, pData->FirstChildElement("RunChance"));
    ParseValueFromXmlElem(&m_RunSpeed, pData->FirstChildElement("RunSpeed"));
    ParseValueFromXmlElem(&m_BackSpeed, pData->FirstChildElement("BackSpeed"));
    ParseValueFromXmlElem(&m_UppercutDamage, pData->FirstChildElement("UppercutDamage"));
    ParseValueFromXmlElem(&m_SwordDamage, pData->FirstChildElement("SwordDamage"));
    ParseValueFromXmlElem(&m_CheckEveryMs, pData->FirstChildElement("CheckEveryMs"));
    ParseValueFromXmlElem(&m_CounterDelayMs, pData->FirstChildElement("CounterDelayMs"));
    std::string proto;
    if (ParseValueFromXmlElem(&proto, pData->FirstChildElement("MagicPrototype")))
    {
        m_MagicProto = StringToEnum_ActorPrototype(proto);
    }
    return true;
}

void WolvingtonMovesAIStateComponent::VPostInit()
{
    BaseEnemyAIStateComponent::VPostInit();
}

bool WolvingtonMovesAIStateComponent::ClawLevel(double& dx, double& absDx, bool& ducking)
{
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return false;
    }
    const Point me = m_pPositionComponent->GetPosition();
    const Point claw = pClaw->GetPositionComponent()->GetPosition();
    dx = claw.x - me.x;
    absDx = fabs(dx);
    ducking = false;
    if (auto pCtrl = MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name)))
    {
        if (pCtrl->IsDying())
        {
            return false;
        }
        ducking = pCtrl->IsDucking();
    }
    return fabs(claw.y - me.y) <= m_LevelRange;
}

WolvingtonMovesAIStateComponent::Move WolvingtonMovesAIStateComponent::Decide()
{
    double dx, absDx;
    bool ducking;
    if (!ClawLevel(dx, absDx, ducking) || absDx >= m_FarRange || absDx < m_CloseRange)
    {
        return Move_None;
    }
    const int roll = rand() % 1000;
    if (absDx < m_NearRange)
    {
        if (roll < m_BlowChance)
        {
            return ducking ? Move_Sword : Move_Uppercut;
        }
        return Move_Back;
    }
    if (roll < m_MagicChance) return Move_Magic;
    if (roll < m_MagicChance + m_RunChance) return Move_Run;
    return Move_None; // advance: his patrol walks on
}

void WolvingtonMovesAIStateComponent::Face(double dx)
{
    m_pRenderComponent->SetMirrored(dx > 0);
}

void WolvingtonMovesAIStateComponent::VUpdate(uint32 msDiff)
{
    if (!m_IsActive)
    {
        // a hit: 500 ms after it the run and uppercut (0x4a4380)
        if (auto pHealth = MakeStrongPtr(m_pOwner->GetComponent<HealthComponent>(HealthComponent::g_Name)))
        {
            const int hp = pHealth->GetCurrentHealth();
            if (m_LastHealth >= 0 && hp < m_LastHealth && hp > 0)
            {
                m_CounterMs = m_CounterDelayMs;
            }
            m_LastHealth = hp;
        }
        BaseEnemyAIStateComponent* pCurrent = m_pEnemyAIComponent->GetCurrentState();
        if (!pCurrent || m_pEnemyAIComponent->IsDead())
        {
            m_CheckMs = 0;
            return;
        }
        const EnemyAIState type = pCurrent->VGetStateType();
        if (type == EnemyAIState_TakingDamage || type == EnemyAIState_Parry)
        {
            return;
        }
        if (m_CounterMs >= 0)
        {
            m_CounterMs -= (int)msDiff;
            if (m_CounterMs >= 0) return;
            m_CounterMs = -1;
            double dx, absDx;
            bool ducking;
            if (type == EnemyAIState_Patrolling && ClawLevel(dx, absDx, ducking) && absDx < m_FarRange)
            {
                m_Planned = Move_Run;
                m_pEnemyAIComponent->EnterBestState(true);
                m_Planned = Move_None;
            }
            return;
        }
        if (type != EnemyAIState_Patrolling)
        {
            m_CheckMs = 0;
            return;
        }
        if ((m_CheckMs += msDiff) < m_CheckEveryMs)
        {
            return;
        }
        m_CheckMs = 0;
        m_Planned = Decide();
        if (m_Planned != Move_None)
        {
            m_pEnemyAIComponent->EnterBestState(true);
        }
        m_Planned = Move_None;
        return;
    }

    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    const uint32 guid = m_pOwner->GetGUID();
    const Point vel = pPhysics->VGetVelocity(guid);
    m_TimeLeft -= (int)msDiff;

    if (m_Move == Move_Back || m_Move == Move_Run)
    {
        const double x = m_pPositionComponent->GetX();
        double dx, absDx;
        bool ducking;
        const bool level = ClawLevel(dx, absDx, ducking);
        bool stop = m_TimeLeft <= 0;
        if (m_Move == Move_Back)
        {
            stop = stop || (m_Dir > 0 && x >= m_TargetX) || (m_Dir < 0 && x <= m_TargetX);
        }
        else if (level && absDx < m_NearRange)
        {
            // close enough: the uppercut on the spot (0x4a4cc0 -> 0x4a4a70)
            pPhysics->VSetLinearSpeed(guid, Point(0, vel.y));
            m_pAnimationComponent->SetFrameTimeScale(1.0f);
            Face(dx);
            m_Move = Move_Uppercut;
            StartBlow("strike2");
            return;
        }
        // no floor ahead or a wall: stop there
        const double ahead = x + m_Dir * 40.0;
        const Point me = m_pPositionComponent->GetPosition();
        if (pPhysics->VRayCast(Point(me.x, me.y - 20), Point(ahead, me.y - 20), CollisionFlag_Solid).foundIntersection ||
            !pPhysics->VRayCast(Point(ahead, me.y), Point(ahead, me.y + 120), CollisionFlag_Solid | CollisionFlag_Ground).foundIntersection)
        {
            stop = true;
        }
        if (stop)
        {
            pPhysics->VSetLinearSpeed(guid, Point(0, vel.y));
            Finish();
            return;
        }
        const double speed = m_Move == Move_Run ? m_RunSpeed : m_BackSpeed;
        pPhysics->VSetLinearSpeed(guid, Point(m_Dir * speed / 75.0, vel.y));
        return;
    }

    // a blow or a shot: its frame, then the end of the animation
    pPhysics->VSetLinearSpeed(guid, Point(0, vel.y));
    std::shared_ptr<Animation> pAnim = m_pAnimationComponent->GetCurrentAnimation();
    if (!pAnim || pAnim->GetName() != m_Anim)
    {
        Finish();
        return;
    }
    const int frame = (int)pAnim->GetCurrentAnimationFrame()->idx;
    const bool facingRight = m_pRenderComponent->IsMirrored();
    const double side = facingRight ? 1.0 : -1.0;
    const Direction dir = facingRight ? Direction_Right : Direction_Left;
    const int hitFrame = m_Move == Move_Uppercut ? 5 : m_Move == Move_Sword ? 3 : 4;
    if (!m_HitDone && frame >= hitFrame)
    {
        m_HitDone = true;
        const Point me = m_pPositionComponent->GetPosition();
        if (m_Move == Move_Uppercut)
        {
            // CLAW.EXE's box for it: (-59,-63)..(-36,-8) in front of him
            ActorTemplates::CreateAreaDamage(Point(me.x + side * 48, me.y - 36), Point(40, 56),
                m_UppercutDamage, CollisionFlag_EnemyAIAttack, "Rectangle", DamageType_MeleeAttack, dir, m_pOwner->GetGUID());
            Util::PlayRandomSoundFromList(m_pEnemyAIComponent->GetMeleeAttackSounds());
        }
        else if (m_Move == Move_Sword)
        {
            // (-95,1)..(-24,42): low, at Claw's feet
            ActorTemplates::CreateAreaDamage(Point(me.x + side * 60, me.y + 21), Point(71, 41),
                m_SwordDamage, CollisionFlag_EnemyAIAttack, "Rectangle", DamageType_MeleeAttack, dir, m_pOwner->GetGUID());
            Util::PlayRandomSoundFromList(m_pEnemyAIComponent->GetMeleeAttackSounds());
        }
        else if (m_Move == Move_Magic && m_MagicProto != ActorPrototype_None)
        {
            const bool low = (m_ShotIdx & 1) != 0;
            ActorTemplates::CreateActor_Projectile(m_MagicProto, Point(me.x + side * 65, me.y + (low ? 10 : -20)), dir, m_pOwner->GetGUID());
            Util::PlayRandomSoundFromList(m_pEnemyAIComponent->GetRangedAttackSounds());
        }
    }
    const bool ended = (m_HitDone && frame < m_LastFrame) || m_TimeLeft <= 0 ||
        (pAnim->IsAtLastAnimFrame() && m_HitDone && frame > hitFrame && pAnim->GetCurrentAnimationFrame()->duration == 0);
    m_LastFrame = frame;
    if (!ended)
    {
        return;
    }
    if (m_Move == Move_Magic && --m_ShotsLeft > 0)
    {
        ++m_ShotIdx;
        double dx, absDx;
        bool ducking;
        if (ClawLevel(dx, absDx, ducking)) Face(dx);
        StartBlow((m_ShotIdx & 1) ? "strike4" : "strike3");
        return;
    }
    Finish();
}

void WolvingtonMovesAIStateComponent::StartBlow(const std::string& anim)
{
    m_Anim = anim;
    m_HitDone = false;
    m_LastFrame = 0;
    m_TimeLeft = 1500;
    m_pAnimationComponent->SetAnimation(anim);
    m_pAnimationComponent->ResetAnimation();
}

void WolvingtonMovesAIStateComponent::Start(Move move)
{
    m_Move = move;
    double dx = 0, absDx = 0;
    bool ducking;
    ClawLevel(dx, absDx, ducking);
    Face(dx);
    const double toward = dx < 0 ? -1.0 : 1.0;
    switch (move)
    {
        case Move_Uppercut: StartBlow("strike2"); break;
        case Move_Sword: StartBlow("strike1"); break;
        case Move_Magic:
            // 0x4a4600: counter = rand & 3, a shot per step until it reaches 3
            m_ShotIdx = rand() % 4;
            m_ShotsLeft = 4 - m_ShotIdx;
            StartBlow((m_ShotIdx & 1) ? "strike4" : "strike3");
            break;
        case Move_Back:
        {
            m_Dir = -toward;
            m_TargetX = m_pPositionComponent->GetX() + m_Dir * (75 + rand() % 51);
            m_TimeLeft = (int)(fabs(m_TargetX - m_pPositionComponent->GetX()) / m_BackSpeed * 1000.0) + 300;
            m_pAnimationComponent->SetAnimation("advance");
            break;
        }
        case Move_Run:
            m_Dir = toward;
            m_TimeLeft = (int)((double)m_FarRange / m_RunSpeed * 1000.0) + 500;
            m_pAnimationComponent->SetAnimation("fastadvance");
            m_pAnimationComponent->SetFrameTimeScale(0.25f);
            break;
        default: break;
    }
}

void WolvingtonMovesAIStateComponent::Finish()
{
    m_pAnimationComponent->SetFrameTimeScale(1.0f);
    m_IsActive = false;
    m_Move = Move_None;
    m_pEnemyAIComponent->EnterBestState(true);
}

void WolvingtonMovesAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    Start(m_Planned);
}

void WolvingtonMovesAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    if (m_IsActive)
    {
        g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
        m_pAnimationComponent->SetFrameTimeScale(1.0f);
    }
    m_IsActive = false;
    m_Move = Move_None;
}
