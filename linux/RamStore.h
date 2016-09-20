#pragma once

#include <OpenHome/Av/KvpStore.h>

namespace OpenHome {
namespace Av {

// Store for compile time static data properties.
class RamStore : public IStaticDataSource
{
public:
    RamStore(const Brx& aImageFileName);
    virtual ~RamStore();
private: // from IStaticDataSource
    void LoadStaticData(IStoreLoaderStatic& aLoader) override;
private:
    Brhz iImageFileName;
};

} // namespace Av
} // namespace OpenHome
