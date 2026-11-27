#ifndef __POWDER_KEG_COMPONENT_H__
#define __POWDER_KEG_COMPONENT_H__

// Powder kegs Claw can lift, carry over his head and throw (the original
// game's "lift" action). A thrown keg explodes when it hits the ground, a
// wall, a ceiling or an enemy; the explosion itself is done by the keg's
// HealthComponent / ExplodeableComponent / DestroyableComponent, exactly as
// when a keg is shot.

#include "../../SharedDefines.h"
#include "../ActorComponent.h"

class PowderKegComponent : public ActorComponent
{
public:
    enum KegState
    {
        KegState_Idle,
        KegState_Carried,
        KegState_Thrown
    };

    PowderKegComponent();

    static const char* g_Name;
    virtual const char* VGetName() const override { return g_Name; }

    virtual bool VInit(TiXmlElement* pData) override { return true; }
    virtual void VUpdate(uint32 msDiff) override;

    // Save states (SaveState/ComponentStates.cpp)
    virtual void VSerializeState(StateArchive& ar) override;
    virtual void VOnStateRestored() override;

    bool CanBeLifted() const;
    bool IsCarried() const { return m_State == KegState_Carried; }

    // Claw picked the keg up: it is taken out of the physics world and
    // follows the carrier.
    void OnLifted(uint32 carrierId);
    // Thrown from `position` with `velocity` (Box2D units, like projectiles).
    // gravityScale < 0: the default flight gravity.
    void Throw(const Point& position, const Point& velocity, float gravityScale = -1.0f);
    // Put down at `position` without exploding (carrier got hurt, climbed...).
    void Drop(const Point& position);

    // Offset of the keg from the carrier's position while carried.
    static Point GetCarryOffset() { return Point(0, -72); }

private:
    void FollowCarrier();
    void Explode();
    void MoveTo(const Point& position);

    // CLAW.EXE LEVEL_POWDERKEG LIFTED / LIFTED2 / THROWN: upright while
    // being lifted, on its side above Claw's head, spinning in flight.
    void ShowFrame(int imageId);
    int m_ShownFrame = 0;
    KegState m_State;
    uint32 m_CarrierId;
    int m_FlightTime;
    Point m_LastVelocity;
    Point m_ThrowVelocity;
};

#endif
