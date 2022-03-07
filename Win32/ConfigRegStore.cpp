#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>

#include "ConfigRegStore.h"
#include "MemoryCheck.h"

using namespace OpenHome;
using namespace OpenHome::Configuration;

// Registry based implementation of the IStoreReadWrite interface

ConfigRegStore::ConfigRegStore()
    : iLock("RAMS")
{
}

ConfigRegStore::~ConfigRegStore()
{
}

// Convert Brx normal string data to a native wide string.
LPWSTR ConfigRegStore::BrxToWString(const Brx& aKey)
{
    WCHAR  *wcstring;
    size_t  convertedChars;
    TUint   length = aKey.Bytes();

    if (length <= 0)
    {
        return NULL;
    }

    // Allocate a wide character array to hold the string, accommodating a
    // NULL terminator.
    wcstring = new WCHAR[length+1];

    mbstowcs_s(&convertedChars, wcstring, length+1, (char *)aKey.Ptr(), length);

    return wcstring;
}

TBool ConfigRegStore::GetAppRegistryKey()
{
    // Obtain a handle to our application key.
    //
    // This will be created if one doesn't already exist.
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       L"Software\\OpenHome\\Player",
                               0,
                               NULL,
                               REG_OPTION_NON_VOLATILE,
                               KEY_ALL_ACCESS,
                               NULL,
                               &iHk,
                               NULL) == ERROR_SUCCESS)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void ConfigRegStore::Read(const Brx& /*aKey*/, IWriter& /*aWriter*/)
{ }

void ConfigRegStore::Read(const Brx& aKey, Bwx& aDest)
{
    AutoMutex a(iLock);

    if (GetAppRegistryKey())
    {
        LPWSTR keyString;

        keyString = BrxToWString(aKey);

        if (keyString != NULL)
        {
            DWORD bytes = aDest.MaxBytes();
            LONG  retVal;

            // Attempt to read the required key value.
            retVal = RegQueryValueEx(iHk,
                                     keyString,
                                     NULL,
                                     NULL,
                                     (LPBYTE)aDest.Ptr(),
                                     (LPDWORD)&bytes);

            RegCloseKey(iHk);
            delete[] keyString;

            switch (retVal)
            {
                case ERROR_SUCCESS:
                {
                    break;
                }

                case ERROR_MORE_DATA:
                {
                    THROW(StoreReadBufferUndersized);
                    break;
                }

                case ERROR_FILE_NOT_FOUND:
                {
                    THROW(StoreKeyNotFound);
                    break;
                }

                default:
                {
                    THROW(StoreKeyNotFound);
                    break;
                }
            }

            // Set the number of bytes copied into the buffer.
            aDest.SetBytes(bytes);
        }
    }
}

void ConfigRegStore::Write(const Brx& aKey, const Brx& aSource)
{
    AutoMutex a(iLock);

    if (GetAppRegistryKey())
    {
        LPWSTR keyString;

        keyString = BrxToWString(aKey);

        if (keyString != NULL)
        {
            // Create the key if required, setting it to the supplied
            // value.
            RegSetValueEx(iHk,
                          keyString,
                          0,
                          REG_BINARY,
                          (LPBYTE)aSource.Ptr(),
                          aSource.Bytes());

            delete[] keyString;
        }

        RegCloseKey(iHk);
    }
}

void ConfigRegStore::Delete(const Brx& aKey)
{
    AutoMutex a(iLock);

    if (GetAppRegistryKey())
    {
        LPWSTR keyString;

        keyString = BrxToWString(aKey);

        if (keyString != NULL)
        {
            LONG  retVal;

            // Attempt to delete the required key.
            retVal = RegDeleteKeyEx(iHk,
                                    keyString,
                                    KEY_WOW64_32KEY,
                                    0);

            RegCloseKey(iHk);
            delete[] keyString;

            // Throw exception if the deletion failed.
            if (retVal != ERROR_SUCCESS)
            {
                THROW(StoreKeyNotFound);
            }
        }
    }
}

void ConfigRegStore::ResetToDefaults()
{ }
