#include "EnemyAIComponent.h"
#include "TigerGuardAIStateComponent.h"
#include "EnemyAIStateComponent.h"
#include "../RenderComponent.h"
#include "../PositionComponent.h"
#include "../ControllerComponents/HealthComponent.h"
#include "../PhysicsComponent.h"
#include "../AnimationComponent.h"
#include "../FollowableComponent.h"

#include "../ControllableComponent.h"

#include "../../../GameApp/BaseGameApp.h"
#include "../../../GameApp/BaseGameLogic.h"
#include "../../../UserInterface/HumanView.h"
#include "../../../Scene/SceneNodes.h"

#include "../../../Events/EventMgr.h"
#include "../../../Events/Events.h"

const char* EnemyAIComponent::g_Name = "EnemyAIComponent";

EnemyAIComponent::EnemyAIComponent()
    :
    m_bInitialized(false),
    m_bDead(false),
    m_bHasStateLock(true),
    m_TimeSinceLastSpeechSound(0),
    m_MinTimeIntervalForSpeechSound(0)
{

}

EnemyAIComponent::~EnemyAIComponent()
{

}

bool EnemyAIComponent::VInit(TiXmlElement* pData)
{
    assert(pData);

    if (TiXmlElement* pElem = pData->FirstChildElement("DeathAnimation"))
    {
        m_DeathAnimation = pElem->GetText();
    }
    
    if (TiXmlElement* pSoundsElem = pData->FirstChildElement("Sounds"))
    {
        for (TiXmlElement* pElem = pSoundsElem->FirstChildElement("Sound");
            pElem != NULL;
            pElem = pElem->NextSiblingElement())
        {
            std::string soundType = pElem->Attribute("SoundType");
            std::string soundName = pElem->Attribute("SoundName");

            if (soundType == "TakeDamage")
            {
                m_TakeDamageSounds.push_back(soundName);
            }
            else if (soundType == "MeleeAttack")
            {
                m_MeleeAttackSounds.push_back(soundName);
            }
            else if (soundType == "RangedAttack")
            {
                m_RangedAttackSounds.push_back(soundName);
            }
            else if (soundType == "Death")
            {
                m_DeathSounds.push_back(soundName);
            }
            else if (soundType == "Quote")
            {
                m_QuoteToHostileUnitSounds.push_back(soundName);
            }
            else
            {
                LOG_ERROR("Conflicting sound type: " + soundType + ", SoundName: " + soundName);
                assert(false && "Unknown sound type");
            }
        }
    }

    ParseValueFromXmlElem(&m_MinTimeIntervalForSpeechSound, pData->FirstChildElement("MinTimeIntervalForSpeechSound"));

    return true;
}

void EnemyAIComponent::VPostInit()
{
    m_pRenderComponent = MakeStrongPtr(m_pOwner->GetComponent<ActorRenderComponent>(ActorRenderComponent::g_Name));
    m_pPositionComponent = m_pOwner->GetPositionComponent();
    assert(m_pRenderComponent);
    assert(m_pPositionComponent);

    shared_ptr<HealthComponent> pHealthComp = 
        MakeStrongPtr(m_pOwner->GetComponent<HealthComponent>(HealthComponent::g_Name));
    assert(pHealthComp);

    pHealthComp->AddObserver(this);
}

void EnemyAIComponent::VPostPostInit()
{
    assert(!m_StateMap.empty());
    EnterBestState(true);
}

