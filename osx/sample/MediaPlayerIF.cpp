//
//  SampleMediaPlayer.cpp
//  LitePipe Sample Mediaplayer
//
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

#include "SampleMediaPlayer.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Sample;
using namespace OpenHome::Media;
using namespace OpenHome::Net;


SampleMediaPlayer::SampleMediaPlayer()
{
    // set up our media player
    setup();
}

SampleMediaPlayer::~SampleMediaPlayer()
{
    // delete the proxies if they haven't been already
    if (_playlistProxy != NULL)
    {
        delete _playlistProxy;
        _playlistProxy = NULL;
    }
}

void SampleMediaPlayer::initialiseProxies()
{
    // create proxies for the Playlist service on our player device
    _playlistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*cpPlayerPlaylist);
    
    // create callbacks for playlist notifications we're interested in.
    funcGenericInitialEvent = MakeFunctor(*this, &SampleMediaPlayer::ohNetGenericInitialEvent);
    _playlistProxy->SetPropertyInitialEvent(funcGenericInitialEvent);
    funcIdChanged = MakeFunctor(*this, &SampleMediaPlayer::ohNetPlaylistIdChangedEvent);
    _playlistProxy->SetPropertyIdChanged(funcIdChanged);
    
    // subscribe to the playlist service
    _playlistProxy->Subscribe();
}

void SampleMediaPlayer::ohNetGenericInitialEvent()
{
}

void SampleMediaPlayer::ohNetPlaylistIdChangedEvent()
{
}

void SampleMediaPlayer::playlistStop()
{
    _playlistProxy->SyncStop();
}

void SampleMediaPlayer::playlistPlay()
{
    _playlistProxy->SyncPlay();
}

void SampleMediaPlayer::playlistPause()
{
    _playlistProxy->SyncPause();
}

TBool SampleMediaPlayer::setup ()
{
    // Parse options.
    options.Parse(0, nil);
    
    // Create lib.
    lib = BaseMediaPlayerInit::CreateLibrary(options.Loopback().Value(), options.Adapter().Value());
    cookie = "BaseMediaPlayerMain";
    adapter = lib->CurrentSubnetAdapter(cookie);
    lib->StartCombined(adapter->Subnet(), cpStack, dvStack);
    
    // Seed random number generator.
    BaseMediaPlayerInit::SeedRandomNumberGenerator(dvStack->Env(), options.Room().Value(), adapter->Address(), dvStack->ServerUpnp());
    adapter->RemoveRef(cookie);
    
    // Set/construct UDN.
    Bwh udn;
    BaseMediaPlayerInit::AppendUniqueId(dvStack->Env(), options.Udn().Value(), Brn("OsxMediaPlayer"), udn);
    
    // Create MediaPlayer.
    driver = nil;
    mp = new BaseMediaPlayer(*dvStack, udn, options.Room().CString(), options.Name().CString(),
                             options.TuneIn().Value(), options.Tidal().Value(), options.Qobuz().Value(),
                             options.UserAgent().Value());
    
    if(mp != nil)
        driver = new DriverOsx(dvStack->Env(), mp->Pipeline());
    
    if (driver == nil)
    {
        delete mp;
        return false;
    }
    
    // now that we've created the media player (and volume control), hook
    // it up to the host audio driver
    mp->VolumeControl().SetHost(driver);
  
    mp->Pipeline().AddObserver(*this);
    iState = EPipelineStopped;

    mp->Run();
    
    // create a CpDeviceDv for our internal player
    cpPlayerPlaylist = CpDeviceDv::New(*cpStack, *(mp->Device()));
    
    // initialise ohNet Proxy for playlist
    initialiseProxies();
    
    return true;
}


void SampleMediaPlayer::shutdown() {
    // we're shutting down so stop the current playlist if required
    playlistStop();
    
    // unsubscribe to volume and playlist services
    _playlistProxy->Unsubscribe();
    
    // delete our media player
    delete mp;
    mp = nil;
    
    // and free our library
    delete lib;
    lib = nil;
    
}

TBool SampleMediaPlayer::play() {
    if (canPlay())
    {
        playlistPlay();
        return true;
    }
    return false;
}

TBool SampleMediaPlayer::pause() {
    if (canPause())
    {
        playlistPause();
        return true;
    }
    return false;
}

TBool SampleMediaPlayer::stop() {
    if (canStop())
    {
        playlistStop();
        return true;
    }
    return false;
}

TBool SampleMediaPlayer::canPlay() {
    return (iState == EPipelinePaused || iState == EPipelineStopped);
}

TBool SampleMediaPlayer::canPause() {
    return (!iLive && iState == EPipelinePlaying);
}

TBool SampleMediaPlayer::canStop() {
    return (iState == EPipelinePlaying || iState == EPipelinePaused);
}

// Pipeline Observer callbacks.
void SampleMediaPlayer::NotifyPipelineState(Media::EPipelineState aState)
{
    iState = aState;
    
    //Log::Print("Pipeline State: %d\n", aState);
}

void SampleMediaPlayer::NotifyTrack(Media::Track& /*aTrack*/, const Brx& /*aMode*/, TBool /*aStartOfStream*/)
{
}

void SampleMediaPlayer::NotifyMetaText(const Brx& /*aText*/)
{
}

void SampleMediaPlayer::NotifyTime(TUint /*aSeconds*/, TUint /*aTrackDurationSeconds*/)
{
}

void SampleMediaPlayer::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
    iLive = aStreamInfo.Live();
}

