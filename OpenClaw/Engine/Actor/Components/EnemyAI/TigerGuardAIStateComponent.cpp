#include "TigerGuardAIStateComponent.h"
#include "EnemyAIComponent.h"
#include "../../Actor.h"
#include "../AnimationComponent.h"
#include "../PositionComponent.h"
#include "../RenderComponent.h"
#include "../ControllableComponent.h"
#include "../../ActorTemplates.h"

#include "../../../GameApp/BaseGameLogic.h"
#include "../../../GameApp/BaseGameApp.h"
#include "../../../Events/EventMgr.h"
#include "../../../Events/Events.h"

const char* TigerGuardAIStateComponent::g_Name = "TigerGuardAIStateComponent";

namespace
{
    const char* kSoundDir = "/LEVEL14/SOUNDS/TIGERGUARD/";

    void PlayTigerSound(const char* name)
    {
        SoundInfo sound(std::string(kSoundDir) + name);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(sound)));
    }

    // px/s -> Box2D m/s
    double Mps(double px) { return px / 75.0; }
}

TigerGuardAIStateComponent::TigerGuardAIStateComponent()
    :
    BaseEnemyAIStateComponent("TigerGuardState"),
    m_Action(Action_None),
    m_KnifeLeft(0),
    m_ActionMs(0),
    m_CooldownMs(0),
    m_BlockCooldownMs(0),
    m_bWasHit(false),
    m_StartX(0.0),
    m_TargetX(0.0),
    m_Dir(-1.0),
    m_MoveDir(-1.0),
    m_bKnifeAfterFlip(false),
    m_bWasTakingDamage(false)
{
}

TigerGuardAIStateComponent::~TigerGuardAIStateComponent()
{
}

bool TigerGuardAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    m_pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    return true;
}

void TigerGuardAIStateComponent::VPostInit()
{
    BaseEnemyAIStateComponent::VPostInit();
    m_pAnimationComponent->AddObserver(this);
}

Actor* TigerGuardAIStateComponent::GetClaw()
{
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return NULL;
    }
    auto pCtrl = MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name));
    if (pCtrl && pCtrl->IsDying())
    {
        return NULL;
    }
    return pClaw.get();
}

bool TigerGuardAIStateComponent::ClawInReach(double maxDx)
{
    Actor* pClaw = GetClaw();
    if (!pClaw)
    {
        return false;
    }
    const double dx = fabs(pClaw->GetPositionComponent()->GetX() - m_pPositionComponent->GetX());
    return dx <= maxDx && IsActorWithinLOS(pClaw);
}

void TigerGuardAIStateComponent::FaceClaw()
{
    if (Actor* pClaw = GetClaw())
    {
        m_Dir = pClaw->GetPositionComponent()->GetX() < m_pPositionComponent->GetX() ? -1.0 : 1.0;
    }
    // the sprites face left; mirrored = facing right
    m_pRenderComponent->SetMirrored(m_Dir > 0);
}

bool TigerGuardAIStateComponent::VCanEnter()
{
    if (m_pEnemyAIComponent->IsDead())
    {
        return false;
    }
    if (m_Action != Action_None)
    {
        return true;
    }
    // CLAW.EXE: attack range 180 px, a new decision every 1.25-2 s; right
    // after a hit the tiger strikes back at once.
    return (m_CooldownMs <= 0 || m_bWasHit) && ClawInReach(180.0);
}