void EnemyAIComponent::VUpdate(uint32 msDiff)
{
    if (m_LiftState != Lift_None && !m_bDead)
    {
        UpdateLift(msDiff);
        return;
    }
    if (m_bDead && m_bCorpseGone)
    {
        return;
    }
    if (m_bDead)
    {
        const double dt = msDiff / 1000.0;
        m_CorpseTime += msDiff;
        if (m_CorpseKickCooldown > 0) m_CorpseKickCooldown -= msDiff;

        // Claw's sword sends the body flying again
        StrongActorPtr pClaw = g_pApp->GetGameLogic() ? g_pApp->GetGameLogic()->GetClawActor() : nullptr;
        if (pClaw && m_CorpseKickCooldown <= 0)
        {
            shared_ptr<ClawControllableComponent> pClawCtrl = MakeStrongPtr(pClaw->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name));
            if (pClawCtrl && pClawCtrl->IsMeleeAttacking())
            {
                const Point clawPos = pClaw->GetPositionComponent()->GetPosition();
                const double facing = pClawCtrl->GetFacing() == Direction_Left ? -1.0 : 1.0;
                const double dx = (m_pPositionComponent->GetX() - clawPos.x) * facing;
                const double dy = m_pPositionComponent->GetY() - clawPos.y;
                if (dx > -20 && dx < 110 && dy > -110 && dy < 80)
                {
                    m_CorpseVelocity.Set(facing * 260.0, -520.0);
                    m_CorpseKickCooldown = 350;
                    m_CorpseTime = std::min(m_CorpseTime, 500);
                    Util::PlayRandomHitSound();
                }
            }
        }

        // Flies under gravity
        m_CorpseVelocity.y += 1500.0 * dt;
        m_pPositionComponent->SetPosition(m_pPositionComponent->GetX() + m_CorpseVelocity.x * dt,
                                          m_pPositionComponent->GetY() + m_CorpseVelocity.y * dt);

        // This feels like a hack
        if (HumanView* pHumanView = g_pApp->GetHumanView())
        {
            shared_ptr<CameraNode> pCamera = pHumanView->GetCamera();
            if (pCamera)
            {
                shared_ptr<EventData_Move_Actor> pEvent(new EventData_Move_Actor(m_pOwner->GetGUID(), m_pPositionComponent->GetPosition()));
                IEventMgr::Get()->VTriggerEvent(pEvent);

                // Gone below the screen (or out of sight for a while)
                SDL_Rect renderRect = m_pRenderComponent->VGetPositionRect();
                SDL_Rect cam = pCamera->GetCameraRect();
                const bool below = renderRect.y > cam.y + cam.h;
                const bool lost = !pHumanView->IsRectInAnyView(renderRect) && m_CorpseTime > 3000;
                if (below || lost)
                {
                    shared_ptr<EventData_Destroy_Actor> pEvent(new EventData_Destroy_Actor(m_pOwner->GetGUID()));
                    IEventMgr::Get()->VQueueEvent(pEvent);

                    // This is really weird... but it is in original game exactly like this
                    SoundInfo splashSound("/GAME/SOUNDS/SPLASH.WAV");
                    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
                        new EventData_Request_Play_Sound(splashSound)));
                    m_bCorpseGone = true;   // (destroyed next frame: once only)
                }
            }
            else
            {
                LOG_ERROR("Could not retrieve camera");
            }
        }

        if (!m_DeathAnimation.empty())
        {
            auto pAnimComp = MakeStrongPtr(m_pOwner->GetComponent<AnimationComponent>(AnimationComponent::g_Name));
            assert(pAnimComp);

            pAnimComp->SetAnimation(m_DeathAnimation);
        }
    }
    else
    {
        m_TimeSinceLastSpeechSound += msDiff;
    }
}

void EnemyAIComponent::VOnHealthBelowZero(DamageType damageType, int sourceActorId)
{
    m_bDead = true;
    // The body flies up and away from Claw (it was a straight slide before)
    {
        double away = 1.0;
        StrongActorPtr pClaw = g_pApp->GetGameLogic() ? g_pApp->GetGameLogic()->GetClawActor() : nullptr;
        if (pClaw && pClaw->GetPositionComponent()->GetX() > m_pPositionComponent->GetX()) away = -1.0;
        m_CorpseVelocity.Set(away * 220.0, -380.0);
        m_CorpseTime = 0;
        m_CorpseKickCooldown = 250;
    }
    for (const auto &stateComponentIter : m_StateMap)
    {
        if (stateComponentIter.second->IsActive())
        {
            stateComponentIter.second->VOnStateLeave(NULL);
        }
    }

    // Play death sound
    Util::PlayRandomSoundFromList(m_DeathSounds);

    shared_ptr<PhysicsComponent> pPhysicsComponent = m_pOwner->GetPhysicsComponent();
    assert(pPhysicsComponent);

    pPhysicsComponent->Destroy();

    // HACK: This should be general like Subject/Observer but this is the only place so far
    // that the killed-by information is used
    StrongActorPtr pKiller = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(sourceActorId));
    if (pKiller != nullptr)
    {
        shared_ptr<ClawControllableComponent> pClaw = MakeStrongPtr(pKiller->GetComponent<ClawControllableComponent>());
        if (pClaw)
        {
            pClaw->OnClawKilledEnemy(damageType, m_pOwner.get());
        }
    }
}

void EnemyAIComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    // If he died we do not care
    auto pTiger = MakeStrongPtr(m_pOwner->GetComponent<TigerGuardAIStateComponent>(TigerGuardAIStateComponent::g_Name));
    // A heal (Omar given a segment back) is no hit
    if (newHealth >= 0 && newHealth < oldHealth && !(pTiger && pTiger->IsUnflinching()))
    {
        if (HasState("TakeDamageState"))
        {
            EnterState("TakeDamageState");
        }
    }

    if (newHealth < oldHealth)
    {
        ActorTemplates::CreateSingleAnimation(impactPoint, AnimationType_RedHitPoint);
        Util::PlayRandomHitSound();
    }
}

void EnemyAIComponent::VOnResistDamage(DamageType damageType, Point impactPoint)
{
    if (m_bPhased)
    {
        return;
    }
    if (HasState(EnemyAIState_Parry))
    {
        AcquireStateLock(GetState(EnemyAIState_Parry));
        EnterState(EnemyAIState_Parry);
    }
}

bool EnemyAIComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    // Held above Claw's head or flying: nothing else hurts him
    if (m_LiftState != Lift_None && damageType != DamageType_DeathTile)
    {
        return true;
    }
    if (m_bPhased)
    {
        return true;
    }
    if (auto pTiger = MakeStrongPtr(m_pOwner->GetComponent<TigerGuardAIStateComponent>(TigerGuardAIStateComponent::g_Name)))
    {
        if (pTiger->TryBlock(damageType))
        {
            return true;
        }
    }
    if (HasState(EnemyAIState_Parry))
    {
        shared_ptr<ParryEnemyAIStateComponent> pParryStateComponent =
            MakeStrongPtr(m_pOwner->GetComponent<ParryEnemyAIStateComponent>());
        assert(pParryStateComponent);

        BaseEnemyAIStateComponent* pCurrent = GetCurrentState();
        if (pCurrent == NULL)
        {
            return false;   // between states (e.g. a boss before its fight)
        }

        return pParryStateComponent->CanParry(damageType, pCurrent->VGetStateType());
    }

    return false;
}

void EnemyAIComponent::LeaveAllStates(BaseEnemyAIStateComponent* pNextState)
{
    for (const auto& stateIter : m_StateMap)
    {
        if (stateIter.second->IsActive())
        {
            stateIter.second->VOnStateLeave(pNextState);
        }
    }
}

void EnemyAIComponent::EnterState(const std::string &stateName)
{
    BaseEnemyAIStateComponent* pCurrentState = GetCurrentState();

    auto findIt = m_StateMap.find(stateName);
    assert(findIt != m_StateMap.end());

    LeaveAllStates(findIt->second);
    findIt->second->VOnStateEnter(pCurrentState);
}

void EnemyAIComponent::EnterState(EnemyAIState state)
{
    BaseEnemyAIStateComponent* pCurrentState = GetCurrentState();

    for (const auto &stateIter : m_StateMap)
    {
        if (stateIter.second->VGetStateType() == state)
        {
            LeaveAllStates(stateIter.second);
            stateIter.second->VOnStateEnter(pCurrentState);
            return;
        }
    }

    assert(false && "Did not find given state");
}

void EnemyAIComponent::EnterState(BaseEnemyAIStateComponent* pState)
{
    assert(pState != NULL);
    BaseEnemyAIStateComponent* pCurrentState = GetCurrentState();

    LeaveAllStates(pState);
    pState->VOnStateEnter(pCurrentState);
}

void EnemyAIComponent::AcquireStateLock(BaseEnemyAIStateComponent* pNewState)
{
    LeaveAllStates(pNewState);
    m_bHasStateLock = true;
}

