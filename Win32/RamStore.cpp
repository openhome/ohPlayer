#include "RamStore.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
   #ifndef DBG_NEW
      #define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
      #define new DBG_NEW
   #endif
#endif  // _DEBUG

using namespace OpenHome;
using namespace OpenHome::Av;

RamStore::~RamStore() {}

// Initialise static, read only properties.
void RamStore::LoadStaticData(IStoreLoaderStatic& aLoader)
{
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerName, "OpenHome");
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerInfo, "insert oh info here...");
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerUrl, "http://www.openhome.org");
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerImageUrl, "http://www.openhome.org/common/images/core/open-home-logo.png");
    aLoader.AddStaticItem(StaticDataKey::kBufModelName, "OpenHome Media Player (test)");
    aLoader.AddStaticItem(StaticDataKey::kBufModelInfo, "Example implementation of ohMediaPlayer");
    aLoader.AddStaticItem(StaticDataKey::kBufModelUrl, "http://www.openhome.org/wiki/OhMedia");
    aLoader.AddStaticItem(StaticDataKey::kBufModelImageUrl, "http://www.openhome.org/common/images/core/open-home-logo.png");
}
