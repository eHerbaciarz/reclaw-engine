// ReClaw PS2: operator new that writes the size of a request it cannot meet
// (with how much of the heap is in use), then throws std::bad_alloc as usual.
#include <cstdlib>
#include <new>
#include <malloc.h>
#include <SDL2/SDL_log.h>

static void* ReclawNew(std::size_t n)
{
    if (n == 0) n = 1;
    if (void* p = std::malloc(n)) return p;
    static int s_Count = 0;
    if (s_Count++ < 5)
    {
        const struct mallinfo mi = mallinfo();
        SDL_Log("ERROR: out of memory: new of %u bytes (heap in use %d KB, arena %d KB)",
                (unsigned)n, (int)(mi.uordblks / 1024), (int)(mi.arena / 1024));
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t n) { return ReclawNew(n); }
void* operator new[](std::size_t n) { return ReclawNew(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try { return ReclawNew(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept
{
    try { return ReclawNew(n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
