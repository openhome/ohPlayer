#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>

#include <vector>

#include <sys/stat.h>
#include <stdlib.h>

#include "ConfigGTKKeyStore.h"
#include "OpenHomePlayer.h"

using namespace OpenHome;
using namespace OpenHome::Configuration;

using namespace std;


// GTK Key File  based implementation of the IStoreReadWrite interface

//
// Accept a vector of elements in a directory path.
// eg. '/home/pi/.config', 'appdir'.
//
// Non-existent elements will be created.
//
// Returns true on success false on error.
//
bool ConfigGTKKeyStore::mkPath(vector<string> folderPath)
{
    string dirString;

    for (unsigned int i=0; i<folderPath.size(); i++)
    {
        struct stat buf;

        if (i == 0)
        {
            dirString = folderPath[i];
        }
        else
        {
            dirString += "/" + folderPath[i];
        }

        if (stat(dirString.c_str(), &buf) != 0)
        {
            int res = mkdir(dirString.c_str(),
                            S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

            if (res != 0)
            {
                return false;
            }
        }
    }

    return true;
}

ConfigGTKKeyStore::ConfigGTKKeyStore()
    : iLock("RAMS"),
      iConfigGroup("Properties"),
      iKeyFile(NULL)
{
    const char *homePath = getenv("HOME");

    if (homePath != NULL)
    {
        GError *error = NULL;

        // User specific configuration root folder.
        string configDir(homePath);
        configDir += "/.config";

        // Ensure the, application specific, configuration folder exists.
        vector<string> configFolder {configDir, g_appName};
        mkPath(configFolder);

        iKeyFile     = g_key_file_new();
        iConfigFile  = configDir;
        iConfigFile += "/";
        iConfigFile += g_appName;
        iConfigFile += "/configStore.conf";

        // Load any existing configuration file into the key file object.
        g_key_file_load_from_file(iKeyFile,
                                  iConfigFile.c_str(),
                  (GKeyFileFlags)(G_KEY_FILE_KEEP_COMMENTS |
                                  G_KEY_FILE_KEEP_TRANSLATIONS),
                                 &error);

        // Free up any allocated error.
        if (error != NULL)
        {
            g_error_free(error);
        }
    }
}

void ConfigGTKKeyStore::Read(const Brx& aKey, Bwx& aDest)
{
    gchar  *propertyValue;
    GError *error = NULL;
    string  keyStr((const char *)(aKey.Ptr()), aKey.Bytes());

    if (! iKeyFile)
    {
        return;
    }

    AutoMutex a(iLock);

    propertyValue = g_key_file_get_string (iKeyFile,
                                           iConfigGroup,
                                           keyStr.c_str(),
                                          &error);

    if (propertyValue == NULL)
    {
        THROW(StoreKeyNotFound);
    }

    // Data is stored in Base64 format. Decode here.
    gsize   decodedLen;
    guchar *decodedData = g_base64_decode (propertyValue, &decodedLen);

    if (decodedLen > aDest.MaxBytes())
    {
        g_free(decodedData);

        THROW(StoreReadBufferUndersized);
    }

    // Copy the data into the return buffer.
    memcpy((guchar *)(aDest.Ptr()), decodedData, decodedLen);

    // Set the number of bytes copied into the buffer.
    aDest.SetBytes(decodedLen);

    g_free(decodedData);
    g_free(propertyValue);

    // Free up any allocated error.
    if (error != NULL)
    {
        g_error_free(error);
    }
}

void ConfigGTKKeyStore::Read(const Brx& aKey, IWriter& aWriter)
{}

void ConfigGTKKeyStore::Write(const Brx& aKey, const Brx& aSource)
{
    string  keyStr((const char *)(aKey.Ptr()), aKey.Bytes());
    gchar  *encodedData;
    GError *error = NULL;

    if (! iKeyFile)
    {
        return;
    }

    AutoMutex a(iLock);

    // Base64 encode the data to allow storage as an ASCII string.
    encodedData = g_base64_encode((const guchar *)(aSource.Ptr()),
                                  aSource.Bytes());

    g_key_file_set_string(iKeyFile,
                          iConfigGroup,
                          keyStr.c_str(),
                          encodedData);

    g_free(encodedData);

    if (iConfigFile.empty())
    {
        return;
    }

    // Save key file object to the physical configuration file.
    // to keep things in sync.
    g_key_file_save_to_file(iKeyFile,
                            iConfigFile.c_str(),
                           &error);

    // Free up any allocated error.
    if (error != NULL)
    {
        g_error_free(error);
    }
}

void ConfigGTKKeyStore::Delete(const Brx& aKey)
{
    GError *error = NULL;
    string  keyStr((const char *)(aKey.Ptr()), aKey.Bytes());

    if (! iKeyFile)
    {
        return;
    }

    AutoMutex a(iLock);

    if (! g_key_file_remove_key (iKeyFile,
                                 iConfigGroup,
                                 keyStr.c_str(),
                                 &error))
    {
        // Free up any allocated error.
        g_error_free(error);

        THROW(StoreKeyNotFound);
    }

    if (iConfigFile.empty())
    {
        return;
    }

    // Save key file object to the physical configuration file.
    // to keep things in sync.
    g_key_file_save_to_file(iKeyFile,
                            iConfigFile.c_str(),
                           &error);

    // Free up any allocated error.
    if (error != NULL)
    {
        g_error_free(error);
    }
}

void ConfigGTKKeyStore::ResetToDefaults()
{}
