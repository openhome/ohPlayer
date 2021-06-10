#include <process.h>
#include <Windows.h>

#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Printer.h>

#include "ExampleMediaPlayer.h"
#include "AudioDriver.h"
#include "ConfigRegStore.h"
#include "CustomMessages.h"
#include "MediaPlayerIF.h"
#include "MemoryCheck.h"
#include "UpdateCheck.h"
#include "version.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

static ExampleMediaPlayer* g_emp = NULL; // Example media player instance.
static Library*            g_lib = NULL; // Library instance.

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

// Media Player thread entry point.
DWORD WINAPI InitAndRunMediaPlayer( LPVOID lpParam )
{
    // Handle supplied arguments.
    InitArgs   *args   = (InitArgs *)lpParam;
    HWND        hwnd   = args->hwnd;            // Main window handle.
    TIpAddress  subnet = args->subnet;          // Preferred subnet.

    // Pipeline configuration.
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = sizeof(computerName)/sizeof(computerName[0]);
    if (!GetComputerNameA(computerName, &len))
    {
        return 0;
    }
    const TChar        *room  = computerName;
    static const TChar *name  = "SoftPlayer";
    char udn[1024];
    strcpy_s(udn, "WinPlayer-");
    strcat_s(udn, computerName);
    static const TChar *cookie = "ExampleMediaPlayer";

    Bws<512>        roomStore;
    Bws<512>        nameStore;
    const TChar    *productRoom = room;
    const TChar    *productName = name;

    NetworkAdapter *adapter = NULL;
    Net::CpStack   *cpStack = NULL;
    Net::DvStack   *dvStack = NULL;
    AudioDriver    *driver  = NULL;

    HANDLE hTimer      = NULL;
    HANDLE hTimerQueue = NULL;

    // Assign the maximum priority for this process.
    if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
    {
        Log::Print("Can't up the process priority of InitAndRunMediaPlayer "
                   "\n");
    }

    // Create the library on the supplied subnet.
    g_lib  = ExampleMediaPlayerInit::CreateLibrary(subnet);
    if (g_lib == NULL)
    {
        return 1;
    }

    // create a read/write store using the new config framework
    Configuration::ConfigRegStore configStore;

    // Get the current network adapter.
    adapter = g_lib->CurrentSubnetAdapter(cookie);
    if (adapter == NULL)
    {
        goto cleanup;
    }

    // Start a control point and dv stack.
    //
    // The control point will be used for playback control.
    g_lib->StartCombined(adapter->Subnet(), cpStack, dvStack);

    adapter->RemoveRef(cookie);

    // Set the default room name from any existing key in the
    // config store.
    try
    {
        configStore.Read(Brn("Product.Room"), roomStore);
        productRoom = roomStore.PtrZ();
    }
    catch (StoreReadBufferUndersized)
    {
        Log::Print("Error: MediaPlayerIF: 'productRoom' too short\n");
    }
    catch (StoreKeyNotFound)
    {
        // If no key exists use the hard coded room name and set it
        // in the config store.
        configStore.Write(Brn("Product.Room"), Brn(productRoom));
    }

    // Set the default product name from any existing key in the
    // config store.
    try
    {
        configStore.Read(Brn("Product.Name"), nameStore);
        productName = nameStore.PtrZ();
    }
    catch (StoreReadBufferUndersized)
    {
        Log::Print("Error: MediaPlayerIF: 'productName' too short\n");
    }
    catch (StoreKeyNotFound)
    {
        // If no key exists use the hard coded product name and set it
        // in the config store.
        configStore.Write(Brn("Product.Name"), Brn(productName));
    }

    // Create the ExampleMediaPlayer instance.
    g_emp = new ExampleMediaPlayer(hwnd, *dvStack, *cpStack, Brn(udn), productRoom,
                                   productName, Brx::Empty()/*aUserAgent*/);

    // ~~ DEBUG
#pragma warning(push)
#pragma warning(disable : 4239)
    g_lib->Env()
          .Logger()
          .SwapOutput(MakeFunctorMsg(*g_emp, &ExampleMediaPlayer::DebugLogOutput));
#pragma warning(pop)


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

        const TBool areSubnetsEqual = subnet.iFamily == kFamilyV4 ? CompareIPv4Addrs(subnet, InitArgs::NO_SUBNET)
                                                                  : CompareIPv6Addrs(subnet, InitArgs::NO_SUBNET_V6);

        if (areSubnetsEqual)
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

    delete driver;
    delete g_emp;
    delete g_lib;

    return 1;
}

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

// Create a subnet menu vector containing network adaptor and associate
// subnet information.
std::vector<SubnetRecord*> *GetSubnets()
{
    if (g_emp != NULL)
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

            free(fullName);

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
