#include <glib.h>
#include <string>

#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/ContainerFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Web/ConfigUi/FileResourceHandler.h>
#include <OpenHome/Web/ConfigUi/ConfigUiMediaPlayer.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Web/ConfigUi/ConfigUi.h>
#include <OpenHome/Private/Shell.h>
#include <OpenHome/Private/ShellCommandDebug.h>
#include <OpenHome/OAuth.h>

#include "ConfigGTKKeyStore.h"
#include "ControlPointProxy.h"
#include "CustomMessages.h"
#include "ExampleMediaPlayer.h"
#include "IconOpenHome.h"
#include "OpenHomePlayer.h"
#include "MediaPlayerIF.h"
#include "OptionalFeatures.h"
#include "RamStore.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;
using namespace OpenHome::Web;

// ExampleMediaPlayer
const Brn kPrefix("OpenHome");

const Brn ExampleMediaPlayer::kIconOpenHomeFileName("OpenHomeIcon");
const Brn ExampleMediaPlayer::kResourceDir(Brn("/usr/share/"
                                              "openhome-player/res/"));
const Brn ExampleMediaPlayer::kPlayerName("Softplayer");

ExampleMediaPlayer::ExampleMediaPlayer(Net::DvStack& aDvStack,
									   Net::CpStack& aCpStack,
                                       const Brx& aUdn,
                                       const TChar* aRoom,
                                       const TChar* aProductName,
                                       const Brx& aUserAgent)
    : iSemShutdown("TMPS", 0)
    , iDisabled("test", 0)
    , iCpProxy(NULL)
    , iTxTimestamper(NULL)
    , iRxTimestamper(NULL)
    , iTxTsMapper(NULL)
    , iRxTsMapper(NULL)
    , iUserAgent(aUserAgent)
{
    iShell = new Shell(aDvStack.Env(), kShellPort);
    iShellDebug = new ShellCommandDebug(*iShell);
    iInfoLogger = new Media::AllocatorInfoLogger();

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

    // The renderer name should be <room name>:<UPnP AV source name> to allow
    // our control point to match the renderer device to the upnp av source.
    iDeviceUpnpAv = new DvDeviceStandard(aDvStack, buf);
    // Friendly name not set here
    iDeviceUpnpAv->SetAttribute("Upnp.Domain", "upnp.org");
    iDeviceUpnpAv->SetAttribute("Upnp.Type", "MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.Version", "1");
    iDeviceUpnpAv->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDeviceUpnpAv->SetAttribute("Upnp.ModelName", "ExampleMediaPlayer");

    // create read/write store.  This creates a number of static (constant)
    // entries automatically
    iRamStore = new RamStore(kIconOpenHomeFileName);

    // create a read/write store using the new config framework
    iConfigStore = ConfigGTKKeyStore::getInstance();

    // Volume Control
    VolumeProfile  volumeProfile;
    VolumeConsumer volumeInit;

    if (iVolume.IsVolumeSupported())
    {
        volumeInit.SetVolume(iVolume);
        volumeInit.SetBalance(iVolume);
        volumeInit.SetFade(iVolume);
    }
    else
    {
        Log::Print("Volume Control Unavailable\n");
    }

    // Set pipeline thread priority just below the pipeline animator.
    iInitParams = PipelineInitParams::New();
    iInitParams->SetThreadPriorityMax(kPriorityHighest);
    iInitParams->SetStarvationRamperMinSize(100 * Jiffies::kPerMs);
    iInitParams->SetGorgerDuration(iInitParams->DecodedReservoirJiffies());

    // create MediaPlayer
    iAudioTime = new AudioTimeCpu(aDvStack.Env());
	auto mpInit = MediaPlayerInitParams::New(Brn(aRoom), Brn(aProductName), kPrefix);
    
	iMediaPlayer = new MediaPlayer( aDvStack, aCpStack, *iDevice, *iRamStore,
                                   *iConfigStore, iInitParams,
                                   *iAudioTime, volumeInit, volumeProfile, *iInfoLogger,
                                    aUdn, mpInit);

#ifdef DEBUG
    iPipelineStateLogger = new LoggingPipelineObserver();
    iMediaPlayer->Pipeline().AddObserver(*iPipelineStateLogger);
#endif // DEBUG

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
	auto webAppInit = new WebAppFrameworkInitParams();
    webAppInit->SetServerPort(0);
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
    delete iCpProxy;
#ifdef DEBUG
    delete iPipelineStateLogger;
#endif // DEBUG
    delete iMediaPlayer;
    delete iInfoLogger;
    delete iShellDebug;
    delete iShell;
    delete iDevice;
    delete iDeviceUpnpAv;
    delete iRamStore;
	delete iAudioTime;
}

