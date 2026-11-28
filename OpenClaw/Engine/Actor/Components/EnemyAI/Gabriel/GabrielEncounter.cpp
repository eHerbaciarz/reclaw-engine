#include "GabrielEncounter.h"

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

const char* GabrielAIStateComponent::g_Name = "GabrielAIStateComponent";
const char* GabrielCannonButtonComponent::g_Name = "GabrielCannonButtonComponent";
const char* GabrielCannonComponent::g_Name = "GabrielCannonComponent";

//=====================================================================================================================
// GabrielAIStateComponent
//=====================================================================================================================

GabrielAIStateComponent::GabrielAIStateComponent()
    :
    BaseBossAIStateComponennt("Gabriel"),
    m_ThrowBombAnimFrameIdx(0),
    m_FireCannonAnimFrameIdx(0),
    m_TimeSinceLastAction(0),
    m_CurrActionDelay(0),
    m_bShouldThrowBombs(false)
{

}

GabrielAIStateComponent::~GabrielAIStateComponent()
{

}

bool GabrielAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    if (!BaseBossAIStateComponennt::VDelegateInit(pData))
    {
        return false;
    }

    DO_AND_CHECK(ParseValueFromXmlElem(&m_IdleAnim, pData->FirstChildElement("IdleAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_ThrowBombAnim, pData->FirstChildElement("ThrowBombAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_FireCannonAnim, pData->FirstChildElement("FireCannonAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_SpawnPirateAnim, pData->FirstChildElement("SpawnPirateAnim")));

    DO_AND_CHECK(ParseValueFromXmlElem(&m_ThrowBombAnimFrameIdx, pData->FirstChildElement("ThrowBombAnimFrameIdx")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_FireCannonAnimFrameIdx, pData->FirstChildElement("FireCannonAnimFrameIdx")));

    DO_AND_CHECK(ParseValueFromXmlElem(&m_NumThrownBombs, pData->FirstChildElement("NumThrownBombs")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_NumSpawnedPirates, pData->FirstChildElement("NumSpawnedPirates")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_BombDamage, pData->FirstChildElement("BombDamage")));

    DO_AND_CHECK(ParseValueFromXmlElem(&m_ActionMinInterval, pData->FirstChildElement("ActionMinInterval")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_ActionMaxInterval, pData->FirstChildElement("ActionMaxInterval")));

    for (TiXmlElement* pSoundElem = pData->FirstChildElement("ThrowBombSound");
        pSoundElem != NULL;
        pSoundElem = pSoundElem->NextSiblingElement("ThrowBombSound"))
    {
        m_ThrowBombSoundList.push_back(pSoundElem->GetText());
    }

    for (TiXmlElement* pSoundElem = pData->FirstChildElement("SummonPirateSound");
        pSoundElem != NULL;
        pSoundElem = pSoundElem->NextSiblingElement("SummonPirateSound"))
    {
        m_SpawnPirateSoundList.push_back(pSoundElem->GetText());
    }

    for (TiXmlElement* pSoundElem = pData->FirstChildElement("FireCannonSound");
        pSoundElem != NULL;
        pSoundElem = pSoundElem->NextSiblingElement("FireCannonSound"))
    {
        m_FireCannonSoundList.push_back(pSoundElem->GetText());
    }

    return true;
}

void GabrielAIStateComponent::VPostInit()
{
    BaseBossAIStateComponennt::VPostInit();

    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);

    m_pQuestionMark = m_pOwner->GetRawComponent<FollowableComponent>(true);

    m_CurrActionDelay = Util::GetRandomNumber(m_ActionMinInterval, m_ActionMaxInterval);
    
    // Populate his actions, should be data-driven but this is unique instance
    GabrielAction throwBombAction;
    throwBombAction.actionType = GabrielActionType_ThrowBombs;
    throwBombAction.probability = 40;
    throwBombAction.reuseTime = 2500;
    throwBombAction.timeSinceLastUse = 0;

    GabrielAction summonPirateAction;
    summonPirateAction.actionType = GabrielActionType_SummonPirate;
    summonPirateAction.probability = 30;
    summonPirateAction.reuseTime = 4000;
    summonPirateAction.timeSinceLastUse = 0;

    GabrielAction fireCannonAction;
    fireCannonAction.actionType = GabrielActionType_FireCannon;
    fireCannonAction.probability = 30;
    fireCannonAction.reuseTime = 3000;
    fireCannonAction.timeSinceLastUse = 0;

    m_GabrielActionList.push_back(throwBombAction);
    m_GabrielActionList.push_back(summonPirateAction);
    m_GabrielActionList.push_back(fireCannonAction);
}

void GabrielAIStateComponent::VOnWorldFinishedLoading()
{
    StrongActorPtr pGabrielCannonActor = g_pApp->GetGameLogic()->FindActorByName("Level8_GabrielCannon", true);
    assert(pGabrielCannonActor != nullptr);

    m_pGabrielCannonComponent = pGabrielCannonActor->GetRawComponent<GabrielCannonComponent>(true);
}

static const char* GB_SOUNDS = "/LEVEL8/SOUNDS/GABRIEL/";

static void GbSound(const char* const* names, int count)
{
    Util::PlaySimpleSound(std::string(GB_SOUNDS) + names[Util::GetRandomNumber(0, count - 1)]);
}

void GabrielAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    if (m_bBossFightStarted)
    {
        SetAct(Act_Idle);
    }
    else
    {
        m_pAnimationComponent->SetAnimation(m_IdleAnim);
    }
}

void GabrielAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
}

