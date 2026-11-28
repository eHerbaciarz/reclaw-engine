#ifndef __MARROW_ENCOUNTER_H__
#define __MARROW_ENCOUNTER_H__

#include "../EnemyAIStateComponent.h"
#include "../../TriggerComponents/TriggerComponent.h"

enum MarrowActionType
{
    MarrowActionType_None,
};

typedef BossAction<MarrowActionType> MarrowAction;

typedef std::vector<MarrowAction> MarrowActionList;

class MarrowParrotAIStateComponent;
class FollowableComponent;
class MarrowAIStateComponent : public  BaseBossAIStateComponennt, public AnimationObserver
{
public:
    // Save states (SaveState/ComponentStates.cpp)
    virtual void VSerializeState(StateArchive& ar) override;

    MarrowAIStateComponent();
    virtual ~MarrowAIStateComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;

    virtual void VOnWorldFinishedLoading() override;

    // EnemyAIStateComponent API
    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_BrainMarrow; }

    virtual void VUpdate(uint32 msDiff) override;

    // Can enemy enter this state ?
    virtual bool VCanEnter() override;

    //virtual int VGetPriority() override;

    // AnimationObserver API
    virtual void VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame) override;
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    // HealthObserver API
    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;

    // Marrow will be the bossfight director
    void OnCanActivateFloor();
    // He resists all while waiting for or carried by his parrot
    virtual bool VCanResistDamage(DamageType damageType, Point impactPoint) override;

    // CLAW.EXE 0x45b850 LS_WAITFORPARROT and the parrot's LS_CARRYMARROW
    bool IsCallingParrot() const { return m_Phase == Phase_Calling; }
    bool AreFloorsOpen() const { return m_bFloorsOpen; }
    Point GetPickupPoint() const;
    double GetParrotSide() const { return m_ParrotSide; }
    double GetCarryTargetX() const;
    void OnPickedUp();
    void OnCarried(const Point& parrotPos);
    void OnDropped();
    void SetFloorsOpen(bool open) { m_bFloorsOpen = open; }

protected:
    virtual void VOnBossFightStarted() override;
    virtual void VOnBossFightEnded(bool isBossDead) override;

private:

    int m_ReqDamageToTransition;

    enum Phase { Phase_None, Phase_Whistling, Phase_Calling, Phase_Carried };
    void StartWaiting();
    void EndWaiting();
    Phase m_Phase = Phase_None;
    int m_PhaseTime = 0;
    int m_Threshold = 0;     // 0x4e8: his health when the last quarter began
    double m_ParrotSide = 58; // 0x4f8: the parrot takes him from this side
    Point m_CarryOffset;
    bool m_bFloorsOpen = false;

    

    //------------------------------------
    void OnQuarterHealthGone();

    AnimationComponent* m_pAnimationComponent;
    FollowableComponent* m_pQuestionMark;
    MarrowParrotAIStateComponent* m_pParrot;
    ActorList m_MarrowFloorActorList;

    MarrowActionList m_GabrielActionList;
};

class MarrowFloorComponent;
class MarrowParrotAIStateComponent : public BaseEnemyAIStateComponent, public AnimationObserver, public HealthObserver
{
public:
    MarrowParrotAIStateComponent();
    virtual ~MarrowParrotAIStateComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VDelegateInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual void VOnWorldFinishedLoading() override;

    // EnemyAIStateComponent API
    virtual void VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState) override;
    virtual void VOnStateLeave(BaseEnemyAIStateComponent* pNextState) override;
    virtual EnemyAIState VGetStateType() const override { return EnemyAIState_BrainMarrowParrot; }
    virtual bool VCanEnter() override;

    // AnimationObserver API
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

    // HealthObserver API: a blow sends him back to his perch
    virtual void VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId) override;

    void StartFight();
    void Reset();

private:
    // CLAW.EXE 0x467ae0 "Parrot": LS_IDLE, LS_FLYABOUT, LS_STRIKESETUP, LS_STRIKE,
    // LS_GENERICHIT, LS_FLYTOIDLE, LS_FLYTOMARROW, LS_CARRYMARROW
    enum Mode { Mode_Perched, Mode_FlyAbout, Mode_StrikeSetup, Mode_Strike, Mode_Hit, Mode_FlyToPerch, Mode_FlyToMarrow, Mode_Carry };
    void SetMode(Mode mode);
    bool MoveTo(const Point& target, double speed, uint32 msDiff);
    void Face(double dx);

    std::string m_IdleAnim;
    Mode m_Mode = Mode_Perched;
    Point m_Perch;
    double m_MinX = 40488, m_MaxX = 41128, m_MinY = 7029;
    double m_MidX = 40808;
    double m_Dir = 1.0;
    Point m_Target;
    Point m_Velocity;
    int m_SquawkCooldown = 0;
    int m_StrikeTick = 0;
    int m_ModeTime = 0;

    MarrowAIStateComponent* m_pMarrow = nullptr;
    AnimationComponent* m_pAnimationComponent = nullptr;
    HealthComponent* m_pHealth = nullptr;
};

class MarrowFloorComponent : public ActorComponent
{
public:
    // Save states (SaveState/ComponentStates.cpp)
    virtual void VSerializeState(StateArchive& ar) override;

    MarrowFloorComponent();
    virtual ~MarrowFloorComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual void VOnWorldFinishedLoading() override;

    void Activate();
    void Deactivate();

private:
    // XML Data
    Direction m_ActivateDirection;
    double m_Speed;
    int m_ActivatedBorderX;
    int m_DeactivatedBorderX;

    // Internal
    bool m_bActive;
    bool m_bOpen = false;
    double m_HomeX = 0, m_OpenX = 0, m_Y = 0;
    class MarrowAIStateComponent* m_pMarrow = nullptr;
};

#endif