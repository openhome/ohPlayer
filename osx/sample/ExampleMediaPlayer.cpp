#include "BaseMediaPlayer.h"

#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Web/ConfigUi/ConfigUi.h>
#include <OpenHome/Av/Utils/IconDriverSongcastSender.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Net/Core/DvDevice.h>

#include "ConfigRamStore.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Sample;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Web;

const Brn BaseMediaPlayer::kSongcastSenderIconFileName("SongcastSenderIcon");


BaseMediaPlayer::BaseMediaPlayer(Net::DvStack& aDvStack, const Brx& aUdn, const TChar* aRoom, const TChar* aProductName,
const Brx& aTuneInPartnerId, const Brx& aTidalId, const Brx& aQobuzIdSecret, const Brx& aUserAgent)
: iSemShutdown("TMPS", 0)
, iDisabled("test", 0)
, iObservableFriendlyName(new Bws<RaopDevice::kMaxNameBytes>())
{
    Bws<256> friendlyName;
    friendlyName.Append(aRoom);
    friendlyName.Append(':');
    friendlyName.Append(aProductName);
    
    // create UPnP device
    iDevice = new DvDeviceStandard(aDvStack, aUdn, *this);
    iDevice->SetAttribute("Upnp.Domain", "av.openhome.org");
    iDevice->SetAttribute("Upnp.Type", "Source");
    iDevice->SetAttribute("Upnp.Version", "1");
    iDevice->SetAttribute("Upnp.FriendlyName", friendlyName.PtrZ());
    iDevice->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDevice->SetAttribute("Upnp.ModelName", "Avarice");

    // create separate UPnP device for standard MediaRenderer
    Bws<256> buf(aUdn);
    buf.Append("-MediaRenderer");
    // The renderer name should be <room name>:<UPnP AV source name> to allow
    // our control point to match the renderer device to the upnp av source.
    //
    // FIXME - will have to allow this to be dynamically changed at runtime if
    // someone changes the name of the UPnP AV source.
    // Disable device -> change name -> re-enable device.
    Bws<256> rendererName(aRoom);
    rendererName.Append(":");
    rendererName.Append(SourceUpnpAv::kSourceName);
    iDeviceUpnpAv = new DvDeviceStandard(aDvStack, buf);
    iDeviceUpnpAv->SetAttribute("Upnp.Domain", "upnp.org");
    iDeviceUpnpAv->SetAttribute("Upnp.Type", "MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.Version", "1");
    friendlyName.Append(":MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.FriendlyName", rendererName.PtrZ());
    iDeviceUpnpAv->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDeviceUpnpAv->SetAttribute("Upnp.ModelName", "BaseMediaPlayer");

    // create read/write store.  This creates a number of static (constant) entries automatically
    // FIXME - to be removed; this only exists to populate static data
    iRamStore = new RamStore();

    // create a read/write store using the new config framework
    iConfigRamStore = new ConfigRamStore();

    // FIXME - available store keys should be listed somewhere
    iConfigRamStore->Write(Brn("Product.Room"), Brn(aRoom));
    iConfigRamStore->Write(Brn("Product.Name"), Brn(aProductName));
    
    // Volume Control
    VolumeProfile  volumeProfile;
    VolumeConsumer volumeInit(iVolume);
    
    volumeInit.SetBalance(iVolume);
    volumeInit.SetFade(iVolume);
    
    
    PipelineInitParams* pipelineParams = PipelineInitParams::New();
    pipelineParams->SetThreadPriorityMax(kPriorityHighest);
    
    // create MediaPlayer
    iMediaPlayer = new MediaPlayer( aDvStack,
                                   *iDevice,
                                   *iRamStore,
                                   *iConfigRamStore,
                                   pipelineParams,
                                   volumeInit,
                                   volumeProfile,
                                   aUdn,
                                   Brn("Main Room"),
                                   Brn("Softplayer"));
    
}

BaseMediaPlayer::~BaseMediaPlayer()
{
    ASSERT(!iDevice->Enabled());
    delete iMediaPlayer;
    delete iDevice;
    delete iDeviceUpnpAv;
    delete iRamStore;
    delete iConfigRamStore;
}

void BaseMediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iMediaPlayer->AddAttribute(aAttribute);
}

void BaseMediaPlayer::Run()
{
    // Register all of our supported plugin formats
    RegisterPlugins(iMediaPlayer->Env());
    
    // now we are ready to start our mediaplayer
    iMediaPlayer->Start();
    
    // now enable our UPNP devices
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();

}

PipelineManager& BaseMediaPlayer::Pipeline()
{
    return iMediaPlayer->Pipeline();
}

DvDeviceStandard* BaseMediaPlayer::Device()
{
    return iDevice;
}

