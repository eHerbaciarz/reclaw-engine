#include "../EnemyAIComponent.h"
#include "../../AnimationComponent.h"
#include "../../PositionComponent.h"
#include "../../RenderComponent.h"
#include "../../ControllerComponents/HealthComponent.h"

#include "../../../../GameApp/BaseGameLogic.h"
#include "../../../../GameApp/BaseGameApp.h"
#include "../../../../Physics/ClawPhysics.h"

#include "../../../../Events/EventMgr.h"
#include "../../../../Events/Events.h"
#include "../../../ActorTemplates.h"
#include "../../../../SoundStrings.h"
#include "OmarEncounter.h"

#include <cmath>
#include <algorithm>

const char* OmarAIStateComponent::g_Name = "OmarAIStateComponent";
const char* OmarSwordSpawnerComponent::g_Name = "OmarSwordSpawnerComponent";
bool OmarSwordSpawnerComponent::s_bBossDefeated = false;

static const char* OMAR_SOUND_HIT = "/LEVEL14/SOUNDS/OMAR/00520010.WAV";
static const char* OMAR_SOUND_WARP = "/LEVEL14/SOUNDS/OMAR/SKYTHUNDER.WAV";
static const char* OMAR_SOUND_BLOCK = "/LEVEL14/SOUNDS/TIGERGUARD/BLOCK1.WAV";

static void OmarPlaySound(const char* sound, const Point& pos)
{
    SoundInfo soundInfo(sound);
    soundInfo.setDistanceEffect = true;
    soundInfo.soundSourcePosition = pos;
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(soundInfo)));
}

static void OmarMoveActor(Actor* pActor, const Point& pos)
{
    pActor->GetPositionComponent()->SetPosition(pos);
    IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Move_Actor(pActor->GetGUID(), pos)));
}

//=====================================================================================================================
// OmarAIStateComponent
//=====================================================================================================================

OmarAIStateComponent::OmarAIStateComponent()
    :
    BaseBossAIStateComponennt("OmarAIStateComponent"),
    m_HitDamage(25),
    m_InvulnerableTime(1500),
    m_WarpDelay(600),
    m_ShieldBallCount(6),
    m_ShieldRadius(75.0),
    m_ShieldSpeed(160.0),
    m_ShieldDamage(10),
    m_Shield(Shield_Fire),
    m_ShieldAngle(0.0),
    m_ShieldHitCooldown(0),
    m_InvulnerableLeft(0),
    m_WarpLeft(0),
    m_PositionIdx(0),
    m_bPendingHit(false),
    m_bPendingHeal(false),
    m_Phase(1),
    m_MoveTime(2000),
    m_MoveLeft(0),
    m_ShieldDir(-1.0),
    m_bAllowDamage(false),
    m_bDead(false)
{
}

OmarAIStateComponent::~OmarAIStateComponent()
{
    DestroyShield();
}

bool OmarAIStateComponent::VDelegateInit(TiXmlElement* pData)
{
    if (!BaseBossAIStateComponennt::VDelegateInit(pData))
    {
        return false;
    }

    ParseValueFromXmlElem(&m_HitDamage, pData->FirstChildElement("HitDamage"));
    ParseValueFromXmlElem(&m_InvulnerableTime, pData->FirstChildElement("InvulnerableTime"));
    ParseValueFromXmlElem(&m_WarpDelay, pData->FirstChildElement("WarpDelay"));
    ParseValueFromXmlElem(&m_ShieldBallCount, pData->FirstChildElement("ShieldBallCount"));
    ParseValueFromXmlElem(&m_ShieldRadius, pData->FirstChildElement("ShieldRadius"));
    ParseValueFromXmlElem(&m_ShieldSpeed, pData->FirstChildElement("ShieldSpeed"));
    ParseValueFromXmlElem(&m_ShieldDamage, pData->FirstChildElement("ShieldDamage"));
    ParseValueFromXmlElem(&m_ShieldBallSpacing, pData->FirstChildElement("ShieldBallSpacing"));

    ParseValueFromXmlElem(&m_MoveTime, pData->FirstChildElement("MoveTime"));

    // CLAW.EXE: two spots by the lava (ring phase) and two up on the ledge
    // reached by the lift (second phase).
    auto parsePositions = [pData](const char* name, std::vector<Point>& out)
    {
        if (TiXmlElement* pPositions = pData->FirstChildElement(name))
        {
            for (TiXmlElement* pElem = pPositions->FirstChildElement("Position");
                pElem != NULL; pElem = pElem->NextSiblingElement("Position"))
            {
                Point pos;
                if (ParseValueFromXmlElem(&pos, pElem, "x", "y"))
                {
                    out.push_back(pos);
                }
            }
        }
    };
    parsePositions("WarpPositions", m_Positions);
    parsePositions("Phase2Positions", m_Phase2Positions);

    return true;
}

