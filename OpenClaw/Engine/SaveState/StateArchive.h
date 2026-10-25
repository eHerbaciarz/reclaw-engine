#pragma once

// Binary archive used by the save-state system (quick save / quick load /
// rewind). One function per component serves both directions:
//
//     void MyComponent::VSerializeState(StateArchive& ar)
//     {
//         ar(m_Timer);          // ints, floats, bools, enums
//         ar(m_Name);           // std::string
//         ar(m_Position);       // Point
//         ar.ActorRef(m_pTarget); // Actor* / StrongActorPtr -> stored as GUID
//     }
//
// Only runtime state goes here (timers, flags, current mode). Configuration
// from the actor XML is recreated by the factory, and contact-derived lists
// (actors inside a trigger, foot contacts) are rebuilt by the physics engine
// after a restore, so they must NOT be serialized.

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "../SharedDefines.h"

class Actor;

class StateArchive
{
public:
    // Writing: appends to `buffer`. Reading: reads `buffer` from `offset`.
    StateArchive(std::vector<uint8_t>* buffer, bool loading, size_t offset = 0)
        : m_pBuffer(buffer), m_bLoading(loading), m_Pos(offset) { }

    bool IsLoading() const { return m_bLoading; }
    bool IsSaving() const { return !m_bLoading; }
    bool Failed() const { return m_bFailed; }
    size_t Position() const { return m_Pos; }

    template <class T>
    typename std::enable_if<std::is_arithmetic<T>::value || std::is_enum<T>::value>::type
    operator()(T& value)
    {
        Raw(&value, sizeof(T));
    }

    void operator()(std::string& value)
    {
        uint32_t size = (uint32_t)value.size();
        (*this)(size);
        if (m_bLoading)
        {
            if (m_bFailed || m_Pos + size > m_pBuffer->size()) { m_bFailed = true; return; }
            value.assign((const char*)m_pBuffer->data() + m_Pos, size);
            m_Pos += size;
        }
        else
        {
            m_pBuffer->insert(m_pBuffer->end(), value.begin(), value.end());
        }
    }

    void operator()(Point& p)
    {
        (*this)(p.x);
        (*this)(p.y);
    }

    void operator()(SDL_Rect& r)
    {
        (*this)(r.x); (*this)(r.y); (*this)(r.w); (*this)(r.h);
    }

    void operator()(SDL_Color& c)
    {
        (*this)(c.r); (*this)(c.g); (*this)(c.b); (*this)(c.a);
    }

    template <class T>
    void operator()(std::vector<T>& v)
    {
        uint32_t size = (uint32_t)v.size();
        (*this)(size);
        if (m_bLoading)
        {
            if (m_bFailed || size > 1000000) { m_bFailed = true; return; }
            v.resize(size);
        }
        for (auto& item : v) (*this)(item);
    }

    template <class K, class V>
    void operator()(std::map<K, V>& m)
    {
        uint32_t size = (uint32_t)m.size();
        (*this)(size);
        if (m_bLoading)
        {
            if (m_bFailed || size > 1000000) { m_bFailed = true; return; }
            m.clear();
            for (uint32_t i = 0; i < size && !m_bFailed; ++i)
            {
                K key{};
                V value{};
                (*this)(key);
                (*this)(value);
                m[key] = value;
            }
        }
        else
        {
            for (auto& kv : m)
            {
                K key = kv.first;
                (*this)(key);
                (*this)(kv.second);
            }
        }
    }

    // References to other actors are stored as GUIDs and resolved after all
    // actors have been restored (some may be recreated during the restore).
    void ActorRef(Actor*& pActor);
    void ActorRef(std::shared_ptr<Actor>& pActor);
    void ActorRef(std::weak_ptr<Actor>& pActor);
    void ActorRefList(std::vector<std::shared_ptr<Actor>>& list);

    // Called by the save-state manager once every actor exists again.
    using Fixup = std::function<void()>;
    std::vector<Fixup>& Fixups() { return m_Fixups; }

    // Length-prefixed blocks, so one component that reads too much or too
    // little cannot shift the data of the next one.
    size_t BeginBlock()
    {
        size_t marker = m_pBuffer->size();
        uint32_t placeholder = 0;
        Raw(&placeholder, sizeof(placeholder));
        return marker;
    }
    void EndBlock(size_t marker)
    {
        uint32_t size = (uint32_t)(m_pBuffer->size() - marker - sizeof(uint32_t));
        memcpy(m_pBuffer->data() + marker, &size, sizeof(size));
    }
    // Reading: returns the block size and leaves the cursor at its start.
    uint32_t ReadBlockSize()
    {
        uint32_t size = 0;
        Raw(&size, sizeof(size));
        if (m_Pos + size > m_pBuffer->size()) { m_bFailed = true; return 0; }
        return size;
    }
    void Seek(size_t pos) { m_Pos = pos; }
    void ClearFailure() { m_bFailed = false; }

    void Raw(void* data, size_t size)
    {
        if (m_bLoading)
        {
            if (m_bFailed || m_Pos + size > m_pBuffer->size()) { m_bFailed = true; memset(data, 0, size); return; }
            memcpy(data, m_pBuffer->data() + m_Pos, size);
            m_Pos += size;
        }
        else
        {
            const uint8_t* p = (const uint8_t*)data;
            m_pBuffer->insert(m_pBuffer->end(), p, p + size);
        }
    }

private:
    std::vector<uint8_t>* m_pBuffer;
    bool m_bLoading;
    size_t m_Pos;
    bool m_bFailed = false;
    std::vector<Fixup> m_Fixups;
};
