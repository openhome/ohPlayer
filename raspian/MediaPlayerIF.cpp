
// Extras to try and fix compiler errors
// End of Extras

#include <gtk/gtk.h>

#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Printer.h>

#include "DriverAlsa.h"
#include "ExampleMediaPlayer.h"
#include "LitePipeTestApp.h"
#include "MediaPlayerIF.h"
#include "UpdateCheck.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// Location of application release JSON file.
static const TChar RELEASE_URL[] = "http://elmo/~alans/application.json";
static const TInt  TenSeconds    = 10;
static const TInt  FourHours     = 4 * 60 * 60;

static ExampleMediaPlayer* g_emp = NULL; // Example media player instance.
static Library*            g_lib = NULL; // Library instance.
static gint                g_tID = 0;

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

        gdk_threads_add_idle((GSourceFunc)updatesAvailable,
                             (gpointer)urlString);
    }

    if (period == TenSeconds)
    {
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
    InitArgs   *iArgs   = (InitArgs *)args;
    TIpAddress  subnet = iArgs->subnet;          // Preferred subnet.

    // Pipeline configuration.
    static const TChar *aRoom  = "ExampleTestRoom";
    static const TChar *aName  = "ExamplePlayer";
    static const TChar *aUdn   = "ExampleDevice";
    static const TChar *cookie = "ExampleMediaPlayer";

    NetworkAdapter *adapter = NULL;
    Net::CpStack   *cpStack = NULL;
    Net::DvStack   *dvStack = NULL;
    DriverAlsa     *driver  = NULL;

    // Create the library on the supplied subnet.
    g_lib  = ExampleMediaPlayerInit::CreateLibrary(subnet);
    if (g_lib == NULL)
    {
        return;
    }

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

    // Create the ExampleMediaPlayer instance.
    g_emp = new ExampleMediaPlayer(*dvStack, Brn(aUdn), aRoom, aName,
                                   Brx::Empty()/*aUserAgent*/);

    // Add the audio driver to the pipeline.
    driver = new DriverAlsa(g_emp->Pipeline(), 25000);
    if (driver == NULL)
    {
        goto cleanup;
    }

    // Create the timeout for update checking.
    if (subnet != InitArgs::NO_SUBNET)
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
