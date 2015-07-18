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

static NSString *const kUpdateUri = OPENHOME_UPDATE_URI;

MediaPlayerIF::MediaPlayerIF(TIpAddress subnet)
{
#ifdef DEBUG
    Debug::SetLevel(Debug::kBonjour);
    Debug::SetLevel(Debug::kDvDevice);
    Debug::SetLevel(Debug::kError);
    Debug::SetLevel(Debug::kHttp);
    Debug::SetLevel(Debug::BreakBeforeThrow());
#endif  // DEBUG
    
    // set up our media player
    setup(subnet);
}

MediaPlayerIF::~MediaPlayerIF()
{
    if (iExampleMediaPlayer != NULL)
    {
        iExampleMediaPlayer->StopPipeline();
        delete iExampleMediaPlayer;
    }
    
    delete iDriver;
    delete iLib;
    delete iArbDriver;
    delete iArbPipeline;
}

TChar * MediaPlayerIF::checkForUpdate(TUint major, TUint minor)
{
    Bws<1024> urlBuf;
    
    if (UpdateChecker::updateAvailable(iDvStack->Env(),
                                       [kUpdateUri cStringUsingEncoding:NSASCIIStringEncoding],
                                       urlBuf,
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
    
    initParams->SetDvEnableBonjour();
    
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
        if (subnet == preferredSubnet)
        {
            index = i;
            break;
        }
        
        // If the last used subnet is available, note it.
        // We'll fall back to it if the requested subnet is not available.
        if (subnet == lastSubnet)
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
    
    Log::Print("Using Subnet %d.%d.%d.%d\n", subnet&0xff, (subnet>>8)&0xff,
               (subnet>>16)&0xff,
               (subnet>>24)&0xff);
    
    return lib;

}


TBool MediaPlayerIF::setup (TIpAddress subnet)
{
    
    // Pipeline configuration.
    NSString *computerName = [[NSHost currentHost] name];
    const TChar *room  = [computerName cStringUsingEncoding:NSUTF8StringEncoding];
    static const TChar *name  = "SoftPlayer";
    // 4c494e4e- prefix is a temporary measure to allow recognition by Linn Konfig
    NSString *udn = [NSString stringWithFormat:@"4c494e4e-OsxPlayer-%@",computerName];
    static const TChar *cookie = "ExampleMediaPlayer";

    // Create the library on the supplied subnet.
    iLib = CreateLibrary(subnet);
    if (!iLib)
        return false;
    
    // Get the current network adapter.
    iAdapter = iLib->CurrentSubnetAdapter(cookie);
    if (!iAdapter)
        goto cleanup;
    
    iLib->StartCombined(iAdapter->Subnet(), iCpStack, iDvStack);
    
    iAdapter->RemoveRef(cookie);
    
    // Create MediaPlayer.
    iDriver = nil;
    iExampleMediaPlayer = new ExampleMediaPlayer(*iDvStack,
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
    delete iDriver;
    delete iExampleMediaPlayer;
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
    
    // delete our media player
    delete iExampleMediaPlayer;
    iExampleMediaPlayer = nil;
    
    // and free our library
    delete iLib;
    iLib = nil;
}


TChar * updateCheck(TUint major, TUint minor)
{
    return nil;
}