bool EnemyAIComponent::EnterBestState(bool canForceEnter)
{
    if (m_bPhased)
    {
        return false;
    }

    BaseEnemyAIStateComponent* pCurrentState = GetCurrentState();

    // If some other state has state lock we will not force him to exit.
    // If no state is set at the moment, then force it aswell
    // Maybe this can revisited in future
    //LOG("m_bHasStateLock: " + ToStr(m_bHasStateLock) + ", canForceEnter: " + ToStr(canForceEnter));
    if ((!m_bHasStateLock && !canForceEnter && (pCurrentState != NULL)) /*&&
        pCurrentState->VCanEnter()*/)
    {
        return false;
    }

    BaseEnemyAIStateComponent* pBestState = NULL;
    int bestStatePrio = -1;

    for (const auto &stateIter : m_StateMap)
    {
        BaseEnemyAIStateComponent* pState = stateIter.second;
        if (pState == pCurrentState)
        {
            continue;
        }
        
        if (pState->VCanEnter() && 
            pState->VGetPriority() > bestStatePrio)
        {
            pBestState = pState;
            bestStatePrio = pState->VGetPriority();
        }
    }

    /*assert(pBestState != NULL);
    assert(bestStatePrio >= 0);*/

    if ((pCurrentState == NULL) ||
        !pCurrentState->VCanEnter() ||
        (pCurrentState->VGetPriority() < bestStatePrio))
    {
        AcquireStateLock(pBestState);
        // If best prio has some positive values, then we can only force switch it
        if (bestStatePrio > 0)
        {
            m_bHasStateLock = false;
        }
        EnterState(pBestState);
        return true;
    }

    return false;
}

BaseEnemyAIStateComponent* EnemyAIComponent::GetCurrentState()
{
    for (const auto &stateIter : m_StateMap)
    {
        if (stateIter.second->IsActive())
        {
            return stateIter.second;
        }
    }

    //assert(false && "Could not find any state ?");

    return NULL;
}

BaseEnemyAIStateComponent* EnemyAIComponent::GetState(EnemyAIState state)
{
    for (const auto &stateIter : m_StateMap)
    {
        if (stateIter.second->VGetStateType() == state)
        {
            return stateIter.second;
        }
    }

    return NULL;
}

bool EnemyAIComponent::HasState(const std::string &stateName)
{
    return m_StateMap.count(stateName) > 0;
}

bool EnemyAIComponent::HasState(EnemyAIState state)
{
    for (const auto &stateIter : m_StateMap)
    {
        if (stateIter.second->VGetStateType() == state)
        {
            return true;
        }
    }

    return false;
}

bool EnemyAIComponent::TryPlaySpeechSound(int chance, const SoundList& speechSounds)
{
    if (Util::RollDice(chance) && (m_TimeSinceLastSpeechSound > m_MinTimeIntervalForSpeechSound))
    {
        std::string playedSound = Util::PlayRandomSoundFromList(speechSounds);
        m_TimeSinceLastSpeechSound = 0;

        // Exclamation mark
        shared_ptr<FollowableComponent> pExclamationMark = 
            MakeStrongPtr(m_pOwner->GetComponent<FollowableComponent>());
        assert(pExclamationMark != nullptr);

        pExclamationMark->Activate(Util::GetSoundDurationMs(playedSound));

        return true;
    }

    return false;
}

/*bool EnemyAIComponent::TryFindClosestHostileActorOffset(Point& outOffset)
{
    outOffset = Point(DBL_MAX, DBL_MAX);
    bool found = false;

    for (auto stateIter : m_StateMap)
    {
        if (BaseAttackAIStateComponent* pEnemyState = dynamic_cast<BaseAttackAIStateComponent*>(stateIter.second))
        {
            if (pEnemyState->FindClosestHostileActorOffset().Length() < outOffset.Length())
            {
                outOffset = pEnemyState->FindClosestHostileActorOffset();
                found = true;
            }
        }
    }

    return found;
}*/
//=====================================================================================================================
// Lifting and throwing (CLAW.EXE: LS_LIFTED / LS_THROWN / LS_DROPPED, 0x441440 / 0x4412b0 / 0x441a20)
//=====================================================================================================================