void TigerGuardAIStateComponent::VUpdate(uint32 msDiff)
{
    m_CooldownMs = std::max(0, m_CooldownMs - (int)msDiff);
    m_BlockCooldownMs = std::max(0, m_BlockCooldownMs - (int)msDiff);

    // A hit (the take-damage state) makes the next attack come at once.
    BaseEnemyAIStateComponent* pCurrent = m_pEnemyAIComponent->GetCurrentState();
    const bool takingDamage = pCurrent && pCurrent->VGetStateType() == EnemyAIState_TakingDamage;
    if (takingDamage && !m_bWasTakingDamage)
    {
        m_bWasHit = true;
    }
    m_bWasTakingDamage = takingDamage;

    if (!m_IsActive)
    {
        // Patrolling: start an attack when Claw comes into reach.
        if (pCurrent && pCurrent->VGetStateType() == EnemyAIState_Patrolling && VCanEnter())
        {
            m_pEnemyAIComponent->EnterBestState(true);
        }
        return;
    }

    m_ActionMs += msDiff;
    const double x = m_pPositionComponent->GetX();
    int left = INT_MIN, right = INT_MAX;
    if (auto pPatrol = MakeStrongPtr(m_pOwner->GetComponent<PatrolEnemyAIStateComponent>(PatrolEnemyAIStateComponent::g_Name)))
    {
        if (pPatrol->GetLeftPatrolBorder() < pPatrol->GetRightPatrolBorder())
        {
            left = pPatrol->GetLeftPatrolBorder();
            right = pPatrol->GetRightPatrolBorder();
        }
    }
    const bool atBorder = (m_MoveDir < 0 && x <= left) || (m_MoveDir > 0 && x >= right);

    switch (m_Action)
    {
        case Action_Lunge:
        {
            // the crouch (first frame, 0.5 s), then the leap until Claw's
            // place is passed or the path ends
            const bool crouching = m_pAnimationComponent->GetCurrentAnimation() &&
                m_pAnimationComponent->GetCurrentAnimation()->GetCurrentAnimationFrame()->idx == 0;
            const bool passed = (m_MoveDir < 0 && x <= m_TargetX) || (m_MoveDir > 0 && x >= m_TargetX);
            if (crouching || passed || atBorder)
            {
                Stop();
            }
            else
            {
                m_pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(m_MoveDir * Mps(300.0), 0.0));
            }
            if (atBorder && !crouching)
            {
                Finish();
            }
            break;
        }
        case Action_FrontFlip:
        case Action_BackFlip:
        {
            const bool reached = (m_MoveDir < 0 && x <= m_TargetX) || (m_MoveDir > 0 && x >= m_TargetX);
            if (reached || atBorder || m_ActionMs >= 1000)
            {
                Stop();
                if (m_Action == Action_FrontFlip && m_bKnifeAfterFlip)
                {
                    StartKnife(1);
                }
                else
                {
                    Finish();
                }
            }
            else
            {
                m_pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(m_MoveDir * Mps(250.0), 0.0));
            }
            break;
        }
        case Action_Block:
            Stop();
            if (m_ActionMs >= 250)
            {
                Finish();
            }
            break;
        case Action_Knife:
            Stop();
            break;
        case Action_None:
            Decide();
            break;
    }
}

void TigerGuardAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    if (m_Action == Action_None)
    {
        Decide();
    }
}

void TigerGuardAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
    m_Action = Action_None;
    m_pEnemyAIComponent->SetPhased(false);
    Stop();
}

void TigerGuardAIStateComponent::Stop()
{
    m_pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0.0, 0.0));
}

// CLAW.EXE 0x4988e0 (and 0x498630 right after a hit)
void TigerGuardAIStateComponent::Decide()
{
    Actor* pClaw = GetClaw();
    if (!pClaw || !ClawInReach(220.0))
    {
        Finish();
        return;
    }
    FaceClaw();
    const int r = rand() % 101;
    if (m_bWasHit)
    {
        // strikes back: half a knife combo, half a lunge
        m_bWasHit = false;
        PlayTigerSound("00510028.WAV");
        if (r > 49) StartKnife(rand() % 3 + 2);
        else StartLunge();
    }
    else if (r < 10 && fabs(pClaw->GetPositionComponent()->GetX() - m_pPositionComponent->GetX()) < 60.0)
    {
        StartFlip(false);   // too close: a back flip away
    }
    else if (r < 40)
    {
        StartLunge();
    }
    else if (r < 80)
    {
        StartKnife(rand() % 3 + 2);
    }
    else
    {
        m_bKnifeAfterFlip = true;
        StartFlip(true);
    }
    m_CooldownMs = rand() % 751 + 1250;
}

void TigerGuardAIStateComponent::StartKnife(int count)
{
    m_Action = Action_Knife;
    m_KnifeLeft = count;
    m_ActionMs = 0;
    m_pEnemyAIComponent->SetPhased(false);
    FaceClaw();
    if (rand() % 101 < 40)
    {
        static const char* kVoices[] = { "00510004.WAV", "00510006.WAV", "00510018.WAV", "00510029.WAV" };
        PlayTigerSound(kVoices[rand() % 4]);
    }
    m_pAnimationComponent->SetAnimation("strike1");
}

void TigerGuardAIStateComponent::StartLunge()
{
    m_Action = Action_Lunge;
    m_ActionMs = 0;
    m_MoveDir = m_Dir;
    Actor* pClaw = GetClaw();
    m_TargetX = pClaw ? pClaw->GetPositionComponent()->GetX() : m_pPositionComponent->GetX() + m_Dir * 150.0;
    PlayTigerSound("00510009.WAV");
    m_pAnimationComponent->SetAnimation("strike3");
}

