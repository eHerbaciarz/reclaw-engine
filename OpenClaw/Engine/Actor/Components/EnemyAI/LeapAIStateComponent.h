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

    int m_CheckMs;
    int m_CooldownLeftMs;
    bool m_bWantLeap;
    double m_PlannedX;

    // flight
    Point m_Start;
    double m_Vx, m_Vy, m_Duration, m_Time, m_TargetX;
};

#endif
