#include "ExampleMediaPlayer.h"

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

#import "ConfigPersistentStore.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Example;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Web;

const Brn ExampleMediaPlayer::kSongcastSenderIconFileName("SongcastSenderIcon");


ExampleMediaPlayer::ExampleMediaPlayer(Net::DvStack& aDvStack, const Brx& aUdn, const TChar* aRoom, const TChar* aProductName,
const Brx& aUserAgent)
: iSemShutdown("TMPS", 0)
, iDisabled("test", 0)
, iLive(false)
, iUserAgent(aUserAgent)
, iObservableFriendlyName(new Bws<RaopDevice::kMaxNameBytes>())
, iTxTimestamper(NULL)
, iRxTimestamper(NULL)
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
    iDevice->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

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
    iDeviceUpnpAv->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

    // create read/write store.  This creates a number of static (constant) entries automatically
    // FIXME - to be removed; this only exists to populate static data
    iRamStore = new RamStore();

    // create a read/write store using the new config framework
    iConfigPersistentStore = new ConfigPersistentStore();

    // FIXME - available store keys should be listed somewhere
    iConfigPersistentStore->Write(Brn("Product.Room"), Brn(aRoom));
    iConfigPersistentStore->Write(Brn("Product.Name"), Brn(aProductName));
    
    // Volume Control
    VolumeProfile  volumeProfile;
    VolumeConsumer volumeInit;
    volumeInit.SetVolume(iVolume);
    volumeInit.SetBalance(iVolume);
    volumeInit.SetFade(iVolume);
    
    
    PipelineInitParams* pipelineParams = PipelineInitParams::New();
    pipelineParams->SetThreadPriorityMax(kPrioritySystemHighest-1);
    
    // create MediaPlayer
    iMediaPlayer = new MediaPlayer( aDvStack,
                                   *iDevice,
                                   *iRamStore,
                                   *iConfigPersistentStore,
                                   pipelineParams,
                                   volumeInit,
                                   volumeProfile,
                                   aUdn,
                                   Brn(aRoom),
                                   Brn(aProductName));
    
    // Register an observer to monitor the pipeline status.
    iMediaPlayer->Pipeline().AddObserver(*this);
    
    // Set up config app.
    static const TUint addr = 0;    // Bind to all addresses.
    static const TUint port = 0;    // Bind to whatever free port the OS allocates to the framework server.
    iAppFramework = new WebAppFramework(aDvStack.Env(), addr, port, kMaxUiTabs, kUiSendQueueSize);
}

ExampleMediaPlayer::~ExampleMediaPlayer()
{
    ASSERT(!iDevice->Enabled());
    delete iAppFramework;
    delete iCpProxy;
    delete iMediaPlayer;
    delete iDevice;
    delete iDeviceUpnpAv;
    delete iRamStore;
    delete iConfigPersistentStore;
}

void ExampleMediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iMediaPlayer->AddAttribute(aAttribute);
}

void ExampleMediaPlayer::Run(Net::CpStack& aCpStack)
{
    // Register all of our supported plugin formats
    RegisterPlugins(iMediaPlayer->Env());
    
    // now we are ready to start our mediaplayer
    iMediaPlayer->Start();

    AddConfigApp();
    iAppFramework->Start();

    // now enable our UPNP devices
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();

    iCpProxy = new ControlPointProxy(aCpStack, *(Device()));
}


void ExampleMediaPlayer::AddConfigApp()
{
    std::vector<const Brx*> sourcesBufs;
    Product& product = iMediaPlayer->Product();
    for (TUint i=0; i<product.SourceCount(); i++) {
        Bws<ISource::kMaxSystemNameBytes> systemName;
        Bws<ISource::kMaxSourceNameBytes> name;
        Bws<ISource::kMaxSourceTypeBytes> type;
        TBool visible;
        product.GetSourceDetails(i, systemName, type, name, visible);
        sourcesBufs.push_back(new Brh(systemName));
    }
    // FIXME - take resource dir as param or copy res dir to build dir
    iConfigApp = new ConfigAppMediaPlayer(iMediaPlayer->ConfigManager(), sourcesBufs, Brn("SoftPlayer"), Brn(""), kMaxUiTabs, kUiSendQueueSize);
    iAppFramework->Add(iConfigApp, MakeFunctorGeneric(*this, &ExampleMediaPlayer::PresentationUrlChanged));
    for (TUint i=0;i<sourcesBufs.size(); i++) {
        delete sourcesBufs[i];
    }
}

PipelineManager& ExampleMediaPlayer::Pipeline()
{
    return iMediaPlayer->Pipeline();
}

DvDeviceStandard* ExampleMediaPlayer::Device()
{
    return iDevice;
}

void ExampleMediaPlayer::SetSongcastTimestampers(IOhmTimestamper& aTxTimestamper, IOhmTimestamper& aRxTimestamper)
{
    iTxTimestamper = &aTxTimestamper;
    iRxTimestamper = &aRxTimestamper;
}

void ExampleMediaPlayer::SetSongcastTimestampMappers(IOhmTimestamper& aTxTsMapper, IOhmTimestamper& aRxTsMapper)
{
    iTxTsMapper = &aTxTsMapper;
    iRxTsMapper = &aRxTsMapper;
}


void ExampleMediaPlayer::StopPipeline()
{
    TUint waitCount = 0;
    
    if (TryDisable(*iDevice))
    {
        waitCount++;
    }
    
    if (TryDisable(*iDeviceUpnpAv))
    {
        waitCount++;
    }
    
    while (waitCount > 0)
    {
        iDisabled.Wait();
        waitCount--;
    }
    
    iMediaPlayer->Quit();
    iSemShutdown.Signal();
}

