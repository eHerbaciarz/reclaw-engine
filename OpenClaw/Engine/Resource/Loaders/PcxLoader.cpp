#include "PcxLoader.h"

#include "../../Graphics2D/Image.h"
#include "../../GameApp/BaseGameApp.h"
#include "../ResourceMgr.h"
#include "../../Util/StartupTrace.h"
#include <cstdio>

//=================================================================================================
// class PcxResourceExtraData
//
//     This class implements the IResourceExtraData
//

void PcxResourceExtraData::LoadImage(char* rawBuffer, uint32 size, bool useColorKey, SDL_Color colorKey)
{
    if (m_pImage == nullptr)
    {
        m_pImage.reset(Image::CreatePcxImage(rawBuffer, size, g_pApp->GetRenderer(), useColorKey, colorKey));
    }
}

//=================================================================================================
// class PcxResourceLoader
//
//     This class implements the IResourceLoader interface with PCX file loading
//

shared_ptr<Image> PcxResourceLoader::LoadAndReturnImage(const char* resourceString, bool useColorKey, SDL_Color colorKey)
{
    Resource resource(resourceString);

    // Use ResourceMgr to allow ASSETS.ZIP to override CLAW.REZ assets
    LOWMEM_TRACE("pcx: handle");
    shared_ptr<ResourceHandle> handle = g_pApp->GetResourceMgr()->VGetHandle(&resource);
    assert(handle != nullptr);
    if (!handle)
    {
        LOG_ERROR(std::string("Picture not found: ") + resourceString);
        return nullptr;
    }
#if defined(RECLAW_PS2) || defined(RECLAW_DREAMCAST)
    {
        char t[96];
        snprintf(t, sizeof(t), "pcx: %u bytes, sp %p", (unsigned)handle->GetSize(), __builtin_frame_address(0));
        LOWMEM_TRACE(t);
    }
#endif

    shared_ptr<PcxResourceExtraData> extraData = std::static_pointer_cast<PcxResourceExtraData>(handle->GetExtraData());
    if (!extraData)
    {
        extraData = shared_ptr<PcxResourceExtraData>(new PcxResourceExtraData());
        extraData->LoadImage(handle->GetDataBuffer(), handle->GetSize(), useColorKey, colorKey);
        LOWMEM_TRACE("pcx: image made");

        if (!extraData->GetImage())
        {
            LOG_ERROR(extraData->VToString() + ": GetImage() returned nullptr. Check if PcxResourceLoader is registered.");
            return nullptr;
        }

        handle->SetExtraData(extraData);
    }

    return extraData->GetImage();
}

shared_ptr<PcxResourceLoader> PcxResourceLoader::Create()
{
    return shared_ptr<PcxResourceLoader>(new PcxResourceLoader());
};