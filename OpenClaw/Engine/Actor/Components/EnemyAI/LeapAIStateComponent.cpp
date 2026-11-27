#include "LeapAIStateComponent.h"
#include "../PhysicsComponent.h"
#include "EnemyAIComponent.h"
#include "../../Actor.h"
#include "../AnimationComponent.h"
#include "../PositionComponent.h"
#include "../RenderComponent.h"
#include "../ControllableComponent.h"

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
    if (fabs(claw.y - me.y) > 50 || fabs(claw.x - me.x) > 300)
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
        if ((m_CheckMs += msDiff) < m_CheckEveryMs)
        {
            return;
        }
        m_CheckMs = 0;
        double x;
        if (rand() % 100 < m_Chance && FindTarget(x))
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
