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
    m_IdleTime(0)
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

void AquatisAIStateComponent::HitByStalactite(int damage)
{
    HealthComponent* pHealth = m_pOwner->GetRawComponent<HealthComponent>();
    if (!pHealth || pHealth->GetCurrentHealth() <= 0)
    {
        return;
    }

    STARTUP_TRACE("aquatis: hit by an icicle, health " + ToStr(pHealth->GetCurrentHealth()) + " -> " + ToStr(pHealth->GetCurrentHealth() - damage));

    m_bAllowDamage = true;
    pHealth->AddHealth(-damage, DamageType_MeleeAttack, GetPosition(), m_pOwner->GetGUID());
    m_bAllowDamage = false;
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

    //m_pQuestionMark = m_pOwner->GetRawComponent<FollowableComponent>(true);
}

void AquatisAIStateComponent::VOnWorldFinishedLoading()
{
    //m_pGabrielCannonComponent = pGabrielCannonActor->GetRawComponent<GabrielCannonComponent>(true);
}

void AquatisAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    //m_pAnimationComponent->SetAnimation(m_IdleAnim);

    //m_IsActive = true;
}

void AquatisAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
}

void AquatisAIStateComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
}

void AquatisAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }

    //m_pAnimationComponent->SetAnimation(m_IdleAnim);
}

void AquatisAIStateComponent::VUpdate(uint32 msDiff)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
}

void AquatisAIStateComponent::VOnBossFightStarted()
{
    // To refresh contact list after Claw's death
    g_pApp->GetGameLogic()->VGetGamePhysics()->VActivate(m_pOwner->GetGUID());

    m_pEnemyAIComponent->EnterBestState(true);
}

void AquatisAIStateComponent::VOnBossFightEnded(bool isBossDead)
{
    if (isBossDead)
    {
        s_bDefeated = true;

        SoundInfo soundInfo(SOUND_GAME_AMULET_RISE);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
            new EventData_Request_Play_Sound(soundInfo)));

        StrongActorPtr pGem = ActorTemplates::CreateActor(
            ActorPrototype_Level12_BossGem,
            m_pOwner->GetPositionComponent()->GetPosition());

        /*Point gemForce(-3.5, -7);
        g_pApp->GetGameLogic()->VGetGamePhysics()->VApplyLinearImpulse(pGem->GetGUID(), gemForce);*/
    }
    else
    {
        // To refresh contact list after Claw's death
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());

        /*m_TimeSinceLastAction = 0;
        m_CurrActionDelay = Util::GetRandomNumber(m_ActionMinInterval, m_ActionMaxInterval);*/
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
    return !pAquatis || !pAquatis->IsFightActive() || AquatisAIStateComponent::IsDefeated();
}

void AquatisCrackComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    if (newHealth >= oldHealth)
    {
        return;
    }

    m_Cooldown = 1500;
    m_pHealthComponent->SetMaxHealth();

    bool dropped = AquatisStalactiteComponent::DropNext();
    STARTUP_TRACE(std::string("aquatis: dynamite hit the crack, icicle ") + (dropped ? "falls" : "- none left"));

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