void OmarAIStateComponent::VPostInit()
{
    BaseBossAIStateComponennt::VPostInit();

    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pAnimationComponent->AddObserver(this);

    m_Positions.insert(m_Positions.begin(), m_pOwner->GetPositionComponent()->GetPosition());

    OmarSwordSpawnerComponent::SetBossDefeated(false);
}

void OmarAIStateComponent::VOnStateEnter(BaseEnemyAIStateComponent* pPreviousState)
{
    m_IsActive = true;
}

void OmarAIStateComponent::VOnStateLeave(BaseEnemyAIStateComponent* pNextState)
{
    m_IsActive = false;
}

bool OmarAIStateComponent::IsHitByRightSword(DamageType damageType) const
{
    return (m_Shield == Shield_Fire && damageType == DamageType_FrostSword) ||
        (m_Shield == Shield_Ice && damageType == DamageType_FireSword);
}

bool OmarAIStateComponent::VCanResistDamage(DamageType damageType, Point impactPoint)
{
    if (m_bAllowDamage)
    {
        return false;
    }

    // Everything bounces off; hits are counted in VUpdate, one segment each,
    // whatever the projectile's own damage is. While he flies he can't be hit.
    if (damageType == DamageType_DeathTile)
    {
        return true;
    }
    const bool canBeHit = m_bBossFightStarted && !m_bDead && m_InvulnerableLeft <= 0 && m_MoveLeft <= 0;
    if (m_Phase == 1)
    {
        // CLAW.EXE: only the opposite magic sword hurts him; the other magic
        // sword gives him a segment back.
        if (canBeHit && IsHitByRightSword(damageType))
        {
            m_bPendingHit = true;
            m_InvulnerableLeft = m_InvulnerableTime;
            return true;
        }
        if (canBeHit && (damageType == DamageType_FireSword || damageType == DamageType_FrostSword))
        {
            m_bPendingHeal = true;
            m_InvulnerableLeft = m_InvulnerableTime;
        }
    }
    else if (canBeHit)
    {
        // Second phase: no ring, any hit takes a segment
        m_bPendingHit = true;
        m_InvulnerableLeft = m_InvulnerableTime;
        return true;
    }

    OmarPlaySound(OMAR_SOUND_BLOCK, m_pOwner->GetPositionComponent()->GetPosition());
    return true;
}

void OmarAIStateComponent::CreateShield()
{
    DestroyShield();

    ActorPrototype proto = (m_Shield == Shield_Fire) ?
        ActorPrototype_Level14_OmarFireShieldBall : ActorPrototype_Level14_OmarIceShieldBall;

    Point center = m_pOwner->GetPositionComponent()->GetPosition();
    for (int i = 0; i < m_ShieldBallCount; i++)
    {
        StrongActorPtr pBall = ActorTemplates::CreateActor(proto, center);
        if (pBall)
        {
            m_ShieldBalls.push_back(pBall);
        }
    }

    UpdateShield(0);
}

void OmarAIStateComponent::DestroyShield()
{
    for (std::weak_ptr<Actor>& pWeakBall : m_ShieldBalls)
    {
        if (StrongActorPtr pBall = pWeakBall.lock())
        {
            IEventMgr::Get()->VQueueEvent(IEventDataPtr(new EventData_Destroy_Actor(pBall->GetGUID())));
        }
    }
    m_ShieldBalls.clear();
}

void OmarAIStateComponent::UpdateShield(uint32 msDiff)
{
    if (m_ShieldBalls.empty())
    {
        return;
    }

    m_ShieldAngle += m_ShieldDir * m_ShieldSpeed * msDiff / 1000.0;
    while (m_ShieldAngle >= 360.0) m_ShieldAngle -= 360.0;
    while (m_ShieldAngle < 0.0) m_ShieldAngle += 360.0;

    Point center = m_pOwner->GetPositionComponent()->GetPosition() + Point(0, -10);
    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    SDL_Rect clawRect = { 0, 0, 0, 0 };
    if (pClaw)
    {
        clawRect = g_pApp->GetGameLogic()->VGetGamePhysics()->VGetAABB(pClaw->GetGUID(), true);
    }

    if (m_ShieldHitCooldown > 0)
    {
        m_ShieldHitCooldown -= (int)msDiff;
    }

    // CLAW.EXE: balls 33 degrees apart, so a 9-ball ring has a gap
    const double step = (m_ShieldBallSpacing > 0.0) ? m_ShieldBallSpacing : 360.0 / (double)m_ShieldBalls.size();
    for (size_t i = 0; i < m_ShieldBalls.size(); i++)
    {
        StrongActorPtr pBall = m_ShieldBalls[i].lock();
        if (!pBall)
        {
            continue;
        }

        double rad = (m_ShieldAngle + step * i) * M_PI / 180.0;
        Point pos(center.x + cos(rad) * m_ShieldRadius, center.y + sin(rad) * m_ShieldRadius * 1.2);
        OmarMoveActor(pBall.get(), pos);

        // The ring burns (or freezes) Claw when he walks into it
        if (pClaw && m_ShieldHitCooldown <= 0 && clawRect.w > 0 &&
            pos.x > clawRect.x - 12 && pos.x < clawRect.x + clawRect.w + 12 &&
            pos.y > clawRect.y - 12 && pos.y < clawRect.y + clawRect.h + 12)
        {
            if (HealthComponent* pHealth = pClaw->GetRawComponent<HealthComponent>(true))
            {
                m_ShieldHitCooldown = 1000;
                pHealth->AddHealth(-m_ShieldDamage, DamageType_EnemyAura, pos, m_pOwner->GetGUID());
            }
        }
    }
}

