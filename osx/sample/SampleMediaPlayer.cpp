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
    setup();
}

SampleMediaPlayer::~SampleMediaPlayer()
{
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
    _volumeProxy = new CpProxyAvOpenhomeOrgVolume1((CpDevice&)*cpPlayer);
    _playlistProxy = new CpProxyAvOpenhomeOrgPlaylist1((CpDevice&)*cpPlayer);

    funcVolumeInitialEvent = MakeFunctor(*this, &SampleMediaPlayer::ohNetVolumeInitialEvent);
    _volumeProxy->SetPropertyInitialEvent(funcVolumeInitialEvent);
    funcVolumeChanged = MakeFunctor(*this, &SampleMediaPlayer::ohNetVolumeChanged);
    _volumeProxy->SetPropertyVolumeChanged(funcVolumeChanged);
    _volumeProxy->SetPropertyVolumeLimitChanged(funcVolumeChanged);
    _volumeProxy->SetPropertyMuteChanged(funcVolumeChanged);
    _volumeProxy->Subscribe();
    
    funcGenericInitialEvent = MakeFunctor(*this, &SampleMediaPlayer::ohNetGenericInitialEvent);
    _playlistProxy->SetPropertyInitialEvent(funcGenericInitialEvent);
    funcIdChanged = MakeFunctor(*this, &SampleMediaPlayer::ohNetPlaylistIdChangedEvent);
    _playlistProxy->SetPropertyIdChanged(funcIdChanged);
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
  
    // create a CpDeviceC for our internal player
    cpPlayer = CpDeviceDvCreate(mp->Device());
    
    // initialise ohNet Proxies for Volume, playlist
    initialiseProxies();
    
    mp->Run();
    
    return true;
}


void SampleMediaPlayer::shutdown() {
    playlistStop();
    
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



