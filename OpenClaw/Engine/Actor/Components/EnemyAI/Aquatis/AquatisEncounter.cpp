#include "../EnemyAIComponent.h"
#include "../../AnimationComponent.h"
#include "../../PhysicsComponent.h"
#include "../../PositionComponent.h"
#include "../../RenderComponent.h"

#include "../../../../GameApp/BaseGameLogic.h"
#include "../../../../GameApp/BaseGameApp.h"
#include "../../../../Physics/ClawPhysics.h"
#include "../../ControllableComponent.h"
#include "../../RenderComponent.h"
#include "../../FollowableComponent.h"

#include "../../../../Events/EventMgr.h"
#include "../../../../Events/Events.h"
#include "AquatisEncounter.h"
#include "../../../../Scene/SceneNodes.h"

#include "../../../ActorTemplates.h"
#include "../../../../Util/Util.h"
#include "../../../../Util/StartupTrace.h"
#include <algorithm>
#include <cmath>

const char* AquatisAIStateComponent::g_Name = "AquatisAIStateComponent";
const char* AquatisTentacleComponent::g_Name = "AquatisTentacleComponent";
const char* AquatisCrackComponent::g_Name = "AquatisCrackComponent";
const char* AquatisStalactiteComponent::g_Name = "AquatisStalactiteComponent";
const char* AquatisDynamiteComponent::g_Name = "AquatisDynamiteComponent";

AquatisAIStateComponent* AquatisAIStateComponent::s_pInstance = NULL;
bool AquatisAIStateComponent::s_bDefeated = false;

static std::vector<AquatisStalactiteComponent*> g_AquatisStalactites;

static const char* AQ_SOUND_TENT_DEATH = "/LEVEL12/SOUNDS/AQUATISTENTACLE/TENTDEATH.WAV";
static const char* AQ_SOUND_TENT_GROW = "/LEVEL12/SOUNDS/AQUATISTENTACLE/TENTGROW.WAV";
static const char* AQ_SOUND_TENT_SWIPE = "/LEVEL12/SOUNDS/AQUATISTENTACLE/TENTSWIPE.WAV";
static const char* AQ_SOUND_ICICLE_SNAP = "/LEVEL12/SOUNDS/STALACTITESNAP2.WAV";
static const char* AQ_SOUND_ICICLE_HIT = "/LEVEL12/SOUNDS/STALACTITEHIT2.WAV";

static Actor* AqClaw()
{
    if (!g_pApp || !g_pApp->GetGameLogic())
    {
        return NULL;
    }
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    return pClaw ? pClaw.get() : NULL;
}

static void AqMoveActor(Actor* pActor, const Point& pos)
{
    pActor->GetPositionComponent()->SetPosition(pos);
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Move_Actor(pActor->GetGUID(), pos)));
}

//=====================================================================================================================
// GabrielAIStateComponent
//=====================================================================================================================

AquatisAIStateComponent::AquatisAIStateComponent()
    :
    BaseBossAIStateComponennt("AquatisAIStateComponent"),
    m_bAllowDamage(false),
    m_IdleTime(0),
    m_Act(Act_Idle),
    m_ActTime(0),
    m_bFrenzy(false),
    m_FrenzyLeft(0),
    m_bNextHigh(true),
    m_bStruck(false),
    m_LastFrame(0),
    m_HitCount(0),
    m_HitCooldown(0),
    m_QuakeLeft(0),
    m_bIcicleFalling(false),
    m_StalactiteHitCooldown(0)
{
    s_pInstance = this;
    s_bDefeated = false;
}

AquatisAIStateComponent::~AquatisAIStateComponent()
{
    if (s_pInstance == this)
    {
        s_pInstance = NULL;
    }
}

Point AquatisAIStateComponent::GetPosition() const
{
    return m_pOwner->GetPositionComponent()->GetPosition();
}

// CLAW.EXE's sounds for him (LEVEL12/SOUNDS/AQUATIS)
static const char* AQ_SOUND_TAUNT[3] = {
    "/LEVEL12/SOUNDS/AQUATIS/00480011.WAV", "/LEVEL12/SOUNDS/AQUATIS/00480012.WAV", "/LEVEL12/SOUNDS/AQUATIS/00480013.WAV" };
