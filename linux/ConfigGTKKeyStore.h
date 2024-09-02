#pragma once

#include <glib.h>

#include <OpenHome/Buffer.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Private/Thread.h>

#include <string>

namespace OpenHome {
namespace Configuration {

// Provides a GTK Key File based read/write store via a singleton pattern
// to ensure a single instance of the store throughout the application.
class ConfigGTKKeyStore : public IStoreReadWrite
{
private:
    ConfigGTKKeyStore();

    // Stop the compiler generating methods of copy and assignment operators.
    ConfigGTKKeyStore(ConfigGTKKeyStore const& copy);
    ConfigGTKKeyStore& operator=(ConfigGTKKeyStore const& copy);

public:
    static ConfigGTKKeyStore *getInstance()
    {
        static ConfigGTKKeyStore instance;
        return &instance;
    }

public: // from IStoreReadWrite
    void Read(const Brx& aKey, Bwx& aDest) override;
    void Read(const Brx& aKey, IWriter& aWriter) override;
    void Write(const Brx& aKey, const Brx& aSource) override;
    void Delete(const Brx& aKey) override;
    void ResetToDefaults() override;
private:
    bool mkPath(std::vector<std::string>);
private:
    mutable Mutex  iLock;
    const gchar   *iConfigGroup; // Group to house our properties
    std::string    iConfigFile;  // Path to the config file
    GKeyFile      *iKeyFile;     // GTK Key file object
};

} // namespace Configuration
} // namespace OpenHome
