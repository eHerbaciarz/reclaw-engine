#include "MarrowEncounter.h"
#include "../EnemyAIComponent.h"
#include "../../../Actor.h"
#include "../../AnimationComponent.h"
#include "../../PhysicsComponent.h"
#include "../../PositionComponent.h"
#include "../../RenderComponent.h"

#include "../../../../GameApp/BaseGameLogic.h"
#include "../../../../GameApp/BaseGameApp.h"
#include "../../../../Physics/ClawPhysics.h"
#include "../../ControllableComponent.h"
#include "../../RenderComponent.h"
#include "../../FollowableComponent.h"

#include "../../../../Events/EventMgr.h"
#include "../../../../Events/Events.h"
#include "../../ControllerComponents/HealthComponent.h"
#include "../../../ActorTemplates.h"
#include "../../../../Util/Util.h"
#include <cmath>

const char* MarrowAIStateComponent::g_Name = "MarrowAIStateComponent";
const char* MarrowParrotAIStateComponent::g_Name = "MarrowParrotAIStateComponent";
const char* MarrowFloorComponent::g_Name = "MarrowFloorComponent";

//=====================================================================================================================
// MarrowAIStateComponent
//=====================================================================================================================

MarrowAIStateComponent::MarrowAIStateComponent()
    :
    BaseBossAIStateComponennt("MarrowAIStateComponent")
{

}

MarrowAIStateComponent::~MarrowAIStateComponent()
{

}

bool MarrowAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    if (!BaseBossAIStateComponennt::VDelegateInit(pData))
    {
        return false;
    }

    return true;
}

void MarrowAIStateComponent::VPostInit()
{
    BaseBossAIStateComponennt::VPostInit();

    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);
}

void MarrowAIStateComponent::VOnWorldFinishedLoading()
{
    StrongActorPtr pParrotActor = g_pApp->GetGameLogic()->FindActorByName("Level10_MarrowParrot", true);
    assert(pParrotActor != nullptr);

    m_pParrot = pParrotActor->GetRawComponent<MarrowParrotAIStateComponent>(true);

    m_MarrowFloorActorList = g_pApp->GetGameLogic()->FindActorByName("Level10_MarrowFloor");
    assert(m_MarrowFloorActorList.size() == 2);
}

static const char* MW_SOUNDS = "/LEVEL10/SOUNDS/MARROW/";
static const char* PR_SOUNDS = "/LEVEL10/SOUNDS/PARROT/";
static const double kMidX = 40808.0;   // CLAW.EXE 0x510190

static Actor* MwClaw()
{
    if (!g_pApp || !g_pApp->GetGameLogic()) return NULL;
    StrongActorPtr p = g_pApp->GetGameLogic()->GetClawActor();
    return p ? p.get() : NULL;
}

static void MwMove(Actor* pActor, const Point& pos)
{
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetPosition(pActor->GetGUID(), pos);
    pActor->GetPositionComponent()->SetPosition(pos);
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Move_Actor(pActor->GetGUID(), pos)));
}

void MarrowAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    if (m_Phase == Phase_Whistling || m_Phase == Phase_Calling)
    {
        m_pAnimationComponent->SetAnimation("idle1");
    }
}

void MarrowAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
}

bool MarrowAIStateComponent::VCanEnter()
{
    return !m_bBossFightStarted || m_Phase != Phase_None;
}

bool MarrowAIStateComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    // 0x45b850 makes him untouchable (flags 4 | 0x80) until the parrot drops him
    return m_Phase != Phase_None;
}

void MarrowAIStateComponent::StartWaiting()
{
    // LS_WAITFORPARROT: face the middle, whistle, wait up to 5 s for the bird
    m_Phase = Phase_Whistling;
    m_PhaseTime = 0;
    const bool left = m_pOwner->GetPositionComponent()->GetX() <= kMidX;
    m_ParrotSide = left ? 58 : -58;
    m_pOwner->GetRawComponent<ActorRenderComponent>()->SetMirrored(left);
    m_pEnemyAIComponent->EnterBestState(true);
    m_pEnemyAIComponent->SetPhased(true);
    m_pAnimationComponent->SetAnimation("idle1");
    Util::PlaySimpleSound(std::string(MW_SOUNDS) + "WHISTLE.WAV");
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
}

