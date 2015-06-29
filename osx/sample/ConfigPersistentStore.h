#pragma once

#include <OpenHome/Configuration/BufferPtrCmp.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Private/Thread.h>

#include <map>

#import <Foundation/Foundation.h>
#import "Prefs.h"

namespace OpenHome {
namespace Configuration {

// Provides a registry based read/write store.
class ConfigPersistentStore : public IStoreReadWrite
{
public:
    ConfigPersistentStore();
    virtual ~ConfigPersistentStore();
public: // from IStoreReadWrite
    void Read(const Brx& aKey, Bwx& aDest) override;
    void Write(const Brx& aKey, const Brx& aSource) override;
    void Delete(const Brx& aKey) override;
private:
    mutable Mutex iLock;
    Prefs * prefs;
};

} // namespace Configuration
} // namespace OpenHome
