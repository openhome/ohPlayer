#include "ExampleMediaPlayer.h"

#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Web/ConfigUi/FileResourceHandler.h>
#include <OpenHome/Web/ConfigUi/ConfigUiMediaPlayer.h>
#include <OpenHome/Web/ConfigUi/ConfigUi.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Private/Shell.h>
#include <OpenHome/Private/ShellCommandDebug.h>

#include "IconOpenHome.h"
#include "OptionalFeatures.h"

#import "ConfigPersistentStore.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Example;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Web;

const Brn kPrefix("OpenHome");

const Brn ExampleMediaPlayer::kIconOpenHomeFileName("OpenHomeIcon");

#define DBG(_x)
//#define DBG(_x)   Log::Print(_x)

ExampleMediaPlayer::ExampleMediaPlayer(Net::DvStack& aDvStack,
                                       Net::CpStack& aCpStack,
                                       const Brx& aUdn,
                                       const TChar* aRoom,
                                       const TChar* aProductName,
                                       const Brx& aUserAgent)
: iDisabled("test", 0)
, iUserAgent(aUserAgent)
, iTxTimestamper(NULL)
, iRxTimestamper(NULL)
{
    iShell = new Shell(aDvStack.Env(), kShellPort);
    iShellDebug = new ShellCommandDebug(*iShell);
    iInfoLogger = new Media::AllocatorInfoLogger();
  
    // clamp the aRoom and aProductName buffers to the maximum allowed values from Product.h
    TChar iRoom[Product::kMaxRoomBytes+1];
    strncpy(iRoom, aRoom, Product::kMaxRoomBytes);
    iRoom[Product::kMaxRoomBytes] = 0;
    
    TChar iProductName[Product::kMaxNameBytes+1];
    strncpy(iProductName, aProductName, Product::kMaxNameBytes);
    iProductName[Product::kMaxNameBytes] = 0;
    
    // Do NOT set UPnP friendly name attributes at this stage.
    // (Wait until MediaPlayer is created so that friendly name can be
    // observed.)

    // create UPnP device
    // Friendly name not set here
    iDevice = new DvDeviceStandard(aDvStack, aUdn, *this);
    iDevice->SetAttribute("Upnp.Domain", "av.openhome.org");
    iDevice->SetAttribute("Upnp.Type", "Source");
    iDevice->SetAttribute("Upnp.Version", "1");
    iDevice->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDevice->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

    // create separate UPnP device for standard MediaRenderer
    Bws<256> buf(aUdn);
    buf.Append("-MediaRenderer");
    iDeviceUpnpAv = new DvDeviceStandard(aDvStack, buf);
    // Friendly name not set here
    iDeviceUpnpAv->SetAttribute("Upnp.Domain", "upnp.org");
    iDeviceUpnpAv->SetAttribute("Upnp.Type", "MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.Version", "1");
    iDeviceUpnpAv->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDeviceUpnpAv->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

    // create read/write store.  This creates a number of static (constant) entries automatically
    iRamStore = new RamStore(kIconOpenHomeFileName);

    // create a read/write store using the new config framework
    iConfigPersistentStore = new ConfigPersistentStore();

    // Volume Control
    VolumeProfile  volumeProfile;
    VolumeConsumer volumeInit;
    volumeInit.SetVolume(iVolume);
    volumeInit.SetBalance(iVolume);
    volumeInit.SetFade(iVolume);
    
    PipelineInitParams* pipelineParams = PipelineInitParams::New();
    pipelineParams->SetThreadPriorityMax(kPrioritySystemHighest-1);
    pipelineParams->SetStarvationRamperMinSize(100 * Jiffies::kPerMs);
    pipelineParams->SetGorgerDuration(pipelineParams->DecodedReservoirJiffies());
    
    // create MediaPlayer
    auto p = MediaPlayerInitParams::New(Brn(aRoom),
                                        Brn(aProductName),
                                        kPrefix);
    
    iMediaPlayer = new MediaPlayer( aDvStack,
                                    aCpStack,
                                   *iDevice,
                                   *iRamStore,
                                   *iConfigPersistentStore,
                                   pipelineParams,
                                   volumeInit,
                                   volumeProfile,
                                   *iInfoLogger,
                                   aUdn,
                                   p);
    
    iFnUpdaterStandard = new
        Av::FriendlyNameAttributeUpdater(iMediaPlayer->FriendlyNameObservable(),
                                         iMediaPlayer->ThreadPool(),
                                        *iDevice);

    iFnManagerUpnpAv = new
        Av::FriendlyNameManagerUpnpAv(kPrefix, iMediaPlayer->Product());

    iFnUpdaterUpnpAv = new
        Av::FriendlyNameAttributeUpdater(*iFnManagerUpnpAv,
                                         iMediaPlayer->ThreadPool(),
                                         *iDeviceUpnpAv);

    // Set up config app.
    static const TUint addr = 0;    // Bind to all addresses.
    static const TUint port = 0;    // Bind to whatever free port the OS allocates to the framework server.
    
    auto webAppInit = new WebAppFrameworkInitParams();
    webAppInit->SetServerPort(port);
    webAppInit->SetSendQueueSize(kUiSendQueueSize);
    
    iAppFramework = new WebAppFramework(aDvStack.Env(),
                                        webAppInit,
                                        iMediaPlayer->ThreadPool());
}