void MarrowAIStateComponent::EndWaiting()
{
    m_Phase = Phase_None;
    m_Threshold = m_pOwner->GetRawComponent<HealthComponent>()->GetCurrentHealth();
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetGravityScale(m_pOwner->GetGUID(), 1.0f);
    m_pEnemyAIComponent->SetPhased(false);
    m_pEnemyAIComponent->EnterBestState(true);
}

Point MarrowAIStateComponent::GetPickupPoint() const
{
    // 0x469890: above him on the side he faces (Marrow + 0x4f8, y - 97)
    const Point me = m_pOwner->GetPositionComponent()->GetPosition();
    return Point(me.x + m_ParrotSide, me.y - 97);
}

double MarrowAIStateComponent::GetCarryTargetX() const
{
    // 0x469b10: to the side away from Claw
    Actor* pClaw = MwClaw();
    return (pClaw && pClaw->GetPositionComponent()->GetX() < kMidX - 0.5) ? 41063.0 : 40553.0;
}

void MarrowAIStateComponent::OnPickedUp()
{
    m_Phase = Phase_Carried;
    m_CarryOffset.Set(-m_ParrotSide, 110);
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetGravityScale(m_pOwner->GetGUID(), 0.0f);
    // the floors slide apart while he flies (0x4dc), with his laugh
    m_bFloorsOpen = true;
    Util::PlaySimpleSound(std::string(MW_SOUNDS) + "00430006.WAV");
}

void MarrowAIStateComponent::OnCarried(const Point& parrotPos)
{
    MwMove(m_pOwner.get(), Point(parrotPos.x + m_CarryOffset.x, parrotPos.y + m_CarryOffset.y));
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
}

void MarrowAIStateComponent::OnDropped()
{
    // 0x469d00: he falls the rest and carries on; the next quarter starts now
    EndWaiting();
}

void MarrowAIStateComponent::VUpdate(uint32 msDiff)
{
    if (!m_bBossFightStarted || m_Phase == Phase_None)
    {
        return;
    }
    m_PhaseTime += msDiff;
    if (m_Phase == Phase_Whistling && m_PhaseTime >= 250)
    {
        // whistle done (0x45b9d0): call the bird
        m_Phase = Phase_Calling;
    }
    if ((m_Phase == Phase_Whistling || m_Phase == Phase_Calling) && m_PhaseTime >= 5000)
    {
        // no bird in 5 s: on he fights (0x45b9d0 timeout)
        EndWaiting();
    }
}

void MarrowAIStateComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
}

void MarrowAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
}

void MarrowAIStateComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    BaseBossAIStateComponennt::VOnHealthChanged(oldHealth, newHealth, damageType, impactPoint, sourceActorId);
    // 0x45b490: a quarter of his health gone -> he calls the parrot
    const int quarter = m_pOwner->GetRawComponent<HealthComponent>()->GetMaxHealth() / 4;
    if (m_bBossFightStarted && m_Phase == Phase_None && newHealth > 0 && newHealth <= m_Threshold - quarter)
    {
        StartWaiting();
    }
}

void MarrowAIStateComponent::OnCanActivateFloor()
{
}

void MarrowAIStateComponent::VOnBossFightStarted()
{
    BaseBossAIStateComponennt::VOnBossFightStarted();

    // To refresh contact list after Claw's death
    g_pApp->GetGameLogic()->VGetGamePhysics()->VActivate(m_pOwner->GetGUID());

    m_Phase = Phase_None;
    m_bFloorsOpen = false;
    m_Threshold = m_pOwner->GetRawComponent<HealthComponent>()->GetCurrentHealth();
    m_pEnemyAIComponent->SetPhased(false);
    m_pOwner->GetRawComponent<EnemyAIComponent>(true)->EnterBestState(true);
    if (m_pParrot)
    {
        m_pParrot->StartFight();
    }
}

void MarrowAIStateComponent::VOnBossFightEnded(bool isBossDead)
{
    BaseBossAIStateComponennt::VOnBossFightEnded(isBossDead);

    m_Phase = Phase_None;
    m_bFloorsOpen = false;
    m_pEnemyAIComponent->SetPhased(false);
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetGravityScale(m_pOwner->GetGUID(), 1.0f);
    if (m_pParrot)
    {
        m_pParrot->Reset();
    }

    if (isBossDead)
    {
        SoundInfo soundInfo(SOUND_GAME_AMULET_RISE);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
            new EventData_Request_Play_Sound(soundInfo)));

        StrongActorPtr pGem = ActorTemplates::CreateActor(
            ActorPrototype_Level10_BossGem,
            m_pOwner->GetPositionComponent()->GetPosition());

        Point gemForce(3.5, -7);
        g_pApp->GetGameLogic()->VGetGamePhysics()->VApplyLinearImpulse(pGem->GetGUID(), gemForce);
    }
    else
    {
        // To refresh contact list after Claw's death
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());
    }
}

