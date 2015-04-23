//
//  SampleMediaPlayer.h
//  LitePipe Sample media player class
//
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

#include <OpenHome/Types.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <Generated/CpAvOpenhomeOrgVolume1.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Functor.h>

#include "DriverOsx.h"
#include "BaseMediaPlayer.h"

namespace OpenHome {
namespace Av {
namespace Sample {

    class SampleMediaPlayer : private Media::IPipelineObserver

{
public:
    SampleMediaPlayer();
    ~SampleMediaPlayer();

    TBool setup();
    void shutdown();
    TBool play();
    TBool pause();
    TBool stop();

    void initialiseProxies();
    void ohNetVolumeInitialEvent();
    void ohNetVolumeChanged();
    void ohNetGenericInitialEvent();
    void ohNetPlaylistIdChangedEvent();

    void initialEventVolume();
    
    void setVolume(TUint volume);
    void setVolumeLimit(TUint limit);
    void setMute(TBool muted);

    void playlistPlay();
    void playlistPause();
    void playlistStop();
    TBool canPlay();
    TBool canPause();
    TBool canStop();

private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyTrack(Media::Track& aTrack, const Brx& aMode, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
    
private:
    
    void volumeChanged();
    
    TBool mute;
    TUint volume;
    TUint volumeLimit;
    TUint playlistTrackId;
    TBool iLive;
    Media::EPipelineState iState;
    
    Net::Library* lib;
    const TChar* cookie;
    NetworkAdapter* adapter;
    Net::DvStack* dvStack;
    Net::CpStack* cpStack;
    Net::CpDeviceDv* cpPlayerVol;
    Net::CpDeviceDv* cpPlayerPlaylist;
    BaseMediaPlayerOptions options;
    BaseMediaPlayer* mp;
    Media::DriverOsx* driver;
    
    Functor funcVolumeInitialEvent;
    Functor funcVolumeChanged;
    Functor funcGenericInitialEvent;
    Functor funcIdChanged;
    
    // Create proxies to control the volume and playback
    // of our local player
    Net::CpProxyAvOpenhomeOrgVolume1 * _volumeProxy;
    Net::CpProxyAvOpenhomeOrgPlaylist1 * _playlistProxy;
};
  
}  // namespace Sample
}  // namespace Av
}  // namespace OpenHome
    