void GabrielAIStateComponent::SetAct(Act act)
{
    m_Act = act;
    m_bStruck = false;
    switch (act)
    {
        case Act_Idle:
            // idle1 or idle2; when it ends he picks his next move
            m_pAnimationComponent->SetAnimation(Util::GetRandomNumber(0, 1) == 0 ? "idle1" : "idle2");
            break;
        case Act_Bombs:
        {
            // 0x4458e0: LS_MULTIBOMBATTACK (strike7)
            static const char* kSounds[3] = { "00400002.WAV", "00400028.WAV", "00400029.WAV" };
            GbSound(kSounds, 3);
            m_pAnimationComponent->SetAnimation(m_ThrowBombAnim);
            break;
        }
        case Act_Thug:
        {
            // 0x445af0: LS_THUGATTACK1 (strike1), a pirate when it ends
            static const char* kSounds[4] = { "00400011.WAV", "00400012.WAV", "00400022.WAV", "00400022.WAV" };
            GbSound(kSounds, 4);
            m_pAnimationComponent->SetAnimation(m_SpawnPirateAnim);
            break;
        }
        case Act_Cannon:
        {
            // 0x445c50: LS_CANNONATTACK (strike7): his order to the cannon
            static const char* kSounds[2] = { "00400014.WAV", "00400015.WAV" };
            GbSound(kSounds, 2);
            m_pAnimationComponent->SetAnimation(m_FireCannonAnim);
            break;
        }
        case Act_Blocked:
        {
            static const char* kSounds[3] = { "00400016.WAV", "00400017.WAV", "00400030.WAV" };
            GbSound(kSounds, 3);
            m_pAnimationComponent->SetAnimation("blocked");
            break;
        }
        case Act_CannonHit:
            Util::PlaySimpleSound(std::string(GB_SOUNDS) + "00400023.WAV");
            m_pAnimationComponent->SetAnimation("hithigh");
            m_CannonHitDelay = 500;
            break;
    }
    m_pAnimationComponent->ResetAnimation();
}

