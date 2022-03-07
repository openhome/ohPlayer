//
//  MediaPlayerIF.cpp
//  Sample Mediaplayer
//
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#include "MediaPlayerIF.h"
#include "UpdateCheck.h"
#import "ConfigPersistentStore.h"
#include <OpenHome/Media/Pipeline/Pipeline.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Example;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

static NSString *const kUpdateUri = @RELEASE_URL;

// Helpers
static TBool CompareIPv4Addrs(const TIpAddress addr1,
                              const TIpAddress addr2)
{
    return addr1.iFamily == kFamilyV4
        && addr2.iFamily == kFamilyV4
        && addr1.iV4 == addr2.iV4;
}

static TBool CompareIPv6Addrs(const TIpAddress addr1,
                              const TIpAddress addr2)
{
    return addr1.iFamily == kFamilyV6
        && addr2.iFamily == kFamilyV6
        && memcmp((TByte*)addr1.iV6[0], (TByte*)addr2.iV6[0], 16) == 0;
}



MediaPlayerIF::MediaPlayerIF(TIpAddress subnet)
{
#ifdef DEBUG
//    Debug::SetLevel(Debug::kBonjour);
//    Debug::SetLevel(Debug::kDvDevice);
//    Debug::SetLevel(Debug::kError);
    //Debug::SetLevel(Debug::kHttp);
#endif  // DEBUG

    // set up our media player
    setup(subnet);
}

MediaPlayerIF::~MediaPlayerIF()
{
    shutdown();
}

TChar * MediaPlayerIF::checkForUpdate(TUint major,
                                      TUint minor,
                                      const TChar *currentVersion)
{
    Bws<1024> urlBuf;

    if (UpdateChecker::updateAvailable(iDvStack->Env(),
                                       [kUpdateUri cStringUsingEncoding:NSASCIIStringEncoding],
                                       urlBuf,
                                       currentVersion,
                                       major,
                                       minor))
    {
        // There is an update available. Obtain the URL of the download
        // and notify the user via a system tray notification.
        TChar *urlString = new TChar[urlBuf.Bytes() + 1];
        if (urlString)
        {
            memcpy(urlString, urlBuf.Ptr(), urlBuf.Bytes());
            urlString[urlBuf.Bytes()] = '\0';
            return urlString;
        }
    }

    return nil;
}

// ExampleMediaPlayerInit

OpenHome::Net::Library* MediaPlayerIF::CreateLibrary(TIpAddress preferredSubnet)
{
    TUint                 index         = 0;
    InitialisationParams *initParams    = InitialisationParams::Create();
    TIpAddress            lastSubnet    = InitArgs::NO_SUBNET;
    const TChar          *lastSubnetStr = "Subnet.LastUsed";

    //initParams->SetDvEnableBonjour();

    Net::Library* lib = new Net::Library(initParams);
    iArbDriver = new Media::PriorityArbitratorDriver(kPrioritySystemHighest);
    ThreadPriorityArbitrator& priorityArbitrator = lib->Env().PriorityArbitrator();
    priorityArbitrator.Add(*iArbDriver);
    iArbPipeline = new Media::PriorityArbitratorPipeline(kPrioritySystemHighest-1);
    priorityArbitrator.Add(*iArbPipeline);

    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();

    if (subnetList->size() == 0)
    {
        Log::Print("ERROR: No adapters found\n");
        ASSERTS();
    }

    Configuration::ConfigPersistentStore iConfigPersistentStore;

    // Check the configuration store for the last subnet joined.
    try
    {
        Bwn lastSubnetBuf = Bwn((TByte *)&lastSubnet, sizeof(lastSubnet));

        iConfigPersistentStore.Read(Brn(lastSubnetStr), lastSubnetBuf);
    }
    catch (StoreKeyNotFound&)
    {
        // No previous subnet stored.
    }
    catch (StoreReadBufferUndersized&)
    {
        // This shouldn't happen.
        Log::Print("ERROR: Invalid 'Subnet.LastUsed' property in Config "
                   "Store\n");
    }

    for (TUint i=0; i<subnetList->size(); ++i)
    {
        TIpAddress subnet = (*subnetList)[i]->Subnet();

        // If the requested subnet is available, choose it.
        const TBool isPreferredSubnet = preferredSubnet.iFamily == kFamilyV4 ? CompareIPv4Addrs(preferredSubnet, subnet)
                                                                             : CompareIPv6Addrs(preferredSubnet, subnet);
        
        if (isPreferredSubnet)
        {
            index = i;
            break;
        }

        // If the last used subnet is available, note it.
        // We'll fall back to it if the requested subnet is not available.
        const TBool isLastSubnet = lastSubnet.iFamily == kFamilyV4 ? CompareIPv4Addrs(lastSubnet, subnet)
                                                                   : CompareIPv6Addrs(lastSubnet, subnet);
        
        if (isLastSubnet)
        {
            index = i;
        }
    }

    // Choose the required adapter.
    TIpAddress subnet = (*subnetList)[index]->Subnet();

    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);

    // Store the selected subnet in persistent storage.
    iConfigPersistentStore.Write(Brn(lastSubnetStr),
                          Brn((TByte *)&subnet, sizeof(subnet)));

    if (subnet.iFamily == kFamilyV4)
    {
        Log::Print("Using Subnet %d.%d.%d.%d\n", subnet.iV4 & 0xff,
                                                 (subnet.iV4 >> 8) & 0xff,
                                                 (subnet.iV4 >> 16) & 0xff,
                                                 (subnet.iV4 >> 24) & 0xff);
    }
    else
    {
        Log::Print("Using Subnet: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
                   subnet.iV6[0], subnet.iV6[1],
                   subnet.iV6[2], subnet.iV6[3],
                   subnet.iV6[4], subnet.iV6[5],
                   subnet.iV6[6], subnet.iV6[7],
                   subnet.iV6[8], subnet.iV6[9],
                   subnet.iV6[10], subnet.iV6[11],
                   subnet.iV6[12], subnet.iV6[13],
                   subnet.iV6[14], subnet.iV6[15]);
    }
    
    return lib;

}


