#include "ExampleMediaPlayer.h"
#include "AudioDriver.h"
#include "CustomMessages.h"
#include "UpdateCheck.h"
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Printer.h>

#include <process.h>

#include <Windows.h>

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

static ExampleMediaPlayer* emp = nullptr; // Test media player instance.

// Timed callback to initiate application update check.
static VOID CALLBACK TimerRoutine(PVOID lpParam, BOOLEAN /*TimerOrWaitFired*/)
{
    HWND hwnd = (HWND)(lpParam);

    Bws<1024> urlBuf;
    if (UpdateChecker::updateAvailable(emp->Env(),
                                       "http://elmo/~alans/application.json",
                                       urlBuf))
    {
        // There is an update available. Obtain the URL of the download
        // and notify the user via a system tray notification.
        TChar *urlString = new TChar[urlBuf.Bytes() + 1];
        if (urlString)
        {
            CopyMemory(urlString, urlBuf.Ptr(), urlBuf.Bytes());
            urlString[urlBuf.Bytes()] = '\0';
        }

        PostMessage(hwnd, WM_APP_UPDATE_AVAILABLE, NULL, (LPARAM)urlString);
    }
}

// Read the string associated with the supplied registry key.
//
// If the key doesn't exists it will be created and initialised to the default
// value.
//
// On success the key value is returned as a narrow string, on failure NULL
// is returned.
TChar * GetRegistryString(HKEY    hk,
                          LPCWSTR keyName,
                          LPCWSTR defVal,
                          size_t  defBufSize)
{
    LONG   retVal;
    DWORD  lpType;
    DWORD  tmpBufSize;
    TByte  tmpBuf[256];
    TChar *retBuf;
    size_t retBufSize;
    size_t cnt;

    tmpBufSize = sizeof(tmpBuf);

    // Attempt to read the required key value.
    retVal = RegQueryValueEx(hk,
                             keyName,
                             NULL,
                             (LPDWORD)&lpType,
                             (LPBYTE)tmpBuf,
                             (LPDWORD)&tmpBufSize);

    // For the query to be valid it must succeed and the returned data
    // be of the expected type.
    if ((retVal != ERROR_SUCCESS) || (lpType != REG_SZ))
    {
        TBool unhandldedErr = false;

        // Check for non-existent key
        if (retVal == ERROR_FILE_NOT_FOUND)
        {
            // Create the key, setting it to the supplied default value.
            RegSetValueEx(hk,
                          keyName,
                          0,
                          REG_SZ,
                          (LPBYTE)defVal,
                          defBufSize);
        }
        else
        {
            unhandldedErr = true;
        }

        // Convert the default value to a narrow string and return it.
        retBufSize = wcslen(defVal)+1;
        retBuf     = new TChar[retBufSize];

        if (retBuf == NULL)
        {
            return NULL;
        }

        LONG retVal1 = wcstombs_s(&cnt, retBuf, retBufSize,
                                  defVal, wcslen(defVal));

        if (retVal1 != 0)
        {
            delete retBuf;
            return NULL;
        }

        if (unhandldedErr)
        {
            Log::Print("[GetRegistryString]: Cannot obtain value of '%s' "
                       "registry key. Error [%d]\n", retBuf, retVal);
        }

        Log::Print("ALDO: UDN: %s\n", retBuf);

        return retBuf;
    }

    // Convert the wide string retrieved from the registry to a narrow string.
    //
    // We have already verified the value type is REG_SZ, so we can rely
    // on the data being a null terminated string.
    retBufSize = wcslen((LPWSTR)tmpBuf) + 1;
    retBuf     = new TChar[retBufSize];

    if (retBuf == NULL)
    {
        return NULL;
    }

    retVal = wcstombs_s(&cnt, retBuf, retBufSize,
                        (LPWSTR)tmpBuf, wcslen((LPWSTR)tmpBuf));

    if (retVal != 0)
    {
        delete retBuf;
        return NULL;
    }

    Log::Print("ALDO: UDN: %s\n", retBuf);

    return retBuf;
}

DWORD WINAPI InitAndRunMediaPlayer( LPVOID lpParam )
{
    /* Pipeline configuration. */
    TChar *aRoom     = "ExampleTestRoom";
    TChar *aName     = "";
    TChar *aUdn      = NULL;

    const TChar* cookie ="ExampleMediaPlayer";

    Library            *lib         = nullptr;
    NetworkAdapter     *adapter     = nullptr;
    Net::CpStack       *cpStack     = nullptr;
    Net::DvStack       *dvStack     = nullptr;
    AudioDriver        *driver      = nullptr;

    Bwh udn;

    HANDLE hTimer = NULL;
    HANDLE hTimerQueue = NULL;

    if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
    {
        Log::Print("Can't up the process priority of InitAndRunMediaPlayer "
                   "\n");
    }

    // Initialise UDN.

    HKEY hk;

    // Obtain a handle to our application key.
    //
    // This will be created if one doesn't already exist.
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       L"Software\\Linn\\LitePipeTestApp",
                               0,
                               NULL,
                               REG_OPTION_NON_VOLATILE,
                               KEY_ALL_ACCESS,
                               NULL,
                               &hk,
                               NULL) == ERROR_SUCCESS)
    {
        WCHAR defaultUdn[] = L"ExampleDevice";

        // Read configuration key data from the registry.
        aUdn = GetRegistryString(hk, L"UDN",
                                 defaultUdn, sizeof(defaultUdn));

        RegCloseKey(hk);

        if (aUdn == NULL)
        {
            return 1;
        }
    }
    else
    {
        Log::Print("ERROR: Cannot obtain registry key\n");
        return 1;
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


    lib->StartCombined(adapter->Subnet(), cpStack, dvStack);

    adapter->RemoveRef(cookie);

    // Create ExampleMediaPlayer.
    emp = new ExampleMediaPlayer(lpParam, *dvStack, Brn(aUdn), aRoom, aName,
                                  Brx::Empty()/*aUserAgent*/);

    driver = new AudioDriver(dvStack->Env(), emp->Pipeline(), lpParam);
    if (driver == nullptr)
    {
        goto cleanup;
    }

    // Create the timer queue for update checking.
    hTimerQueue = CreateTimerQueue();
    if (NULL == hTimerQueue)
    {
        Log::Print("CreateTimerQueue failed (%d)\n", GetLastError());
    }
    else
    {
        // Set a timer to call the timer routine in 10 seconds then at
        // 4 hour intervals..
        if (!CreateTimerQueueTimer( &hTimer, hTimerQueue,
                (WAITORTIMERCALLBACK)TimerRoutine, lpParam,
                10 * 1000,  4 * 60 * 60 * 1000, 0))
        {
            Log::Print("CreateTimerQueueTimer failed (%d)\n", GetLastError());
        }
    }

    /* Run the media player. (Blocking) */
    emp->RunWithSemaphore(*cpStack);

cleanup:
    /* Tidy up on exit. */

    delete aUdn;

    if (hTimerQueue != NULL)
    {
        // Delete all timers in the timer queue.
        DeleteTimerQueue(hTimerQueue);
    }

    if (driver != nullptr)
    {
        delete driver;
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
