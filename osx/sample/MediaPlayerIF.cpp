//
//  MediaPlayerIF.cpp
//  LitePipe Sample Mediaplayer
//
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

#include "MediaPlayerIF.h"
#include "UpdateCheck.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Sample;
using namespace OpenHome::Media;
using namespace OpenHome::Net;


MediaPlayerIF::MediaPlayerIF()
{
    // set up our media player
    setup();
}

MediaPlayerIF::~MediaPlayerIF()
{
    // delete the proxies if they haven't been already
    if (_playlistProxy != NULL)
    {
        delete _playlistProxy;
        _playlistProxy = NULL;
    }
}

void MediaPlayerIF::initialiseProxies()
{
    // create proxies for the Playlist service on our player device
    _playlistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*cpPlayerPlaylist);
    
    // create callbacks for playlist notifications we're interested in.
    funcGenericInitialEvent = MakeFunctor(*this, &MediaPlayerIF::ohNetGenericInitialEvent);
    _playlistProxy->SetPropertyInitialEvent(funcGenericInitialEvent);
    funcIdChanged = MakeFunctor(*this, &MediaPlayerIF::ohNetPlaylistIdChangedEvent);
    _playlistProxy->SetPropertyIdChanged(funcIdChanged);
    
    // subscribe to the playlist service
    _playlistProxy->Subscribe();
}

void MediaPlayerIF::ohNetGenericInitialEvent()
{
}

void MediaPlayerIF::ohNetPlaylistIdChangedEvent()
{
}

void MediaPlayerIF::playlistStop()
{
    _playlistProxy->SyncStop();
}

void MediaPlayerIF::playlistPlay()
{
    _playlistProxy->SyncPlay();
}

void MediaPlayerIF::playlistPause()
{
    _playlistProxy->SyncPause();
}

TChar * MediaPlayerIF::checkForUpdate(TUint major, TUint minor)
{
    Bws<1024> urlBuf;
    if (UpdateChecker::updateAvailable(dvStack->Env(),
                                       "http://elmo/~alans/application.json",
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


TBool MediaPlayerIF::setup ()
{
    // Create lib.
    lib = ExampleMediaPlayerInit::CreateLibrary(options.Loopback().Value(), options.Adapter().Value());
    cookie = "ExampleMediaPlayer";
    adapter = lib->CurrentSubnetAdapter(cookie);
    lib->StartCombined(adapter->Subnet(), cpStack, dvStack);
    
    adapter->RemoveRef(cookie);
    
    // Create MediaPlayer.
    driver = nil;
    mp = new ExampleMediaPlayer(*dvStack, udn, options.Room().CString(), options.Name().CString(),
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


void MediaPlayerIF::shutdown() {
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

TBool MediaPlayerIF::play() {
    if (canPlay())
    {
        playlistPlay();
        return true;
    }
    return false;
}

TBool MediaPlayerIF::pause() {
    if (canPause())
    {
        playlistPause();
        return true;
    }
    return false;
}

TBool MediaPlayerIF::stop() {
    if (canStop())
    {
        playlistStop();
        return true;
    }
    return false;
}

TBool MediaPlayerIF::canPlay() {
    return (iState == EPipelinePaused || iState == EPipelineStopped);
}

TBool MediaPlayerIF::canPause() {
    return (!iLive && iState == EPipelinePlaying);
}

TBool MediaPlayerIF::canStop() {
    return (iState == EPipelinePlaying || iState == EPipelinePaused);
}

// Pipeline Observer callbacks.
void MediaPlayerIF::NotifyPipelineState(Media::EPipelineState aState)
{
    iState = aState;
    
    //Log::Print("Pipeline State: %d\n", aState);
}

void MediaPlayerIF::NotifyTrack(Media::Track& /*aTrack*/, const Brx& /*aMode*/, TBool /*aStartOfStream*/)
{
}

void MediaPlayerIF::NotifyMetaText(const Brx& /*aText*/)
{
}

void MediaPlayerIF::NotifyTime(TUint /*aSeconds*/, TUint /*aTrackDurationSeconds*/)
{
}

void MediaPlayerIF::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
    iLive = aStreamInfo.Live();
}

TChar * updateCheck(TUint major, TUint minor)
{
    return nil;
}