void GabrielAIStateComponent::Decide()
{
    // 0x445740: one of three, alike
    switch (Util::GetRandomNumber(0, 2))
    {
        case 0:
            SetAct(Act_Bombs);
            return;
        case 1:
        {
            // no more than two of his pirates at once
            int alive = 0;
            for (auto& w : m_Pirates) if (!w.expired()) alive++;
            if (alive < 2) { SetAct(Act_Thug); return; }
            break;
        }
        default:
            if (m_pGabrielCannonComponent && m_pGabrielCannonComponent->IsReadyToFire()) { SetAct(Act_Cannon); return; }
            break;
    }
    SetAct(Act_Idle);
}

void GabrielAIStateComponent::ThrowBombs()
{
    // 0x4459e0: three of four bombs from (-75,-75), each landing 475, 350, 225 or
    // 100 px ahead at his height, thrown at 45 degrees under 2000 px/s2
    static const SoundList s_BombFallSoundList =
    {
        "/LEVEL8/SOUNDS/GABRIELBOMB/BOMBWHISTLE1.WAV",
        "/LEVEL8/SOUNDS/GABRIELBOMB/BOMBWHISTLE2.WAV",
        "/LEVEL8/SOUNDS/GABRIELBOMB/BOMBWHISTLE3.WAV"
    };
    Util::PlayRandomSoundFromList(s_BombFallSoundList);

    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    const int skip = Util::GetRandomNumber(0, 3);
    const double kGravity = 2000.0;
    for (int i = 0; i < 4; i++)
    {
        if (i == skip) continue;
        const double dist = 475.0 - 125.0 * i;
        StrongActorPtr pBomb = ActorTemplates::CreateActor_Projectile(ActorPrototype_Level8_GabrielBomb,
            m_pPositionComponent->GetPosition() + Point(-75, -75), Direction_Left, m_pOwner->GetGUID());
        if (!pBomb) continue;
        const double v = sqrt(kGravity * dist / 2.0);
        pPhysics->VSetGravityScale(pBomb->GetGUID(), (float)(kGravity / (9.8 * 75.0)));
        pPhysics->VSetLinearSpeed(pBomb->GetGUID(), Point(-v / 75.0, -v / 75.0));
    }
}

bool GabrielAIStateComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    if (m_bAllowDamage)
    {
        return false;
    }
    if (m_bBossFightStarted && m_IsActive && m_BlockCooldown <= 0 && m_Act == Act_Idle)
    {
        m_BlockCooldown = 500;
        SetAct(Act_Blocked);
    }
    return true;
}

void GabrielAIStateComponent::OnCannonHit()
{
    if (!m_bBossFightStarted)
    {
        return;
    }
    SetAct(Act_CannonHit);
}

void GabrielAIStateComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (!m_IsActive || !m_bBossFightStarted || m_bStruck)
    {
        return;
    }
    // frame 401 of strike7
    if (pNewFrame->idx != 1)
    {
        return;
    }
    if (m_Act == Act_Bombs)
    {
        m_bStruck = true;
        ThrowBombs();
    }
    else if (m_Act == Act_Cannon)
    {
        m_bStruck = true;
        m_pGabrielCannonComponent->Fire();
    }
}

void GabrielAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
    switch (m_Act)
    {
        case Act_Idle:
            Decide();
            break;
        case Act_Thug:
        {
            // 0x445b60: the pirate leaps down to the deck
            static const Point pirateSpawnPos(42666, 5150);
            StrongActorPtr pPirate = ActorTemplates::CreateActor(ActorPrototype_Level8_GabrielPirate, pirateSpawnPos);
            if (pPirate) m_Pirates.push_back(pPirate);
            SetAct(Act_Idle);
            break;
        }
        case Act_CannonHit:
            break; // timed in VUpdate
        default:
            SetAct(Act_Idle);
            break;
    }
}

void GabrielAIStateComponent::VUpdate(uint32 msDiff)
{
    if (m_BlockCooldown > 0) m_BlockCooldown -= msDiff;
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
    if (m_Act == Act_CannonHit && m_CannonHitDelay > 0)
    {
        m_CannonHitDelay -= msDiff;
        if (m_CannonHitDelay <= 0)
        {
            // 0x445f80: -20 (five hits), then he answers with bombs
            HealthComponent* pHealth = m_pOwner->GetRawComponent<HealthComponent>();
            pHealth->SetCurrentHealth(std::max<int>(0, (int)pHealth->GetCurrentHealth() - 20));
            if (pHealth->GetCurrentHealth() > 0 && m_IsActive)
            {
                SetAct(Act_Bombs);
            }
        }
    }
}