bool EnemyAIComponent::CanBeLifted()
{
    if (m_bDead || m_LiftState != Lift_None || m_bPhased)
    {
        return false;
    }

    // Bosses (CLAW.EXE clears the flag for Raux, Marrow and the others have
    // their own classes) and enemies without the "lifted" picture stay down
    for (const auto& kv : m_StateMap)
    {
        if (dynamic_cast<BaseBossAIStateComponennt*>(kv.second))
        {
            return false;
        }
    }
    auto pAnim = MakeStrongPtr(m_pOwner->GetComponent<AnimationComponent>(AnimationComponent::g_Name));
    std::string liftedName("lifted");
    if (!pAnim || !pAnim->HasAnimation(liftedName))
    {
        return false;
    }

    // CLAW.EXE: not while he strikes (or shoots), jumps or jumps back;
    // walking, waiting, blocking or reeling from a hit is fine
    const std::string anim = pAnim->GetCurrentAnimationName();
    // (ReClaw's enemies strike again and again without CLAW.EXE's waits in
    // between, so the second half of a blow - after it landed - counts as
    // that wait)
    auto pCurAnim = pAnim->GetCurrentAnimation();
    const bool swinging = anim.compare(0, 6, "strike") == 0 && pCurAnim && !pCurAnim->IsPaused() &&
        pCurAnim->GetCurrentAnimationFrame()->idx * 2 < (int)pCurAnim->GetAnimFramesSize();
    if (swinging || anim.compare(0, 4, "jump") == 0 ||
        anim.compare(0, 4, "fall") == 0 || anim.compare(0, 4, "dead") == 0 || anim.compare(0, 4, "kill") == 0)
    {
        return false;
    }
    BaseEnemyAIStateComponent* pCurrent = GetCurrentState();
    if (pCurrent && (pCurrent->VGetStateType() == EnemyAIState_Dying || pCurrent->VGetStateType() == EnemyAIState_Falling))
    {
        return false;
    }
    return true;
}

void EnemyAIComponent::SetLiftAnimation(std::initializer_list<const char*> names)
{
    auto pAnim = MakeStrongPtr(m_pOwner->GetComponent<AnimationComponent>(AnimationComponent::g_Name));
    if (!pAnim)
    {
        return;
    }
    for (const char* cname : names)
    {
        std::string name(cname);
        if (pAnim->HasAnimation(name))
        {
            if (pAnim->GetCurrentAnimationName() != name)
            {
                pAnim->SetAnimation(name);
            }
            return;
        }
    }
}

void EnemyAIComponent::OnLifted(uint32 carrierId)
{
    LeaveAllStates(NULL);
    m_bPhased = true;   // no state may start while he is held or flies
    m_LiftState = Lift_Carried;
    m_CarrierId = carrierId;
    m_ThrowTime = 0;
    g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());
    SetLiftAnimation({ "lifted", "hithigh" });
    UpdateLift(0);
}

void EnemyAIComponent::Throw(const Point& position, const Point& velocityPxPerSec)
{
    m_LiftState = Lift_Thrown;
    m_ThrowVelocity = velocityPxPerSec;
    m_ThrowTime = 0;
    m_pPositionComponent->SetPosition(position);
    if (m_pRenderComponent)
    {
        m_pRenderComponent->SetMirrored(velocityPxPerSec.x < 0);
    }
    SetLiftAnimation({ "thrown", "fall", "hithigh" });
}

void EnemyAIComponent::DropFromCarrier(const Point& position)
{
    // CLAW.EXE LS_DROPPED: he falls where he was held and goes on after a moment
    m_LiftState = Lift_None;
    m_CarrierId = 0;
    m_bPhased = false;
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    pPhysics->VSetPosition(m_pOwner->GetGUID(), position);
    m_pPositionComponent->SetPosition(position);
    pPhysics->VActivate(m_pOwner->GetGUID());
    pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
    EnterBestState(true);
}

