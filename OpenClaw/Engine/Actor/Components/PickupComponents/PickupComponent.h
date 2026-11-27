#ifndef __PICKUPCOMPONENT_H__
#define __PICKUPCOMPONENT_H__

#include "../../ActorComponent.h"
#include "../TriggerComponents/TriggerComponent.h"

//=====================================================================================================================
// PickupComponent - base class for derived pickup components
//=====================================================================================================================

class PositionComponent;
class ActorRenderComponent;
class PickupComponent : public ActorComponent, public TriggerObserver
{
public:
    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* data) override;
    virtual void VPostInit() override;
    virtual TiXmlElement* VGenerateXml() override;

    virtual bool VOnApply(Actor* pActorWhoPickedThis) = 0;
    virtual void VOnActorEnteredTrigger(Actor* pActorWhoEntered, FixtureType triggerType) override;

    PickupType GetPickupType() { return m_PickupType; }

protected:
    virtual bool VDelegateInit(TiXmlElement* data) { return true; }
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement) = 0;

    PickupType m_PickupType;
    std::string m_PickupSound;
};

//=====================================================================================================================
// TreasurePickupComponent
//=====================================================================================================================

class TreasurePickupComponent : public PickupComponent
{
public:
    // Save states (SaveState/ComponentStates.cpp)
    virtual void VSerializeState(StateArchive& ar) override;

    TreasurePickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }
    virtual void VUpdate(uint32 msDiff) override;
    virtual void VPostInit() override;

    virtual bool VOnApply(Actor* pActorWhoPickedThis) override;

protected:
    virtual bool VDelegateInit(TiXmlElement* data) override;
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement) override;

private:
    int32 m_ScorePoints;
    bool m_IsPickedUp;
public:
    bool IsPickedUp() const { return m_IsPickedUp; }
private:

    shared_ptr<PositionComponent> m_pPositionComponent;
    shared_ptr<ActorRenderComponent> m_pRenderComponent;
};

//=====================================================================================================================
// LifePickupComponent
//=====================================================================================================================

class LifePickupComponent : public PickupComponent
{
public:
    LifePickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const { return g_Name; }

    virtual bool VOnApply(Actor* pActorWhoPickedThis);

protected:
    virtual bool VDelegateInit(TiXmlElement* data);
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement);

private:
    int32 m_NumLives;
};

//=====================================================================================================================
// HealthPickupComponent
//=====================================================================================================================

class HealthPickupComponent : public PickupComponent
{
public:
    HealthPickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const { return g_Name; }

    virtual bool VOnApply(Actor* pActorWhoPickedThis);

protected:
    virtual bool VDelegateInit(TiXmlElement* data);
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement);

private:
    int32 m_NumRestoredHealth;
};

//=====================================================================================================================
// TeleportPickupComponent
//=====================================================================================================================

class TeleportPickupComponent : public PickupComponent
{
public:
    TeleportPickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const { return g_Name; }

    virtual bool VOnApply(Actor* pActorWhoPickedThis);

protected:
    virtual bool VDelegateInit(TiXmlElement* data);
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement);

private:

    bool m_bIsBossWarp;
    Point m_Destination;
};

//=====================================================================================================================
// PowerupPickupComponent
//=====================================================================================================================

class PowerupPickupComponent : public PickupComponent
{
public:
    PowerupPickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const { return g_Name; }

    virtual bool VOnApply(Actor* pActorWhoPickedThis);

protected:
    virtual bool VDelegateInit(TiXmlElement* data);
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement);

private:
    PowerupType m_PowerupType;
    int32 m_PowerupDuration;
};

//=====================================================================================================================
// AmmoPickupComponent
//=====================================================================================================================

typedef std::vector<std::pair<AmmoType, int>> AmmoList;

class AmmoPickupComponent : public PickupComponent
{
public:
    AmmoPickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const { return g_Name; }

    virtual bool VOnApply(Actor* pActorWhoPickedThis);

protected:
    virtual bool VDelegateInit(TiXmlElement* data);
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement);

private:
    AmmoList m_AmmoPickupList;
};

//=====================================================================================================================
// EndLevelPickupComponent
//=====================================================================================================================

class EndLevelPickupComponent : public PickupComponent
{
public:
    EndLevelPickupComponent();

    static const char* g_Name;
    virtual const char* VGetName() const { return g_Name; }

    virtual bool VOnApply(Actor* pActorWhoPickedThis);
    virtual void VUpdate(uint32 msDiff) override;

    // CLAW.EXE (EndLevelGem, 0x40eac0, 0x43dd60, 0x43e2e0): a boss's gem
    // flies on a slow arc (gravity 100 px/s^2, the lowest launch speed)
    // to a fixed spot given by the boss object in the level file (its
    // SpeedX/SpeedY) and stays there; it cannot be picked up in flight.
    // Set while the level loads; (0,0) = no boss on this level.
    static void SetBossGemTarget(const Point& target) { s_BossGemTarget = target; }

protected:
    virtual bool VDelegateInit(TiXmlElement* data);
    virtual void VCreateInheritedXmlElements(TiXmlElement* pBaseElement);

private:
    static Point s_BossGemTarget;
    int m_FlightState = 0;      // 0 not started, 1 flying, 2 done / not a flight
    double m_FlightTime = 0.0;
    double m_FlightDuration = 0.0;
    Point m_FlightStart;
    Point m_FlightVelocity;     // px/s
};

#endif