void GabrielAIStateComponent::VOnBossFightStarted()
{
    BaseBossAIStateComponennt::VOnBossFightStarted();

    // To refresh contact list after Claw's death
    g_pApp->GetGameLogic()->VGetGamePhysics()->VActivate(m_pOwner->GetGUID());

    m_pEnemyAIComponent->EnterBestState(true);
}

void GabrielAIStateComponent::VOnBossFightEnded(bool isBossDead)
{
    BaseBossAIStateComponennt::VOnBossFightEnded(isBossDead);

    if (isBossDead)
    {
        SoundInfo soundInfo(SOUND_GAME_AMULET_RISE);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
            new EventData_Request_Play_Sound(soundInfo)));

        StrongActorPtr pGem = ActorTemplates::CreateActor(
            ActorPrototype_Level8_BossGem,
            m_pOwner->GetPositionComponent()->GetPosition());

        Point gemForce(-3.5, -7);
        g_pApp->GetGameLogic()->VGetGamePhysics()->VApplyLinearImpulse(pGem->GetGUID(), gemForce);
    }
    else
    {
        // To refresh contact list after Claw's death
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());

        m_TimeSinceLastAction = 0;
        m_CurrActionDelay = Util::GetRandomNumber(m_ActionMinInterval, m_ActionMaxInterval);
    }
}

void GabrielAIStateComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    BaseBossAIStateComponennt::VOnHealthChanged(oldHealth, newHealth, damageType, impactPoint, sourceActorId);
}

//=====================================================================================================================
// GabrielCannonButtonComponent
//=====================================================================================================================

GabrielCannonButtonComponent::GabrielCannonButtonComponent()
{

}

GabrielCannonButtonComponent::~GabrielCannonButtonComponent()
{

}

