#ifndef __LEAP_AI_STATE_COMPONENT_H__
#define __LEAP_AI_STATE_COMPONENT_H__

#include "EnemyAIStateComponent.h"

// A boss's leap as CLAW.EXE does it: a ballistic jump (lowest launch speed
// for the distance, gravity 2000 px/s^2) to a spot found relative to Claw
// on the same floor height.
//   Mode "Over" (Raux, 0x481ba0): when Claw is behind him, over Claw to
//                land just past him.
//   Mode "Away" (Katherine, 0x456550): a flip that puts 200-400 px between
//                her and Claw, on her side.
// <Chance> percent per <CheckEveryMs> while the trigger holds, then at
// least <CooldownMs> before the next one.
class LeapAIStateComponent : public BaseEnemyAIStateComponent
{
public:
    LeapAIStateComponent();
    virtual ~LeapAIStateComponent() { }

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VUpdate(uint32 msDiff) override;
    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_Leap; }
    virtual bool VCanEnter() override;

private:
    bool FindTarget(double& outX);

    std::string m_Mode;
    std::string m_Animation;
    std::string m_Sound;
    int m_MinDistance;
    int m_MaxDistance;
    int m_Chance;
    int m_CheckEveryMs;
    int m_CooldownMs;
    int m_ClawMinRange = 0;   // Claw at least this far (Raux: 150 - his "zone 2")
    int m_ClawMaxRange = 300;
    // Katherine (0x456060): rolled when Claw starts a blow facing her
    bool m_OnClawAttack = false;
    bool m_ClawWasAttacking = false;

    int m_CheckMs;
    int m_CooldownLeftMs;
    bool m_bWantLeap;
    double m_PlannedX;

    // flight
    Point m_Start;
    double m_Vx, m_Vy, m_Duration, m_Time, m_TargetX;
};

// CLAW.EXE's generic enemy steps that Raux uses (0x481a60 decides):
//   Claw within <NearRange> px: <BackChance>% "MOVEBACK" (0x4407a0) - walks
//     75-125 px away from Claw, still facing him;
//   Claw within <MidRange> px: <AheadChance>% "MOVEAHEAD" (0x440a70) -
//     40-80 px toward him; both at 150 px/s, only onto floor.
class StepAIStateComponent : public BaseEnemyAIStateComponent
{
public:
    StepAIStateComponent();
    virtual ~StepAIStateComponent() { }

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VUpdate(uint32 msDiff) override;
    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_Step; }
    virtual bool VCanEnter() override { return m_IsActive || m_bWant; }

private:
    bool Plan();

    std::string m_Animation;
    int m_NearRange = 100, m_BackChance = 50, m_BackMin = 75, m_BackMax = 125;
    int m_MidRange = 150, m_AheadChance = 17, m_AheadMin = 40, m_AheadMax = 80;
    double m_Speed = 150.0;
    int m_CheckEveryMs = 500;

    int m_CheckMs = 0;
    bool m_bWant = false;
    double m_TargetX = 0.0, m_Dir = 0.0;
    bool m_FaceRight = false;
    int m_TimeLeft = 0;
};

// Wolvington's own fight, as CLAW.EXE decides it (0x4a44d0, "LS_HOME"),
// rolled each time he is free (patrolling) and Claw is level with him:
//   Claw 56-200 px away: half the time a blow - standing uppercut (STRIKE2,
//     0x4a4a70) or, when Claw ducks, the low sword (STRIKE1, 0x4a4ba0);
//     otherwise MOVEBACK (0x4407a0) 75-125 px at 150 px/s;
//   Claw 200-500 px away: 10% a magic series (0x4a4600): 1-4 shots
//     (rand 0..3 up to 3) alternating low (STRIKE4, +10 px) and high
//     (STRIKE3, -20 px) from 65 px in front, 300 px/s; 20% run and
//     uppercut (0x4a4c80: FASTADVANCE at 4x his 120 px/s, animation 4x as
//     fast, uppercut once Claw is within 200); the rest he advances
//     (his patrol);
//   500 ms after a hit he answers with the run and uppercut (0x4a4380).
class WolvingtonMovesAIStateComponent : public BaseEnemyAIStateComponent
{
public:
    WolvingtonMovesAIStateComponent();
    virtual ~WolvingtonMovesAIStateComponent() { }

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;
    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_WolvingtonMoves; }
    virtual bool VCanEnter() override { return m_IsActive || m_Planned != Move_None; }

private:
    enum Move { Move_None, Move_Uppercut, Move_Sword, Move_Back, Move_Magic, Move_Run };

    bool ClawLevel(double& dx, double& absDx, bool& ducking);
    Move Decide();
    void Start(Move move);
    void StartBlow(const std::string& anim);
    void Finish();
    void Face(double dx);

    int m_NearRange = 200, m_CloseRange = 56, m_FarRange = 500, m_LevelRange = 60;
    int m_BlowChance = 500, m_MagicChance = 100, m_RunChance = 200; // of 1000
    double m_RunSpeed = 480.0, m_BackSpeed = 150.0;
    int m_UppercutDamage = 5, m_SwordDamage = 5;
    int m_CheckEveryMs = 400, m_CounterDelayMs = 500;
    ActorPrototype m_MagicProto = ActorPrototype_None;

    Move m_Planned = Move_None, m_Move = Move_None;
    int m_CheckMs = 0;
    int m_LastHealth = -1;
    int m_CounterMs = -1;
    int m_ShotsLeft = 0, m_ShotIdx = 0;
    std::string m_Anim;
    int m_LastFrame = 0;
    bool m_HitDone = false;
    int m_TimeLeft = 0;
    double m_Dir = 0.0, m_TargetX = 0.0;
};

#endif
