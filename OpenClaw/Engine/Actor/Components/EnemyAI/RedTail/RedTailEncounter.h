#pragma once

#include "../EnemyAIStateComponent.h"
#include "../../TriggerComponents/TriggerComponent.h"

class RedTailAIStateComponent : public BaseBossAIStateComponennt, public AnimationObserver
{
public:
    RedTailAIStateComponent();
    virtual ~RedTailAIStateComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;

    virtual void VOnWorldFinishedLoading() override;

    // EnemyAIStateComponent API

    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_BrainRedTail; }

    virtual void VUpdate(uint32 msDiff) override;

    // His whole fight runs in this state (CLAW.EXE 0x481ef0, LS_* states)
    virtual bool VCanEnter() override { return !m_pEnemyAIComponent->IsDead(); }

    //virtual int VGetPriority() override;

    // AnimationObserver API
    virtual void VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame) override;
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    // HealthObserver API
    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;

protected:
    virtual void VOnBossFightStarted() override;
    virtual void VOnBossFightEnded(bool isBossDead) override;

private:
    enum Act { Act_Home, Act_Advance, Act_Jump, Act_Knife, Act_Gun, Act_Slide, Act_Kick, Act_Wind };
    // what to do after a run or a jump (CLAW.EXE 0x4840a0)
    enum Next { Next_Home = 0, Next_Wind = 1, Next_Gun = 2, Next_Knife = 3, Next_Slide = 4, Next_Kick = 5 };

    bool ClawLow() const;
    bool SelfLow() const;
    bool SameLevel() const { return ClawLow() == SelfLow(); }
    int Zone() const;
    double ClawDx() const;
    void FaceClaw();
    void SetAnim(const std::string& anim);
    void Taunt(int chance);

    void GoHome();
    void Decide();
    void Pick(int depth = 0);
    void DoNext(Next next, int depth = 0);
    void Advance(Next next);
    void JumpToOtherLevel(Next next);
    void Jump(bool up, Next next);
    void Knife(int depth);
    void Gun(int depth);
    void Slide(int depth);
    void Kick(int depth);
    void Wind(int depth);
    void ThrowKnives();
    void UpdateWindAndSpikes(uint32 msDiff);
    void UpdateWindDebris(uint32 msDiff);

    // CLAW.EXE's arena (0x529854..0x529860): the beach below y 1920
    int m_LevelY = 1920;
    Point m_UpTarget = Point(37500, 1770);
    int m_DownMinX = 37350, m_DownMaxX = 37650, m_DownY = 2150;
    int m_WindLeft = 37100, m_WindRight = 37900, m_WindBottom = 2150;
    double m_WindPush = 300.0;
    int m_WindTime = 5000;
    Point m_SpikesCenter = Point(37090, 2017), m_SpikesSize = Point(64, 208);
    int m_SpikesDamage = 20;
    double m_RunSpeed = 250.0, m_SlideSpeed = 280.0;
    int m_MeleeDamage = 10;

    Act m_Act = Act_Home;
    Next m_Next = Next_Home;
    int m_ActTime = 0;
    int m_DecideCooldown = 0;   // 0x4e0/0x4e4 and the wind's 2-2.5 s (0x138)
    int m_AttackCooldown = 0;   // 0x4d8/0x4dc: 1-1.5 s between attacks
    bool m_bStruck = false;
    int m_LastFrame = 0;
    double m_SlideTargetX = 0, m_Dir = 0;
    // jump on a parabola, then let him fall
    Point m_JumpStart, m_JumpVel;
    double m_JumpTime = 0, m_JumpDuration = 0;
    bool m_bFlying = false;
    bool m_bJumpUp = false;
    double m_FeetOffset = 60.0;   // from his position down to the ground he stands on
    int m_HitCount = 0;
    bool m_bEscape = false;
    int m_WindLeftMs = 0;
    // WindDebris (CLAW.EXE 0x487930 / 0x4a3090): grass, leaves and sand blown past
    struct WindDebris { StrongActorPtr pActor; Point pos; Point speed; int ageMs; };
    std::vector<WindDebris> m_WindDebris;
    int m_DebrisSpawnMs = 0;
    int m_SpikesCooldown = 0;
};
