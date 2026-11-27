#ifndef __ENEMY_AI_COMPONENT_H__
#define __ENEMY_AI_COMPONENT_H__

#include "../../../SharedDefines.h"
#include "../../ActorComponent.h"

#include "../ControllerComponents/HealthComponent.h"
#include "EnemyAIStateComponent.h"

class BaseEnemyAIStateComponent;
class PositionComponent;
class ActorRenderComponent;

typedef std::map<std::string, BaseEnemyAIStateComponent*> EnemyStateMap;

class EnemyAIComponent : public ActorComponent, public HealthObserver
{
public:
    bool IsDead() const { return m_bDead; }
    // Faded out (the chameleon): cannot be hit, does not change state.
    void SetPhased(bool phased) { m_bPhased = phased; }
    bool IsPhased() const { return m_bPhased; }

    // CLAW.EXE: Claw can lift ordinary enemies (not bosses) and throw them
    bool CanBeLifted();
    void OnLifted(uint32 carrierId);
    void Throw(const Point& position, const Point& velocityPxPerSec);
    void DropFromCarrier(const Point& position);
    bool IsCarried() const { return m_LiftState == Lift_Carried; }
    bool IsLiftedOrThrown() const { return m_LiftState != Lift_None; }
    // Save states (SaveState/ComponentStates.cpp)
    virtual void VSerializeState(StateArchive& ar) override;

    EnemyAIComponent();
    ~EnemyAIComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VPostPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual TiXmlElement* VGenerateXml() override { return NULL; }

    void RegisterState(std::string stateName, BaseEnemyAIStateComponent* pState) { m_StateMap[stateName] = pState; }

    virtual void VOnHealthBelowZero(DamageType damageType, int sourceActorId) override;
    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;
    virtual bool VCanResistDamage(DamageType damageType, Point impactPoint) override;
    virtual void VOnResistDamage(DamageType damageType, Point impactPoint) override;

    // bool TryFindClosestHostileActorOffset(Point& outOffset);

    bool TryPlaySpeechSound(int chance, const SoundList& speechSounds);

    //void OnStateCanFinish();
    bool EnterBestState(bool canForceEnter);

    const SoundList& GetTakeDamageSounds() const { return m_TakeDamageSounds; }
    const SoundList& GetMeleeAttackSounds() const { return m_MeleeAttackSounds; }
    const SoundList& GetRangedAttackSounds() const { return m_RangedAttackSounds; }
    const SoundList& GetDeathSounds() const { return m_DeathSounds; }
    const SoundList& GetQuotToHostileUnitSounds() const { return m_QuoteToHostileUnitSounds; }

    BaseEnemyAIStateComponent* GetCurrentState();

private:
    void LeaveAllStates(BaseEnemyAIStateComponent* pNextState);
    bool HasState(const std::string &stateName);
    bool HasState(EnemyAIState state);
    void EnterState(const std::string &stateName);
    void EnterState(EnemyAIState state);
    void EnterState(BaseEnemyAIStateComponent* pState);
    void AcquireStateLock(BaseEnemyAIStateComponent* pNewState);
    BaseEnemyAIStateComponent* GetState(EnemyAIState state);

    bool m_bInitialized;
    bool m_bDead;
    bool m_bHasStateLock;
    EnemyStateMap m_StateMap;

    int m_TimeSinceLastSpeechSound;
    int m_MinTimeIntervalForSpeechSound;

    std::string m_DeathAnimation;

    // The defeated enemy flies off as in the original and can be hit again
    // with the sword while it is still on the screen.
    Point m_CorpseVelocity;
    int m_CorpseTime = 0;
    int m_CorpseKickCooldown = 0;
    bool m_bCorpseGone = false;
    bool m_bPhased = false;

    enum LiftState { Lift_None, Lift_Carried, Lift_Thrown };
    void UpdateLift(uint32 msDiff);
    void LandThrown(Actor* pHitEnemy);
    void SetLiftAnimation(std::initializer_list<const char*> names);
    LiftState m_LiftState = Lift_None;
    uint32 m_CarrierId = 0;
    Point m_ThrowVelocity;
    int m_ThrowTime = 0;

    shared_ptr<PositionComponent> m_pPositionComponent;
    shared_ptr<ActorRenderComponent> m_pRenderComponent;

    SoundList m_TakeDamageSounds;
    SoundList m_MeleeAttackSounds;
    SoundList m_RangedAttackSounds;
    SoundList m_DeathSounds;
    SoundList m_QuoteToHostileUnitSounds;
};

#endif