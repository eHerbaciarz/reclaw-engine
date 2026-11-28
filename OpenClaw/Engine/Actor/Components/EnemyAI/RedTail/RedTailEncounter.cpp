#include "../EnemyAIComponent.h"
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
#include "RedTailEncounter.h"
#include "../../ControllerComponents/HealthComponent.h"
#include "../../../ActorTemplates.h"
#include "../../../../Util/Util.h"
#include <cmath>

const char* RedTailAIStateComponent::g_Name = "RedTailAIStateComponent";

//=====================================================================================================================
// GabrielAIStateComponent
//=====================================================================================================================

RedTailAIStateComponent::RedTailAIStateComponent()
    :
    BaseBossAIStateComponennt("RedTailAIStateComponent")
{

}

RedTailAIStateComponent::~RedTailAIStateComponent()
{

}

bool RedTailAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    if (!BaseBossAIStateComponennt::VDelegateInit(pData))
    {
        return false;
    }

    return true;
}

void RedTailAIStateComponent::VPostInit()
{
    BaseBossAIStateComponennt::VPostInit();

    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);

    //m_pQuestionMark = m_pOwner->GetRawComponent<FollowableComponent>(true);
}

void RedTailAIStateComponent::VOnWorldFinishedLoading()
{
}

static const char* RT_SOUND_DIR = "/LEVEL13/SOUNDS/REDTAIL/";
static const double kRtGravity = 2000.0;

static Actor* RtClaw()
{
    if (!g_pApp || !g_pApp->GetGameLogic()) return NULL;
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    return pClaw ? pClaw.get() : NULL;
}

bool RedTailAIStateComponent::ClawLow() const
{
    Actor* pClaw = RtClaw();
    return pClaw && pClaw->GetPositionComponent()->GetY() > m_LevelY;
}

bool RedTailAIStateComponent::SelfLow() const
{
    return m_pPositionComponent->GetY() > m_LevelY;
}

double RedTailAIStateComponent::ClawDx() const
{
    Actor* pClaw = RtClaw();
    return pClaw ? pClaw->GetPositionComponent()->GetX() - m_pPositionComponent->GetX() : 0.0;
}

int RedTailAIStateComponent::Zone() const
{
    // his zones (0x481ef0 constructor): 2000, 600, 300, 200, 100 px wide, 1000 high
    Actor* pClaw = RtClaw();
    if (!pClaw) return 0;
    const double dx = fabs(ClawDx());
    if (fabs(pClaw->GetPositionComponent()->GetY() - m_pPositionComponent->GetY()) > 1000) return 0;
    if (dx < 100) return 5;
    if (dx < 200) return 4;
    if (dx < 300) return 3;
    if (dx < 600) return 2;
    if (dx < 2000) return 1;
    return 0;
}

void RedTailAIStateComponent::FaceClaw()
{
    m_pRenderComponent->SetMirrored(ClawDx() > 0);
}

void RedTailAIStateComponent::SetAnim(const std::string& anim)
{
    m_pAnimationComponent->SetAnimation(anim);
    m_pAnimationComponent->ResetAnimation();
    m_bStruck = false;
    m_LastFrame = 0;
    m_ActTime = 0;
}

void RedTailAIStateComponent::Taunt(int chance)
{
    static const char* kTaunts[4] = { "01040022.WAV", "01040023.WAV", "01040025.WAV", "01040001.WAV" };
    if (Util::GetRandomNumber(0, 100) < chance)
    {
        Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + kTaunts[Util::GetRandomNumber(0, 3)]);
    }
}

void RedTailAIStateComponent::GoHome()
{
    // 0x482e70: idle, half a second before the next choice
    m_Act = Act_Home;
    FaceClaw();
    SetAnim("idle");
    m_DecideCooldown = std::max(m_DecideCooldown, 500);
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
}

void RedTailAIStateComponent::Decide()
{
    // 0x482ef0 (LS_HOME)
    const int zone = Zone();
    const int r = Util::GetRandomNumber(0, 100);
    switch (zone)
    {
        case 0:
        case 5:
            Advance(Next_Home);
            return;
        case 1: case 2: case 3:
            if (r < 90) { Pick(); return; }
            JumpToOtherLevel(Next_Home);
            return;
        case 4:
            if (!SameLevel())
            {
                if (r < 45) { Knife(0); return; }
                if (r < 80) { Wind(0); return; }
            }
            else
            {
                if (r < 33) { Kick(0); return; }
                if (r < 66) { Slide(0); return; }
            }
            JumpToOtherLevel(Next_Home);
            return;
    }
}

