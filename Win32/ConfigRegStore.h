#pragma once

#include <OpenHome/Configuration/BufferPtrCmp.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Private/Thread.h>

#include <map>
#include <Windows.h>

namespace OpenHome {
namespace Configuration {

// Provides a registry based read/write store.
class ConfigRegStore : public IStoreReadWrite
{
public:
    ConfigRegStore();
    virtual ~ConfigRegStore();
public: // from IStoreReadWrite
    void Read(const Brx& aKey, Bwx& aDest) override;
    void Read(const Brx& aKey, IWriter& aWriter) override;
    void Write(const Brx& aKey, const Brx& aSource) override;
    void Delete(const Brx& aKey) override;
    void ResetToDefaults() override;
private:
    LPWSTR BrxToWString(const Brx& aKey);
    TBool GetAppRegistryKey();
private:
    HKEY          iHk;
    mutable Mutex iLock;
};

} // namespace Configuration
} // namespace OpenHome
