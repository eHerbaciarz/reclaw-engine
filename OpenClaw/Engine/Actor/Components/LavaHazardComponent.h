#ifndef __LAVA_HAZARD_COMPONENT_H__
#define __LAVA_HAZARD_COMPONENT_H__

#include "../../SharedDefines.h"
#include "../ActorComponent.h"
#include "AnimationComponent.h"

// Level 14 (Temple) lava traps: LavaGeyser, LavaMouth and LavaHand.
//
// The animation plays once, then waits Pause ms and plays again. While the
// current frame is in [ActiveFrameFrom, ActiveFrameTo] Claw gets hurt when he
// touches the visible picture (shrunk by HitboxInset), so the hurting area
// follows the lava in the art without any hand-made boxes.
//
// With a TriggerArea (LavaHand) the animation only starts while Claw is inside
// it, and on ThrowFrameIdx a lava ball is lobbed at him.
class ActorRenderComponent;
class LavaHazardComponent : public ActorComponent, public AnimationObserver
{
public:
    LavaHazardComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override;
    virtual void VPostInit() override;
    virtual void VUpdate(uint32 msDiff) override;

    virtual TiXmlElement* VGenerateXml() override { assert(false && "Unimplemented"); return NULL; }

    virtual void VOnAnimationFrameChanged(Animation* pAnimation, AnimationFrame* pLastFrame, AnimationFrame* pNewFrame) override;
    virtual void VOnAnimationLooped(Animation* pAnimation) override;

private:
    bool IsClawInTriggerArea();
    void StartCycle();
    void ThrowProjectile();
    void TryToHurtClaw();

    // XML data
    int m_StartDelay;
    int m_Pause;
    int m_ActiveFrameFrom;
    int m_ActiveFrameTo;
    Point m_HitboxInset;
    int m_Damage;
    int m_HitCooldown;
    std::string m_Sound;
    int m_SoundVolume;
    SDL_Rect m_TriggerArea;
    int m_ThrowFrameIdx;
    ActorPrototype m_ProjectileProto;
    Point m_ProjectileOffset;
    int m_ProjectileFlightTime;

    // Internal state
    AnimationComponent* m_pAnimationComponent;
    ActorRenderComponent* m_pARC;
    int m_StartDelayLeft;
    int m_PauseLeft;
    int m_HitCooldownLeft;
    int m_CurrentFrameIdx;
    bool m_bPlaying;
};

#endif