void RedTailAIStateComponent::Pick(int depth)
{
    // 0x483090: the attack, by distance
    if (depth > 3 || m_AttackCooldown > 0)
    {
        GoHome();
        return;
    }
    FaceClaw();
    const int r = Util::GetRandomNumber(0, 100);
    switch (Zone())
    {
        case 0: case 1:
            if (r < 40) Knife(depth + 1);
            else if (r < 80) Gun(depth + 1);
            else if (r < 90) Slide(depth + 1);
            else Wind(depth + 1);
            return;
        case 2: case 3:
            if (r < 20) Knife(depth + 1);
            else if (r < 40) Gun(depth + 1);
            else if (r < 80) Slide(depth + 1);
            else Wind(depth + 1);
            return;
        case 4:
            if (r < 70) Kick(depth + 1);
            else Wind(depth + 1);
            return;
        default:
            GoHome();
            return;
    }
}

void RedTailAIStateComponent::DoNext(Next next, int depth)
{
    switch (next)
    {
        case Next_Wind: Wind(depth); return;
        case Next_Gun: Gun(depth); return;
        case Next_Knife: Knife(depth); return;
        case Next_Slide: Slide(depth); return;
        case Next_Kick: Kick(depth); return;
        default: GoHome(); return;
    }
}

void RedTailAIStateComponent::Advance(Next next)
{
    // 0x484d90: LS_FASTADVANCE at 250 px/s up to Claw
    m_Act = Act_Advance;
    m_Next = next;
    FaceClaw();
    m_Dir = ClawDx() > 0 ? 1.0 : -1.0;
    SetAnim("fastadvance");
}

void RedTailAIStateComponent::JumpToOtherLevel(Next next)
{
    Jump(SelfLow(), next);
}

void RedTailAIStateComponent::Jump(bool up, Next next)
{
    // 0x484110 / 0x4844e0: onto the log bridge (37500, 1770) or down to the beach
    if (up != SelfLow())
    {
        GoHome(); // already there
        return;
    }
    m_Act = Act_Jump;
    m_Next = next;
    m_bJumpUp = up;
    m_JumpStart = m_pPositionComponent->GetPosition();
    {
        RaycastResult ground = g_pApp->GetGameLogic()->VGetGamePhysics()->VRayCast(m_JumpStart,
            Point(m_JumpStart.x, m_JumpStart.y + 300), CollisionFlag_Solid | CollisionFlag_Ground);
        if (ground.foundIntersection && ground.closestPixelDistance > 10 && ground.closestPixelDistance < 200)
        {
            m_FeetOffset = ground.closestPixelDistance;
        }
    }
    const Point target = up ? m_UpTarget : Point(Util::GetRandomNumber(m_DownMinX, m_DownMaxX), m_DownY - 60);
    m_JumpDuration = up ? 0.7 : 0.6;
    const double T = m_JumpDuration;
    m_JumpVel.Set((target.x - m_JumpStart.x) / T, (target.y - m_JumpStart.y - 0.5 * kRtGravity * T * T) / T);
    m_JumpTime = 0;
    m_bFlying = true;
    m_pRenderComponent->SetMirrored(target.x > m_JumpStart.x);
    SetAnim("jump");
    Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + "RTJUMP.WAV");
}

void RedTailAIStateComponent::Knife(int depth)
{
    // 0x4832e0: three knives at Claw on the other level
    if (SameLevel())
    {
        if (Zone() == 4) Kick(depth);
        else Gun(depth);
        return;
    }
    m_Act = Act_Knife;
    FaceClaw();
    SetAnim(ClawLow() ? "strike2" : "strike3");
    m_AttackCooldown = Util::GetRandomNumber(1000, 1500);
    Taunt(25);
}

void RedTailAIStateComponent::Gun(int depth)
{
    // 0x483ae0: the flare gun, from Claw's level
    if (ClawLow() && !SelfLow()) { Jump(false, Next_Gun); return; }
    if (!ClawLow() && SelfLow()) { Jump(true, Next_Gun); return; }
    m_Act = Act_Gun;
    FaceClaw();
    SetAnim("strike4");
    m_AttackCooldown = Util::GetRandomNumber(1000, 1500);
    Taunt(25);
}

void RedTailAIStateComponent::Slide(int depth)
{
    // 0x483830: only on the beach, from up close, past Claw
    if (!ClawLow()) { Pick(depth + 1); return; }
    if (!SelfLow()) { Jump(false, Next_Slide); return; }
    if (Zone() != 4) { Advance(Next_Slide); return; }
    m_Act = Act_Slide;
    FaceClaw();
    m_Dir = ClawDx() > 0 ? 1.0 : -1.0;
    Actor* pClaw = RtClaw();
    m_SlideTargetX = (pClaw ? pClaw->GetPositionComponent()->GetX() : m_pPositionComponent->GetX()) + m_Dir * 150;
    SetAnim("strike7");
    m_AttackCooldown = Util::GetRandomNumber(1000, 1500);
    Taunt(25);
    Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + "RTSLIDE.WAV");
}