static const char* AQ_SOUND_HIGHPUNCH = "/LEVEL12/SOUNDS/AQUATIS/00480000.WAV";
static const char* AQ_SOUND_LOWPUNCH = "/LEVEL12/SOUNDS/AQUATIS/00480001.WAV";
static const char* AQ_SOUND_FRENZY = "/LEVEL12/SOUNDS/AQUATIS/00480002.WAV";
static const char* AQ_SOUND_SCARED = "/LEVEL12/SOUNDS/AQUATIS/00480005.WAV";
static const char* AQ_SOUND_ICICLE_PAIN = "/LEVEL12/SOUNDS/AQUATIS/00480015.WAV";

void AquatisAIStateComponent::HitByStalactite(int damage)
{
    m_bIcicleFalling = false;
    HealthComponent* pHealth = m_pOwner->GetRawComponent<HealthComponent>();
    if (!pHealth || pHealth->GetCurrentHealth() <= 0 || m_StalactiteHitCooldown > 0)
    {
        return;
    }

    STARTUP_TRACE("aquatis: hit by an icicle, health " + ToStr(pHealth->GetCurrentHealth()) + " -> " + ToStr(pHealth->GetCurrentHealth() - damage));

    // 0x405a70: -20, 1 s before the next one counts, then the frenzy
    m_StalactiteHitCooldown = 1000;
    m_bAllowDamage = true;
    pHealth->AddHealth(-damage, DamageType_MeleeAttack, GetPosition(), m_pOwner->GetGUID());
    m_bAllowDamage = false;
    if (pHealth->GetCurrentHealth() > 0)
    {
        SetAct(Act_StalactiteHit);
    }
}

bool AquatisAIStateComponent::CanQuake() const
{
    // 0x406f60: not while he rages, an icicle falls or one is about to
    return m_bBossFightStarted && !s_bDefeated && !m_bFrenzy && !m_bIcicleFalling && m_QuakeLeft <= 0;
}

bool AquatisAIStateComponent::OnQuake()
{
    if (!CanQuake())
    {
        return false;
    }
    // 4 s of earthquake with him cowering, then the next icicle drops (0x407060)
    m_QuakeLeft = 4000;
    CameraNode::Shake(4000);
    if (m_Act == Act_Idle || m_Act == Act_HighPunch || m_Act == Act_LowPunch || m_Act == Act_Hit)
    {
        SetAct(Act_Scared);
    }
    return true;
}

bool AquatisAIStateComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    if (m_bAllowDamage)
    {
        return false;
    }
    // 0x405ce0: Claw's blows make him flinch (500 ms apart) but never hurt;
    // the third one in a row scares him. Not while he rages or cowers.
    if (m_bBossFightStarted && m_HitCooldown <= 0 && !m_bFrenzy &&
        (m_Act == Act_Idle || m_Act == Act_HighPunch || m_Act == Act_LowPunch || m_Act == Act_Hit))
    {
        m_HitCooldown = 500;
        SetAct(Act_Hit);
    }
    return true;
}

bool AquatisAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    if (!BaseBossAIStateComponennt::VDelegateInit(pData))
    {
        return false;
    }

    return true;
}

void AquatisAIStateComponent::VPostInit()
{
    BaseBossAIStateComponennt::VPostInit();

    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);
}

void AquatisAIStateComponent::VOnWorldFinishedLoading()
{
}

void AquatisAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    if (m_bBossFightStarted && !s_bDefeated)
    {
        // back from another state: carry on with what he was doing
        SetAct(m_Act == Act_StalactiteHit || m_Act == Act_Scared ? m_Act : Act_Idle);
    }
}

void AquatisAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
}

void AquatisAIStateComponent::FaceClaw()
{
    if (Actor* pClaw = AqClaw())
    {
        const double dx = pClaw->GetPositionComponent()->GetX() - GetPosition().x;
        m_pRenderComponent->SetMirrored(dx > 0);
    }
}

void AquatisAIStateComponent::Punch(bool high)
{
    SetAct(high ? Act_HighPunch : Act_LowPunch);
}

