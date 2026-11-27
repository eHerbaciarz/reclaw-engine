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

#endif