void RedTailAIStateComponent::Kick(int depth)
{
    // 0x4836b0
    if (!SameLevel()) { Jump(SelfLow(), Next_Kick); return; }
    if (Zone() != 4) { Advance(Next_Kick); return; }
    m_Act = Act_Kick;
    FaceClaw();
    SetAnim("strike8");
    m_AttackCooldown = Util::GetRandomNumber(1000, 1500);
}

void RedTailAIStateComponent::Wind(int depth)
{
    // 0x483e40: from the bridge, once until the wind has died down
    if (!ClawLow() || m_WindLeftMs > 0) { Pick(depth + 1); return; }
    if (SelfLow()) { Jump(true, Next_Wind); return; }
    m_Act = Act_Wind;
    m_pRenderComponent->SetMirrored(false); // blows to the left, at the spikes
    SetAnim("strike6");
    m_WindLeftMs = m_WindTime;
    static const char* kWindTaunts[2] = { "01040012.WAV", "01040020.WAV" };
    Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + kWindTaunts[Util::GetRandomNumber(0, 1)]);
    Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + "WINDWHISTLING.WAV");
}

void RedTailAIStateComponent::ThrowKnives()
{
    // 0x483480: from (35, 4), 250 px/s ahead and 300 down (Claw below) or up;
    // the 2nd one 150 px/s further back, the 3rd also 150 higher
    const bool right = m_pRenderComponent->IsMirrored();
    const double side = right ? 1.0 : -1.0;
    const Point me = m_pPositionComponent->GetPosition();
    const double vx = side * 250, vy = ClawLow() ? 300 : -300;
    const Point vels[3] = { Point(vx, vy), Point(vx - 150, vy), Point(vx - 150, vy - 150) };
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    for (int k = 0; k < 3; k++)
    {
        StrongActorPtr pKnife = ActorTemplates::CreateActor_Projectile(ActorPrototype_Level13_RedTailKnife,
            Point(me.x + side * 35, me.y + 4), right ? Direction_Right : Direction_Left, m_pOwner->GetGUID());
        if (pKnife)
        {
            pPhysics->VSetLinearSpeed(pKnife->GetGUID(), Point(vels[k].x / 75.0, vels[k].y / 75.0));
            if (ActorRenderComponent* pRender = pKnife->GetRawComponent<ActorRenderComponent>())
            {
                pRender->SetMirrored(vels[k].x > 0);
            }
        }
    }
    Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + "3KNIVES.WAV");
}

void RedTailAIStateComponent::UpdateWindAndSpikes(uint32 msDiff)
{
    Actor* pClaw = RtClaw();
    if (!pClaw) return;
    const Point claw = pClaw->GetPositionComponent()->GetPosition();
    // RedTailWind (0x487830): Claw on the beach is blown left at 300 px/s
    if (m_WindLeftMs > 0)
    {
        m_WindLeftMs -= msDiff;
        if (claw.x >= m_WindLeft && claw.x <= m_WindRight && claw.y > m_LevelY && claw.y <= m_WindBottom)
        {
            if (PhysicsComponent* pPhys = pClaw->GetRawComponent<PhysicsComponent>())
            {
                pPhys->SetExternalConveyorBeltSpeed(Point(-m_WindPush / 75.0, 0));
            }
        }
    }
    // RedTailSpikes (0x486ee0): 20 points
    if (m_SpikesCooldown > 0) m_SpikesCooldown -= msDiff;
    if (m_SpikesCooldown <= 0 &&
        fabs(claw.x - m_SpikesCenter.x) < m_SpikesSize.x / 2 + 20 && fabs(claw.y - m_SpikesCenter.y) < m_SpikesSize.y / 2 + 40)
    {
        m_SpikesCooldown = 250;
        ActorTemplates::CreateAreaDamage(m_SpikesCenter, m_SpikesSize, m_SpikesDamage, CollisionFlag_EnemyAIAttack,
            "Rectangle", DamageType_MeleeAttack, Direction_Right, m_pOwner->GetGUID());
    }
}

void RedTailAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
    if (!m_bBossFightStarted)
    {
        m_pAnimationComponent->SetAnimation(m_BossDialogAnimation);
        return;
    }
    m_bFlying = false;
    if (m_bEscape)
    {
        // every third blow he leaps to the other level (0x484cc0 -> 0x484020)
        m_bEscape = false;
        Jump(!ClawLow() ? false : true, Next_Home);
        return;
    }
    GoHome();
}

void RedTailAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
    m_bFlying = false;
}

void RedTailAIStateComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (!m_IsActive || !m_bBossFightStarted || m_bStruck)
    {
        return;
    }
    const bool right = m_pRenderComponent->IsMirrored();
    const double side = right ? 1.0 : -1.0;
    const Point me = m_pPositionComponent->GetPosition();
    const Direction dir = right ? Direction_Right : Direction_Left;
    const int idx = (int)pNewFrame->idx;
    if (m_Act == Act_Gun && idx == 3)
    {
        // frame 209: the flare from (66, -9)
        m_bStruck = true;
        ActorTemplates::CreateActor_Projectile(ActorPrototype_Level13_RedTailBullet, Point(me.x + side * 66, me.y - 9), dir, m_pOwner->GetGUID());
        Util::PlaySimpleSound(std::string(RT_SOUND_DIR) + "FLAREGUN3.WAV");
    }
    else if (m_Act == Act_Kick && idx == 1)
    {
        // frame 402: (-56,-28)..(0,22)
        m_bStruck = true;
        ActorTemplates::CreateAreaDamage(Point(me.x + side * 28, me.y - 3), Point(56, 50), m_MeleeDamage,
            CollisionFlag_EnemyAIAttack, "Rectangle", DamageType_MeleeAttack, dir, m_pOwner->GetGUID());
    }
}

void RedTailAIStateComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (!m_IsActive || !m_bBossFightStarted)
    {
        return;
    }
    switch (m_Act)
    {
        case Act_Knife:
            ThrowKnives();
            GoHome();
            break;
        case Act_Gun:
        case Act_Kick:
            GoHome();
            break;
        case Act_Wind:
            GoHome();
            m_DecideCooldown = Util::GetRandomNumber(2000, 2500);
            break;
        default:
            break;
    }
}

