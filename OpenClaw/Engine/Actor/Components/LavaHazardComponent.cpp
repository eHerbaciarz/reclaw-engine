#include "LavaHazardComponent.h"
#include "RenderComponent.h"
#include "PositionComponent.h"
#include "ControllerComponents/HealthComponent.h"
#include "../ActorTemplates.h"

#include "../../GameApp/BaseGameApp.h"
#include "../../GameApp/BaseGameLogic.h"
#include "../../UserInterface/HumanView.h"

#include "../../Events/EventMgr.h"
#include "../../Events/Events.h"

const char* LavaHazardComponent::g_Name = "LavaHazardComponent";

static const double kPixelsPerMeter = 75.0;
static const double kGravity = 9.8; // m/s^2, same as the physics world

LavaHazardComponent::LavaHazardComponent()
    :
    m_StartDelay(0),
    m_Pause(1500),
    m_ActiveFrameFrom(0),
    m_ActiveFrameTo(1000),
    m_HitboxInset(8, 8),
    m_Damage(10),
    m_HitCooldown(1000),
    m_SoundVolume(60),
    m_ThrowFrameIdx(-1),
    m_ProjectileProto(ActorPrototype_None),
    m_ProjectileFlightTime(900),
    m_pAnimationComponent(NULL),
    m_pARC(NULL),
    m_StartDelayLeft(0),
    m_PauseLeft(0),
    m_HitCooldownLeft(0),
    m_CurrentFrameIdx(0),
    m_bPlaying(false)
{
    m_TriggerArea = { 0, 0, 0, 0 };
}

bool LavaHazardComponent::VInit(TiXmlElement* pData)
{
    assert(pData != NULL);

    ParseValueFromXmlElem(&m_StartDelay, pData->FirstChildElement("StartDelay"));
    ParseValueFromXmlElem(&m_Pause, pData->FirstChildElement("Pause"));
    ParseValueFromXmlElem(&m_ActiveFrameFrom, pData->FirstChildElement("ActiveFrameFrom"));
    ParseValueFromXmlElem(&m_ActiveFrameTo, pData->FirstChildElement("ActiveFrameTo"));
    ParseValueFromXmlElem(&m_HitboxInset, pData->FirstChildElement("HitboxInset"), "x", "y");
    ParseValueFromXmlElem(&m_Damage, pData->FirstChildElement("Damage"));
    ParseValueFromXmlElem(&m_HitCooldown, pData->FirstChildElement("HitCooldown"));
    ParseValueFromXmlElem(&m_Sound, pData->FirstChildElement("Sound"));
    ParseValueFromXmlElem(&m_SoundVolume, pData->FirstChildElement("SoundVolume"));

    Point triggerMin, triggerMax;
    ParseValueFromXmlElem(&triggerMin, pData->FirstChildElement("TriggerMin"), "x", "y");
    ParseValueFromXmlElem(&triggerMax, pData->FirstChildElement("TriggerMax"), "x", "y");
    if (triggerMax.x > triggerMin.x && triggerMax.y > triggerMin.y)
    {
        m_TriggerArea = { (int)triggerMin.x, (int)triggerMin.y,
            (int)(triggerMax.x - triggerMin.x), (int)(triggerMax.y - triggerMin.y) };
    }

    ParseValueFromXmlElem(&m_ThrowFrameIdx, pData->FirstChildElement("ThrowFrameIdx"));
    std::string protoStr;
    if (ParseValueFromXmlElem(&protoStr, pData->FirstChildElement("ProjectilePrototype")) && !protoStr.empty())
    {
        m_ProjectileProto = StringToEnum_ActorPrototype(protoStr);
    }
    ParseValueFromXmlElem(&m_ProjectileOffset, pData->FirstChildElement("ProjectileOffset"), "x", "y");
    ParseValueFromXmlElem(&m_ProjectileFlightTime, pData->FirstChildElement("ProjectileFlightTime"));

    if (m_ProjectileFlightTime < 200)
    {
        m_ProjectileFlightTime = 200;
    }

    return true;
}

void LavaHazardComponent::VPostInit()
{
    m_pAnimationComponent = m_pOwner->GetRawComponent<AnimationComponent>(true);
    m_pARC = m_pOwner->GetRawComponent<ActorRenderComponent>(true);
    assert(m_pAnimationComponent != NULL);
    assert(m_pARC != NULL);

    m_pAnimationComponent->AddObserver(this);
    if (m_pAnimationComponent->GetCurrentAnimation())
    {
        m_pAnimationComponent->ResetAnimation();
        m_pAnimationComponent->PauseAnimation();
    }

    m_StartDelayLeft = m_StartDelay;
    m_bPlaying = false;
}

bool LavaHazardComponent::IsClawInTriggerArea()
{
    if (m_TriggerArea.w <= 0 || m_TriggerArea.h <= 0)
    {
        return true;
    }

    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return false;
    }

    Point clawPos = pClaw->GetPositionComponent()->GetPosition();
    return clawPos.x >= m_TriggerArea.x && clawPos.x <= m_TriggerArea.x + m_TriggerArea.w &&
        clawPos.y >= m_TriggerArea.y && clawPos.y <= m_TriggerArea.y + m_TriggerArea.h;
}