void BaseMediaPlayer::RegisterPlugins(Environment& aEnv)
{
    const Brn kSupportedProtocols(
    "http-get:*:audio/x-flac:*,"    // Flac
    "http-get:*:audio/wav:*,"       // Wav
    "http-get:*:audio/wave:*,"      // Wav
    "http-get:*:audio/x-wav:*,"     // Wav
    "http-get:*:audio/aiff:*,"      // AIFF
    "http-get:*:audio/x-aiff:*,"    // AIFF
    "http-get:*:audio/x-m4a:*,"     // Alac
    "http-get:*:audio/x-scpls:*,"   // M3u (content processor)
    "http-get:*:text/xml:*,"        // Opml ??  (content processor)
    "http-get:*:audio/aac:*,"       // Aac
    "http-get:*:audio/aacp:*,"      // Aac
    "http-get:*:audio/mp4:*,"       // Mpeg4 (container)
    "http-get:*:audio/ogg:*,"       // Vorbis
    "http-get:*:audio/x-ogg:*,"     // Vorbis
    "http-get:*:application/ogg:*," // Vorbis
    "tidalhifi.com:*:*:*,"          // Tidal
    );
    DoRegisterPlugins(aEnv, kSupportedProtocols);
}

void BaseMediaPlayer::DoRegisterPlugins(Environment& aEnv, const Brx& aSupportedProtocols)
{
    // Add codecs
    iMediaPlayer->Add(Codec::CodecFactory::NewAac());
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff());
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc());
    iMediaPlayer->Add(Codec::CodecFactory::NewAlac());
    iMediaPlayer->Add(Codec::CodecFactory::NewAdts());
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac());
    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis());
    iMediaPlayer->Add(Codec::CodecFactory::NewWav());


    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer, aSupportedProtocols));
    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer, *iDeviceUpnpAv, aSupportedProtocols));

    Bwh hostName(iDevice->Udn().Bytes()+1); // space for null terminator
    hostName.Replace(iDevice->Udn());
    const TChar* friendlyName;
    iDevice->GetAttribute("Upnp.FriendlyName", &friendlyName);
    iObservableFriendlyName.Replace(Brn(friendlyName));
    iMediaPlayer->Add(SourceFactory::NewRaop(*iMediaPlayer, hostName.PtrZ(), iObservableFriendlyName, macAddr));

    iMediaPlayer->Add(SourceFactory::NewReceiver(*iMediaPlayer, NULL, NULL, kSongcastSenderIconFileName)); // FIXME - will want to replace timestamper with access to a driver on embedded platforms
}



void BaseMediaPlayer::WriteResource(const Brx& aUriTail, TIpAddress /*aInterface*/, std::vector<char*>& /*aLanguageList*/, IResourceWriter& aResourceWriter)
{
    if (aUriTail == kSongcastSenderIconFileName) {
    aResourceWriter.WriteResourceBegin(sizeof(kIconDriverSongcastSender), kIconDriverSongcastSenderMimeType);
    aResourceWriter.WriteResource(kIconDriverSongcastSender, sizeof(kIconDriverSongcastSender));
    aResourceWriter.WriteResourceEnd();
    }
}


void BaseMediaPlayer::PresentationUrlChanged(const Brx& aUrl)
{
    Bws<Uri::kMaxUriBytes+1> url(aUrl);   // +1 for '\0'
    iDevice->SetAttribute("Upnp.PresentationUrl", url.PtrZ());
}

TBool BaseMediaPlayer::TryDisable(DvDevice& aDevice)
{
    if (aDevice.Enabled()) {
    aDevice.SetDisabled(MakeFunctor(*this, &BaseMediaPlayer::Disabled));
    return true;
    }
    return false;
}

void BaseMediaPlayer::Disabled()
{
    iDisabled.Signal();
}


// BaseMediaPlayerInit

OpenHome::Net::Library* BaseMediaPlayerInit::CreateLibrary(TBool aLoopback, TUint aAdapter)
{
    InitialisationParams* initParams = InitialisationParams::Create();
    initParams->SetDvEnableBonjour();
    if (aLoopback == true) {
        initParams->SetUseLoopbackNetworkAdapter();
    }

    Debug::SetLevel(Debug::kSongcast | Debug::kPipeline);
    Net::Library* lib = new Net::Library(initParams);

    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();
    const TUint adapterIndex = aAdapter;
    if (subnetList->size() <= adapterIndex) {
        Log::Print("ERROR: adapter %u doesn't exist\n", adapterIndex);
        ASSERTS();
    }
    Log::Print ("adapter list:\n");
    for (unsigned i=0; i<subnetList->size(); ++i) {
        TIpAddress addr = (*subnetList)[i]->Address();
        Log::Print ("  %d: %d.%d.%d.%d\n", i, addr&0xff, (addr>>8)&0xff, (addr>>16)&0xff, (addr>>24)&0xff);
    }

    TIpAddress subnet = (*subnetList)[adapterIndex]->Subnet();
    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);
    Log::Print("using subnet %d.%d.%d.%d\n", subnet&0xff, (subnet>>8)&0xff, (subnet>>16)&0xff, (subnet>>24)&0xff);
    return lib;
}