void AquatisAIStateComponent::SetAct(Act act)
{
    m_Act = act;
    m_ActTime = 0;
    m_bStruck = false;
    m_LastFrame = 0;
    switch (act)
    {
        case Act_Idle:
            // 0x405220: idle1 or idle2, a taunt, a second before he may punch
            FaceClaw();
            m_pAnimationComponent->SetAnimation(Util::GetRandomNumber(0, 100) < 50 ? "idle1" : "idle2");
            Util::PlaySimpleSound(AQ_SOUND_TAUNT[Util::GetRandomNumber(0, 2)]);
            break;
        case Act_HighPunch:
            FaceClaw();
            m_pAnimationComponent->SetAnimation("strike1");
            Util::PlaySimpleSound(AQ_SOUND_HIGHPUNCH);
            break;
        case Act_LowPunch:
            FaceClaw();
            m_pAnimationComponent->SetAnimation("strike2");
            Util::PlaySimpleSound(AQ_SOUND_LOWPUNCH);
            break;
        case Act_Hit:
        {
            bool ducking = false;
            if (Actor* pClaw = AqClaw())
            {
                if (auto pCtrl = MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name)))
                {
                    ducking = pCtrl->IsDucking();
                }
            }
            m_pAnimationComponent->SetAnimation(ducking ? "hitlow" : "hithigh");
            break;
        }
        case Act_Scared:
            // 0x405560: idle3, at least 4 s and as long as the quake lasts
            m_HitCount = 0;
            m_pAnimationComponent->SetAnimation("idle3");
            Util::PlaySimpleSound(AQ_SOUND_SCARED);
            break;
        case Act_StalactiteHit:
            m_bFrenzy = true;
            m_pAnimationComponent->SetAnimation("idle2");
            Util::PlaySimpleSound(AQ_SOUND_ICICLE_PAIN);
            break;
        case Act_Frenzy:
            // 0x405650: 4 s of punches, high and low by turns
            m_bFrenzy = true;
            m_FrenzyLeft = 4000;
            Util::PlaySimpleSound(AQ_SOUND_FRENZY);
            Punch(m_bNextHigh);
            m_bNextHigh = !m_bNextHigh;
            return;
        case Act_Dead:
            break;
    }
    m_pAnimationComponent->ResetAnimation();
}

void AquatisAIStateComponent::ActEnded()
{
    switch (m_Act)
    {
        case Act_HighPunch:
        case Act_LowPunch:
            if (m_bFrenzy && m_FrenzyLeft > 0)
            {
                Punch(m_bNextHigh);
                m_bNextHigh = !m_bNextHigh;
                return;
            }
            m_bFrenzy = false;
            SetAct(Act_Idle);
            return;
        case Act_Hit:
            // 0x405c50: the third blow scares him, else mostly a punch back
            if (++m_HitCount >= 3)
            {
                SetAct(Act_Scared);
            }
            else if (Util::GetRandomNumber(0, 100) < 85)
            {
                Punch(Util::GetRandomNumber(0, 1) == 0);
            }
            else
            {
                SetAct(Act_Idle);
            }
            return;
        case Act_StalactiteHit:
            SetAct(Act_Frenzy);
            return;
        default:
            return;
    }
}

void AquatisAIStateComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
    if (m_bStruck || (m_Act != Act_HighPunch && m_Act != Act_LowPunch))
    {
        return;
    }
    // the blow lands on the last frame (204 / 207), with CLAW.EXE's boxes
    const bool high = m_Act == Act_HighPunch;
    if ((int)pNewFrame->idx != (high ? 3 : 2))
    {
        return;
    }
    m_bStruck = true;
    const bool right = m_pRenderComponent->IsMirrored();
    const double side = right ? 1.0 : -1.0;
    const Point me = GetPosition();
    const Point center = high ? Point(me.x + side * 59, me.y - 33) : Point(me.x + side * 65, me.y + 35);
    const Point size = high ? Point(118, 86) : Point(130, 50);
    ActorTemplates::CreateAreaDamage(center, size, 10, CollisionFlag_EnemyAIAttack, "Rectangle",
        DamageType_MeleeAttack, right ? Direction_Right : Direction_Left, m_pOwner->GetGUID());
}

void AquatisAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
    if (m_Act == Act_Idle || m_Act == Act_Scared)
    {
        return; // timed in VUpdate
    }
    ActEnded();
}