void MarrowAIStateComponent::OnQuarterHealthGone()
{
}

//=====================================================================================================================
// MarrowParrotAIStateComponent
//=====================================================================================================================

MarrowParrotAIStateComponent::MarrowParrotAIStateComponent()
    :
    BaseEnemyAIStateComponent("MarrowParrotAIStateComponent")
{
}

MarrowParrotAIStateComponent::~MarrowParrotAIStateComponent()
{
}

bool MarrowParrotAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    DO_AND_CHECK(ParseValueFromXmlElem(&m_IdleAnim, pData->FirstChildElement("IdleAnim")));
    return true;
}

void MarrowParrotAIStateComponent::VPostInit()
{
    BaseEnemyAIStateComponent::VPostInit();
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);
    m_pAnimationComponent->SetAnimation(m_IdleAnim);
    m_pHealth = m_pOwner->GetRawComponent<HealthComponent>(true);
    m_pHealth->AddObserver(this);
    m_Perch = m_pOwner->GetPositionComponent()->GetPosition();
}

void MarrowParrotAIStateComponent::VOnWorldFinishedLoading()
{
    StrongActorPtr pMarrowActor = g_pApp->GetGameLogic()->FindActorByName("Level10_Marrow", true);
    assert(pMarrowActor != nullptr);
    m_pMarrow = pMarrowActor->GetRawComponent<MarrowAIStateComponent>(true);
}

void MarrowParrotAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
}

void MarrowParrotAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
}

bool MarrowParrotAIStateComponent::VCanEnter()
{
    return true;
}

void MarrowParrotAIStateComponent::StartFight()
{
    // the constructor (0x467c50) sends him off flying at once
    SetMode(Mode_FlyAbout);
}

void MarrowParrotAIStateComponent::Reset()
{
    MwMove(m_pOwner.get(), m_Perch);
    SetMode(Mode_Perched);
}

void MarrowParrotAIStateComponent::Face(double dx)
{
    m_pOwner->GetRawComponent<ActorRenderComponent>()->SetMirrored(dx > 0);
}

void MarrowParrotAIStateComponent::SetMode(Mode mode)
{
    m_Mode = mode;
    m_ModeTime = 0;
    switch (mode)
    {
        case Mode_Perched:
            // 0x4688e0: on the perch; the floors close again
            m_pAnimationComponent->SetAnimation(m_IdleAnim);
            if (m_pMarrow) m_pMarrow->SetFloorsOpen(false);
            break;
        case Mode_FlyAbout:
            m_pAnimationComponent->SetAnimation("fly");
            break;
        case Mode_StrikeSetup:
        {
            // 0x468f80: 200 px in from the far end, at Claw's height (up to 30 above)
            Actor* pClaw = MwClaw();
            const double cx = pClaw ? pClaw->GetPositionComponent()->GetX() : m_MidX;
            const double cy = pClaw ? pClaw->GetPositionComponent()->GetY() : m_MinY;
            m_Target.Set(cx < m_MidX + 1 ? m_MaxX - 200 : m_MinX + 200, cy - Util::GetRandomNumber(0, 30));
            m_pAnimationComponent->SetAnimation("fly");
            break;
        }
        case Mode_Strike:
        {
            // 0x4692c0: swoop at Claw, 800 px/s, on across to the end
            Actor* pClaw = MwClaw();
            const Point me = m_pOwner->GetPositionComponent()->GetPosition();
            const double cx = pClaw ? pClaw->GetPositionComponent()->GetX() : m_MidX;
            const double dx = cx - me.x, dy = m_Target.y - me.y;
            const double len = std::max(1.0, sqrt(dx * dx + dy * dy));
            m_Velocity.Set(dx / len * 800.0, dy / len * 800.0);
            if (fabs(m_Velocity.x) < 100) m_Velocity.x = (dx >= 0 ? 1 : -1) * 100;
            Face(m_Velocity.x);
            m_pAnimationComponent->SetAnimation("strike1");
            Util::PlaySimpleSound(std::string(PR_SOUNDS) + "PARROTSWOOP.WAV");
            m_StrikeTick = 0;
            break;
        }
        case Mode_Hit:
            m_pAnimationComponent->SetAnimation("hit");
            Util::PlaySimpleSound(std::string(PR_SOUNDS) + "PARROTHIT.WAV");
            break;
        case Mode_FlyToPerch:
        case Mode_FlyToMarrow:
        case Mode_Carry:
            m_pAnimationComponent->SetAnimation("fly");
            break;
    }
}

