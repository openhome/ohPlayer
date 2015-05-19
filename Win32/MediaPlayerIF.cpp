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

DWORD WINAPI InitAndRunMediaPlayer( LPVOID lpParam )
{
    /* Pipeline configuration. */
    TChar *aRoom     = "ExampleTestRoom";
    TChar *aName     = "ExamplePlayer";
    TChar *aUdn      = "ExampleDevice";

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