void AquatisAIStateComponent::VUpdate(uint32 msDiff)
{
    if (m_HitCooldown > 0) m_HitCooldown -= msDiff;
    if (m_StalactiteHitCooldown > 0) m_StalactiteHitCooldown -= msDiff;

    if (!m_IsActive || !m_bBossFightStarted || s_bDefeated)
    {
        return;
    }

    m_ActTime += msDiff;
    if (m_bFrenzy && m_Act != Act_StalactiteHit)
    {
        m_FrenzyLeft -= msDiff;
    }

    if (m_QuakeLeft > 0)
    {
        m_QuakeLeft -= msDiff;
        if (m_QuakeLeft <= 0)
        {
            m_QuakeLeft = 0;
            m_bIcicleFalling = AquatisStalactiteComponent::DropNext();
        }
    }

    if (m_Act == Act_Idle && m_ActTime >= 1000)
    {
        // 0x405310: Claw on his level - a punch, high or low
        Actor* pClaw = AqClaw();
        if (pClaw && fabs(pClaw->GetPositionComponent()->GetY() - GetPosition().y) <= 500)
        {
            Punch(Util::GetRandomNumber(0, 1) == 0);
        }
        else
        {
            SetAct(Act_Idle);
        }
    }
    else if (m_Act == Act_Scared && m_ActTime >= 4000 && m_QuakeLeft <= 0)
    {
        SetAct(Act_Idle);
    }
}

void AquatisAIStateComponent::VOnBossFightStarted()
{
    // To refresh contact list after Claw's death
    g_pApp->GetGameLogic()->VGetGamePhysics()->VActivate(m_pOwner->GetGUID());

    m_HitCount = 0;
    m_bFrenzy = false;
    m_QuakeLeft = 0;
    m_bIcicleFalling = false;
    m_pEnemyAIComponent->EnterBestState(true);
    SetAct(Act_Idle);
}

void AquatisAIStateComponent::VOnBossFightEnded(bool isBossDead)
{
    m_bFrenzy = false;
    m_QuakeLeft = 0;
    m_bIcicleFalling = false;
    if (isBossDead)
    {
        s_bDefeated = true;
        m_Act = Act_Dead;

        SoundInfo soundInfo(SOUND_GAME_AMULET_RISE);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
            new EventData_Request_Play_Sound(soundInfo)));

        StrongActorPtr pGem = ActorTemplates::CreateActor(
            ActorPrototype_Level12_BossGem,
            m_pOwner->GetPositionComponent()->GetPosition());
    }
    else
    {
        // To refresh contact list after Claw's death
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());
        m_pAnimationComponent->SetAnimation(m_BossDialogAnimation);
    }
}

void AquatisAIStateComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    BaseBossAIStateComponennt::VOnHealthChanged(oldHealth, newHealth, damageType, impactPoint, sourceActorId);
}


//=====================================================================================================================
// AquatisTentacleComponent
//=====================================================================================================================

AquatisTentacleComponent::AquatisTentacleComponent()
    :
    m_RespawnTime(4000),
    m_AttackOffsetX(55),
    m_Damage(10),
    m_AttackInterval(900),
    m_State(State_Idle),
    m_StateTime(0),
    m_Cooldown(0),
    m_bStayDead(false),
    m_bStrikeDone(false),
    m_pAnimationComponent(NULL),
    m_pRenderComponent(NULL),
    m_pHealthComponent(NULL)
{
    m_AttackRange.Set(150, 120);
    m_AttackAreaSize.Set(110, 70);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &AquatisTentacleComponent::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
}

AquatisTentacleComponent::~AquatisTentacleComponent()
{
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &AquatisTentacleComponent::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
}

bool AquatisTentacleComponent::VInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_RespawnTime, pData->FirstChildElement("RespawnTime"));
    ParseValueFromXmlElem(&m_AttackRange, pData->FirstChildElement("AttackRange"), "x", "y");
    ParseValueFromXmlElem(&m_AttackAreaSize, pData->FirstChildElement("AttackAreaSize"), "width", "height");
    ParseValueFromXmlElem(&m_AttackOffsetX, pData->FirstChildElement("AttackOffsetX"));
    ParseValueFromXmlElem(&m_Damage, pData->FirstChildElement("Damage"));
    ParseValueFromXmlElem(&m_AttackInterval, pData->FirstChildElement("AttackInterval"));
    return true;
}