Environment& ExampleMediaPlayer::Env()
{
    return iMediaPlayer->Env();
}

void ExampleMediaPlayer::SetSongcastTimestampers(
               IOhmTimestamper& aTxTimestamper, IOhmTimestamper& aRxTimestamper)
{
    iTxTimestamper = &aTxTimestamper;
    iRxTimestamper = &aRxTimestamper;
}

void ExampleMediaPlayer::SetSongcastTimestampMappers(
                                              IOhmTimestamper& aTxTsMapper,
                                              IOhmTimestamper& aRxTsMapper)
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

void ExampleMediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iMediaPlayer->AddAttribute(aAttribute);
}

void ExampleMediaPlayer::RunWithSemaphore(Net::CpStack& aCpStack)
{
    RegisterPlugins(iMediaPlayer->Env());
    AddConfigApp();
    iMediaPlayer->Start(iRebootHandler);
    iAppFramework->Start();
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();

    iCpProxy = new ControlPointProxy(aCpStack,
                                     *(Device()),
                                     *(UpnpAvDevice()),
                                     iMediaPlayer->Pipeline());

    iSemShutdown.Wait();
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

void ExampleMediaPlayer::RegisterPlugins(Environment& aEnv)
{
    // Register containers.
#ifndef USE_LIBAVCODEC
    iMediaPlayer->Add(Codec::ContainerFactory::NewId3v2());
    // Registering this container breaks the Libavcodec AAC ptobe.
    iMediaPlayer->Add(Codec::ContainerFactory::NewMpeg4(iMediaPlayer->MimeTypes()));
#endif // USE_LIBAVCODEC
    iMediaPlayer->Add(Codec::ContainerFactory::NewMpegTs(iMediaPlayer->MimeTypes()));

    // Add codecs
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewWav(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc(iMediaPlayer->MimeTypes()));
#ifdef USE_LIBAVCODEC
#if defined (ENABLE_AAC) || defined (ENABLE_MP3)
    // Use distributable MP3/AAC Codec, using libavcodec
    iMediaPlayer->Add(Codec::CodecFactory::NewMp3(iMediaPlayer->MimeTypes()));
#endif // ENABLE_AAC || ENABLE_MP3
#else // USE_LIBAVCODEC
#ifdef ENABLE_AAC
    // Disabled by default - requires patent license
    iMediaPlayer->Add(Codec::CodecFactory::NewAacFdkMp4(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAacFdkAdts(iMediaPlayer->MimeTypes()));
#endif // ENABLE_AAC

#ifdef ENABLE_MP3
    // Disabled by default - requires patent and copyright licenses
    iMediaPlayer->Add(Codec::CodecFactory::NewMp3(iMediaPlayer->MimeTypes()));
#endif // ENABLE_MP3
#endif // USE_LIBAVCODEC
    iMediaPlayer->Add(Codec::CodecFactory::NewAlacApple(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis(iMediaPlayer->MimeTypes()));

    // Add protocol modules
    SslContext& ssl = iMediaPlayer->Ssl();

    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHls(aEnv, ssl, iUserAgent));

    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer,Optional<IPlaylistLoader>(nullptr)));

    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer, *iDeviceUpnpAv));

    iMediaPlayer->Add(SourceFactory::NewReceiver(
                                  *iMediaPlayer,
                                   Optional<IClockPuller>(nullptr),
                                   Optional<IOhmTimestamper>(iTxTimestamper),
                                   Optional<IOhmTimestamper>(iRxTimestamper),
                                   Optional<IOhmMsgProcessor>(nullptr)));

#ifdef ENABLE_TIDAL
    // You must define your Tidal token
    std::vector<OpenHome::OAuthAppDetails> aAppDetails;
    aAppDetails.emplace_back(TildaAuthAppId, TildaAuthClientId, TildaAuthClientSecret);

    iMediaPlayer->Add(ProtocolFactory::NewTidal(
                                            aEnv,
											ssl,
                                            TildaAuthClientId,
                                            TildaAuthClientSecret,
                                            aAppDetails,
                                            *iMediaPlayer));