void OmarAIStateComponent::WarpTo(int positionIdx)
{
    if (m_Positions.empty())
    {
        return;
    }

    m_PositionIdx = positionIdx % (int)m_Positions.size();
    Point target = m_Positions[m_PositionIdx];

    ActorTemplates::CreateGlitter("Glitter_Red", m_pOwner->GetPositionComponent()->GetPosition());
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetPosition(m_pOwner->GetGUID(), target);
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
    OmarMoveActor(m_pOwner.get(), target);
    ActorTemplates::CreateGlitter("Glitter_Red", target);
    OmarPlaySound(OMAR_SOUND_WARP, target);
}

void OmarAIStateComponent::StartMove(const Point& target)
{
    m_MoveFrom = m_pOwner->GetPositionComponent()->GetPosition();
    m_MoveTo = target;
    m_MoveLeft = std::max(1, m_MoveTime);
    OmarPlaySound(OMAR_SOUND_WARP, m_MoveFrom);
}

void OmarAIStateComponent::UpdateMove(uint32 msDiff)
{
    m_MoveLeft -= (int)msDiff;
    double t = 1.0 - (double)std::max(0, m_MoveLeft) / (double)std::max(1, m_MoveTime);
    Point pos(m_MoveFrom.x + (m_MoveTo.x - m_MoveFrom.x) * t, m_MoveFrom.y + (m_MoveTo.y - m_MoveFrom.y) * t);

    // He flies: no gravity on the way, faces where he goes
    shared_ptr<IGamePhysics> pPhysics = g_pApp->GetGameLogic()->VGetGamePhysics();
    pPhysics->VSetPosition(m_pOwner->GetGUID(), pos);
    pPhysics->VSetLinearSpeed(m_pOwner->GetGUID(), Point(0, 0));
    OmarMoveActor(m_pOwner.get(), pos);

    if (m_MoveLeft <= 0)
    {
        m_MoveLeft = 0;
        if (m_Phase == 1)
        {
            CreateShield();
        }
        m_pEnemyAIComponent->EnterBestState(true);
    }
}