void LavaHazardComponent::StartCycle()
{
    if (!m_pAnimationComponent->GetCurrentAnimation())
    {
        return;
    }

    m_bPlaying = true;
    m_CurrentFrameIdx = 0;
    m_pAnimationComponent->ResetAnimation();
    m_pAnimationComponent->ResumeAnimation();

    // Only make noise when the player can see it
    Point pos = m_pOwner->GetPositionComponent()->GetPosition();
    if (!m_Sound.empty() && g_pApp->GetHumanView() && g_pApp->GetHumanView()->IsPointInAnyView(pos, 1.1f))
    {
        SoundInfo sound(m_Sound);
        sound.soundVolume = m_SoundVolume;
        sound.setPositionEffect = true;
        sound.setDistanceEffect = true;
        sound.soundSourcePosition = pos;
        IEventMgr::Get()->VTriggerEvent(IEventDataPtr(new EventData_Request_Play_Sound(sound)));
    }
}

void LavaHazardComponent::VUpdate(uint32 msDiff)
{
    if (m_HitCooldownLeft > 0)
    {
        m_HitCooldownLeft -= (int)msDiff;
    }

    if (m_bPlaying)
    {
        TryToHurtClaw();
        return;
    }

    // Keep the picture on its first frame while waiting
    shared_ptr<Animation> pAnim = m_pAnimationComponent->GetCurrentAnimation();
    if (pAnim && !pAnim->IsPaused())
    {
        m_pAnimationComponent->ResetAnimation();
        m_pAnimationComponent->PauseAnimation();
    }

    if (m_StartDelayLeft > 0)
    {
        m_StartDelayLeft -= (int)msDiff;
        return;
    }

    if (m_PauseLeft > 0)
    {
        m_PauseLeft -= (int)msDiff;
        return;
    }

    if (IsClawInTriggerArea())
    {
        StartCycle();
    }
}

void LavaHazardComponent::TryToHurtClaw()
{
    if (m_Damage <= 0 || m_HitCooldownLeft > 0 ||
        m_CurrentFrameIdx < m_ActiveFrameFrom || m_CurrentFrameIdx > m_ActiveFrameTo)
    {
        return;
    }

    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return;
    }

    SDL_Rect img = m_pARC->VGetPositionRect();
    img.x += (int)m_HitboxInset.x;
    img.y += (int)m_HitboxInset.y;
    img.w -= 2 * (int)m_HitboxInset.x;
    img.h -= 2 * (int)m_HitboxInset.y;
    if (img.w <= 0 || img.h <= 0)
    {
        return;
    }

    SDL_Rect claw = g_pApp->GetGameLogic()->VGetGamePhysics()->VGetAABB(pClaw->GetGUID(), true);
    if (claw.w <= 0 || claw.h <= 0)
    {
        return;
    }

    SDL_Rect overlap;
    if (!SDL_IntersectRect(&img, &claw, &overlap))
    {
        return;
    }

    HealthComponent* pHealth = pClaw->GetRawComponent<HealthComponent>(true);
    if (!pHealth || pHealth->GetCurrentHealth() <= 0)
    {
        return;
    }

    m_HitCooldownLeft = m_HitCooldown;
    Point impact(overlap.x + overlap.w / 2, overlap.y + overlap.h / 2);
    pHealth->AddHealth(-m_Damage, DamageType_EnemyAura, impact, m_pOwner->GetGUID());
}

void LavaHazardComponent::ThrowProjectile()
{
    if (m_ProjectileProto == ActorPrototype_None)
    {
        return;
    }

    StrongActorPtr pClaw = g_pApp->GetGameLogic()->GetClawActor();
    if (!pClaw)
    {
        return;
    }

    Point start = m_pOwner->GetPositionComponent()->GetPosition() + m_ProjectileOffset;
    Point target = pClaw->GetPositionComponent()->GetPosition();

    StrongActorPtr pProjectile = ActorTemplates::CreateActor_Projectile(
        m_ProjectileProto, start, Direction_Right, m_pOwner->GetGUID());
    if (!pProjectile)
    {
        return;
    }

    // Lob it so that it lands where Claw stands right now
    double t = m_ProjectileFlightTime / 1000.0;
    double dx = (target.x - start.x) / kPixelsPerMeter;
    double dy = (target.y - start.y) / kPixelsPerMeter;
    Point velocity(dx / t, dy / t - 0.5 * kGravity * t);
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetGravityScale(pProjectile->GetGUID(), 1.0f);
    g_pApp->GetGameLogic()->VGetGamePhysics()->VSetLinearSpeed(pProjectile->GetGUID(), velocity);

    if (shared_ptr<ActorRenderComponent> pRender = MakeStrongPtr(pProjectile->GetComponent<ActorRenderComponent>()))
    {
        pRender->SetMirrored(velocity.x < 0);
    }
}

void LavaHazardComponent::VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame)
{
    if (!m_bPlaying || pNewFrame == NULL)
    {
        return;
    }

    m_CurrentFrameIdx = pNewFrame->idx;
    if (m_ThrowFrameIdx >= 0 && m_CurrentFrameIdx == m_ThrowFrameIdx)
    {
        ThrowProjectile();
    }
}

void LavaHazardComponent::VOnAnimationLooped(Animation* pAnimation)
{
    if (!m_bPlaying)
    {
        return;
    }

    m_bPlaying = false;
    m_CurrentFrameIdx = 0;
    m_pAnimationComponent->ResetAnimation();
    m_pAnimationComponent->PauseAnimation();
    m_PauseLeft = m_Pause;
}
