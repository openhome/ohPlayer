#include "ExampleMediaPlayer.h"
#include "AudioDriver.h"
#include "CustomMessages.h"
#include "UpdateCheck.h"
#include "MediaPlayerIF.h"
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
static Library*            lib = nullptr;

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
    // Handle supplied arguements.
    InitArgs   *args   = (InitArgs *)lpParam;
    HWND        hwnd   = args->hwnd;
    TIpAddress  subnet = args->subnet;

    // Pipeline configuration.
    TChar *aRoom     = "ExampleTestRoom";
    TChar *aName     = "ExamplePlayer";
    TChar *aUdn      = "ExampleDevice";

    const TChar* cookie ="ExampleMediaPlayer";

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

    // Create lib on the supplied subnet.
    lib  = ExampleMediaPlayerInit::CreateLibrary(subnet);
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
    emp = new ExampleMediaPlayer(hwnd, *dvStack, Brn(aUdn), aRoom, aName,
                                 Brx::Empty()/*aUserAgent*/);

    driver = new AudioDriver(dvStack->Env(), emp->Pipeline(), hwnd);
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
        static const DWORD TenSeconds = 10 * 1000;
        static const DWORD FourHours  = 4 * 60 * 60 * 1000;

        DWORD initialTimeout = TenSeconds;

        if (subnet != InitArgs::NO_SUBNET)
        {
            // If we are restarting due to a user instigated subnet change we
            // don't want to recheck for updates so set the initial check to be
            // the timer period.
            initialTimeout = FourHours;
        }

        if (!CreateTimerQueueTimer(&hTimer, hTimerQueue,
                (WAITORTIMERCALLBACK)TimerRoutine, hwnd,
                initialTimeout, FourHours, 0))
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

// Free up resources allocated to a subnet menu vector.
void FreeSubnets(std::vector<SubnetRecord*> *subnetVector)
{
    std::vector<SubnetRecord*>::iterator it;

    for (it=subnetVector->begin(); it < subnetVector->end(); it++)
    {
        delete (*it)->menuString;
        delete *it;
    }

    delete subnetVector;
}

// Cerate a subnet menu vector.
std::vector<SubnetRecord*> *GetSubnets()
{
    if (emp != nullptr)
    {
        // Obtain a refernce to the current active network adapter.
        const TChar    *cookie  = "GetSubnets";
        NetworkAdapter *adapter = nullptr;

        adapter = lib->CurrentSubnetAdapter(cookie);

        // Obtain a list of available network adapters.
        std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();

        if (subnetList->size() == 0) {
            return NULL;
        }

        std::vector<SubnetRecord*> *subnetVector =
            new std::vector<SubnetRecord*>;

        for (unsigned i=0; i<subnetList->size(); ++i) {
            SubnetRecord *subnetEntry = new SubnetRecord;

            if (subnetEntry == NULL)
            {
                break;
            }

            // Get a string containing ip address and adapter name and store
            // it in our vectoe element.
            TChar *fullName = (*subnetList)[i]->FullName();

            subnetEntry->menuString = new std::string(fullName);

            delete fullName;

            if (subnetEntry->menuString == NULL)
            {
                delete subnetEntry;
                break;
            }

            // Store the subnet address the adapter attaches to in our vector
            // element.
            subnetEntry->subnet = (*subnetList)[i]->Subnet();

            // Note if this is the current active subnet.
            if ((*subnetList)[i] == adapter)
            {
                subnetEntry->isCurrent = true;
            }
            else
            {
                subnetEntry->isCurrent = false;
            }

            subnetVector->push_back(subnetEntry);
        }

        Library::DestroySubnetList(subnetList);

        if (adapter != NULL) {
            adapter->RemoveRef(cookie);
        }

        return subnetVector;
    }

    return NULL;
}