bool GabrielCannonButtonComponent::VInit(TiXmlElement* pData)
{
    DO_AND_CHECK(ParseValueFromXmlElem(&m_BlinkingAnim, pData->FirstChildElement("BlinkingAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_PressedImageName, pData->FirstChildElement("PressedImageName")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_ButtonPressSound, pData->FirstChildElement("ButtonPressSound")));

    return true;
}

void GabrielCannonButtonComponent::VPostInit()
{
    m_pRenderComponent = m_pOwner->GetRawComponent<ActorRenderComponent>(true);
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    DO_AND_CHECK(m_pAnimationComponent->SetAnimation(m_BlinkingAnim));

    m_pOwner->GetRawComponent<TriggerComponent>(true)->AddObserver(this);
}

void GabrielCannonButtonComponent::VUpdate(uint32 msDiff)
{
    /*static int delay = 2000;

    delay -= msDiff;
    if (delay < 0)
    {
        if (m_pGabrielCannonComponent->IsReadyToFire())
        {
            m_pGabrielCannonComponent->Fire();
            delay = 5000;
            Reset();
        }
    }*/
}

void GabrielCannonButtonComponent::VOnWorldFinishedLoading()
{
    StrongActorPtr pGabrielCannonActor = g_pApp->GetGameLogic()->FindActorByName("Level8_GabrielCannon", true);
    assert(pGabrielCannonActor != nullptr);

    m_pGabrielCannonComponent = pGabrielCannonActor->GetRawComponent<GabrielCannonComponent>(true);
}

void GabrielCannonButtonComponent::VOnActorEnteredTrigger(Actor* pActorWhoEntered, FixtureType triggerType)
{
    if (triggerType != FixtureType_Trigger_GabrielButton)
    {
        return;
    }

    // Switch is red
    if (m_pAnimationComponent->GetCurrentAnimation()->IsPaused())
    {
        return;
    }

    m_pGabrielCannonComponent->Rise();
    m_pAnimationComponent->PauseAnimation();
    m_pRenderComponent->SetImage(m_PressedImageName);
    Util::PlaySimpleSound(m_ButtonPressSound);
}

void GabrielCannonButtonComponent::Reset()
{
    if (m_pAnimationComponent->GetCurrentAnimation()->IsPaused())
    {
        m_pAnimationComponent->ResetAnimation();
    }
}

//=====================================================================================================================
// GabrielCannonComponent
//=====================================================================================================================

GabrielCannonComponent::GabrielCannonComponent()
    :
    m_HorizontalFireAnimIdx(0),
    m_VerticalFireAnimIdx(0),
    m_bQueuedRise(false),
    m_bActive(false)
{
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &GabrielCannonComponent::BossFightStartedDelegate), EventData_Boss_Fight_Started::sk_EventType);
    IEventMgr::Get()->VAddListener(MakeDelegate(this, &GabrielCannonComponent::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
}

GabrielCannonComponent::~GabrielCannonComponent()
{
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &GabrielCannonComponent::BossFightStartedDelegate), EventData_Boss_Fight_Started::sk_EventType);
    IEventMgr::Get()->VRemoveListener(MakeDelegate(this, &GabrielCannonComponent::BossFightEndedDelegate), EventData_Boss_Fight_Ended::sk_EventType);
}

bool GabrielCannonComponent::VInit(TiXmlElement* pData)
{
    DO_AND_CHECK(ParseValueFromXmlElem(&m_HorizontalFireAnim, pData->FirstChildElement("HorizontalFireAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_VerticalFireAnim, pData->FirstChildElement("VerticalFireAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_ToVerticalAnim, pData->FirstChildElement("ToVerticalAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_ToHorizontalAnim, pData->FirstChildElement("ToHorizontalAnim")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_IdleAnim, pData->FirstChildElement("IdleAnim")));

    DO_AND_CHECK(ParseValueFromXmlElem(&m_CannonRiseSound, pData->FirstChildElement("CannonRiseSound")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_CannonDropSound, pData->FirstChildElement("CannonDropSound")));

    DO_AND_CHECK(ParseValueFromXmlElem(&m_HorizontalFireAnimIdx, pData->FirstChildElement("HorizontalFireAnimIdx")));
    DO_AND_CHECK(ParseValueFromXmlElem(&m_VerticalFireAnimIdx, pData->FirstChildElement("VerticalFireAnimIdx")));

    return true;
}

void GabrielCannonComponent::VPostInit()
{
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);
    m_pAnimationComponent->SetAnimation(m_IdleAnim);
}

void GabrielCannonComponent::VUpdate(uint32 msDiff)
{
    if (!m_bActive)
    {
        return;
    }
}

void GabrielCannonComponent::VOnWorldFinishedLoading()
{
    StrongActorPtr pGabrielCannonButtonActor = g_pApp->GetGameLogic()->FindActorByName("Level8_GabrielButton", true);
    assert(pGabrielCannonButtonActor != nullptr);

    m_pCannonButton = pGabrielCannonButtonActor->GetRawComponent<GabrielCannonButtonComponent>(true);

    StrongActorPtr pGabrielActor = g_pApp->GetGameLogic()->FindActorByName("Level8_Gabriel", true);
    assert(pGabrielCannonButtonActor != nullptr);

    m_pGabrielHealthComponent = pGabrielActor->GetRawComponent<HealthComponent>(true);
    m_pGabrielAI = pGabrielActor->GetRawComponent<GabrielAIStateComponent>();
}

void GabrielCannonComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (pAnimation->GetName() == m_VerticalFireAnim && pNewFrame->idx == m_VerticalFireAnimIdx)
    {
        
    }
    else if (pAnimation->GetName() == m_HorizontalFireAnim && pNewFrame->idx == m_HorizontalFireAnimIdx)
    {
        
    }
}

void GabrielCannonComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (pAnimation->GetName() == m_ToVerticalAnim)
    {
        m_pAnimationComponent->SetAnimation(m_VerticalFireAnim);
    }
    else if (pAnimation->GetName() == m_VerticalFireAnim)
    {
        m_pAnimationComponent->SetAnimation(m_ToHorizontalAnim);
        Util::PlaySimpleSound(m_CannonDropSound);
    }
    else if (pAnimation->GetName() == m_ToHorizontalAnim)
    {
        m_pAnimationComponent->SetAnimation(m_IdleAnim);
    }
    else if (pAnimation->GetName() == m_HorizontalFireAnim)
    {
        m_pAnimationComponent->SetAnimation(m_IdleAnim);
    }
}

void GabrielCannonComponent::VOnAnimationChanged(Animation* pOldAnimation, Animation* pNewAnimation)
{
    if (pNewAnimation->GetName() == m_IdleAnim && m_bQueuedRise)
    {
        m_pAnimationComponent->SetAnimation(m_ToVerticalAnim);
        Util::PlaySimpleSound(m_CannonRiseSound);
        m_bQueuedRise = false;
    }
}

void GabrielCannonComponent::VOnAnimationAtLastFrame(Animation* pAnimation)
{
    if (pAnimation->GetName() == m_ToVerticalAnim)
    {
        pAnimation->Pause();
    }
}

void GabrielCannonComponent::Fire()
{
    std::string currAnimName = m_pAnimationComponent->GetCurrentAnimationName();
    /*if (currAnimName != m_IdleAnim)
    {
        return;
    }

    m_pAnimationComponent->SetAnimation(m_HorizontalFireAnim);*/

    if (currAnimName == m_IdleAnim)
    {
        m_pAnimationComponent->SetAnimation(m_HorizontalFireAnim);

        m_pCannonButton->Reset();
        ActorTemplates::CreateActor_Projectile(
            ActorPrototype_Level8_CannonBall,
            m_pOwner->GetPositionComponent()->GetPosition() + Point(-35, -12),
            Direction_Left,
            m_pOwner->GetGUID());

        Util::PlaySimpleSound("/LEVEL8/SOUNDS/GABRIELCANNON/CANNON.WAV");
    }
    else if (currAnimName == m_ToVerticalAnim)
    {
        m_pAnimationComponent->SetAnimation(m_VerticalFireAnim);
        m_pCannonButton->Reset();

        // the ball goes up into Gabriel (CLAW.EXE: LS_CANNONHIT, -20)
        if (GabrielAIStateComponent* pGabriel = m_pGabrielAI)
        {
            pGabriel->OnCannonHit();
        }

        Util::PlaySimpleSound("/LEVEL8/SOUNDS/GABRIELCANNON/CANNON.WAV");
    }
}

void GabrielCannonComponent::Rise()
{
    if (m_pAnimationComponent->GetCurrentAnimationName() == m_IdleAnim)
    {
        m_pAnimationComponent->SetAnimation(m_ToVerticalAnim);
        Util::PlaySimpleSound(m_CannonRiseSound);
    }
    else
    {
        m_bQueuedRise = true;
    }
}

void GabrielCannonComponent::BossFightStartedDelegate(IEventDataPtr pEvent)
{
    m_bActive = true;
    LOG_TRACE("Started !");
}

void GabrielCannonComponent::BossFightEndedDelegate(IEventDataPtr pEvent)
{
    m_bActive = false;
    m_pAnimationComponent->SetAnimation(m_IdleAnim);
    m_bQueuedRise = false;
}

bool GabrielCannonComponent::IsReadyToFire()
{
    return (
        (m_pAnimationComponent->GetCurrentAnimationName() == m_IdleAnim) ||
        (m_pAnimationComponent->GetCurrentAnimationName() == m_ToVerticalAnim && 
        m_pAnimationComponent->GetCurrentAnimation()->IsPaused())
        );
}