#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Printer.h>

#include "OptionalFeatures.h"

#include <OpenHome/Net/Core/CpAvOpenhomeOrgPlaylist1.h>
#ifdef ENABLE_RADIO
#include <OpenHome/Net/Core/CpAvOpenhomeOrgRadio1.h>
#endif // ENABLE_RADIO
#include <OpenHome/Net/Core/CpAvOpenhomeOrgReceiver1.h>
#include <OpenHome/Net/Core/CpAvOpenhomeOrgProduct2.h>
#include <OpenHome/Net/Core/CpUpnpOrgAVTransport1.h>

#include <string>

#include "ControlPointProxy.h"
#include "DriverOsx.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

using namespace std;

// Available transport states.
static const std::string kTransportStatePlaying("Playing");
static const std::string kTransportStatePaused("Paused");
static const std::string kTransportStateStopped("Stopped");
static const std::string kTransportStateBuffering("Buffering");

// Available pipeline states.
static const std::string kPipelineStatePlaying("PLAYING");
static const std::string kPipelineStatePaused("PAUSED_PLAYBACK");
static const std::string kPipelineStateStopped("STOPPED");
static const std::string kPipelineStateBuffering("TRANSITIONING");

// ControlPointProxy to control playback from the active source.

ControlPointProxy::ControlPointProxy(CpStack& aCpStack,
                                     DvDevice& aDevice,
                                     Net::DvDevice& aUpnpDevice,
                                     Media::PipelineManager& aPipeline,
                                     Media::DriverOsx& aDriver) :
    iActiveSource(UNKNOWN)
{
    iCpPlayer       = CpDeviceDv::New(aCpStack, aDevice);
    iCpUpnpAvPlayer = CpDeviceDv::New(aCpStack, aUpnpDevice);

    iCpPlaylist = new CPPlaylist(*iCpPlayer, aDriver);
#ifdef ENABLE_RADIO
    iCpRadio    = new CPRadio(*iCpPlayer, aDriver);
#endif // ENABLE_RADIO
    iCpReceiver = new CPReceiver(*iCpPlayer, aDriver);
    iCpUpnpAv   = new CPUpnpAv(*iCpUpnpAvPlayer, aPipeline, aDriver);
    iCpProduct  = new CPProduct(*iCpPlayer, *this);
}

ControlPointProxy::~ControlPointProxy()
{
    // Delete the proxies if it hasn't been done already
    if (iCpProduct != NULL)
    {
        delete iCpProduct;
        iCpProduct = NULL;
    }

    if (iCpPlaylist != NULL)
    {
        delete iCpPlaylist;
        iCpPlaylist = NULL;
    }

#ifdef ENABLE_RADIO
    if (iCpRadio != NULL)
    {
        delete iCpRadio;
        iCpRadio = NULL;
    }
#endif // ENABLE_RADIO

    if (iCpReceiver != NULL)
    {
        delete iCpReceiver;
        iCpReceiver = NULL;
    }

    if (iCpUpnpAv != NULL)
    {
        delete iCpUpnpAv;
        iCpUpnpAv = NULL;
    }

    iCpPlayer->RemoveRef();
    iCpUpnpAvPlayer->RemoveRef();
}

// Playlist Proxy

ControlPointProxy::CPPlaylist::CPPlaylist(Net::CpDeviceDv &aCpPlayer,
                                          Media::DriverOsx &aDriver) :
    iIsActive(false),
    iDriver(aDriver)
{
    iCpPlayer = &aCpPlayer;
    iCpPlayer->AddRef();

    iPlaylistProxy = new CpProxyAvOpenhomeOrgPlaylist1(*iCpPlayer);

    // Create callback for a state change.
    iTransportStateChanged =
        MakeFunctor(*this,
                    &ControlPointProxy::CPPlaylist::transportChangedEvent);

    iPlaylistProxy->SetPropertyTransportStateChanged(iTransportStateChanged);

    // Subscribe to the playlist service
    iPlaylistProxy->Subscribe();
}

ControlPointProxy::CPPlaylist::~CPPlaylist()
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

void ControlPointProxy::CPPlaylist::transportChangedEvent()
{
    Brhz state;

    // Ignore this notification if we are not the active source.
    if (! iIsActive)
    {
        return;
    }

    // Read the new playlist state.
    try
    {
        iPlaylistProxy->PropertyTransportState(state);
    }
    catch (ProxyError& aPe)
    {
        return;
    }

    // Keep the native audio state synced with the transport state.
    string stateStr(state.CString());

    // Log the new state.
    Log::Print("New Playlist State: %s\n", state.CString());
}

