#ifndef ACTORFACTORY_H_
#define ACTORFACTORY_H_

#include <map>
#include <vector>

#include "ActorComponent.h"

//-------------------------------------------------------------------------------------------------
// Actor factory
//-------------------------------------------------------------------------------------------------

class ActorFactory
{
public:
    ActorFactory();

    StrongActorPtr CreateActor(TiXmlElement* pActorRoot, TiXmlElement* overrides);
    // Save states: recreate an actor with the GUID it had when the state was taken.
    StrongActorPtr CreateActorWithGuid(TiXmlElement* pActorRoot, TiXmlElement* overrides, uint32_t guid);
    // Keep the creation XML of every actor (needed by save states).
    static void SetRecordCreationData(bool record) { s_bRecordCreationData = record; }
    StrongActorPtr CreateActor(const char* actorResource, TiXmlElement* overrides);
    void ModifyActor(StrongActorPtr actor, TiXmlElement* overrides);

    virtual StrongActorComponentPtr VCreateComponent(TiXmlElement* data);

protected:
    GenericObjectFactory<ActorComponent, uint32_t> _componentFactory;

private:
    StrongActorPtr CreateActorImpl(TiXmlElement* pActorRoot, TiXmlElement* overrides, uint32_t guid);

    static bool s_bRecordCreationData;
    std::vector<uint32_t> _creationStack;   // GUIDs of actors being created (nesting)
    uint32_t _lastActorGUID;
    uint32_t GetNextActorGUID() { ++_lastActorGUID; return _lastActorGUID; }
};

#endif