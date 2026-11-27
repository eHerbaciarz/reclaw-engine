#pragma once

#include "../EnemyAIStateComponent.h"

// Level 14 final boss: Omar (CLAW.EXE 0x4665a0-0x4670d0).
//
// Ring phase, by the lava: Omar stands behind a ring of 9 fire or ice balls
// (33 degrees apart, 250 degrees/s). Only the opposite magic sword hurts him
// (ice sword against the fire ring, fire sword against the ice ring); every
// such hit takes a quarter of his bar, he flies (2 s) to his other spot and
// the ring changes element and turns the other way. The wrong magic sword
// gives him a quarter back.
// When the ring phase bar is empty he flies up to the ledge the lift leads to,
// bar full again, no ring: there any hit takes a quarter, and he flies
// between two spots up there. Energy balls (300 px/s, straight ahead) and a
// strike when Claw comes close in both phases. When he falls the gem appears
// and picking it up ends the game.
class OmarAIStateComponent : public BaseBossAIStateComponennt, public AnimationObserver
{
public:
    OmarAIStateComponent();
    virtual ~OmarAIStateComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;

    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_BrainOmar; }

    virtual void VUpdate(uint32 msDiff) override;
    virtual bool VCanEnter() override { return m_bBossFightStarted == false; }

    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;
    virtual bool VCanResistDamage(DamageType damageType, Point impactPoint) override;

protected:
    virtual void VOnBossFightStarted() override;
    virtual void VOnBossFightEnded(bool isBossDead) override;

private:
    enum ShieldType { Shield_Fire, Shield_Ice };

    void CreateShield();
    void DestroyShield();
    void UpdateShield(uint32 msDiff);
    void WarpTo(int positionIdx);
    void StartMove(const Point& target);
    void UpdateMove(uint32 msDiff);
    bool IsHitByRightSword(DamageType damageType) const;

    // XML
    int m_HitDamage;
    int m_InvulnerableTime;
    int m_WarpDelay;
    int m_ShieldBallCount;
    double m_ShieldRadius;
    double m_ShieldSpeed; // degrees per second
    int m_ShieldDamage;
    std::vector<Point> m_Positions;        // ring phase, by the lava (start spot first)
    std::vector<Point> m_Phase2Positions;  // second phase, up on the ledge
    double m_ShieldBallSpacing = 0.0;      // degrees; 0 = spread over the circle

    // State
    ShieldType m_Shield;
    std::vector<std::weak_ptr<Actor>> m_ShieldBalls;
    double m_ShieldAngle;
    int m_ShieldHitCooldown;
    int m_InvulnerableLeft;
    int m_WarpLeft;
    int m_PositionIdx;
    bool m_bPendingHit;
    bool m_bPendingHeal;
    int m_Phase;
    int m_MoveTime;
    int m_MoveLeft;
    double m_ShieldDir;
    Point m_MoveFrom;
    Point m_MoveTo;
    bool m_bAllowDamage;
    bool m_bDead;
};

// Puts the magic sword back on its spot a while after Claw takes it, so he can
// fetch a new one for every ring.
class OmarSwordSpawnerComponent : public ActorComponent
{
public:
    OmarSwordSpawnerComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VUpdate(uint32 msDiff) override;

    static void SetBossDefeated(bool defeated) { s_bBossDefeated = defeated; }

private:
    static bool s_bBossDefeated;

    int m_PickupType;
    int m_RespawnTime;
    int m_TimeLeft;
    std::weak_ptr<Actor> m_pPickup;
};
