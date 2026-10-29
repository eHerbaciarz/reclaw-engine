#include <cstring>
#include <map>
#include <string>
#include <SDL2/SDL.h>
#include <tinyxml.h>

#include "../SharedDefines.h"
#include "Actor.h"

#include "Components/PositionComponent.h"
#include "Components/PhysicsComponent.h"
#include "../SaveState/StateArchive.h"

Actor::Actor(uint32 actorGUID)
{
    _GUID = actorGUID;
    _name = "Unknown";
    _resource = "Unknown";
}

Actor::~Actor()
{
    //LOG_TAG("Actor", "Destructor: Destroying actor: " + _name + ", GUID: " + std::to_string(_GUID));
    assert(_components.empty() && "Actors components are not empty !");
}

bool Actor::Init(TiXmlElement* data)
{
    if (data->Attribute("Type") != NULL)
    {
        _name = data->Attribute("Type");
    }
    else
    {
        _name = "UNSET";
    }
    //_resource = data->Attribute("resource");

    /*LOG_TAG("Actor", "Constructor: Initializing actor: " + _name + ", GUID: " + std::to_string(_GUID) +
        " from resource: " + _resource);*/

    return true;
}

void Actor::PostInit()
{
    m_pPositionComponent = MakeStrongPtr(GetComponent<PositionComponent>(PositionComponent::g_Name));
    m_pPhysicsComponent = MakeStrongPtr(GetComponent<PhysicsComponent>());

    for (auto &component : _components)
    {
        component.second->VPostInit();
    }
}

// Every component writes its state into its own length-prefixed block, keyed
// by component id, so a mismatch in one component cannot corrupt the rest.
void Actor::SerializeState(StateArchive& ar)
{
    if (ar.IsSaving())
    {
        uint32_t count = (uint32_t)_components.size();
        ar(count);
        for (auto& component : _components)
        {
            uint32_t id = component.first;
            ar(id);
            size_t marker = ar.BeginBlock();
            component.second->VSerializeState(ar);
            ar.EndBlock(marker);
        }
        return;
    }

    uint32_t count = 0;
    ar(count);
    for (uint32_t i = 0; i < count && !ar.Failed(); ++i)
    {
        uint32_t id = 0;
        ar(id);
        uint32_t size = ar.ReadBlockSize();
        size_t start = ar.Position();
        auto findIt = _components.find(id);
        if (findIt != _components.end() && size > 0)
        {
            findIt->second->VSerializeState(ar);
            if (ar.Failed() || ar.Position() != start + size)
            {
                LOG_WARNING("Save state: component " + std::string(findIt->second->VGetName()) +
                            " of " + _name + " read a different amount of data");
                ar.ClearFailure();
            }
        }
        ar.Seek(start + size);
    }
}

void Actor::OnStateRestored()
{
    for (auto& component : _components)
    {
        component.second->VOnStateRestored();
    }
}

void Actor::PostPostInit()
{
    for (auto &component : _components)
    {
        component.second->VPostPostInit();
    }
}

void Actor::Destroy()
{
    //LOG("Destroying actor: " + _name);
    // Remove references (Actor has components and Component has an owner)
    // This actor and component won't be freed without it!
    m_pPositionComponent.reset();
    m_pPhysicsComponent.reset();
    _components.clear();
}

#ifdef OPENCLAW_STARTUP_TRACE
int g_PerfActorUpdates = 0;
#endif

void Actor::Update(uint32 msDiff)
{
#ifdef OPENCLAW_STARTUP_TRACE
    g_PerfActorUpdates++;
#endif
    for (auto &component : _components)
    {
        component.second->VUpdate(msDiff);
    }
}

bool Actor::IsPausableWhenFar()
{
    if (_pausableWhenFar < 0)
    {
        // Components that only animate, sit still or wait to be touched.
        // Anything else (enemies, elevators, spikes, sounds, Claw...) keeps
        // running everywhere.
        static const char* kPausable[] = {
            "AnimationComponent", "ActorRenderComponent", "PositionComponent", "PhysicsComponent",
            "TriggerComponent", "SoundTriggerComponent", "CollisionComponent", "PickupComponent",
            "TreasurePickupComponent", "LifePickupComponent", "HealthPickupComponent", "AmmoPickupComponent",
            "PowerupPickupComponent", "CheckpointComponent", "GlitterComponent", "LootComponent",
            "DestroyableComponent", "ConcreteComponent",
        };
        bool ok = !_components.empty();
        for (auto &component : _components)
        {
            bool found = false;
            for (const char* name : kPausable)
                if (strcmp(component.second->VGetName(), name) == 0) { found = true; break; }
            if (!found) { ok = false; break; }
        }
        _pausableWhenFar = ok ? 1 : 0;
    }
    return _pausableWhenFar == 1;
}

std::string Actor::ToXML()
{
    TiXmlDocument outXml;

    return "";
}

void Actor::AddComponent(StrongActorComponentPtr component)
{
    std::pair<ActorComponentsMap::iterator, bool> success =
        _components.insert(std::make_pair(component->VGetId(), component));

    assert(success.second);
}

void Actor::OnWorldFinishedLoading()
{
    for (const auto& componentPair : _components)
    {
        componentPair.second->VOnWorldFinishedLoading();
    }
}