ExampleMediaPlayer::~ExampleMediaPlayer()
{
    ASSERT(!iDevice->Enabled());
    delete iAppFramework;
    delete iFnUpdaterStandard;
    delete iFnUpdaterUpnpAv;
    delete iFnManagerUpnpAv;
    Pipeline().Quit();
    delete iCpProxy;
    delete iMediaPlayer;
    delete iInfoLogger;
    delete iShellDebug;
    delete iShell;
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
    
    AddConfigApp();
    // now we are ready to start our mediaplayer
    iMediaPlayer->Start(iRebootHandler);
    iAppFramework->Start();

    // now enable our UPNP devices
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();

    iCpProxy = new ControlPointProxy(aCpStack,
                                     *(Device()),
                                     *(UpnpAvDevice()),
                                     iMediaPlayer->Pipeline(),
                                     *iDriver);
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

    // Obtaiun the full path to the application resource directory.
    NSString *nsAppRes      = [[NSBundle mainBundle] resourcePath];
    NSString *nsFullResPath = [NSString stringWithFormat:@"%@/SoftPlayer/",
                               nsAppRes];
    const TChar *resDir     = [nsFullResPath cStringUsingEncoding:NSUTF8StringEncoding];

    iConfigApp = new ConfigAppMediaPlayer(*iInfoLogger,
                                          iMediaPlayer->Env(),
                                          iMediaPlayer->Product(),
                                          iMediaPlayer->ConfigManager(),
                                          iFileResourceHandlerFactory,
                                          sourcesBufs,
                                          Brn("SoftPlayer"),
                                          Brn(resDir),
                                          30, //Resource handler count
                                          kMaxUiTabs,
                                          kUiSendQueueSize,
                                          iRebootHandler);

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

DvDevice* ExampleMediaPlayer::UpnpAvDevice()
{
    return iDeviceUpnpAv;
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
}

TBool ExampleMediaPlayer::CanPlay()
{
    return iCpProxy->canPlay();
}

TBool ExampleMediaPlayer::CanPause()
{
    return iCpProxy->canPause();
}

TBool ExampleMediaPlayer::CanHalt()
{
    return iCpProxy->canStop();
}

void ExampleMediaPlayer::PlayPipeline()
{
    iCpProxy->cpPlay();
}

void ExampleMediaPlayer::PausePipeline()
{
    iCpProxy->cpPause();
}

void ExampleMediaPlayer::HaltPipeline()
{
    iCpProxy->cpStop();
}


void ExampleMediaPlayer::RegisterPlugins(Environment& aEnv)
{
    // Add containers
#ifndef USE_AFSCODEC
    iMediaPlayer->Add(Codec::ContainerFactory::NewId3v2());
    iMediaPlayer->Add(Codec::ContainerFactory::NewMpeg4(iMediaPlayer->MimeTypes()));
#endif // USE_AFSCODEC
    iMediaPlayer->Add(Codec::ContainerFactory::NewMpegTs(iMediaPlayer->MimeTypes()));

    // Add codecs
    Log::Print("Codec Registration: [\n");

    Log::Print("Codec:\tFlac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac(iMediaPlayer->MimeTypes()));
    Log::Print("Codec\tWav\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewWav(iMediaPlayer->MimeTypes()));
    Log::Print("Codec\tAiff\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff(iMediaPlayer->MimeTypes()));
    Log::Print("Codec\tAifc\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc(iMediaPlayer->MimeTypes()));
#ifdef USE_AFSCODEC
#if defined (ENABLE_AAC) || defined (ENABLE_MP3)
    // Use distributable MP3/AAC Codec, based on an Audio File Stream
    iMediaPlayer->Add(Codec::CodecFactory::NewMp3(iMediaPlayer->MimeTypes()));
#endif // ENABLE_AAC || ENABLE_MP3
#else // USE_AFSCODEC
#ifdef ENABLE_AAC
    // AAC is disabled by default as it requires a patent license
    Log::Print("Codec\tAac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAacFdkMp4(iMediaPlayer->MimeTypes()));
    Log::Print("Codec\tAdts\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAacFdkAdts(iMediaPlayer->MimeTypes()));
#endif  /* ENABLE_AAC */
#ifdef ENABLE_MP3
    // MP3 is disabled by default as it requires patent and copyright licenses
    Log::Print("Codec:\tMP3\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewMp3(iMediaPlayer->MimeTypes()));
#endif  /* ENABLE_MP3 */
#endif // USE_AFSCODEC
    Log::Print("Codec\tAlac\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewAlacApple(iMediaPlayer->MimeTypes()));
    Log::Print("Codec\tPcm\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    Log::Print("Codec\tVorbis\n");
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis(iMediaPlayer->MimeTypes()));
    
    Log::Print("]\n");
    
    // Add protocol modules
    auto& ssl = iMediaPlayer->Ssl();
    
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHls(aEnv, ssl, iUserAgent));
    
    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer, Optional<IPlaylistLoader>(nullptr)));
    
    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer,
                                               *iDeviceUpnpAv));
    
    iMediaPlayer->Add(SourceFactory::NewReceiver(
                                  *iMediaPlayer,
                                   Optional<IClockPuller>(nullptr),
                                   Optional<IOhmTimestamper>(iTxTimestamper),
                                   Optional<IOhmTimestamper>(iRxTimestamper),
                                   Optional<IOhmMsgProcessor>(nullptr)));

#ifdef ENABLE_TIDAL
    // You must define your Tidal token
    iMediaPlayer->Add(ProtocolFactory::NewTidal(
                                             aEnv,
                                             Brn(TIDAL_TOKEN),
                                            *iMediaPlayer));
#endif  /* ENABLE_TIDAL */
    
#ifdef ENABLE_QOBUZ
    // You must define your QOBUZ appId and secret key
    iMediaPlayer->Add(ProtocolFactory::NewQobuz(
                                             Brn(QOBUZ_APPID),
                                             Brn(QOBUZ_SECRET),
                                            *iMediaPlayer));
#endif  /* ENABLE_QOBUZ */
    
#ifdef ENABLE_RADIO
    // Radio is disabled by default as many stations depend on AAC
    iMediaPlayer->Add(SourceFactory::NewRadio(*iMediaPlayer,
                                               Brn(TUNEIN_PARTNER_ID)));
#endif  /* ENABLE_RADIO */
}


void ExampleMediaPlayer::WriteResource(const Brx& aUriTail,
                                       const TIpAddress& /*aInterface*/,
                                       std::vector<char*>& /*aLanguageList*/,
                                       IResourceWriter& aResourceWriter)
{
    if (aUriTail == kIconOpenHomeFileName)
    {
        aResourceWriter.WriteResourceBegin(sizeof(kIconOpenHome),
                                           kIconOpenHomeMimeType);
        aResourceWriter.WriteResource(kIconOpenHome,
                                      sizeof(kIconOpenHome));
        aResourceWriter.WriteResourceEnd();
    }
}


void ExampleMediaPlayer::PresentationUrlChanged(const Brx& aUrl)
{
    iPresentationUrl.Replace(aUrl);
    iMediaPlayer->Product().SetConfigAppUrl(iPresentationUrl);
    iDevice->SetAttribute("Upnp.PresentationUrl", iPresentationUrl.PtrZ());
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


