// Runtime state of the actor components, for quick save / quick load / rewind.
//
// Every function serves both directions (see StateArchive.h). Only state that
// changes while the level is played is stored; everything that comes from the
// actor XML is recreated by the factory. Lists derived from physics contacts
// (actors inside a trigger, ground contacts, carried bodies) are left alone:
// Box2D reports the contacts again after the bodies are moved back.

#include "StateArchive.h"

#include "../Events/EventMgr.h"
#include "../Events/Events.h"

#include "../Actor/Actor.h"
#include "../Actor/Components/PositionComponent.h"
#include "../Actor/Components/PhysicsComponent.h"
#include "../Actor/Components/ControllableComponent.h"
#include "../Actor/Components/Animation.h"
#include "../Actor/Components/AnimationComponent.h"
#include "../Actor/Components/RenderComponent.h"
#include "../Actor/Components/ControllerComponents/AmmoComponent.h"
#include "../Actor/Components/ControllerComponents/HealthComponent.h"
#include "../Actor/Components/ControllerComponents/LifeComponent.h"
#include "../Actor/Components/ControllerComponents/ScoreComponent.h"
#include "../Actor/Components/ControllerComponents/PowerupComponent.h"
#include "../Actor/Components/PickupComponents/PickupComponent.h"
#include "../Actor/Components/DestroyableComponent.h"
#include "../Actor/Components/CheckpointComponent.h"
#include "../Actor/Components/TriggerComponents/TriggerComponent.h"
#include "../Actor/Components/TriggerComponents/BossStagerTriggerComponent.h"
#include "../Actor/Components/TriggerComponents/SoundTriggerComponent.h"
#include "../Actor/Components/KinematicComponent.h"
#include "../Actor/Components/PathElevatorComponent.h"
#include "../Actor/Components/SawBladeComponent.h"
#include "../Actor/Components/SteppingGroundComponent.h"
#include "../Actor/Components/SpringBoardComponent.h"
#include "../Actor/Components/RopeComponent.h"
#include "../Actor/Components/AIComponents/ProjectileAIComponent.h"
#include "../Actor/Components/ProjectileSpawnerComponent.h"
#include "../Actor/Components/GlobalAmbientSoundComponent.h"
#include "../Actor/Components/AuraComponents/AuraComponent.h"
#include "../Actor/Components/FollowableComponent.h"
#include "../Actor/Components/GlitterComponent.h"
#include "../Actor/Components/PredefinedMoveComponent.h"
#include "../Actor/Components/AreaDamageComponent.h"
#include "../Actor/Components/EnemyAI/EnemyAIComponent.h"
#include "../Actor/Components/EnemyAI/EnemyAIStateComponent.h"
#include "../Actor/Components/EnemyAI/Gabriel/GabrielEncounter.h"
#include "../Actor/Components/EnemyAI/Marrow/MarrowEncounter.h"

//=====================================================================================================================
// Core
//=====================================================================================================================

void PositionComponent::VSerializeState(StateArchive& ar)
{
    ar(m_Position);
}

void PhysicsComponent::VSerializeState(StateArchive& ar)
{
    ar(m_IsClimbing);
    ar(m_IsStopped);
    ar(m_IsRunning);
    ar(m_Direction);
    ar(m_IsFalling);
    ar(m_IsJumping);
    ar(m_HeightInAir);
    ar(m_FallHeight);
    ar(m_HasConstantSpeed);
    ar(m_ConstantSpeed);
    ar(m_CurrentSpeed);
    ar(m_ExternalSourceSpeed);
    ar(m_ExternalConveyorBeltSpeed);
    ar(m_ClimbingSpeed);
    ar(m_IgnoreJump);
    ar(m_DoNothingTimeout);
    ar(m_bIsForcedUp);
    ar(m_ForcedUpHeight);
    ar(m_MaxJumpHeight);
    ar(m_GravityScale);
}

void ControllableComponent::VSerializeState(StateArchive& ar)
{
    ar(m_Active);
    ar(m_bFrozen);
    ar(m_DuckingTime);
    ar(m_LookingUpTime);
    ar(m_FrozenTime);
    ar(m_MaxJumpHeight);
}

