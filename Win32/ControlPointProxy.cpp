#include <OpenHome/Types.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Printer.h>
#include <CpAvOpenhomeOrgVolume1.h>
#include <CpAvOpenhomeOrgPlaylist1.h>


#include "AudioDriver.h"
#include "ControlPointProxy.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
   #ifndef DBG_NEW
      #define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
      #define new DBG_NEW
   #endif
#endif  // _DEBUG

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// ControlPointProxy

ControlPointProxy::ControlPointProxy(CpStack& aCpStack, DvDevice& aDevice)
{
    _cpPlayer = CpDeviceDv::New(aCpStack, aDevice);

    // create proxies for the Volume and Playlist services on our
    // player device
    _volumeProxy   = new CpProxyAvOpenhomeOrgVolume1(*_cpPlayer);
    _playlistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*_cpPlayer);

    // create callbacks for all of the notifications we're interested in.
    funcVolumeInitialEvent =
        MakeFunctor(*this, &ControlPointProxy::ohNetVolumeInitialEvent);

    _volumeProxy->SetPropertyInitialEvent(funcVolumeInitialEvent);

    funcVolumeChanged =
        MakeFunctor(*this, &ControlPointProxy::ohNetVolumeChanged);

    // register for notifications about various volume properties
    _volumeProxy->SetPropertyVolumeChanged(funcVolumeChanged);
    _volumeProxy->SetPropertyVolumeLimitChanged(funcVolumeChanged);
    _volumeProxy->SetPropertyMuteChanged(funcVolumeChanged);

    // subscribe to the volume change service
    _volumeProxy->Subscribe();

    // create callbacks for playlist notifications we're interested in.
    funcGenericInitialEvent =
        MakeFunctor(*this, &ControlPointProxy::ohNetGenericInitialEvent);

    _playlistProxy->SetPropertyInitialEvent(funcGenericInitialEvent);

    funcIdChanged =
        MakeFunctor(*this, &ControlPointProxy::ohNetPlaylistIdChangedEvent);

    _playlistProxy->SetPropertyIdChanged(funcIdChanged);

    // subscribe to the playlist service
    _playlistProxy->Subscribe();
}

ControlPointProxy::~ControlPointProxy()
{
    // delete the proxies if they haven't been already
    if (_playlistProxy != NULL)
    {
        _playlistProxy->Unsubscribe();

        delete _playlistProxy;
        _playlistProxy = NULL;
    }

    if (_volumeProxy != NULL)
    {
        _volumeProxy->Unsubscribe();

        delete _volumeProxy;
        _volumeProxy = NULL;
    }

    _cpPlayer->RemoveRef();
}

void ControlPointProxy::ohNetGenericInitialEvent()
{
}

void ControlPointProxy::ohNetPlaylistIdChangedEvent()
{
}


void ControlPointProxy::ohNetVolumeInitialEvent()
{
    volumeChanged();
}


void ControlPointProxy::ohNetVolumeChanged()
{
    volumeChanged();
}

void ControlPointProxy::volumeChanged()
{
    TUint newVolume;
    TUint newLimit;
    TBool newMute;

    _volumeProxy->PropertyVolume(newVolume);
    _volumeProxy->PropertyVolumeLimit(newLimit);
    _volumeProxy->PropertyMute(newMute);

    Log::Print("ControlPointProxy::volumeChanged [%u]\n", newVolume);

    // Set the audio session volume.
    AudioDriver::SetVolume(float(newVolume)/100.0f, newMute);
}

void ControlPointProxy::initialEventVolume()
{
    volumeChanged();
}

void ControlPointProxy::playlistStop()
{
    _playlistProxy->SyncStop();
}

void ControlPointProxy::playlistPlay()
{
    _playlistProxy->SyncPlay();
}

void ControlPointProxy::playlistPause()
{
    _playlistProxy->SyncPause();
}
