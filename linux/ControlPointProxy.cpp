#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Printer.h>

#include <CpAvOpenhomeOrgPlaylist1.h>

#include "ControlPointProxy.h"


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// ControlPointProxy to control playback.

ControlPointProxy::ControlPointProxy(CpStack& aCpStack, DvDevice& aDevice)
{
    iCpPlayer = CpDeviceDv::New(aCpStack, aDevice);

    // Create a proxies for the Playlist services on our
    // player device
    iPlaylistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*iCpPlayer);

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
    // Delete the proxy if it hasn't been already
    if (iPlaylistProxy != NULL)
    {
        iPlaylistProxy->Unsubscribe();

        delete iPlaylistProxy;
        iPlaylistProxy = NULL;
    }

    iCpPlayer->RemoveRef();
}

void ControlPointProxy::ohNetGenericInitialEvent()
{
}

void ControlPointProxy::ohNetPlaylistIdChangedEvent()
{
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