bool MarrowParrotAIStateComponent::MoveTo(const Point& target, double speed, uint32 msDiff)
{
    const Point me = m_pOwner->GetPositionComponent()->GetPosition();
    const double dx = target.x - me.x, dy = target.y - me.y;
    const double dist = sqrt(dx * dx + dy * dy);
    const double step = speed * msDiff / 1000.0;
    if (fabs(dx) > 1) Face(dx);
    if (dist <= step)
    {
        MwMove(m_pOwner.get(), target);
        return true;
    }
    MwMove(m_pOwner.get(), Point(me.x + dx / dist * step, me.y + dy / dist * step));
    return false;
}

void MarrowParrotAIStateComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    if (newHealth >= oldHealth)
    {
        return;
    }
    m_pHealth->SetMaxHealth();
    // LS_GENERICHIT, then back to his perch (0x469fe0 -> 0x469630 LS_FLYTOIDLE)
    if (m_Mode == Mode_FlyAbout || m_Mode == Mode_StrikeSetup || m_Mode == Mode_Strike)
    {
        SetMode(Mode_Hit);
    }
}

void MarrowParrotAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (m_Mode == Mode_Hit)
    {
        SetMode(Mode_FlyToPerch);
    }
}

void MarrowParrotAIStateComponent::VUpdate(uint32 msDiff)
{
    if (!m_pMarrow)
    {
        return;
    }
    m_ModeTime += msDiff;
    if (m_SquawkCooldown > 0) m_SquawkCooldown -= msDiff;
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
    const Point me = m_pOwner->GetPositionComponent()->GetPosition();
    Actor* pClaw = MwClaw();
    const double clawX = pClaw ? pClaw->GetPositionComponent()->GetX() : m_MidX;

    switch (m_Mode)
    {
        case Mode_Perched:
            // 0x4689f0: when Marrow whistles, off to him
            if (m_pMarrow->IsCallingParrot())
            {
                SetMode(Mode_FlyToMarrow);
            }
            break;
        case Mode_FlyAbout:
        {
            // 0x468cb0: back and forth under the ceiling at 300 px/s; at an end, 75%
            // a swoop if Claw is on the far half
            if (m_SquawkCooldown <= 0 && Util::GetRandomNumber(0, 99) < 1)
            {
                Util::PlaySimpleSound(std::string(PR_SOUNDS) + (Util::GetRandomNumber(0, 1) ? "POLLY2.WAV" : "POLLY3.WAV"));
                m_SquawkCooldown = 15000;
            }
            if (me.y > m_MinY + 1)
            {
                MoveTo(Point(me.x, m_MinY), 300, msDiff);
                break;
            }
            const double endX = m_Dir > 0 ? m_MaxX : m_MinX;
            if (MoveTo(Point(endX, m_MinY), 300, msDiff))
            {
                const bool clawFar = m_Dir > 0 ? clawX < m_MidX + 1 : clawX > m_MidX - 1;
                m_Dir = -m_Dir;
                if (clawFar && Util::GetRandomNumber(0, 99) < 75)
                {
                    SetMode(Mode_StrikeSetup);
                }
            }
            break;
        }
        case Mode_StrikeSetup:
            if (MoveTo(m_Target, 800, msDiff) || m_ModeTime > 2000)
            {
                SetMode(Mode_Strike);
            }
            break;
        case Mode_Strike:
        {
            Point pos(me.x + m_Velocity.x * msDiff / 1000.0, me.y + m_Velocity.y * msDiff / 1000.0);
            if (fabs(pos.y - m_Target.y) < fabs(m_Velocity.y * msDiff / 1000.0) + 1)
            {
                m_Velocity.y = 0;
                pos.y = m_Target.y;
            }
            pos.x = std::max(m_MinX, std::min(m_MaxX, pos.x));
            MwMove(m_pOwner.get(), pos);
            if ((m_StrikeTick -= msDiff) <= 0)
            {
                // his beak: (-26,-5)..(-5,22), 10 points
                m_StrikeTick = 100;
                const double side = m_Velocity.x > 0 ? 1.0 : -1.0;
                ActorTemplates::CreateAreaDamage(Point(pos.x + side * 16, pos.y + 8), Point(21, 27), 10,
                    CollisionFlag_EnemyAIAttack, "Rectangle", DamageType_MeleeAttack,
                    side > 0 ? Direction_Right : Direction_Left, m_pOwner->GetGUID());
            }
            if (pos.x <= m_MinX || pos.x >= m_MaxX || m_ModeTime > 3000)
            {
                m_Dir = pos.x <= m_MinX ? 1.0 : -1.0;
                SetMode(Mode_FlyAbout);
            }
            break;
        }
        case Mode_Hit:
            break;
        case Mode_FlyToPerch:
            if (MoveTo(m_Perch, 300, msDiff))
            {
                SetMode(Mode_Perched);
            }
            break;
        case Mode_FlyToMarrow:
            if (!m_pMarrow->IsCallingParrot())
            {
                SetMode(Mode_FlyToPerch);
                break;
            }
            if (MoveTo(m_pMarrow->GetPickupPoint(), 300, msDiff))
            {
                m_Target.Set(m_pMarrow->GetCarryTargetX() + m_pMarrow->GetParrotSide(), me.y);
                m_pMarrow->OnPickedUp();
                SetMode(Mode_Carry);
            }
            break;
        case Mode_Carry:
        {
            // 0x469d00: across at 200 px/s, Marrow under him
            const bool done = MoveTo(m_Target, 200, msDiff);
            m_pMarrow->OnCarried(m_pOwner->GetPositionComponent()->GetPosition());
            if (done || m_ModeTime > 6000)
            {
                m_pMarrow->OnDropped();
                SetMode(Mode_FlyAbout);
            }
            break;
        }
    }
}