void AquatisTentacleComponent::VPostInit()
{
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pRenderComponent = m_pOwner->GetRawComponent<ActorRenderComponent>(true);
    m_pHealthComponent = m_pOwner->GetRawComponent<HealthComponent>(true);

    m_pAnimationComponent->AddObserver(this);
    m_pHealthComponent->AddObserver(this);

    SetState(State_Idle);
}

void AquatisTentacleComponent::SetState(State state)
{
    m_State = state;
    m_StateTime = 0;

    switch (state)
    {
        case State_Idle:
            m_pRenderComponent->SetVisible(true);
            m_pAnimationComponent->SetAnimation("idle");
            break;
        case State_Striking:
        {
            m_bStrikeDone = false;
            int which = Util::GetRandomNumber(1, 3);
            m_pAnimationComponent->SetAnimation("strike" + ToStr(which));
            Util::PlaySimpleSound(AQ_SOUND_TENT_SWIPE);
            break;
        }
        case State_Dying:
            m_pAnimationComponent->SetAnimation("killfall");
            Util::PlaySimpleSound(AQ_SOUND_TENT_DEATH);
            break;
        case State_Dead:
            m_pRenderComponent->SetVisible(false);
            break;
        case State_Respawning:
            m_pRenderComponent->SetVisible(true);
            m_pHealthComponent->SetMaxHealth();
            m_pAnimationComponent->SetAnimation("respawn");
            Util::PlaySimpleSound(AQ_SOUND_TENT_GROW);
            break;
    }
}

void AquatisTentacleComponent::VUpdate(uint32 msDiff)
{
    m_StateTime += msDiff;
    if (m_Cooldown > 0)
    {
        m_Cooldown -= msDiff;
    }

    if (m_State == State_Dead)
    {
        if (!m_bStayDead && !AquatisAIStateComponent::IsDefeated() && m_StateTime >= m_RespawnTime)
        {
            SetState(State_Respawning);
        }
        return;
    }

    if (m_State != State_Idle)
    {
        return;
    }

    // Tentacles only fight together with their master
    AquatisAIStateComponent* pAquatis = AquatisAIStateComponent::Get();
    if (!pAquatis || !pAquatis->IsFightActive())
    {
        return;
    }

    Actor* pClaw = AqClaw();
    if (!pClaw)
    {
        return;
    }

    Point myPos = m_pOwner->GetPositionComponent()->GetPosition();
    Point clawPos = pClaw->GetPositionComponent()->GetPosition();
    double dx = clawPos.x - myPos.x;
    double dy = clawPos.y - myPos.y;

    // The pictures face left, mirror them when Claw is to the right
    m_pRenderComponent->SetMirrored(dx > 0);

    if (m_Cooldown <= 0 && fabs(dx) <= m_AttackRange.x && fabs(dy) <= m_AttackRange.y)
    {
        SetState(State_Striking);
    }
}

void AquatisTentacleComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (m_State != State_Striking || m_bStrikeDone || pNewFrame == NULL || pNewFrame->idx < 1)
    {
        return;
    }

    m_bStrikeDone = true;

    bool toRight = m_pRenderComponent->IsMirrored();
    Direction dir = toRight ? Direction_Right : Direction_Left;
    Point offset(toRight ? m_AttackOffsetX : -m_AttackOffsetX, 0);

    ActorTemplates::CreateAreaDamage(
        m_pOwner->GetPositionComponent()->GetPosition() + offset,
        m_AttackAreaSize,
        m_Damage,
        CollisionFlag_EnemyAIAttack,
        "Rectangle",
        DamageType_MeleeAttack,
        dir,
        m_pOwner->GetGUID());
}

void AquatisTentacleComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (m_State == State_Striking)
    {
        m_Cooldown = m_AttackInterval;
        SetState(State_Idle);
    }
    else if (m_State == State_Dying)
    {
        SetState(State_Dead);
    }
    else if (m_State == State_Respawning)
    {
        m_Cooldown = m_AttackInterval;
        SetState(State_Idle);
    }
}