void ControlPointProxy::CPPlaylist::setActive(TBool active)
{
    iIsActive = active;
}

TBool ControlPointProxy::CPPlaylist::canStop()
{
    Brhz state;

    iPlaylistProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return ((stateStr == kTransportStatePlaying) ||
            (stateStr == kTransportStatePaused));
}

TBool ControlPointProxy::CPPlaylist::canPlay()
{
    Brhz state;

    iPlaylistProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return ((stateStr == kTransportStateStopped) ||
            (stateStr == kTransportStatePaused));
}

TBool ControlPointProxy::CPPlaylist::canPause()
{
    Brhz state;

    iPlaylistProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return ((stateStr == kTransportStatePlaying) ||
            (stateStr == kTransportStateBuffering));
}

void ControlPointProxy::CPPlaylist::playlistStop()
{
    if (iIsActive)
    {
        if (canStop())
        {
            iPlaylistProxy->SyncStop();
        }
    }
}

void ControlPointProxy::CPPlaylist::playlistPlay()
{
    if (iIsActive)
    {
        if (canPlay())
        {
            iPlaylistProxy->SyncPlay();
        }
    }
}

void ControlPointProxy::CPPlaylist::playlistPause()
{
    if (iIsActive)
    {
        if (canPause())
        {
            iPlaylistProxy->SyncPause();
        }
    }
}

#ifdef ENABLE_RADIO
// Radio Proxy

ControlPointProxy::CPRadio::CPRadio(Net::CpDeviceDv &aCpPlayer,
                                    Media::DriverOsx &aDriver) :
    iIsActive(false),
    iDriver(aDriver)
{
    iCpPlayer = &aCpPlayer;
    iCpPlayer->AddRef();

    iRadioProxy = new CpProxyAvOpenhomeOrgRadio1(*iCpPlayer);

    // Create callback for a state change.
    iTransportStateChanged =
        MakeFunctor(*this,
                    &ControlPointProxy::CPRadio::transportChangedEvent);

    iRadioProxy->SetPropertyTransportStateChanged(iTransportStateChanged);

    // Subscribe to the radio service
    iRadioProxy->Subscribe();
}

ControlPointProxy::CPRadio::~CPRadio()
{
    // Delete the proxy if it hasn't been already
    if (iRadioProxy != NULL)
    {
        iRadioProxy->Unsubscribe();

        delete iRadioProxy;
        iRadioProxy = NULL;
    }

    iCpPlayer->RemoveRef();
}

void ControlPointProxy::CPRadio::transportChangedEvent()
{
    Brhz  state;

    // Ignore this notification if we are not the active source.
    if (! iIsActive)
    {
        return;
    }

    // Read the new radio state.
    try
    {
        iRadioProxy->PropertyTransportState(state);
    }
    catch (ProxyError& aPe)
    {
        return;
    }

    // Keep the native audio state synced with the transport state.
    string stateStr(state.CString());

    // Log the new state.
    Log::Print("New Radio State: %s\n", state.CString());
}

void ControlPointProxy::CPRadio::setActive(TBool active)
{
    iIsActive = active;
}

TBool ControlPointProxy::CPRadio::canStop()
{
    Brhz state;

    iRadioProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return ((stateStr == kTransportStatePlaying) ||
            (stateStr == kTransportStateBuffering));
}

TBool ControlPointProxy::CPRadio::canPlay()
{
    Brhz state;

    iRadioProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return (stateStr == kTransportStateStopped);
}

void ControlPointProxy::CPRadio::radioStop()
{
    if (iIsActive)
    {
        if (canStop())
        {
            iRadioProxy->SyncStop();
        }
    }
}

void ControlPointProxy::CPRadio::radioPlay()
{
    if (iIsActive)
    {
        if (canPlay())
        {
            iRadioProxy->SyncPlay();
        }
    }
}
#endif // ENABLE_RADIO

// Receiver Proxy

ControlPointProxy::CPReceiver::CPReceiver(Net::CpDeviceDv &aCpPlayer,
                                          Media::DriverOsx &aDriver) :
    iIsActive(false),
    iDriver(aDriver)
{
    iCpPlayer = &aCpPlayer;
    iCpPlayer->AddRef();

    iReceiverProxy = new CpProxyAvOpenhomeOrgReceiver1(*iCpPlayer);

    // Create callback for a state change.
    iTransportStateChanged =
        MakeFunctor(*this,
                    &ControlPointProxy::CPReceiver::transportChangedEvent);

    iReceiverProxy->SetPropertyTransportStateChanged(iTransportStateChanged);

    // Subscribe to the receiver service
    iReceiverProxy->Subscribe();
}

