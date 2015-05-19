#pragma once

#include <OpenHome/Av/KvpStore.h>

namespace OpenHome {
namespace Av {


// Store for compile time static data properties.
class RamStore : public IStaticDataSource
{
public:
    virtual ~RamStore();
private: // from IStaticDataSource
    void LoadStaticData(IStoreLoaderStatic& aLoader) override;
};

} // namespace Av
} // namespace OpenHome
