#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Printer.h>
#include <CpAvOpenhomeOrgVolume1.h>
#include <CpAvOpenhomeOrgPlaylist1.h>

#include "AudioDriver.h"
#include "ControlPointProxy.h"
#include "MemoryCheck.h"


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// ControlPointProxy to control playback.

ControlPointProxy::ControlPointProxy(CpStack& aCpStack, DvDevice& aDevice)
{
    iCpPlayer = CpDeviceDv::New(aCpStack, aDevice);

    // Create proxies for the Volume and Playlist services on our
    // player device
    iVolumeProxy   = new CpProxyAvOpenhomeOrgVolume1(*iCpPlayer);
    iPlaylistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*iCpPlayer);

    // Create callbacks for all of the notifications we're interested in.
    iFuncVolumeInitialEvent =
        MakeFunctor(*this, &ControlPointProxy::ohNetVolumeInitialEvent);

    iVolumeProxy->SetPropertyInitialEvent(iFuncVolumeInitialEvent);

    iFuncVolumeChanged =
        MakeFunctor(*this, &ControlPointProxy::ohNetVolumeChanged);

    // Register for notifications about various volume properties
    iVolumeProxy->SetPropertyVolumeChanged(iFuncVolumeChanged);
    iVolumeProxy->SetPropertyVolumeLimitChanged(iFuncVolumeChanged);
    iVolumeProxy->SetPropertyMuteChanged(iFuncVolumeChanged);

    // Subscribe to the volume change service
    iVolumeProxy->Subscribe();

    // Create callbacks for playlist notifications we're interested in.
    iFuncGenericInitialEvent =
        MakeFunctor(*this, &ControlPointProxy::ohNetGenericInitialEvent);

    iPlaylistProxy->SetPropertyInitialEvent(iFuncGenericInitialEvent);

    iFuncIdChanged =
        MakeFunctor(*this, &ControlPointProxy::ohNetPlaylistIdChangedEvent);

    iPlaylistProxy->SetPropertyIdChanged(iFuncIdChanged);

    // Subscribe to the playlist service
    iPlaylistProxy->Subscribe();
}

ControlPointProxy::~ControlPointProxy()
{
    // Delete the proxies if they haven't been already
    if (iPlaylistProxy != NULL)
    {
        iPlaylistProxy->Unsubscribe();

        delete iPlaylistProxy;
        iPlaylistProxy = NULL;
    }

    if (iVolumeProxy != NULL)
    {
        iVolumeProxy->Unsubscribe();

        delete iVolumeProxy;
        iVolumeProxy = NULL;
    }

    iCpPlayer->RemoveRef();
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

    iVolumeProxy->PropertyVolume(newVolume);
    iVolumeProxy->PropertyVolumeLimit(newLimit);
    iVolumeProxy->PropertyMute(newMute);

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
    iPlaylistProxy->SyncStop();
}

void ControlPointProxy::playlistPlay()
{
    iPlaylistProxy->SyncPlay();
}

void ControlPointProxy::playlistPause()
{
    iPlaylistProxy->SyncPause();
}