bool AquatisTentacleComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    return m_State == State_Dying || m_State == State_Dead;
}

void AquatisTentacleComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    if (newHealth < oldHealth && m_State != State_Dying && m_State != State_Dead)
    {
        SetState(State_Dying);
    }
}

void AquatisTentacleComponent::BossFightEndedDelegate(IEventDataPtr pEvent)
{
    shared_ptr<EventData_Boss_Fight_Ended> pCastEventData = static_pointer_cast<EventData_Boss_Fight_Ended>(pEvent);
    if (pCastEventData->GetIsBossDead())
    {
        // Aquatis is gone - his tentacles go with him
        m_bStayDead = true;
        if (m_State != State_Dying && m_State != State_Dead)
        {
            SetState(State_Dying);
        }
    }
    else
    {
        // Claw died - everything is back where it started
        m_Cooldown = 0;
        m_pHealthComponent->SetMaxHealth();
        SetState(State_Idle);
    }
}

//=====================================================================================================================
// AquatisCrackComponent
//=====================================================================================================================

AquatisCrackComponent::AquatisCrackComponent()
    :
    m_Cooldown(0),
    m_pHealthComponent(NULL)
{
}

AquatisCrackComponent::~AquatisCrackComponent()
{
}

void AquatisCrackComponent::VPostInit()
{
    m_pHealthComponent = m_pOwner->GetRawComponent<HealthComponent>(true);
    m_pHealthComponent->AddObserver(this);

    if (AnimationComponent* pAnim = m_pOwner->GetRawComponent<AnimationComponent>())
    {
        pAnim->SetAnimation("crackglow");
    }
}

void AquatisCrackComponent::VUpdate(uint32 msDiff)
{
    if (m_Cooldown > 0)
    {
        m_Cooldown -= msDiff;
    }
}

bool AquatisCrackComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    // Only explosions matter. One blast drops one icicle.
    if (damageType != DamageType_Explosion || m_Cooldown > 0)
    {
        return true;
    }
    AquatisAIStateComponent* pAquatis = AquatisAIStateComponent::Get();
    return !pAquatis || !pAquatis->CanQuake();
}

void AquatisCrackComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    if (newHealth >= oldHealth)
    {
        return;
    }

    m_Cooldown = 1500;
    m_pHealthComponent->SetMaxHealth();

    // CLAW.EXE 0x406f60: the earthquake; the icicle comes down when it ends
    AquatisAIStateComponent* pAquatis = AquatisAIStateComponent::Get();
    const bool quake = pAquatis && pAquatis->OnQuake();
    STARTUP_TRACE(std::string("aquatis: dynamite hit the crack") + (quake ? ", earthquake" : ""));

    Util::PlaySimpleSound("/LEVEL12/SOUNDS/EARTHQUAKE" + ToStr(Util::GetRandomNumber(1, 5)) + ".WAV");
}

//=====================================================================================================================
// AquatisStalactiteComponent
//=====================================================================================================================

AquatisStalactiteComponent::AquatisStalactiteComponent()
    :
    m_Order(1),
    m_TargetY(3530),
    m_Damage(20),
    m_State(State_Hanging),
    m_Speed(0),
    m_pAnimationComponent(NULL),
    m_pRenderComponent(NULL)
{
    g_AquatisStalactites.push_back(this);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &AquatisStalactiteComponent::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
}

AquatisStalactiteComponent::~AquatisStalactiteComponent()
{
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &AquatisStalactiteComponent::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
    g_AquatisStalactites.erase(
        std::remove(g_AquatisStalactites.begin(), g_AquatisStalactites.end(), this),
        g_AquatisStalactites.end());
}

bool AquatisStalactiteComponent::VInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_Order, pData->FirstChildElement("Order"));
    ParseValueFromXmlElem(&m_TargetY, pData->FirstChildElement("TargetY"));
    ParseValueFromXmlElem(&m_Damage, pData->FirstChildElement("Damage"));
    return true;
}

void AquatisStalactiteComponent::VPostInit()
{
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pRenderComponent = m_pOwner->GetRawComponent<ActorRenderComponent>(true);
    m_pAnimationComponent->AddObserver(this);
    m_pAnimationComponent->SetAnimation("home");

    m_HomePosition = m_pOwner->GetPositionComponent()->GetPosition();
}