void OmarAIStateComponent::VUpdate(uint32 msDiff)
{
    // The ring and the hits are handled whatever state (patrol, melee, ranged)
    // the enemy AI is in
    if (!m_bBossFightStarted || m_bDead)
    {
        return;
    }

    if (m_InvulnerableLeft > 0)
    {
        m_InvulnerableLeft -= (int)msDiff;
    }

    HealthComponent* pHealth = m_pOwner->GetRawComponent<HealthComponent>(true);
    int segment = m_HitDamage;
    if (pHealth && pHealth->GetMaxHealth() > 0 && m_HitDamage > 0 && m_HitDamage <= 100)
    {
        segment = (pHealth->GetMaxHealth() * m_HitDamage + 99) / 100;
    }

    if (m_bPendingHeal)
    {
        m_bPendingHeal = false;
        if (pHealth && pHealth->GetCurrentHealth() > 0)
        {
            int heal = std::min<int>(segment, (int)(pHealth->GetMaxHealth() - pHealth->GetCurrentHealth()));
            if (heal > 0)
            {
                m_bAllowDamage = true;
                pHealth->AddHealth(heal, DamageType_None, Point(), m_pOwner->GetGUID());
                m_bAllowDamage = false;
            }
        }
    }

    if (m_bPendingHit)
    {
        m_bPendingHit = false;

        if (pHealth && pHealth->GetCurrentHealth() > 0)
        {
            Point pos = m_pOwner->GetPositionComponent()->GetPosition();
            OmarPlaySound(OMAR_SOUND_HIT, pos);

            if (m_Phase == 1 && pHealth->GetCurrentHealth() - segment <= 0 && !m_Phase2Positions.empty())
            {
                // CLAW.EXE: the ring phase over, he flies up to the ledge the
                // lift leads to, with his bar full again.
                DestroyShield();
                m_bAllowDamage = true;
                pHealth->AddHealth(pHealth->GetMaxHealth() - pHealth->GetCurrentHealth(), DamageType_None, Point(), m_pOwner->GetGUID());
                m_bAllowDamage = false;
                m_Phase = 2;
                m_PositionIdx = 0;
                StartMove(m_Phase2Positions[0]);
                return;
            }

            m_bAllowDamage = true;
            pHealth->AddHealth(-segment, DamageType_MeleeAttack, pos, m_pOwner->GetGUID());
            m_bAllowDamage = false;

            if (pHealth->GetCurrentHealth() <= 0)
            {
                m_bDead = true;
                return;
            }

            // To the other spot of this phase
            std::vector<Point>& spots = (m_Phase == 1) ? m_Positions : m_Phase2Positions;
            if (!spots.empty())
            {
                m_PositionIdx = (m_PositionIdx + 1) % (int)spots.size();
                if (m_Phase == 1)
                {
                    DestroyShield();
                    m_Shield = (m_Shield == Shield_Fire) ? Shield_Ice : Shield_Fire;
                    m_ShieldDir = -m_ShieldDir;
                }
                StartMove(spots[m_PositionIdx]);
                return;
            }
        }
    }

    if (m_MoveLeft > 0)
    {
        UpdateMove(msDiff);
        return;
    }

    UpdateShield(msDiff);
}

void OmarAIStateComponent::VOnBossFightStarted()
{
    g_pApp->GetGameLogic()->VGetGamePhysics()->VActivate(m_pOwner->GetGUID());

    m_bDead = false;
    m_bPendingHit = false;
    m_bPendingHeal = false;
    m_InvulnerableLeft = 0;
    m_WarpLeft = 0;
    m_MoveLeft = 0;
    m_Phase = 1;
    m_ShieldDir = -1.0;
    m_Shield = Shield_Fire;
    if (m_PositionIdx != 0)
    {
        WarpTo(0);
    }
    CreateShield();

    m_pEnemyAIComponent->EnterBestState(true);
}

void OmarAIStateComponent::VOnBossFightEnded(bool isBossDead)
{
    DestroyShield();

    if (isBossDead)
    {
        m_bDead = true;
        OmarSwordSpawnerComponent::SetBossDefeated(true);

        SoundInfo soundInfo(SOUND_GAME_AMULET_RISE);
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(soundInfo)));

        ActorTemplates::CreateActor(
            ActorPrototype_Level14_BossGem,
            m_pOwner->GetPositionComponent()->GetPosition() + Point(0, -40));
    }
    else
    {
        // Claw died: back to the start, full health, fire ring
        m_MoveLeft = 0;
        m_Phase = 1;
        WarpTo(0);
        m_Shield = Shield_Fire;
        m_ShieldDir = -1.0;
        g_pApp->GetGameLogic()->VGetGamePhysics()->VDeactivate(m_pOwner->GetGUID());
    }
}

void OmarAIStateComponent::VOnHealthChanged(int32 oldHealth, int32 newHealth, DamageType damageType, Point impactPoint, int sourceActorId)
{
    BaseBossAIStateComponennt::VOnHealthChanged(oldHealth, newHealth, damageType, impactPoint, sourceActorId);
}

//=====================================================================================================================
// OmarSwordSpawnerComponent
//=====================================================================================================================

OmarSwordSpawnerComponent::OmarSwordSpawnerComponent()
    :
    m_PickupType(0),
    m_RespawnTime(3000),
    m_TimeLeft(0)
{
}

bool OmarSwordSpawnerComponent::VInit(TiXmlElement* pData)
{
    ParseValueFromXmlElem(&m_PickupType, pData->FirstChildElement("PickupType"));
    ParseValueFromXmlElem(&m_RespawnTime, pData->FirstChildElement("RespawnTime"));
    return true;
}

void OmarSwordSpawnerComponent::VUpdate(uint32 msDiff)
{
    if (!m_pPickup.expired() || s_bBossDefeated || m_PickupType <= 0)
    {
        return;
    }

    m_TimeLeft -= (int)msDiff;
    if (m_TimeLeft > 0)
    {
        return;
    }

    m_TimeLeft = m_RespawnTime;
    m_pPickup = ActorTemplates::CreateActorPickup(
        (PickupType)m_PickupType,
        m_pOwner->GetPositionComponent()->GetPosition(),
        true);
}
