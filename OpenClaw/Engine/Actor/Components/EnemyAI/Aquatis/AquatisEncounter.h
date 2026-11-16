#pragma once

#include "../EnemyAIStateComponent.h"
#include "../../TriggerComponents/TriggerComponent.h"

class GabrielCannonComponent;
class FollowableComponent;
class AquatisAIStateComponent : public BaseBossAIStateComponennt, public AnimationObserver
{
public:
    AquatisAIStateComponent();
    virtual ~AquatisAIStateComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;

    virtual void VOnWorldFinishedLoading() override;

    // EnemyAIStateComponent API

    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_BrainAquatis; }

    virtual void VUpdate(uint32 msDiff) override;

    // Can enemy enter this state ?
    virtual bool VCanEnter() override { return m_bBossFightStarted == false; }

    //virtual int VGetPriority() override;

    // AnimationObserver API
    virtual void VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame) override;
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    // HealthObserver API
    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;
    // Only a falling icicle hurts Aquatis - every other kind of damage is resisted
    virtual bool VCanResistDamage(DamageType damageType, Point impactPoint) override { return !m_bAllowDamage; }

    static AquatisAIStateComponent* Get() { return s_pInstance; }
    static bool IsDefeated() { return s_bDefeated; }
    bool IsFightActive() const { return m_bBossFightStarted; }
    Point GetPosition() const;
    void HitByStalactite(int damage);

protected:
    virtual void VOnBossFightStarted() override;
    virtual void VOnBossFightEnded(bool isBossDead) override;

private:
    static AquatisAIStateComponent* s_pInstance;
    static bool s_bDefeated;

    bool m_bAllowDamage;
    int m_IdleTime;
};

//=====================================================================================================================
// Tentacles that grow out of the floor in front of Aquatis. A single hit cuts one off; it grows back after a while.
//=====================================================================================================================
class AquatisTentacleComponent : public ActorComponent, public AnimationObserver, public HealthObserver
{
public:
    AquatisTentacleComponent();
    virtual ~AquatisTentacleComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual void VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame) override;
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;
    virtual bool VCanResistDamage(DamageType damageType, Point impactPoint) override;

private:
    enum State { State_Idle, State_Striking, State_Dying, State_Dead, State_Respawning };

    void BossFightEndedDelegate(IEventDataPtr pEvent);
    void SetState(State state);

    int m_RespawnTime;
    Point m_AttackRange;
    Point m_AttackAreaSize;
    int m_AttackOffsetX;
    int m_Damage;
    int m_AttackInterval;

    State m_State;
    int m_StateTime;
    int m_Cooldown;
    bool m_bStayDead;
    bool m_bStrikeDone;

    AnimationComponent* m_pAnimationComponent;
    ActorRenderComponent* m_pRenderComponent;
    HealthComponent* m_pHealthComponent;
};

//=====================================================================================================================
// The glowing crack behind Aquatis. Dynamite exploding at it makes the next icicle fall from the ceiling.
//=====================================================================================================================
class AquatisCrackComponent : public ActorComponent, public HealthObserver
{
public:
    AquatisCrackComponent();
    virtual ~AquatisCrackComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override { return true; }
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual bool VCanResistDamage(DamageType damageType, Point impactPoint) override;
    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;

private:
    int m_Cooldown;
    HealthComponent* m_pHealthComponent;
};

//=====================================================================================================================
// One of the five icicles hanging above Aquatis
//=====================================================================================================================
class AquatisStalactiteComponent : public ActorComponent, public AnimationObserver
{
public:
    AquatisStalactiteComponent();
    virtual ~AquatisStalactiteComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    // Drops the hanging icicle with the lowest order. Returns false when none is left.
    static bool DropNext();

    int GetOrder() const { return m_Order; }
    bool IsHanging() const { return m_State == State_Hanging; }
    void Drop();
    void Reset();

private:
    enum State { State_Hanging, State_Falling, State_Crumbling, State_Gone };

    void BossFightEndedDelegate(IEventDataPtr pEvent);

    int m_Order;
    int m_TargetY;
    int m_Damage;

    State m_State;
    double m_Speed;
    Point m_HomePosition;

    AnimationComponent* m_pAnimationComponent;
    ActorRenderComponent* m_pRenderComponent;
};

//=====================================================================================================================
// Keeps a dynamite pickup at the spot where the level places it, so Claw never runs out during the fight
//=====================================================================================================================
class AquatisDynamiteComponent : public ActorComponent
{
public:
    AquatisDynamiteComponent();
    virtual ~AquatisDynamiteComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VUpdate(uint32 msDiff) override;

private:
    int m_RespawnTime;
    int m_TimeLeft;
    std::weak_ptr<Actor> m_pPickup;
};
