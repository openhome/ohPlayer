#include "ExampleMediaPlayer.h"
#include "AudioDriver.h"
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Printer.h>

#include <process.h>
#include <propsys.h>
#include <Shobjidl.h>
#include <string.h>

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
using namespace OpenHome::Media;
using namespace OpenHome::Net;

static ExampleMediaPlayer* emp = nullptr; /* Test media player instance. */

// Register a custom property schema.
static bool RegisterSchema(PCWSTR pszFileName)
{
    HRESULT hr = PSRegisterPropertySchema(pszFileName);
    if (SUCCEEDED(hr))
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Remove a custom property schema.
static void UnregisterSchema(PCWSTR pszFileName)
{
    PSUnregisterPropertySchema(pszFileName);
}

// Get the property store for a specified file.
HRESULT GetPropertyStore(PCWSTR pszFilename,
                         GETPROPERTYSTOREFLAGS gpsFlags,
                         IPropertyStore** ppps)
{
    WCHAR szExpanded[MAX_PATH];
    HRESULT hr = ExpandEnvironmentStrings(pszFilename,
                                          szExpanded,
                                          ARRAYSIZE(szExpanded)) ? S_OK : HRESULT_FROM_WIN32(GetLastError());

    if (SUCCEEDED(hr))
    {
        WCHAR szAbsPath[MAX_PATH];
        hr = _wfullpath(szAbsPath,
                        szExpanded,
                        ARRAYSIZE(szAbsPath)) ? S_OK : E_FAIL;

        if (SUCCEEDED(hr))
        {
            hr = SHGetPropertyStoreFromParsingName(szAbsPath,
                                                   NULL,
                                                   gpsFlags,
                                                   IID_PPV_ARGS(ppps));
        }
    }

    return hr;
}

// Return the required property value as a string.
TChar *GetPropertyString(IPropertyStore *pps, REFPROPERTYKEY key)
{
    TChar *retVal            = NULL;
    PROPVARIANT propvarValue = {0};
    HRESULT hr               = pps->GetValue(key, &propvarValue);

    if (SUCCEEDED(hr))
    {
        const size_t bufferLen = 256;
        PWSTR pszStringValue   = NULL;

        hr = PSFormatForDisplayAlloc(key,
                                     propvarValue,
                                     PDFF_DEFAULT,
                                     &pszStringValue);

        if (SUCCEEDED(hr))
        {
            size_t theSize;

            // Allocate a buffer to hold the property.
            retVal = (TChar *)malloc(bufferLen);

            if (retVal != NULL)
            {
                if (wcstombs_s(&theSize,
                               retVal,
                               bufferLen,
                               pszStringValue,
                               bufferLen-1) != 0)
                {
                    free(retVal);
                    retVal = NULL;
                }
            }

            CoTaskMemFree(pszStringValue);
        }

        PropVariantClear(&propvarValue);

        return retVal;
    }

    return NULL;
}

TChar *GetPropertyValue(PCWSTR pszFilename, PCWSTR pszCanonicalName)
{
    TChar *retVal = NULL;

    // Convert the Canonical name of the property to PROPERTYKEY
    PROPERTYKEY key;
    HRESULT hr = PSGetPropertyKeyFromName(pszCanonicalName, &key);

    if (SUCCEEDED(hr))
    {
        IPropertyStore* pps = NULL;

        // Call the helper to get the property store for the initialized item
        hr = GetPropertyStore(pszFilename, GPS_DEFAULT, &pps);
        if (SUCCEEDED(hr))
        {
            retVal = GetPropertyString(pps, key);
            pps->Release();

            return retVal;
        }
    }

    return NULL;
}

DWORD WINAPI InitAndRunMediaPlayer( LPVOID /*lpParam*/ )
{
    /* Pipeline configuration. */
    TChar *aRoom     = "ExampleTestRoom";
    TChar *aUdn      = NULL;
    TChar *aName     = "";

    const TChar* cookie ="ExampleMediaPlayer";

    Library            *lib     = nullptr;
    NetworkAdapter     *adapter = nullptr;
    Net::DvStack       *dvStack = nullptr;
    AudioDriver        *driver  = nullptr;
    Bwh udn;

    if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
    {
        Log::Print("Can't up the process priority of InitAndRunMediaPlayer "
                   "\n");
    }

    // Read persistent configuration from the application property store,

    // Initialise COM
    HRESULT hr = CoInitializeEx(NULL,
                                COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);

    if (SUCCEEDED(hr))
    {
        // Register the custom property schema used for persistent data purposes.
        if (RegisterSchema(L"LitePipeTestApp.propdesc"))
        {
            // Read configuration.
            aUdn = GetPropertyValue(L"Config.docx", L"Linn.LitePipeTestApp.UDN");

            // Unregister schema
            UnregisterSchema(L"LitePipeTestApp.propdesc");
        }

        // Close COM
        CoUninitialize();
    }

    // Allocate a dummy UDN if none populated from the property store
    if (aUdn == NULL)
    {
        aUdn = _strdup("ExampleDevice");
    }

    // Create lib.
    lib  = ExampleMediaPlayerInit::CreateLibrary();
    if (lib == nullptr)
    {
        return 1;
    }

    adapter = lib->CurrentSubnetAdapter(cookie);
    if (adapter == nullptr)
    {
        goto cleanup;
    }

    dvStack = lib->StartDv();
    if (dvStack == nullptr)
    {
        goto cleanup;
    }

    adapter->RemoveRef(cookie);

    emp = new ExampleMediaPlayer(*dvStack, Brn(aUdn), aRoom, aName,
                                  Brx::Empty()/*aUserAgent*/);

    // Create ExampleMediaPlayer.
    driver = new AudioDriver(dvStack->Env(), emp->Pipeline());
    if (driver == nullptr)
    {
        goto cleanup;
    }

    /* Run the media player. (Blocking) */
    emp->RunWithSemaphore();

cleanup:
    /* Tidy up on exit. */

    free(aUdn);

    if (driver != nullptr)
    {
        delete driver;
    }

    if (dvStack == nullptr)
    {
        /* Freeing dvStack causes compiler error. Maybe one to mention to
         * the customer. */
        //delete dvStack;
    }

    if (emp != nullptr)
    {
        delete emp;
    }

    if (lib != nullptr)
    {
        delete lib;
    }

    return 1;
}

void ExitMediaPlayer()
{
    if (emp != nullptr)
    {
        emp->StopPipeline();
    }
}

void PipeLinePlay()
{
    if (emp != nullptr)
    {
        emp->PlayPipeline();
    }
}

void PipeLinePause()
{
    if (emp != nullptr)
    {
        emp->PausePipeline();
    }
}

void PipeLineStop()
{
    if (emp != nullptr)
    {
        emp->HaltPipeline();
    }
}