ControlPointProxy::CPReceiver::~CPReceiver()
{
    // Delete the proxy if it hasn't been already
    if (iReceiverProxy != NULL)
    {
        iReceiverProxy->Unsubscribe();

        delete iReceiverProxy;
        iReceiverProxy = NULL;
    }

    iCpPlayer->RemoveRef();
}

void ControlPointProxy::CPReceiver::transportChangedEvent()
{
    Brhz  state;

    // Ignore this notification if we are not the active source.
    if (! iIsActive)
    {
        return;
    }

    // Read the new playlist state.
    try
    {
        iReceiverProxy->PropertyTransportState(state);
    }
    catch (ProxyError& aPe)
    {
        return;
    }

    // Keep the native audio state synced with the transport state.
    string stateStr(state.CString());

    // Log the new state.
    Log::Print("New Receiver State: %s\n", state.CString());
}

void ControlPointProxy::CPReceiver::setActive(TBool active)
{
    iIsActive = active;
}

TBool ControlPointProxy::CPReceiver::canStop()
{
    Brhz state;

    iReceiverProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return ((stateStr == kTransportStatePlaying) ||
            (stateStr == kTransportStateBuffering));
}

TBool ControlPointProxy::CPReceiver::canPlay()
{
    Brhz state;

    iReceiverProxy->PropertyTransportState(state);

    string stateStr(state.CString());

    return (stateStr == kTransportStateStopped);
}

void ControlPointProxy::CPReceiver::receiverStop()
{
    if (iIsActive)
    {
        if (canStop())
        {
            iReceiverProxy->SyncStop();
        }
    }
}

void ControlPointProxy::CPReceiver::receiverPlay()
{
    if (iIsActive)
    {
        if (canPlay())
        {
            iReceiverProxy->SyncPlay();
        }
    }
}

// UpnpAV Proxy

ControlPointProxy::CPUpnpAv::CPUpnpAv(Net::CpDeviceDv &aCpPlayer,
                                      Media::PipelineManager &aPipeline,
                                      Media::DriverOsx &aDriver) :
    iIsActive(false),
    iPipeline(aPipeline),
    iDriver(aDriver)
{
    iCpPlayer = &aCpPlayer;
    iCpPlayer->AddRef();

    iUpnpAvProxy = new CpProxyUpnpOrgAVTransport1(*iCpPlayer);

    // Create callback for a pipeline state change.
    iPipeline.AddObserver(*this);

    // Subscribe to the receiver service
    iUpnpAvProxy->Subscribe();
}

ControlPointProxy::CPUpnpAv::~CPUpnpAv()
{
    // Delete the proxy if it hasn't been already
    if (iUpnpAvProxy != NULL)
    {
        iUpnpAvProxy->Unsubscribe();

        delete iUpnpAvProxy;
        iUpnpAvProxy = NULL;
    }

    iCpPlayer->RemoveRef();
}

void ControlPointProxy::CPUpnpAv::pipelineChangedEvent()
{
    Brh  state;
    Brh  dummy;

    // Ignore this notification if we are not the active source.
    if (! iIsActive)
    {
        return;
    }

    // Read the new transport state.
    try
    {
        iUpnpAvProxy->SyncGetTransportInfo(0, state, dummy, dummy);
    }
    catch (ProxyError& aPe)
    {
        return;
    }

    // Keep the native audio state synced with the transport state.
    string stateStr(state.Extract());

    // Log the new state.
    Log::Print("New UpnpAV State: %s\n", stateStr.c_str());
}

void ControlPointProxy::CPUpnpAv::setActive(TBool active)
{
    iIsActive = active;
}

TBool ControlPointProxy::CPUpnpAv::canStop()
{
    Brh state;
    Brh dummy;

    iUpnpAvProxy->SyncGetTransportInfo(0, state, dummy, dummy);

    string stateStr(state.Extract());

    return ((stateStr == kPipelineStatePlaying) ||
            (stateStr == kPipelineStatePaused));
}

TBool ControlPointProxy::CPUpnpAv::canPlay()
{
    Brh state;
    Brh dummy;

    iUpnpAvProxy->SyncGetTransportInfo(0, state, dummy, dummy);

    string stateStr(state.Extract());

    return ((stateStr == kPipelineStateStopped) ||
            (stateStr == kPipelineStatePaused));
}