void TigerGuardAIStateComponent::StartFlip(bool front)
{
    // front: towards Claw, back: away (still facing him); ~100 px,
    // untouchable meanwhile
    FaceClaw();
    m_MoveDir = front ? m_Dir : -m_Dir;
    m_Action = front ? Action_FrontFlip : Action_BackFlip;
    m_ActionMs = 0;
    m_StartX = m_pPositionComponent->GetX();
    m_TargetX = m_StartX + m_MoveDir * 100.0;
    m_pEnemyAIComponent->SetPhased(true);
    PlayTigerSound(rand() % 2 ? "FLIP.WAV" : "FLIP2.WAV");
    m_pAnimationComponent->SetAnimation(front ? "frontflip" : "backflip");
}

void TigerGuardAIStateComponent::Finish()
{
    m_Action = Action_None;
    m_bKnifeAfterFlip = false;
    m_pEnemyAIComponent->SetPhased(false);
    Stop();
    m_pAnimationComponent->SetAnimation("idle");
    // back to patrolling until the next decision (VCanEnter says no now),
    // or straight on after a hit
    m_pEnemyAIComponent->EnterBestState(true);
}

bool TigerGuardAIStateComponent::IsUnflinching() const
{
    return m_IsActive && (m_Action == Action_Lunge || IsFlipping());
}

// CLAW.EXE 0x498410 / 0x499310: a blow from the front, 40%, at most every 0.5 s
bool TigerGuardAIStateComponent::TryBlock(DamageType damageType)
{
    if (m_pEnemyAIComponent->IsDead() || m_BlockCooldownMs > 0)
    {
        return false;
    }
    if (damageType != DamageType_MeleeAttack && damageType != DamageType_Bullet)
    {
        return false;
    }
    if (m_IsActive && m_Action != Action_None && m_Action != Action_Block)
    {
        return false;
    }
    Actor* pClaw = GetClaw();
    if (!pClaw)
    {
        return false;
    }
    auto pCtrl = MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name));
    const double clawX = pClaw->GetPositionComponent()->GetX();
    const bool clawFacesTiger = pCtrl &&
        ((pCtrl->GetFacing() == Direction_Right) == (clawX < m_pPositionComponent->GetX()));
    if (!clawFacesTiger)
    {
        return false;
    }
    m_BlockCooldownMs = 500;
    if (rand() % 100 >= 40)
    {
        return false;
    }

    PlayTigerSound("BLOCK1.WAV");
    m_Action = Action_Block;     // VCanEnter: yes while blocking
    m_ActionMs = 0;
    if (!m_IsActive)
    {
        m_pEnemyAIComponent->EnterBestState(true);
    }
    if (m_IsActive)
    {
        FaceClaw();
        Stop();
        m_pAnimationComponent->SetAnimation("block");
    }
    else
    {
        m_Action = Action_None;
    }
    return true;
}

void TigerGuardAIStateComponent::Strike(double cx, double cy, double w, double h, int damage)
{
    // offsets given for a tiger facing left (CLAW.EXE rects)
    Point offset(m_Dir < 0 ? cx : -cx, cy);
    ActorTemplates::CreateAreaDamage(
        m_pPositionComponent->GetPosition() + offset,
        Point(w, h),
        damage,
        CollisionFlag_EnemyAIAttack,
        "Rectangle",
        DamageType_MeleeAttack,
        m_Dir < 0 ? Direction_Left : Direction_Right,
        m_pOwner->GetGUID());
    Util::PlayRandomSoundFromList(m_pEnemyAIComponent->GetMeleeAttackSounds());
}

void TigerGuardAIStateComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (!m_IsActive || m_pAnimationComponent->GetCurrentAnimation() != nullptr &&
        m_pAnimationComponent->GetCurrentAnimation()->GetCurrentAnimationFrame() != pNewFrame)
    {
        return;
    }
    const std::string anim = m_pAnimationComponent->GetCurrentAnimationName();
    if (m_Action == Action_Knife && anim == "strike1" && pNewFrame->idx == 1)
    {
        // rect (-63,-58)-(0,-1), 15 damage
        Strike(-32.0, -30.0, 63.0, 57.0, 15);
    }
    else if (m_Action == Action_Lunge && anim == "strike3" && pNewFrame->idx == 3)
    {
        // rect (-74,-39)-(-1,15), 20 damage
        Strike(-38.0, -12.0, 73.0, 54.0, 20);
    }
}

void TigerGuardAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (!m_IsActive)
    {
        return;
    }
    if (m_Action == Action_Knife)
    {
        if (--m_KnifeLeft > 0)
        {
            StartKnife(m_KnifeLeft);
        }
        else
        {
            Finish();
        }
    }
    else if (m_Action == Action_Lunge)
    {
        Finish();
    }
}