void EnemyAIComponent::UpdateLift(uint32 msDiff)
{
    if (m_LiftState == Lift_Carried)
    {
        StrongActorPtr pCarrier = MakeStrongPtr(g_pApp->GetGameLogic()->VGetActor(m_CarrierId));
        if (!pCarrier || !pCarrier->GetPositionComponent())
        {
            DropFromCarrier(m_pPositionComponent->GetPosition());
            return;
        }
        // above Claw's head, struggling, facing where Claw faces
        Point pos = pCarrier->GetPositionComponent()->GetPosition() + Point(0, -80);
        m_pPositionComponent->SetPosition(pos);
        g_pApp->GetGameLogic()->VGetGamePhysics()->VSetPosition(m_pOwner->GetGUID(), pos);
        if (auto pCtrl = MakeStrongPtr(pCarrier->GetComponent<ClawControllableComponent>(ClawControllableComponent::g_Name)))
        {
            if (m_pRenderComponent)
            {
                m_pRenderComponent->SetMirrored(pCtrl->GetFacing() == Direction_Left);
            }
            // CLAW.EXE: "lifted" while Claw picks him up, then "lifted2" above his head
            auto pClawAnim = MakeStrongPtr(pCarrier->GetComponent<AnimationComponent>(AnimationComponent::g_Name));
            const std::string clawAnim = pClawAnim ? pClawAnim->GetCurrentAnimationName() : "";
            if (clawAnim == "lift" || clawAnim == "ducklift")
                SetLiftAnimation({ "lifted", "hithigh" });
            else
                SetLiftAnimation({ "lifted2", "lifted", "hithigh" });
        }
        shared_ptr<EventData_Move_Actor> pEvent(new EventData_Move_Actor(m_pOwner->GetGUID(), pos));
        IEventMgr::Get()->VTriggerEvent(pEvent);
        return;
    }

    // Thrown: flies under the objects' gravity (2000 px/s²) until it hits
    // a wall, the floor or another enemy
    const double dt = msDiff / 1000.0;
    m_ThrowTime += msDiff;
    Point from = m_pPositionComponent->GetPosition();
    m_ThrowVelocity.y += 2000.0 * dt;
    Point to(from.x + m_ThrowVelocity.x * dt, from.y + m_ThrowVelocity.y * dt);

    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    const double dirX = m_ThrowVelocity.x < 0 ? -1.0 : 1.0;
    // body edges: ~25 px to the sides, ~45 px down to the feet
    RaycastResult wall = pPhysics->VRayCast(Point(from.x, from.y), Point(to.x + dirX * 25, to.y), CollisionFlag_Solid);
    RaycastResult floor;
    if (m_ThrowVelocity.y > 0)
    {
        floor = pPhysics->VRayCast(Point(to.x, from.y), Point(to.x, to.y + 45), CollisionFlag_Solid | CollisionFlag_Ground);
    }

    m_pPositionComponent->SetPosition(to);
    pPhysics->VSetPosition(m_pOwner->GetGUID(), to);
    shared_ptr<EventData_Move_Actor> pEvent(new EventData_Move_Actor(m_pOwner->GetGUID(), to));
    IEventMgr::Get()->VTriggerEvent(pEvent);

    if (wall.foundIntersection || floor.foundIntersection || m_ThrowTime > 4000)
    {
        LandThrown(nullptr);
        return;
    }

    // Another enemy in the way
    SDL_Rect me = { (int)to.x - 25, (int)to.y - 40, 50, 80 };
    for (const auto& kv : g_pApp->GetGameLogic()->GetActorMap())
    {
        const StrongActorPtr& pActor = kv.second;
        if (!pActor || pActor.get() == m_pOwner.get())
        {
            continue;
        }
        shared_ptr<EnemyAIComponent> pOther = MakeStrongPtr(pActor->GetComponent<EnemyAIComponent>(EnemyAIComponent::g_Name));
        if (!pOther || pOther->IsDead() || pOther->IsLiftedOrThrown())
        {
            continue;
        }
        SDL_Rect other = pPhysics->VGetAABB(pActor->GetGUID(), true);
        if (other.w > 0 && SDL_HasIntersection(&me, &other))
        {
            LandThrown(pActor.get());
            return;
        }
    }
}

void EnemyAIComponent::LandThrown(Actor* pHitEnemy)
{
    const uint32 thrower = m_CarrierId;
    const Point pos = m_pPositionComponent->GetPosition();
    m_LiftState = Lift_None;
    m_CarrierId = 0;
    m_bPhased = false;

    // The enemy it hit is knocked out with it; both count as Claw's kills
    if (pHitEnemy)
    {
        if (auto pHealth = MakeStrongPtr(pHitEnemy->GetComponent<HealthComponent>(HealthComponent::g_Name)))
        {
            pHealth->AddHealth(-(pHealth->GetCurrentHealth() + 1000), DamageType_MeleeAttack, pos, thrower);
        }
    }
    if (auto pHealth = MakeStrongPtr(m_pOwner->GetComponent<HealthComponent>(HealthComponent::g_Name)))
    {
        if (pHealth->GetCurrentHealth() > 0)
        {
            pHealth->AddHealth(-(pHealth->GetCurrentHealth() + 1000), DamageType_MeleeAttack, pos, thrower);
        }
    }
}