#endif  // ENABLE_TIDAL

#ifdef ENABLE_QOBUZ
    // You must define your QOBUZ appId and secret key
    iMediaPlayer->Add(ProtocolFactory::NewQobuz(
                                            QoubusAuthClientId,
                                            QoubusAuthClientSecret,
                                            *iMediaPlayer,
											iUserAgent));
#endif  // ENABLE_QOBUZ

#ifdef ENABLE_RADIO
    // Radio is disabled by default as many stations depend on AAC
    iMediaPlayer->Add(SourceFactory::NewRadio(*iMediaPlayer,
                                              TuneinAuthPartnerId));
#endif  // ENABLE_RADIO
}

void ExampleMediaPlayer::WriteResource(const Brx&          aUriTail,
                                       const TIpAddress&   /*aInterface*/,
                                       std::vector<char*>& /*aLanguageList*/,
                                       IResourceWriter&    aResourceWriter)
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

    iConfigApp = new ConfigAppMediaPlayer(*iInfoLogger,
                                          iMediaPlayer->Env(),
                                          iMediaPlayer->Product(),
                                          iMediaPlayer->ConfigManager(),
                                          iFileResourceHandlerFactory,
                                          sourcesBufs,
                                          kPlayerName,
                                          kResourceDir,
                                          kMinWebUiResourceThreads,
                                          kMaxUiTabs,
                                          kUiSendQueueSize,
                                          kUiMsgBufCount, 
                                          kUiMsgBufBytes,
                                          iRebootHandler);

    iAppFramework->Add(iConfigApp,              // iAppFramework takes ownership
                       MakeFunctorGeneric(*this, &ExampleMediaPlayer::PresentationUrlChanged));

    for (TUint i=0;i<sourcesBufs.size(); i++) {
        delete sourcesBufs[i];
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

// ExampleMediaPlayerInit

OpenHome::Net::Library* ExampleMediaPlayerInit::CreateLibrary(TIpAddress preferredSubnet)
{
    TUint                 index         = 0;
    InitialisationParams *initParams    = InitialisationParams::Create();
    TIpAddress            lastSubnet    = TIpAddress();
    const TChar          *lastSubnetStr = "Subnet.LastUsed";

    //initParams->SetDvEnableBonjour();

    Net::Library* lib = new Net::Library(initParams);

    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();

    if (subnetList->size() == 0)
    {
        Log::Print("ERROR: No adapters found\n");
        ASSERTS();
    }

    ConfigGTKKeyStore *configStore = ConfigGTKKeyStore::getInstance();

    // Check the configuration store for the last subnet joined.
    try
    {
        if(!TIpAddressUtils::IsZero(lastSubnet))
        {
            Bwn lastSubnetBuf = Bwn((TByte *)&lastSubnet, sizeof(lastSubnet));
            configStore->Read(Brn(lastSubnetStr), lastSubnetBuf);
        }
    }
    catch (StoreKeyNotFound&)
    {
        // No previous subnet stored.
    }
    catch (StoreReadBufferUndersized&)
    {
        // This shouldn't happen.
        Log::Print("ERROR: Invalid 'Subnet.LastUsed' property in Config "
                   "Store\n");
    }

    for (TUint i=0; i<subnetList->size(); ++i)
    {
        TIpAddress subnet = (*subnetList)[i]->Subnet();

        // If the requested subnet is available, choose it
      
        const TBool isPreferredSubnet = TIpAddressUtils::Equals(preferredSubnet, subnet);

        if (isPreferredSubnet)
        {
            index = i;
            break;
        }

        // If the last used subnet is available, note it.
        // We'll fall back to it if the requested subnet is not available.
       const TBool isLastSubnet =  TIpAddressUtils::Equals(lastSubnet, subnet);

        if (isLastSubnet)
        {
            index = i;
        }
    }

    // Choose the required adapter.
    TIpAddress subnet = (*subnetList)[index]->Subnet();

    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);

    // Store the selected subnet in persistent storage.
    configStore->Write(Brn(lastSubnetStr),
                       Brn((TByte *)&subnet, sizeof(subnet)));

    Bws<TIpAddressUtils::kMaxAddressBytes> addressBuf;
    TIpAddressUtils::ToString(subnet, addressBuf);
    Log::Print("using subnet %.*s\n", PBUF(addressBuf));

    return lib;
}
