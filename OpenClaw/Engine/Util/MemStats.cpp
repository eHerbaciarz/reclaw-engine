#include "MemStats.h"
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
        snprintf(line, sizeof(line), "textures %lld (%lld KB), sounds %lld (%lld KB), resource cache %lld KB, actors %lld",
                 (long long)g_TextureCount, (long long)(g_TextureBytes / 1024),
                 (long long)g_SoundCount, (long long)(g_SoundBytes / 1024), cacheKb, actors);
        return line;
    }
}