TBool MediaPlayerIF::setup (TIpAddress subnet)
{

    // Pipeline configuration.
    NSString *computerName = [[NSHost currentHost] name];
    const TChar *room  = [computerName cStringUsingEncoding:NSUTF8StringEncoding];
    const TChar *name  = "SoftPlayer-OSX";
    NSString *udn = [NSString stringWithFormat:@"OsxPlayer-%@",computerName];
    const TChar *cookie = "OSXMediaPlayer";

    Bws<512>        roomStore;
    Bws<512>        nameStore;
    const TChar    *productRoom = room;
    const TChar    *productName = name;



    // Create the library on the supplied subnet.
    iLib = CreateLibrary(subnet);
    if (!iLib)
        return false;

    Configuration::ConfigPersistentStore configStore;

    // Get the current network adapter.
    iAdapter = iLib->CurrentSubnetAdapter(cookie);
    if (!iAdapter)
        goto cleanup;

    iLib->StartCombined(iAdapter->Subnet(), iCpStack, iDvStack);

    iAdapter->RemoveRef(cookie);

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

    // Create MediaPlayer.
    iDriver = nil;
    iExampleMediaPlayer = new ExampleMediaPlayer(*iDvStack,
                                                 *iCpStack,
                                                 Brn([udn cStringUsingEncoding:NSUTF8StringEncoding]),
                                                 room,
                                                 name,
                                                 Brx::Empty()/*aUserAgent*/);
    if(iExampleMediaPlayer == nil)
        goto cleanup;

    iDriver = new DriverOsx(iDvStack->Env(), iExampleMediaPlayer->Pipeline());
    if (iDriver == nil)
        goto cleanup;

    // now that we've created the media player (and volume control), hook
    // it up to the host audio driver
    iExampleMediaPlayer->VolumeControl().SetHost(iDriver);

    // and hook up the mediaplayer to the host for control of host audio queue state
    iExampleMediaPlayer->SetHost(iDriver);

    iExampleMediaPlayer->Run(*iCpStack);

    return true;

cleanup:
    delete iExampleMediaPlayer;
    delete iDriver;
    delete iLib;

    return false;
}

// Create a subnet menu vector containing network adaptor and associate
// subnet information.
std::vector<OpenHome::Av::Example::MediaPlayerIF::SubnetRecord*> * MediaPlayerIF::GetSubnets()
{
    if ( iExampleMediaPlayer != NULL)
    {
        // Obtain a reference to the current active network adapter.
        const TChar    *cookie  = "GetSubnets";
        NetworkAdapter *adapter = NULL;

        adapter = iLib->CurrentSubnetAdapter(cookie);

        // Obtain a list of available network adapters.
        std::vector<NetworkAdapter*>* subnetList = iLib->CreateSubnetList();

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

void MediaPlayerIF::PlayPipeLine()
{
    if (iExampleMediaPlayer != NULL)
        iExampleMediaPlayer->PlayPipeline();
}

void MediaPlayerIF::PausePipeLine()
{
    if (iExampleMediaPlayer != NULL)
        iExampleMediaPlayer->PausePipeline();
}

void MediaPlayerIF::StopPipeLine()
{
    if (iExampleMediaPlayer != NULL)
        iExampleMediaPlayer->HaltPipeline();
}

// Free up resources allocated to a subnet menu vector.
void MediaPlayerIF::FreeSubnets(std::vector<SubnetRecord*> *subnetVector)
{
    std::vector<SubnetRecord*>::iterator it;

    for (it=subnetVector->begin(); it < subnetVector->end(); it++)
    {
        delete (*it)->menuString;
        delete *it;
    }

    delete subnetVector;
}


void MediaPlayerIF::shutdown() {

    iExampleMediaPlayer->StopPipeline();

    delete iDriver;
    delete iExampleMediaPlayer;
    delete iLib;
    delete iArbDriver;
    delete iArbPipeline;

    iExampleMediaPlayer = nil;
    iDriver = nil;
    iLib = nil;
    iArbDriver = nil;
    iArbPipeline = nil;
}

void MediaPlayerIF::NotifySuspended()
{
    if(iLib)
        iLib->NotifySuspended();
    if(iDriver)
        iDriver->Pause();
}

void MediaPlayerIF::NotifyResumed()
{
    if(iDriver)
        iDriver->Resume();
    if(iLib)
        iLib->NotifyResumed();
}

TChar * updateCheck(TUint major, TUint minor)
{
    return nil;
}