TBool ControlPointProxy::CPUpnpAv::canPause()
{
    Brh state;
    Brh dummy;

    iUpnpAvProxy->SyncGetTransportInfo(0, state, dummy, dummy);

    string stateStr(state.Extract());

    return ((stateStr == kPipelineStatePlaying) ||
            (stateStr == kPipelineStateBuffering));
}

void ControlPointProxy::CPUpnpAv::upnpAvStop()
{
    if (iIsActive)
    {
        if (canStop())
        {
            iUpnpAvProxy->SyncStop(0);
        }
    }
}

void ControlPointProxy::CPUpnpAv::upnpAvPlay()
{
    if (iIsActive)
    {
        if (canPlay())
        {
            iUpnpAvProxy->SyncPlay(0, Brn("1"));
        }
    }
}

void ControlPointProxy::CPUpnpAv::upnpAvPause()
{
    if (iIsActive)
    {
        if (canPause())
        {
            iUpnpAvProxy->SyncPause(0);
        }
    }
}

// Pipeline Observer callbacks.
void ControlPointProxy::CPUpnpAv::NotifyPipelineState(Media::EPipelineState aState)
{
    pipelineChangedEvent();
}

void ControlPointProxy::CPUpnpAv::NotifyMode(const Brx& /*aMode*/,
                                             const Media::ModeInfo& /*aInfo*/,
                                             const Media::ModeTransportControls& /*aControls*/)
{
}

void ControlPointProxy::CPUpnpAv::NotifyTrack(Media::Track& /*aTrack*/,
                                              TBool         /*aStartOfStream*/)
{
}

void ControlPointProxy::CPUpnpAv::NotifyMetaText(const Brx& /*aText*/)
{
}

void ControlPointProxy::CPUpnpAv::NotifyTime(TUint /*aSeconds*/)
{
}

void ControlPointProxy::CPUpnpAv::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
}

// Product Proxy

ControlPointProxy::CPProduct::CPProduct(Net::CpDeviceDv &aCpPlayer, ControlPointProxy &aCcp) :
    iCcp(aCcp)
{
    iCpPlayer = &aCpPlayer;
    iCpPlayer->AddRef();

    iProductProxy = new CpProxyAvOpenhomeOrgProduct2(*iCpPlayer);

    // Create callbacks for playlist notifications we're interested in.
    iFuncSourceIndexChanged =
        MakeFunctor(*this,
                    &ControlPointProxy::CPProduct::sourceIndexChangedEvent);

    iProductProxy->SetPropertySourceIndexChanged(iFuncSourceIndexChanged);

    // Subscribe to the product service
    iProductProxy->Subscribe();
}

ControlPointProxy::CPProduct::~CPProduct()
{
    // Delete the proxy if it hasn't been already
    if (iProductProxy != NULL)
    {
        iProductProxy->Unsubscribe();

        delete iProductProxy;
        iProductProxy = NULL;
    }

    iCpPlayer->RemoveRef();
}

// Obtain the position in string 's' of the 'n'th occurrence of string 'p'
TInt ControlPointProxy::CPProduct::nthSubstrPos(TInt n, const string& s, const string& p)
{
    string::size_type i = s.find(p);     // Find the first occurrence

    TInt j;
    for (j = 1; j < n && i != string::npos; ++j)
    {
        i = s.find(p, i+1);              // Find the next occurrence
    }

    if (j == n)
    {
        return(TInt(i));
    }

    return(-1);
}

// Identify the audio source at the given index in source Xml stream.
Sources
    ControlPointProxy::CPProduct::GetSourceAtIndex(string &sourceXml,
                                                   TInt sourceIndex)
{
    // Get the position of the nth <Type> tag
    TInt index = nthSubstrPos(sourceIndex, sourceXml, "<Type>");

    if (index == -1)
    {
        return UNKNOWN;
    }

    // Skip over the actual tag
    index += 6;

    // Locate the start of the next tag.
    string::size_type index1 = sourceXml.find_first_of('<', index);

    if (index1 == string::npos)
    {
        return UNKNOWN;
    }

    // Extract the data bounded by the tags.
    string sourceString = sourceXml.substr(index, index1-index);

    if (sourceString == "Playlist")
    {
        return PLAYLIST;
    }
    else if (sourceString == "Radio")
    {
        return RADIO;
    }
    else if (sourceString == "Receiver")
    {
        return RECEIVER;
    }
    else if (sourceString == "UpnpAv")
    {
        return UPNPAV;
    }

    return UNKNOWN;
}

