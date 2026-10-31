#ifndef ACTOR_H_
#define ACTOR_H_

#include <string>
#include <stdint.h>
#include <memory>
#include <map>

#include "../SharedDefines.h"

#include "ActorComponent.h"

typedef std::map<uint32, StrongActorComponentPtr> ActorComponentsMap;

class PositionComponent;
class PhysicsComponent;
class TiXmlElement;
class StateArchive;

class Actor
{
public:
    Actor(uint32_t actorGuid);
    ~Actor();

    bool Init(TiXmlElement* data);
    void PostInit();
    void PostPostInit();
    void Destroy();
    void Update(uint32_t msDiff);

    std::string ToXML();

    // Save states: the XML this actor was created from (shared with save
    // states so destroyed actors can be recreated) and the actor that was
    // being created when this one was (child actors spawned in PostInit).
    void SetCreationData(const std::shared_ptr<const std::string>& rootXml,
                         const std::shared_ptr<const std::string>& overridesXml,
                         uint32_t parentGuid)
    {
        _creationXml = rootXml;
        _creationOverridesXml = overridesXml;
        _parentGuid = parentGuid;
    }
    const std::shared_ptr<const std::string>& GetCreationXml() const { return _creationXml; }
    const std::shared_ptr<const std::string>& GetCreationOverridesXml() const { return _creationOverridesXml; }
    uint32_t GetParentGuid() const { return _parentGuid; }

    void SerializeState(StateArchive& ar);
    void OnStateRestored();

    inline uint32_t GetGUID() const { return _GUID; }
    inline const std::string& GetName() const { return _name; }

    // Retrieves component from given ID or NULL if component not found
    template <class ComponentType>
    weak_ptr<ComponentType> GetComponent(uint32 id)
    {
        ActorComponentsMap::iterator findIter = _components.find(id);
        if (findIter != _components.end())
        {
            StrongActorComponentPtr base(findIter->second);
            shared_ptr<ComponentType> sub(std::static_pointer_cast<ComponentType>(base));  // cast to subclass version of the pointer
            weak_ptr<ComponentType> weakSub(sub);  // convert strong pointer to weak pointer
            return weakSub;  // return the weak pointer
        }

        return weak_ptr<ComponentType>();
    }

    // Retrieves component from given name or NULL if component not found
    template <class ComponentType>
    weak_ptr<ComponentType> GetComponent(const char *name)
    {
        uint32 id = ActorComponent::GetIdFromName(name);
        ActorComponentsMap::iterator findIter = _components.find(id);
        if (findIter != _components.end())
        {
            StrongActorComponentPtr base(findIter->second);
            shared_ptr<ComponentType> sub(static_pointer_cast<ComponentType>(base));  // cast to subclass version of the pointer
            weak_ptr<ComponentType> weakSub(sub);  // convert strong pointer to weak pointer
            return weakSub;  // return the weak pointer
        }
        else
        {
            return weak_ptr<ComponentType>();
        }
    }

    template <class ComponentType>
    weak_ptr<ComponentType> GetComponent()
    {
        return GetComponent<ComponentType>(ComponentType::g_Name);
    }

    template <class ComponentType>
    ComponentType* GetRawComponent(bool bAssertNotNull = false)
    {
        uint32 id = ActorComponent::GetIdFromName(ComponentType::g_Name);
        ActorComponentsMap::iterator findIter = _components.find(id);
        if (findIter != _components.end())
        {
            StrongActorComponentPtr base(findIter->second);
            shared_ptr<ComponentType> sub(static_pointer_cast<ComponentType>(base));  // cast to subclass version of the pointer
            return sub.get();
        }
        
        if (bAssertNotNull)
        {
            LOG_ASSERT("Failed to get component from actor: " + GetName());
        }

        return NULL;
    }

    const ActorComponentsMap* GetComponents() { return &_components; }

    // Scenery and pickups: actors made only of components that may pause while
    // far from the screen (handhelds update only nearby ones; Actor.cpp).
    bool IsPausableWhenFar();
    // An ordinary enemy (not a boss): on the 3DS it also waits while far from
    // the screen, with its physics body switched off (BaseGameLogic).
    bool IsEnemyPausableWhenFar();
    bool m_bPausedFar = false;

    void AddComponent(StrongActorComponentPtr pComponent);

    void OnWorldFinishedLoading();

    //=========================================================================
    // Some components are accessed REALLY often so it makes sense to just
    // put them here and dont use the templated GetComponent method
    //=========================================================================

    inline shared_ptr<PositionComponent> GetPositionComponent() { return m_pPositionComponent; }
    inline shared_ptr<PhysicsComponent> GetPhysicsComponent() { return m_pPhysicsComponent; }

private:
    friend class ActorFactory;

    uint32_t _GUID;
    std::string _name;

    ActorComponentsMap _components;
    int8_t _pausableWhenFar = -1;   // -1: not checked yet
    int8_t _enemyPausableWhenFar = -1;

    // Resource from which this actor was loaded
    std::string _resource;

    std::shared_ptr<const std::string> _creationXml;
    std::shared_ptr<const std::string> _creationOverridesXml;
    uint32_t _parentGuid = 0;

    shared_ptr<PositionComponent> m_pPositionComponent;
    shared_ptr<PhysicsComponent> m_pPhysicsComponent; // Cached because physics objects sync every frame
};

#endif