//=====================================================================================================================
// MarrowFloorComponent: CLAW.EXE 0x45c900 "MarrowFloor" (trap elevators)
//=====================================================================================================================

MarrowFloorComponent::MarrowFloorComponent()
    :
    m_ActivateDirection(Direction_Left),
    m_Speed(300.0),
    m_ActivatedBorderX(0),
    m_DeactivatedBorderX(0),
    m_bActive(false)
{
}

MarrowFloorComponent::~MarrowFloorComponent()
{
}

bool MarrowFloorComponent::VInit(TiXmlElement* pData)
{
    return true;
}

void MarrowFloorComponent::VPostInit()
{
}

void MarrowFloorComponent::VOnWorldFinishedLoading()
{
    const Point pos = m_pOwner->GetPositionComponent()->GetPosition();
    m_HomeX = pos.x;
    m_Y = pos.y;
    // the WWD ranges: the left one slides 180 px left (40534), the right one 160 right (41071)
    m_OpenX = pos.x < kMidX ? pos.x - 180 : pos.x + 160;
    StrongActorPtr pMarrowActor = g_pApp->GetGameLogic()->FindActorByName("Level10_Marrow", true);
    if (pMarrowActor)
    {
        m_pMarrow = pMarrowActor->GetRawComponent<MarrowAIStateComponent>();
    }
}

void MarrowFloorComponent::VUpdate(uint32 msDiff)
{
    if (!m_pMarrow)
    {
        return;
    }
    // ES_OPENING / ES_CLOSING at 300 px/s (0x45cd00 / 0x45cd80)
    const bool wantOpen = m_pMarrow->AreFloorsOpen();
    const double target = wantOpen ? m_OpenX : m_HomeX;
    const double x = m_pOwner->GetPositionComponent()->GetX();
    if (fabs(x - target) < 0.5)
    {
        m_bOpen = wantOpen;
        m_bActive = false;
        return;
    }
    if (!m_bActive)
    {
        // starts sliding (ES_OPENING / ES_CLOSING play PEGSLIDE)
        Util::PlaySimpleSound("/LEVEL10/SOUNDS/PEGSLIDE.WAV");
        m_bActive = true;
    }
    const double step = m_Speed * msDiff / 1000.0;
    const double nx = fabs(target - x) <= step ? target : x + (target > x ? step : -step);
    MwMove(m_pOwner.get(), Point(nx, m_Y));
}

void MarrowFloorComponent::Activate()
{
}

void MarrowFloorComponent::Deactivate()
{
}
