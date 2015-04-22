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
    // if we have a control point initialised then delete
    // the proxies if they haven't been already
    if (cpPlayer != NULL)
    {
        delete _volumeProxy;
        _volumeProxy = NULL;
        
        delete _playlistProxy;
        _playlistProxy = nil;
    }
}

void SampleMediaPlayer::initialiseProxies()
{
    // create proxies for the Volume and Playlist services on our
    // player device
    _volumeProxy = new CpProxyAvOpenhomeOrgVolume1(*cpPlayer);
    _playlistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*cpPlayer);

    // create callbacks for all of the notifications we're interested in.
    funcVolumeInitialEvent = MakeFunctor(*this, &SampleMediaPlayer::ohNetVolumeInitialEvent);
    _volumeProxy->SetPropertyInitialEvent(funcVolumeInitialEvent);
    funcVolumeChanged = MakeFunctor(*this, &SampleMediaPlayer::ohNetVolumeChanged);
    // register for notifications about various volume properties
    _volumeProxy->SetPropertyVolumeChanged(funcVolumeChanged);
    _volumeProxy->SetPropertyVolumeLimitChanged(funcVolumeChanged);
    _volumeProxy->SetPropertyMuteChanged(funcVolumeChanged);
    // subscribe to the volume change service
    _volumeProxy->Subscribe();
    
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


void SampleMediaPlayer::ohNetVolumeInitialEvent()
{
    volumeChanged();
}


void SampleMediaPlayer::ohNetVolumeChanged()
{
    volumeChanged();
}

void SampleMediaPlayer::volumeChanged()
{
    TUint newVolume;
    TUint newLimit;
    TBool newMute;
    
    _volumeProxy->PropertyVolume(newVolume);
    _volumeProxy->PropertyVolumeLimit(newLimit);
    _volumeProxy->PropertyMute(newMute);
    
    // set the OS volume
}

void SampleMediaPlayer::initialEventVolume()
{
    volumeChanged();
}

void SampleMediaPlayer::setVolume(TUint volume)
{    
    _volumeProxy->SyncSetVolume(volume);
}

void SampleMediaPlayer::setMute(TBool muted)
{
    _volumeProxy->SyncSetMute(muted);
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
  
    mp->Run();
    
    // create a CpDeviceC for our internal player
    cpPlayer = CpDeviceDv::New(*cpStack, *(mp->Device()));
    
    // initialise ohNet Proxies for Volume, playlist
    initialiseProxies();
    
    return true;
}


void SampleMediaPlayer::shutdown() {
    playlistStop();
    
    _volumeProxy->Unsubscribe();
    _playlistProxy->Unsubscribe();
    
    delete mp;
    mp = nil;
    
    delete lib;
    lib = nil;
    
}

TBool SampleMediaPlayer::play() {
    playlistPlay();
    return true;
}

TBool SampleMediaPlayer::pause() {
    playlistPause();
    return true;
}

TBool SampleMediaPlayer::stop() {
    playlistStop();
    return true;
}