TBool ExampleMediaPlayer::CanPlay()
{
    return ((iState == EPipelineStopped) || (iState == EPipelinePaused));
}

TBool ExampleMediaPlayer::CanPause()
{
    return (!iLive &&
            ((iState == EPipelinePlaying) || (iState == EPipelineBuffering)));
}

TBool ExampleMediaPlayer::CanHalt()
{
    return ((iState == EPipelinePlaying) || (iState == EPipelinePaused));
}

void ExampleMediaPlayer::PlayPipeline()
{
    if (CanPlay())
    {
        iCpProxy->playlistPlay();
    }
}

void ExampleMediaPlayer::PausePipeline()
{
    if (CanPause())
    {
        iCpProxy->playlistPause();
    }
}

void ExampleMediaPlayer::HaltPipeline()
{
    if (CanHalt())
    {
        iCpProxy->playlistStop();
    }
}


void ExampleMediaPlayer::RegisterPlugins(Environment& aEnv)
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
    //"tidalhifi.com:*:*:*,"          // Tidal
    //"qobuz.com:*:*:*,"              // Qobuz
    );
    DoRegisterPlugins(aEnv, kSupportedProtocols);
}

void ExampleMediaPlayer::DoRegisterPlugins(Environment& aEnv, const Brx& aSupportedProtocols)
{
    // Add codecs
    Log::Print("Codec Registration: [\n");
    
    //Log::Print("Codec\tAac\n");
    // disabled - requires a patent license
    //iMediaPlayer->Add(Codec::CodecFactory::NewAac());
    Log::Print("Codec\tAiff\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff());
    Log::Print("Codec\tAifc\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc());
    Log::Print("Codec\tAlac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAlac());
    Log::Print("Codec\tAdts\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAdts());
    Log::Print("Codec:\tFlac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac());
    // Disabled by default - requires patent and copyright licenses
    //Log::Print("Codec:\tMP3\n");
    //iMediaPlayer->Add(Codec::CodecFactory::NewMp3());
    Log::Print("Codec\tPcm\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    Log::Print("Codec\tVorbis\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis());
    Log::Print("Codec\tWav\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewWav());
    
    Log::Print("]\n");
    
    // Add protocol modules
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHls(aEnv, iUserAgent));
    
    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer,
                                                 aSupportedProtocols));
    
    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer,
                                               *iDeviceUpnpAv,
                                               aSupportedProtocols));
    
    iMediaPlayer->Add(SourceFactory::NewReceiver(*iMediaPlayer,
                                                 NULL,
                                                 iTxTimestamper,
                                                 NULL,
                                                 iRxTimestamper,
                                                 NULL,
                                                 kSongcastSenderIconFileName));
}


void ExampleMediaPlayer::WriteResource(const Brx& aUriTail, TIpAddress /*aInterface*/, std::vector<char*>& /*aLanguageList*/, IResourceWriter& aResourceWriter)
{
    if (aUriTail == kSongcastSenderIconFileName)
    {
        aResourceWriter.WriteResourceBegin(sizeof(kIconDriverSongcastSender), kIconDriverSongcastSenderMimeType);
        aResourceWriter.WriteResource(kIconDriverSongcastSender, sizeof(kIconDriverSongcastSender));
        aResourceWriter.WriteResourceEnd();
    }
}


void ExampleMediaPlayer::PresentationUrlChanged(const Brx& aUrl)
{
    Bws<Uri::kMaxUriBytes+1> url(aUrl);   // +1 for '\0'
    iDevice->SetAttribute("Upnp.PresentationUrl", url.PtrZ());
}

TBool ExampleMediaPlayer::TryDisable(DvDevice& aDevice)
{
    if (aDevice.Enabled())
    {
        aDevice.SetDisabled(MakeFunctor(*this, &ExampleMediaPlayer::Disabled));
        return true;
    }
    return false;
}

void ExampleMediaPlayer::Disabled()
{
    iDisabled.Signal();
}

// Pipeline Observer callbacks.
void ExampleMediaPlayer::NotifyPipelineState(Media::EPipelineState aState)
{
    
    switch (aState)
    {
        case EPipelineStopped:
            Log::Print("Pipeline State: Stopped\n");
            break;
        case EPipelinePaused:
            Log::Print("Pipeline State: Paused\n");
            break;
        case EPipelinePlaying:
            Log::Print("Pipeline State: Playing\n");
            break;
        case EPipelineBuffering:
            Log::Print("Pipeline State: Buffering\n");
            break;
        case EPipelineWaiting:
            Log::Print("Pipeline State: Waiting\n");
            break;
        default:
            Log::Print("Pipeline State: UNKNOWN\n");
            break;
    }
    
    if(iDriver != NULL)
    {
        if(aState==EPipelinePaused)
            iDriver->pause();
        else if(aState==EPipelinePlaying)
            iDriver->resume();
    }
    
    iState = aState;
}

void ExampleMediaPlayer::NotifyMode(const Brx& /*aMode*/, const Media::ModeInfo& /*aInfo*/)
{
}

void ExampleMediaPlayer::NotifyTrack(Media::Track& /*aTrack*/, const Brx& /*aMode*/, TBool /*aStartOfStream*/)
{
}

void ExampleMediaPlayer::NotifyMetaText(const Brx& /*aText*/)
{
}

void ExampleMediaPlayer::NotifyTime(TUint /*aSeconds*/, TUint /*aTrackDurationSeconds*/)
{
}

void ExampleMediaPlayer::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
    iLive = aStreamInfo.Live();
}