bool AquatisStalactiteComponent::DropNext()
{
    AquatisStalactiteComponent* pNext = NULL;
    for (AquatisStalactiteComponent* pIcicle : g_AquatisStalactites)
    {
        if (pIcicle->IsHanging() && (!pNext || pIcicle->GetOrder() < pNext->GetOrder()))
        {
            pNext = pIcicle;
        }
    }

    if (!pNext)
    {
        return false;
    }

    pNext->Drop();
    return true;
}

void AquatisStalactiteComponent::Drop()
{
    m_State = State_Falling;
    m_Speed = 0;
    Util::PlaySimpleSound(AQ_SOUND_ICICLE_SNAP);
}

void AquatisStalactiteComponent::Reset()
{
    m_State = State_Hanging;
    m_Speed = 0;
    m_pRenderComponent->SetVisible(true);
    m_pAnimationComponent->SetAnimation("home");
    AqMoveActor(m_pOwner.get(), m_HomePosition);
}

void AquatisStalactiteComponent::VUpdate(uint32 msDiff)
{
    if (m_State != State_Falling)
    {
        return;
    }

    double dt = msDiff / 1000.0;
    m_Speed += 2000.0 * dt;
    Point pos = m_pOwner->GetPositionComponent()->GetPosition();
    pos.y += m_Speed * dt;

    if (pos.y >= m_TargetY)
    {
        pos.y = m_TargetY;
        AqMoveActor(m_pOwner.get(), pos);

        m_State = State_Crumbling;
        m_pAnimationComponent->SetAnimation("crumble");
        Util::PlaySimpleSound(AQ_SOUND_ICICLE_HIT);

        if (AquatisAIStateComponent* pAquatis = AquatisAIStateComponent::Get())
        {
            if (pAquatis->IsFightActive())
            {
                pAquatis->HitByStalactite(m_Damage);
            }
        }
        return;
    }

    AqMoveActor(m_pOwner.get(), pos);
}

void AquatisStalactiteComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (m_State == State_Crumbling)
    {
        m_State = State_Gone;
        m_pRenderComponent->SetVisible(false);
    }
}

void AquatisStalactiteComponent::BossFightEndedDelegate(IEventDataPtr pEvent)
{
    shared_ptr<EventData_Boss_Fight_Ended> pCastEventData = static_pointer_cast<EventData_Boss_Fight_Ended>(pEvent);
    if (!pCastEventData->GetIsBossDead())
    {
        // Claw died and Aquatis got his health back - the icicles hang again
        Reset();
    }
}

//=====================================================================================================================
// AquatisDynamiteComponent
//=====================================================================================================================

AquatisDynamiteComponent::AquatisDynamiteComponent()
    :
    m_RespawnTime(4000),
    m_TimeLeft(0)
{
}

AquatisDynamiteComponent::~AquatisDynamiteComponent()
{
}

bool AquatisDynamiteComponent::VInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_RespawnTime, pData->FirstChildElement("RespawnTime"));
    return true;
}

void AquatisDynamiteComponent::VUpdate(uint32 msDiff)
{
    if (!m_pPickup.expired() || AquatisAIStateComponent::IsDefeated())
    {
        return;
    }

    m_TimeLeft -= msDiff;
    if (m_TimeLeft > 0)
    {
        return;
    }

    m_TimeLeft = m_RespawnTime;
    StrongActorPtr pPickup = ActorTemplates::CreateActorPickup(
        PickupType_Ammo_Dynamite,
        m_pOwner->GetPositionComponent()->GetPosition(),
        true);
    m_pPickup = pPickup;
}

//=====================================================================================================================
// FallingStalactiteComponent (CLAW.EXE 0x491990 "Stalactite")
//=====================================================================================================================

const char* FallingStalactiteComponent::g_Name = "FallingStalactiteComponent";

FallingStalactiteComponent::FallingStalactiteComponent()
{
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &FallingStalactiteComponent::ClawRespawnedDelegate), EventData_Claw_Respawned::sk_EventType);
}

FallingStalactiteComponent::~FallingStalactiteComponent()
{
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &FallingStalactiteComponent::ClawRespawnedDelegate), EventData_Claw_Respawned::sk_EventType);
}

