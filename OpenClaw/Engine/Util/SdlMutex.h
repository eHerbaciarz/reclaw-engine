#pragma once

// A mutex usable with std::lock_guard on every port. Some console C++
// libraries (PSL1GHT on the PS3) are built without thread support and have
// no std::mutex; SDL's mutex works everywhere SDL does.

#include <SDL2/SDL_mutex.h>

class SdlMutex
{
public:
    SdlMutex() : m_pMutex(SDL_CreateMutex()) { }
    ~SdlMutex() { if (m_pMutex) SDL_DestroyMutex(m_pMutex); }
    SdlMutex(const SdlMutex&) = delete;
    SdlMutex& operator=(const SdlMutex&) = delete;

    void lock() { if (m_pMutex) SDL_LockMutex(m_pMutex); }
    void unlock() { if (m_pMutex) SDL_UnlockMutex(m_pMutex); }

private:
    SDL_mutex* m_pMutex;
};

template <class M>
class SdlLockGuard
{
public:
    explicit SdlLockGuard(M& m) : m_Mutex(m) { m_Mutex.lock(); }
    ~SdlLockGuard() { m_Mutex.unlock(); }
    SdlLockGuard(const SdlLockGuard&) = delete;
    SdlLockGuard& operator=(const SdlLockGuard&) = delete;

private:
    M& m_Mutex;
};
