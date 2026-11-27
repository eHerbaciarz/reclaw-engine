#include "MemStats.h"
#if defined(RECLAW_PS2) || defined(RECLAW_DREAMCAST) || defined(__3DS__) || defined(__vita__) || defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))
#include <malloc.h>
#define RECLAW_HAVE_MALLINFO 1
#endif
#include "../GameApp/BaseGameApp.h"
#include "../GameApp/BaseGameLogic.h"
#include "../Resource/ResourceCache.h"

namespace MemStats
{
    int64_t g_TextureBytes = 0;
    int64_t g_TextureCount = 0;
    int64_t g_SoundBytes = 0;
    int64_t g_SoundCount = 0;

    std::string Report()
    {
        char line[256];
        long long cacheKb = 0;
        if (g_pApp && g_pApp->GetResourceCache())
        {
            cacheKb = (long long)(g_pApp->GetResourceCache()->GetAllocated() / 1024);
        }
        long long actors = 0;
        if (g_pApp && g_pApp->GetGameLogic())
        {
            actors = (long long)g_pApp->GetGameLogic()->GetActorMap().size();
        }
        long long heapKb = -1;
#ifdef RECLAW_HAVE_MALLINFO
        heapKb = (long long)(mallinfo().uordblks / 1024);
#endif
        char heap[48] = "";
        if (heapKb >= 0) snprintf(heap, sizeof(heap), "heap %lld KB, ", heapKb);
        snprintf(line, sizeof(line), "%stextures %lld (%lld KB), sounds %lld (%lld KB), resource cache %lld KB, actors %lld",
                 heap, (long long)g_TextureCount, (long long)(g_TextureBytes / 1024),
                 (long long)g_SoundCount, (long long)(g_SoundBytes / 1024), cacheKb, actors);
        return line;
    }
}
