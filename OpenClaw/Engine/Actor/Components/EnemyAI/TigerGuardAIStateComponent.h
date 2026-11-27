#ifndef __TIGER_GUARD_AI_STATE_COMPONENT_H__
#define __TIGER_GUARD_AI_STATE_COMPONENT_H__

#include "EnemyAIStateComponent.h"

// The tiger guards of level 14 as CLAW.EXE plays them (CTigerGuard,
// 0x497e30-0x499430): instead of one plain strike they pick, every
// 1.25-2 s, a lunge (strike3: a 0.5 s crouch, then a 300 px/s leap with a
// 20-damage blow), a combo of 2-4 knife slashes (strike1, 15 each) or a
// front flip towards Claw followed by a slash; now and then a back flip.
// They block 40% of Claw's blows that come from the front (at most every
// 0.5 s), do not flinch in the middle of a lunge or flip and cannot be hit
// while flipping. Right after being hit they strike back at once.
class TigerGuardAIStateComponent : public BaseEnemyAIStateComponent, public AnimationObserver
{
public:
    TigerGuardAIStateComponent();
    virtual ~TigerGuardAIStateComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;

    virtual void VUpdate(uint32 msDiff) override;
    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_TigerGuard; }
    virtual bool VCanEnter() override;

    virtual void VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame) override;
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    // Asked by EnemyAIComponent before a hit lands.
    bool TryBlock(DamageType damageType);
    bool IsUnflinching() const;
    bool IsFlipping() const { return m_Action == Action_FrontFlip || m_Action == Action_BackFlip; }

private:
    enum Action { Action_None, Action_Knife, Action_Lunge, Action_FrontFlip, Action_BackFlip, Action_Block };

    Actor* GetClaw();
    bool ClawInReach(double maxDx);
    void FaceClaw();
    void Decide();
    void StartKnife(int count);
    void StartLunge();
    void StartFlip(bool front);
    void Finish();
    void Stop();
    void Strike(double cx, double cy, double w, double h, int damage);

    Action m_Action;
    int m_KnifeLeft;
    int m_ActionMs;
    int m_CooldownMs;
    int m_BlockCooldownMs;
    bool m_bWasHit;
    double m_StartX;
    double m_TargetX;
    double m_Dir;           // facing: -1 left, +1 right
    double m_MoveDir;       // moving (lunge, flips)
    bool m_bKnifeAfterFlip;
    bool m_bWasTakingDamage;

    std::shared_ptr<IGamePhysics> m_pPhysics;
};

#endif
