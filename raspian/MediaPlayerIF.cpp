
// Extras to try and fix compiler errors
// End of Extras

#include <gtk/gtk.h>

#include "MediaPlayerIF.h"
#include "LitePipeTestApp.h"

#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Printer.h>

// Intermin includes
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Media/Utils/DriverBasic.h>
// End Df Interim

#include "ExampleMediaPlayer.h"
#if 0
#include "AudioDriver.h"
#include "CustomMessages.h"
#include "MediaPlayerIF.h"
#include "MemoryCheck.h"
#include "UpdateCheck.h"
#else
#endif

#if 0
// Media Player thread entry point.
void InitAndRunMediaPlayer()
{
    int i;

    for (i = 0; i < 10; i++)
    {
      usleep(100000); /* 0.1 s */
      gdk_threads_add_idle((GSourceFunc)updateUI, NULL);
    }

    /* Make sure this thread is joined properly */
    gdk_threads_add_idle((GSourceFunc)terminate, g_thread_self());
}
#endif

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

#if 0
// Location of application release JSON file.
static const TChar RELEASE_URL[] = "http://elmo/~alans/application.json";

static ExampleMediaPlayer* g_emp = NULL; // Example media player instance.
#endif
static Library*            g_lib = NULL; // Library instance.

#if 0
// Timed callback to initiate application update check.
static VOID CALLBACK TimerRoutine(PVOID lpParam, BOOLEAN /*TimerOrWaitFired*/)
{
    Bws<1024> urlBuf;
    HWND hwnd = (HWND)(lpParam);

    if (UpdateChecker::updateAvailable(g_emp->Env(), RELEASE_URL, urlBuf))
    {
        // There is an update available. Obtain the URL of the download
        // location and notify the user via a system tray notification.
        TChar *urlString = new TChar[urlBuf.Bytes() + 1];
        if (urlString)
        {
            CopyMemory(urlString, urlBuf.Ptr(), urlBuf.Bytes());
            urlString[urlBuf.Bytes()] = '\0';
        }

        PostMessage(hwnd, WM_APP_UPDATE_AVAILABLE, NULL, (LPARAM)urlString);
    }
}
#endif

// Media Player thread entry point.
void InitAndRunMediaPlayer(gpointer args)
{
    // Handle supplied arguments.
    InitArgs   *iArgs   = (InitArgs *)args;
    //HWND        hwnd   = args->hwnd;            // Main window handle.
    TIpAddress  subnet = iArgs->subnet;          // Preferred subnet.

    // Pipeline configuration.
    static const TChar *aRoom  = "ExampleTestRoom";
    static const TChar *aName  = "ExamplePlayer";
    static const TChar *aUdn   = "ExampleDevice";
    static const TChar *cookie = "ExampleMediaPlayer";

    NetworkAdapter *adapter = NULL;
    Net::CpStack   *cpStack = NULL;
    Net::DvStack   *dvStack = NULL;
    //AudioDriver    *driver  = NULL;
    DriverBasic    *driver  = NULL;

#if 0
    HANDLE hTimer      = NULL;
    HANDLE hTimerQueue = NULL;

    // Assign the maximum priority for this process.
    if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
    {
        Log::Print("Can't up the process priority of InitAndRunMediaPlayer "
                   "\n");
    }
#endif

    // Create the library on the supplied subnet.
    g_lib  = ExampleMediaPlayerInit::CreateLibrary(subnet);
    if (g_lib == NULL)
    {
        return;
    }

#if 0
    // Get the current network adapter.
    adapter = g_lib->CurrentSubnetAdapter(cookie);
    if (adapter == NULL)
    {
        //goto cleanup;
    }

    // Start a control point and dv stack.
    //
    // The control point will be used for playback control.
    g_lib->StartCombined(adapter->Subnet(), cpStack, dvStack);

    adapter->RemoveRef(cookie);

    // Create the ExampleMediaPlayer instance.
    g_emp = new ExampleMediaPlayer(hwnd, *dvStack, Brn(aUdn), aRoom, aName,
                                   Brx::Empty()/*aUserAgent*/);

    // Add the audio driver to the pipeline.
    driver = new AudioDriver(dvStack->Env(), g_emp->Pipeline(), hwnd);
    if (driver == NULL)
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
    g_emp->RunWithSemaphore(*cpStack);

cleanup:
    /* Tidy up on exit. */

    if (hTimerQueue != NULL)
    {
        // Delete all timers in the timer queue.
        DeleteTimerQueue(hTimerQueue);
    }

    if (driver != NULL)
    {
        delete driver;
    }

    if (g_emp != NULL)
    {
        delete g_emp;
    }

    if (g_lib != NULL)
    {
        delete g_lib;
    }

    return 1;
#endif
}

#if 0
void ExitMediaPlayer()
{
    if (g_emp != NULL)
    {
        g_emp->StopPipeline();
    }
}

void PipeLinePlay()
{
    if (g_emp != NULL)
    {
        g_emp->PlayPipeline();
    }
}

void PipeLinePause()
{
    if (g_emp != NULL)
    {
        g_emp->PausePipeline();
    }
}

void PipeLineStop()
{
    if (g_emp != NULL)
    {
        g_emp->HaltPipeline();
    }
}
#endif

// Create a subnet menu vector containing network adaptor and associate
// subnet information.
std::vector<SubnetRecord*> *GetSubnets()
{
//    if (g_emp != NULL)
    {
        // Obtain a reference to the current active network adapter.
        const TChar    *cookie  = "GetSubnets";
        NetworkAdapter *adapter = NULL;

        adapter = g_lib->CurrentSubnetAdapter(cookie);

        // Obtain a list of available network adapters.
        std::vector<NetworkAdapter*>* subnetList = g_lib->CreateSubnetList();

        if (subnetList->size() == 0)
        {
            return NULL;
        }

        std::vector<SubnetRecord*> *subnetVector =
            new std::vector<SubnetRecord*>;

        for (unsigned i=0; i<subnetList->size(); ++i)
        {
            SubnetRecord *subnetEntry = new SubnetRecord;

            if (subnetEntry == NULL)
            {
                break;
            }

            // Get a string containing ip address and adapter name and store
            // it in our vector element.
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

            // Add the entry to the vector.
            subnetVector->push_back(subnetEntry);
        }

        // Free up the resources allocated by CreateSubnetList().
        Library::DestroySubnetList(subnetList);

        if (adapter != NULL)
        {
            adapter->RemoveRef(cookie);
        }

        return subnetVector;
    }

    return NULL;
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
