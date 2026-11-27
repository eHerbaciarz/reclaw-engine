/* Dreamcast: GPF's SDL2 port has no SDL_TryLockMutex (the second-screen code
   of the engine uses it). Blocking is fine there: the Dreamcast has no second
   screen and the lock is never held for long. */
#include <SDL2/SDL_mutex.h>

int SDL_TryLockMutex(SDL_mutex *mutex)
{
    return SDL_LockMutex(mutex);
}