void ClawControllableComponent::VSerializeState(StateArchive& ar)
{
    ControllableComponent::VSerializeState(ar);

    ar(m_bIsInBossFight);
    ar(m_TakeDamageTimeLeftMs);
    ar(m_ThrowingTime);
    ar(m_State);
    ar(m_LastState);
    ar(m_IdleTime);
    ar(m_Direction);
}

void Animation::SerializeState(StateArchive& ar)
{
    uint32 idx = _currentAnimationFrame.idx;
    ar(idx);
    ar(_currentTime);
    ar(_delay);
    ar(_paused);
    ar(_reversed);
    ar(_isBeingReversed);

    if (ar.IsLoading() && !_animationFrames.empty())
    {
        if (idx >= _animationFrames.size())
        {
            idx = 0;
        }
        _currentAnimationFrame = _animationFrames[idx];
    }
}

void AnimationComponent::VSerializeState(StateArchive& ar)
{
    std::string name = _currentAnimation ? _currentAnimation->GetName() : std::string();
    ar(name);

    if (ar.IsLoading())
    {
        auto findIt = _animationMap.find(name);
        if (findIt == _animationMap.end())
        {
            return;
        }
        // Switch silently: no observers, the other components are restored too.
        _currentAnimation = findIt->second;
    }

    if (_currentAnimation)
    {
        _currentAnimation->SerializeState(ar);
        if (ar.IsLoading())
        {
            OnAnimationFrameStarted(_currentAnimation->GetCurrentAnimationFrame());
        }
    }
}

void ActorRenderComponent::VSerializeState(StateArchive& ar)
{
    ar(m_IsVisible);
    ar(m_IsMirrored);
    ar(m_IsInverted);
    ar(m_Alpha);
    ar(m_ColorMod);

    std::string imageName = m_CurrentImageName;
    ar(imageName);
    if (ar.IsLoading() && !imageName.empty())
    {
        SetImage(imageName);
    }
}

//=====================================================================================================================
// Claw's stats (HUD is refreshed without effects or sounds)
//=====================================================================================================================

void HealthComponent::VSerializeState(StateArchive& ar)
{
    int32 oldHealth = m_CurrentHealth;

    ar(m_CurrentHealth);
    ar(m_MaxHealth);
    ar(m_bInvulnerable);

    if (ar.IsLoading() && m_IsController)
    {
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Updated_Health(oldHealth, m_CurrentHealth, true)));
    }
}

void AmmoComponent::VSerializeState(StateArchive& ar)
{
    ar(m_ActiveAmmoType);
    ar(m_AmmoMap);

    if (ar.IsLoading())
    {
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(
            new EventData_Updated_Ammo_Type(m_pOwner->GetGUID(), (uint32)m_ActiveAmmoType)));
        BroadcastAmmoChanged(m_ActiveAmmoType, m_AmmoMap[m_ActiveAmmoType]);
    }
}

void LifeComponent::VSerializeState(StateArchive& ar)
{
    int oldLives = m_CurrentLives;
    ar(m_CurrentLives);

    if (ar.IsLoading())
    {
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Updated_Lives(oldLives, m_CurrentLives, true)));
    }
}

void ScoreComponent::VSerializeState(StateArchive& ar)
{
    uint32 oldScore = m_CurrentScore;
    ar(m_CurrentScore);

    if (ar.IsLoading())
    {
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(
            new EventData_Updated_Score(m_pOwner->GetGUID(), oldScore, m_CurrentScore, true)));
    }
}

void PowerupComponent::VSerializeState(StateArchive& ar)
{
    PowerupType previous = m_ActivePowerup;

    ar(m_RemainingPowerupTime);
    ar(m_ActivePowerup);

    if (ar.IsLoading())
    {
        if (previous != PowerupType_None && previous != m_ActivePowerup)
        {
            BroadcastPowerupStatusUpdated(m_pOwner->GetGUID(), previous, true);
        }
        if (m_ActivePowerup != PowerupType_None)
        {
            BroadcastPowerupStatusUpdated(m_pOwner->GetGUID(), m_ActivePowerup, false);
            BroadcastPowerupTimeUpdated(m_pOwner->GetGUID(), m_ActivePowerup, m_RemainingPowerupTime / 1000);
        }
        SetPowerupSparklesVisibility(m_ActivePowerup == PowerupType_Catnip);
    }
}