bool FallingStalactiteComponent::VInit(TiXmlElement* pData)
{
    if (TiXmlElement* pArea = pData->FirstChildElement("TriggerArea"))
    {
        pArea->Attribute("left", &m_Left);
        pArea->Attribute("top", &m_Top);
        pArea->Attribute("right", &m_Right);
        pArea->Attribute("bottom", &m_Bottom);
    }
    ParseValueFromXmlElem(&m_Damage, pData->FirstChildElement("Damage"));
    return true;
}

void FallingStalactiteComponent::VPostInit()
{
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pRenderComponent = m_pOwner->GetRawComponent<ActorRenderComponent>(true);
    m_pAnimationComponent->AddObserver(this);
    m_pAnimationComponent->SetAnimation("home");
    m_Home = m_pOwner->GetPositionComponent()->GetPosition();
}

void FallingStalactiteComponent::Crumble()
{
    // ES_DEAD: crumble with STALACTITEHIT2
    m_State = State_Crumbling;
    m_pAnimationComponent->SetAnimation("crumble");
    Util::PlaySimpleSound(AQ_SOUND_ICICLE_HIT);
}

void FallingStalactiteComponent::VUpdate(uint32 msDiff)
{
    Actor* pClaw = AqClaw();
    if (!pClaw)
    {
        return;
    }
    const Point claw = pClaw->GetPositionComponent()->GetPosition();
    if (m_State == State_Hanging)
    {
        // ES_IDLE (0x4922b0): Claw's position inside UserRect1
        if (m_Right > m_Left && claw.x >= m_Left && claw.x < m_Right && claw.y >= m_Top && claw.y < m_Bottom)
        {
            m_State = State_Falling;
            m_Speed = 0;
            Util::PlaySimpleSound(AQ_SOUND_ICICLE_SNAP);
        }
        return;
    }
    if (m_State != State_Falling)
    {
        return;
    }

    // ES_FALLING (0x492370): 2000 px/s2 until it stands on something
    const double dt = msDiff / 1000.0;
    const Point pos = m_pOwner->GetPositionComponent()->GetPosition();
    m_Speed = std::min(m_Speed + 2000.0 * dt, 1500.0);
    const double step = m_Speed * dt;
    const double tipY = pos.y + 44;    // 48x124 image, offset -18
    RaycastResult ground = g_pApp->GetGameLogic()->VGetGamePhysics()->VRayCast(Point(pos.x, tipY - 2),
        Point(pos.x, tipY + step + 1), CollisionFlag_Solid | CollisionFlag_Ground);
    if (ground.foundIntersection)
    {
        AqMoveActor(m_pOwner.get(), Point(pos.x, pos.y + std::max(0.0, (double)ground.closestPixelDistance - 2.0)));
        Crumble();
        return;
    }
    const Point next(pos.x, pos.y + step);
    AqMoveActor(m_pOwner.get(), next);

    // its point on Claw: 5 points (0x120 of the projectile)
    if (fabs(claw.x - next.x) < 30 && claw.y - 50 < next.y + 44 && claw.y + 50 > next.y - 20)
    {
        ActorTemplates::CreateAreaDamage(Point(next.x, next.y + 30), Point(30, 40), m_Damage,
            CollisionFlag_EnemyAIAttack, "Rectangle", DamageType_MeleeAttack, Direction_Left, m_pOwner->GetGUID());
        Crumble();
    }
    if (next.y > m_Home.y + 3000)
    {
        m_State = State_Gone;
        m_pRenderComponent->SetVisible(false);
    }
}

void FallingStalactiteComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (m_State == State_Crumbling)
    {
        // ES_RESTART: hidden until Claw comes back from a death
        m_State = State_Gone;
        m_pRenderComponent->SetVisible(false);
    }
}

void FallingStalactiteComponent::ClawRespawnedDelegate(IEventDataPtr pEvent)
{
    if (m_State == State_Hanging)
    {
        return;
    }
    m_State = State_Hanging;
    m_Speed = 0;
    m_pRenderComponent->SetVisible(true);
    m_pAnimationComponent->SetAnimation("home");
    AqMoveActor(m_pOwner.get(), m_Home);
}
