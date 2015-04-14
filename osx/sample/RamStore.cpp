#include "RamStore.h"
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/Printer.h>

#include <map>

using namespace OpenHome;
using namespace OpenHome::Av;

RamStore::~RamStore() {}

void RamStore::LoadStaticData(IStoreLoaderStatic& aLoader)
{
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerName, "OpenHome");
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerInfo, "OpenHomeOrg");
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerUrl, "http://www.openhome.org");
    aLoader.AddStaticItem(StaticDataKey::kBufManufacturerImageUrl, "http://www.openhome.org/common/images/core/open-home-logo.png");
    aLoader.AddStaticItem(StaticDataKey::kBufModelName, "OpenHome Media Player (test)");
    aLoader.AddStaticItem(StaticDataKey::kBufModelInfo, "OSX Sample ohMediaPlayer");
    aLoader.AddStaticItem(StaticDataKey::kBufModelUrl, "http://www.openhome.org/wiki/OhMedia");
    aLoader.AddStaticItem(StaticDataKey::kBufModelImageUrl, "http://www.openhome.org/common/images/core/open-home-logo.png");
}
