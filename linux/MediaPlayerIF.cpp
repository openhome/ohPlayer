
// Extras to try and fix compiler errors
// End of Extras

#ifdef USE_GTK
#include <gtk/gtk.h>
#else // USE_GTK
#include <glib.h>
#endif // USE_GDK
#include <unistd.h>

#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Media/Debug.h>

#include "ConfigGTKKeyStore.h"
#include "DriverAlsa.h"
#include "ExampleMediaPlayer.h"
#include "OpenHomePlayer.h"
#include "MediaPlayerIF.h"
#include "UpdateCheck.h"
#include "version.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

constexpr TIpAddress InitArgs::NO_SUBNET;
constexpr TIpAddress InitArgs::NO_SUBNET_V6;



static const TInt  TenSeconds    = 10;
static const TInt  FourHours     = 4 * 60 * 60;

static ExampleMediaPlayer* g_emp = NULL; // Example media player instance.
static Library*            g_lib = NULL; // Library instance.
static gint                g_tID = 0;

static Media::PriorityArbitratorDriver* g_arbDriver;
static Media::PriorityArbitratorPipeline* g_arbPipeline;

// Timed callback to initiate application update check.
static gint tCallback(gpointer data)
{
    gint      period = GPOINTER_TO_INT(data);
    Bws<1024> urlBuf;

    if (UpdateChecker::updateAvailable(g_emp->Env(), RELEASE_URL, urlBuf))
    {
        // There is an update available. Obtain the URL of the download
        // location and notify the user via a system tray notification.
        TChar *urlString = new TChar[urlBuf.Bytes() + 1];
        if (urlString)
        {
            memcpy((void *)urlString, (void *)(urlBuf.Ptr()), urlBuf.Bytes());
            urlString[urlBuf.Bytes()] = '\0';
        }

#ifdef USE_GTK
        gdk_threads_add_idle((GSourceFunc)updatesAvailable,
                             (gpointer)urlString);
#else // USE_GTK
        g_main_context_invoke(NULL,
                              (GSourceFunc)updatesAvailable,
                              (gpointer)urlString);
#endif // USE_GDK
    }

    if (period == TenSeconds)
    {
        if (g_tID == 0)
        {
            // Remove the existing update timer source.
            g_source_remove(g_tID);
        }

        // Schedule the next timeout for a longer period.
        g_tID = g_timeout_add_seconds(FourHours,
                                      tCallback,
                                      GINT_TO_POINTER(FourHours));


        // Terminate this timeout.
        return false;
    }

    return true;
}

// Media Player thread entry point.
void InitAndRunMediaPlayer(gpointer args)
{
    // Handle supplied arguments.
    InitArgs   *iArgs     = (InitArgs *)args;
    TBool       restarted = iArgs->restarted;       // MediaPlayer restarted ?
    TIpAddress  subnet    = iArgs->subnet;          // Preferred subnet.

    // Pipeline configuration.
    static const TChar *name  = "SoftPlayer";
    static char udn[1024];
    static char hostname[512];
    gethostname(hostname, 512);
    sprintf(udn, "PiPlayer-%s", hostname);
    static const TChar *cookie = "ExampleMediaPlayer";
    static const TChar *room  = hostname;
    NetworkAdapter *adapter = NULL;
    Net::CpStack   *cpStack = NULL;
    Net::DvStack   *dvStack = NULL;
    DriverAlsa     *driver  = NULL;
    Bws<512>        roomStore;
    Bws<512>        nameStore;
    const TChar    *productRoom = room;
    const TChar    *productName = name;

    Debug::SetLevel(Debug::kPipeline);
    Debug::SetLevel(Debug::kSongcast);
    //Debug::SetLevel(Debug::kError);

    // Create the library on the supplied subnet.
    g_lib  = ExampleMediaPlayerInit::CreateLibrary(subnet);
    if (g_lib == NULL)
    {
        return;
    }

    // create a read/write store using the new config framework
    ConfigGTKKeyStore *configStore = ConfigGTKKeyStore::getInstance();

    g_arbDriver = new Media::PriorityArbitratorDriver(kPrioritySystemHighest);
    ThreadPriorityArbitrator& priorityArbitrator = g_lib->Env().PriorityArbitrator();
    priorityArbitrator.Add(*g_arbDriver);
    g_arbPipeline = new Media::PriorityArbitratorPipeline(kPrioritySystemHighest-1);
    priorityArbitrator.Add(*g_arbPipeline);

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
        configStore->Read(Brn("Product.Room"), roomStore);
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
        configStore->Write(Brn("Product.Room"), Brn(productRoom));
    }

    // Set the default product name from any existing key in the
    // config store.
    try
    {
        configStore->Read(Brn("Product.Name"), nameStore);
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
        configStore->Write(Brn("Product.Name"), Brn(productName));
    }

    // Create the ExampleMediaPlayer instance.
    g_emp = new ExampleMediaPlayer(*dvStack, *cpStack, Brn(udn), productRoom, productName,
                                   Brx::Empty()/*aUserAgent*/);

    // Add the audio driver to the pipeline.
    //
    // The 22052ms value a is a bit of a magic number which get's
    // things going for the Hifiberry Digi+ card.
    //
    // FIXME This should be calculated.
    driver = new DriverAlsa(g_emp->Pipeline(), 22052);
    if (driver == NULL)
    {
        goto cleanup;
    }

    // Create the timeout for update checking.
    if (restarted)
    {
        // If we are restarting due to a user instigated subnet change we
        // don't want to recheck for updates so set the initial check to be
        // the timer period.
        g_tID = g_timeout_add_seconds(FourHours,
                                      tCallback,
                                      GINT_TO_POINTER(FourHours));
    }
    else
    {
        g_tID = g_timeout_add_seconds(TenSeconds,
                                      tCallback,
                                      GINT_TO_POINTER(TenSeconds));
    }

#ifdef USE_GTK
    // Add the network submenu to the application indicator context menu
    // now that the information is available.
    gdk_threads_add_idle((GSourceFunc)networkAdaptersAvailable, NULL);
#endif // USE_GTK

    /* Run the media player. (Blocking) */
    g_emp->RunWithSemaphore(*cpStack);

cleanup:
    /* Tidy up on exit. */

    if (g_tID != 0)
    {
        // Remove the update timer.
        g_source_remove(g_tID);
        g_tID = 0;
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

    delete g_arbDriver;
    delete g_arbPipeline;

    // Terminate the thread.
    g_thread_exit(NULL);
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