void ControlPointProxy::CPProduct::sourceIndexChangedEvent()
{
    TUint index;
    Brhz  sourceXml;

    // Read the new source index.
    iProductProxy->PropertySourceIndex(index);

    // Read the source Xml/
    iProductProxy->PropertySourceXml(sourceXml);

    string sourceXmlStr(sourceXml.CString());

    // Identify the source at the given source index (1..X).
    Sources source = GetSourceAtIndex(sourceXmlStr, (TInt)(index+1));

    // Mark the new source as active.
    iCcp.setActiveCp(source);
}

void ControlPointProxy::setActiveCp(Sources newSource)
{
    // Reset all sources
    iCpPlaylist->setActive(false);
#ifdef ENABLE_RADIO
    iCpRadio->setActive(false);
#endif // ENABLE_RADIO
    iCpReceiver->setActive(false);
    iCpUpnpAv->setActive(false);

    // Activate the new source.
    iActiveSource = newSource;

    switch (newSource)
    {
        case PLAYLIST:
            iCpPlaylist->setActive(true);
            Log::Print("NEW SOURCE: Playlist\n");
            break;
#ifdef ENABLE_RADIO
        case RADIO:
            iCpRadio->setActive(true);
            Log::Print("NEW SOURCE: Radio\n");
            break;
#endif // ENABLE_RADIO
        case RECEIVER:
            iCpReceiver->setActive(true);
            Log::Print("NEW SOURCE: Receiver\n");
            break;
        case UPNPAV:
            iCpUpnpAv->setActive(true);
            Log::Print("NEW SOURCE: UpnpAV\n");
            break;
        default:
            Log::Print("NEW SOURCE: Unknown\n");
            break;
    }
}

void ControlPointProxy::cpStop()
{
    switch (iActiveSource)
    {
        case PLAYLIST:
            iCpPlaylist->playlistStop();
            break;
#ifdef ENABLE_RADIO
        case RADIO:
            iCpRadio->radioStop();
            break;
#endif // ENABLE_RADIO
        case RECEIVER:
            iCpReceiver->receiverStop();
            break;
        case UPNPAV:
            iCpUpnpAv->upnpAvStop();
            break;
        default:
            Log::Print("Warning: Attempt to stop unknown source\n");
            break;
    }
}

void ControlPointProxy::cpPlay()
{
    switch (iActiveSource)
    {
        case PLAYLIST:
            iCpPlaylist->playlistPlay();
            break;
#ifdef ENABLE_RADIO
        case RADIO:
            iCpRadio->radioPlay();
            break;
#endif // ENABLE_RADIO
        case RECEIVER:
            iCpReceiver->receiverPlay();
            break;
        case UPNPAV:
            iCpUpnpAv->upnpAvPlay();
            break;
        default:
            Log::Print("Warning: Attempt to play unknown source\n");
            break;
    }
}

void ControlPointProxy::cpPause()
{
    switch (iActiveSource)
    {
        case PLAYLIST:
            iCpPlaylist->playlistPause();
            break;
        case UPNPAV:
            iCpUpnpAv->upnpAvPause();
            break;
        default:
            Log::Print("Warning: Attempt to pause unknown/invalid source\n");
            break;
    }
}

TBool ControlPointProxy::canStop()
{
    switch (iActiveSource)
    {
        case PLAYLIST:
        {
            return iCpPlaylist->canStop();
        }

#ifdef ENABLE_RADIO
        case RADIO:
        {
            return iCpRadio->canStop();
        }
#endif // ENABLE_RADIO

        case RECEIVER:
        {
            return iCpReceiver->canStop();
        }

        case UPNPAV:
        {
            return iCpUpnpAv->canStop();
        }

        default:
            return false;
    }
}

TBool ControlPointProxy::canPlay()
{
    switch (iActiveSource)
    {
        case PLAYLIST:
        {
            return iCpPlaylist->canPlay();
        }

#ifdef ENABLE_RADIO
        case RADIO:
        {
            return iCpRadio->canPlay();
        }
#endif // ENABLE_RADIO

        case RECEIVER:
        {
            return iCpReceiver->canPlay();
        }

        case UPNPAV:
        {
            return iCpUpnpAv->canPlay();
        }

        default:
            return false;
    }
}

TBool ControlPointProxy::canPause()
{
    switch (iActiveSource)
    {
        case PLAYLIST:
        {
            return iCpPlaylist->canPause();
        }

        case UPNPAV:
        {
            return iCpUpnpAv->canPause();
        }

        default:
            return false;
    }
}