//=====================================================================================================================
// Pickups, crates, checkpoints, triggers
//=====================================================================================================================

void TreasurePickupComponent::VSerializeState(StateArchive& ar)
{
    ar(m_IsPickedUp);
}

void DestroyableComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bIsDead);
    ar(m_DeleteDelayTimeLeft);
}

void CheckpointComponent::VSerializeState(StateArchive& ar)
{
    ar(m_IsSaveCheckpoint);
    ar(m_SaveCheckpointNumber);
}

void TriggerComponent::VSerializeState(StateArchive& ar)
{
    ar(m_TriggerRemaining);
    ar(m_DeactivatedTriggerTypesMap);
}

void SoundTriggerComponent::VSerializeState(StateArchive& ar)
{
    ar(m_EnterCount);
}

void BossStagerTriggerComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bActivated);
    ar(m_bDone);
    ar(m_State);
    ar(m_CurrentDelay);
    ar(m_ActorWhoEnteredId);
    ar.ActorRef(m_pPopupTitleActor);
}

void AreaDamageComponent::VSerializeState(StateArchive& ar)
{
    ar(m_ActiveTime);
}

//=====================================================================================================================
// Moving platforms and hazards
//=====================================================================================================================

void KinematicComponent::VSerializeState(StateArchive& ar)
{
    ar(m_Speed);
    ar(m_bIsTriggered);
    ar(m_bIsDone);
    ar(m_LastPosition);
    ar(m_CurrentSpeed);
    ar(m_LastSpeed);
    ar(m_bCheckCarriedBodies);
    ar(m_TimeSinceLastCarriedBodiesCheck);
}

void PathElevatorComponent::VSerializeState(StateArchive& ar)
{
    ar(m_CurrentStepDef.direction);
    ar(m_CurrentStepDef.isWaiting);
    ar(m_CurrentStepDef.waitMsTime);
    ar(m_CurrentStepDefIdx);
    ar(m_CurrentDirection);
    ar(m_StepTime);
    ar(m_StepElapsedDistance);
    ar(m_LastPosition);
    ar(m_CurrentSpeed);
}

void SawBladeComponent::VSerializeState(StateArchive& ar)
{
    ar(m_CurrTimeOff);
}

void SteppingGroundComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bIsSteppedOn);
    ar(m_TimeBeforeToggleLeft);
    ar(m_TimeOffLeft);
}

void SpringBoardComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bIsSteppedOn);
}

void RopeComponent::VSerializeState(StateArchive& ar)
{
    ar(m_TimeStanceAttach);
    ar.ActorRef(m_pAttachedActor);
}

void ProjectileAIComponent::VSerializeState(StateArchive& ar)
{
    ar(m_IsActive);
    ar(m_DetonationTime);
    ar(m_bHasDetonationTime);
    ar(m_ProjectileSpeed);
}

void ProjectileSpawnerComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bReady);
    ar(m_StartDelayLeft);
}

void GlobalAmbientSoundComponent::VSerializeState(StateArchive& ar)
{
    ar(m_CurrentTimeOff);
    ar(m_TimeOff);
    ar(m_bIsStopped);
}

void BaseAuraComponent::VSerializeState(StateArchive& ar)
{
    ar(m_TimeSinceLastPulse);
    ar(m_bIsEnabled);
}

void FollowableComponent::VSerializeState(StateArchive& ar)
{
    ar(m_CurrentMsDuration);
}

void GlitterComponent::VSerializeState(StateArchive& ar)
{
    ar(m_Active);
}

void PredefinedMoveComponent::VSerializeState(StateArchive& ar)
{
    ar(m_CurrMoveIdx);
    ar(m_CurrMoveTime);
}

//=====================================================================================================================
// Enemies
//=====================================================================================================================

void EnemyAIComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bInitialized);
    ar(m_bDead);
    ar(m_bHasStateLock);
    ar(m_TimeSinceLastSpeechSound);
}