void RedTailAIStateComponent::VUpdate(uint32 msDiff)
{
    if (m_DecideCooldown > 0) m_DecideCooldown -= msDiff;
    if (m_AttackCooldown > 0) m_AttackCooldown -= msDiff;
    if (!m_bBossFightStarted || m_pEnemyAIComponent->IsDead())
    {
        return;
    }
    UpdateWindAndSpikes(msDiff);
    if (!m_IsActive)
    {
        return;
    }
    m_ActTime += msDiff;
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    const uint32 guid = m_pOwner->GetGUID();
    const Point vel = pPhysics->VGetVelocity(guid);

    switch (m_Act)
    {
        case Act_Home:
            pPhysics->VSetLinearSpeed(guid, Point(0, vel.y));
            if (m_DecideCooldown <= 0)
            {
                Decide();
            }
            break;
        case Act_Advance:
        {
            const int zone = Zone();
            const double x = m_pPositionComponent->GetX();
            m_pRenderComponent->SetMirrored(m_Dir > 0);
            const Point me = m_pPositionComponent->GetPosition();
            const double ahead = x + m_Dir * 40.0;
            const bool blocked = pPhysics->VRayCast(Point(me.x, me.y - 20), Point(ahead, me.y - 20), CollisionFlag_Solid).foundIntersection ||
                !pPhysics->VRayCast(Point(ahead, me.y), Point(ahead, me.y + 120), CollisionFlag_Solid | CollisionFlag_Ground).foundIntersection;
            if (zone == 4 || blocked || m_ActTime > 4000)
            {
                pPhysics->VSetLinearSpeed(guid, Point(0, vel.y));
                DoNext(m_Next);
                return;
            }
            pPhysics->VSetLinearSpeed(guid, Point(m_Dir * m_RunSpeed / 75.0, vel.y));
            break;
        }
        case Act_Jump:
        {
            // the parabola (0x484110 / 0x4844e0), then on down until he stands on something
            const double t0 = m_JumpTime;
            m_JumpTime += msDiff / 1000.0;
            const double t = m_JumpTime;
            const Point prev(m_JumpStart.x + m_JumpVel.x * std::min(t0, m_JumpDuration),
                m_JumpStart.y + m_JumpVel.y * t0 + 0.5 * kRtGravity * t0 * t0);
            Point pos(m_JumpStart.x + m_JumpVel.x * std::min(t, m_JumpDuration),
                m_JumpStart.y + m_JumpVel.y * t + 0.5 * kRtGravity * t * t);
            const double vy = m_JumpVel.y + kRtGravity * t;
            // down: only the beach counts, not the bridge he left (0x484780: below y 2000)
            if (vy > 0 && t > 0.1 && (m_bJumpUp || pos.y > m_LevelY))
            {
                RaycastResult ground = pPhysics->VRayCast(Point(pos.x, prev.y + m_FeetOffset - 4),
                    Point(pos.x, pos.y + m_FeetOffset + 2), CollisionFlag_Solid | CollisionFlag_Ground);
                if (ground.foundIntersection)
                {
                    pos.y = prev.y - 4 + ground.closestPixelDistance;
                    pPhysics->VSetPosition(guid, pos);
                    pPhysics->VSetLinearSpeed(guid, Point(0, 0));
                    m_bFlying = false;
                    // landed (0x4847f0): a short rest, then what he jumped for
                    m_DecideCooldown = 750;
                    DoNext(m_Next);
                    return;
                }
            }
            if (t > 3.0)
            {
                m_bFlying = false;
                GoHome();
                return;
            }
            pPhysics->VSetPosition(guid, pos);
            pPhysics->VSetLinearSpeed(guid, Point(0, 0));
            break;
        }
        case Act_Slide:
        {
            // 0x4839f0: on frame 302 the slide hurts; ends past Claw or at an edge
            const double x = m_pPositionComponent->GetX();
            const Point me = m_pPositionComponent->GetPosition();
            const double ahead = x + m_Dir * 40.0;
            const bool blocked = pPhysics->VRayCast(Point(me.x, me.y - 20), Point(ahead, me.y - 20), CollisionFlag_Solid).foundIntersection ||
                !pPhysics->VRayCast(Point(ahead, me.y), Point(ahead, me.y + 120), CollisionFlag_Solid | CollisionFlag_Ground).foundIntersection;
            if ((m_Dir > 0 && x >= m_SlideTargetX) || (m_Dir < 0 && x <= m_SlideTargetX) || blocked || m_ActTime > 3000)
            {
                GoHome();
                return;
            }
            pPhysics->VSetLinearSpeed(guid, Point(m_Dir * m_SlideSpeed / 75.0, vel.y));
            std::shared_ptr<Animation> pAnim = m_pAnimationComponent->GetCurrentAnimation();
            if (pAnim && pAnim->GetCurrentAnimationFrame()->idx >= 1 && m_ActTime - m_LastFrame >= 150)
            {
                // (-52,-17)..(51,41)
                m_LastFrame = m_ActTime;
                ActorTemplates::CreateAreaDamage(Point(me.x, me.y + 12), Point(103, 58), m_MeleeDamage,
                    CollisionFlag_EnemyAIAttack, "Rectangle", DamageType_MeleeAttack,
                    m_Dir > 0 ? Direction_Right : Direction_Left, guid);
            }
            break;
        }
        default:
            pPhysics->VSetLinearSpeed(guid, Point(0, vel.y));
            break;
    }
}

void RedTailAIStateComponent::VOnBossFightStarted()
{
    // To refresh contact list after Claw's death
    g_pApp->GetGameLogic()->VGetGamePhysics()->VActivate(m_pOwner->GetGUID());

    m_HitCount = 0;
    m_bEscape = false;
    m_WindLeftMs = 0;
    m_DecideCooldown = 500;
    m_AttackCooldown = 0;
    m_pEnemyAIComponent->EnterBestState(true);
    if (m_IsActive)
    {
        GoHome();
    }
}

void RedTailAIStateComponent::VOnBossFightEnded(bool isBossDead)
{
    m_WindLeftMs = 0;
    if (isBossDead)
    {
        SoundInfo soundInfo(SOUND_GAME_AMULET_RISE);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(
            new EventData_Request_Play_Sound(soundInfo)));

        StrongActorPtr pGem = ActorTemplates::CreateActor(
            ActorPrototype_Level13_BossGem,
            m_pOwner->GetPositionComponent()->GetPosition());

        /*Point gemForce(-3.5, -7);
        g_pApp->GetGameLogic()->VGetGamePhysics()->VApplyLinearImpulse(pGem->GetGUID(), gemForce);*/
    }
    else
    {
        // To refresh contact list after Claw's death
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());

        /*m_TimeSinceLastAction = 0;
        m_CurrActionDelay = Util::GetRandomNumber(m_ActionMinInterval, m_ActionMaxInterval);*/
    }
}

void RedTailAIStateComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    BaseBossAIStateComponennt::VOnHealthChanged(oldHealth, newHealth, damageType, impactPoint, sourceActorId);
    if (m_bBossFightStarted && newHealth < oldHealth && newHealth > 0 && ++m_HitCount >= 3)
    {
        m_HitCount = 0;
        m_bEscape = true;
    }
}