void BaseEnemyAIStateComponent::VSerializeState(StateArchive& ar)
{
    ar(m_IsActive);
}

static void SerializeAction(StateArchive& ar, std::unique_ptr<EnemyAIAction>& pAction)
{
    bool hasAction = pAction != nullptr;
    ar(hasAction);
    if (!hasAction)
    {
        return;
    }

    bool isActive = pAction ? pAction->isActive : false;
    uint32 activeAnimIdx = pAction ? pAction->activeAnimIdx : 0;
    ar(isActive);
    ar(activeAnimIdx);
    if (ar.IsLoading() && pAction)
    {
        pAction->isActive = isActive;
        pAction->activeAnimIdx = activeAnimIdx < pAction->animations.size() ? activeAnimIdx : 0;
    }
}

void PatrolEnemyAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseEnemyAIStateComponent::VSerializeState(ar);

    ar(m_bInitialized);
    ar(m_bRecalculatePatrolBorders);
    ar(m_LeftPatrolBorder);
    ar(m_RightPatrolBorder);
    ar(m_Direction);
    SerializeAction(ar, m_pWalkAction);
    SerializeAction(ar, m_pIdleAction);
}

void BaseAttackAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseEnemyAIStateComponent::VSerializeState(ar);

    ar(m_CurrentAttackActionIdx);
    if (ar.IsLoading() && m_CurrentAttackActionIdx >= (int)m_AttackActions.size())
    {
        m_CurrentAttackActionIdx = 0;
    }
}

void DiveAttackAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseEnemyAIStateComponent::VSerializeState(ar);

    ar(m_DiveState);
    ar(m_Destination);
}

void FallAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseEnemyAIStateComponent::VSerializeState(ar);

    ar(m_bAlreadyFell);
}

void RollEnemyAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseEnemyAIStateComponent::VSerializeState(ar);

    ar(m_LastRollDirection);
    ar(m_ReuseTimeLeft);
}

void BaseBossAIStateComponennt::VSerializeState(StateArchive& ar)
{
    BaseEnemyAIStateComponent::VSerializeState(ar);

    ar(m_bBossFightStarted);
}

void BaseBossAIStateComponennt::VOnStateRestored()
{
    // Boss bar shows the restored health
    if (m_bBossFightStarted && m_pHealthComponent)
    {
        int maxHealth = m_pHealthComponent->GetMaxHealth();
        int health = m_pHealthComponent->GetCurrentHealth();
        int percentage = maxHealth > 0 ? (int)(((float)health / (float)maxHealth) * 100) : 0;
        IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Boss_Health_Changed(percentage, health)));
    }
}

void GabrielAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseBossAIStateComponennt::VSerializeState(ar);

    ar(m_TimeSinceLastAction);
    ar(m_CurrActionDelay);
    ar(m_bShouldThrowBombs);

    uint32 count = (uint32)m_GabrielActionList.size();
    ar(count);
    for (uint32 i = 0; i < count; i++)
    {
        int timeSinceLastUse = i < m_GabrielActionList.size() ? m_GabrielActionList[i].timeSinceLastUse : 0;
        ar(timeSinceLastUse);
        if (ar.IsLoading() && i < m_GabrielActionList.size())
        {
            m_GabrielActionList[i].timeSinceLastUse = timeSinceLastUse;
        }
    }
}

void GabrielCannonComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bQueuedRise);
    ar(m_bActive);
}

void MarrowAIStateComponent::VSerializeState(StateArchive& ar)
{
    BaseBossAIStateComponennt::VSerializeState(ar);

    uint32 count = (uint32)m_GabrielActionList.size();
    ar(count);
    for (uint32 i = 0; i < count; i++)
    {
        int timeSinceLastUse = i < m_GabrielActionList.size() ? m_GabrielActionList[i].timeSinceLastUse : 0;
        ar(timeSinceLastUse);
        if (ar.IsLoading() && i < m_GabrielActionList.size())
        {
            m_GabrielActionList[i].timeSinceLastUse = timeSinceLastUse;
        }
    }
}

void MarrowFloorComponent::VSerializeState(StateArchive& ar)
{
    ar(m_bActive);
}
