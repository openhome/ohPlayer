#include <Windows.h>

#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Av/Utils/IconDriverSongcastSender.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Web/ConfigUi/ConfigUi.h>

#include "ConfigRegStore.h"
#include "ControlPointProxy.h"
#include "CustomMessages.h"
#include "ExampleMediaPlayer.h"
#include "MemoryCheck.h"
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

const Brn ExampleMediaPlayer::kSongcastSenderIconFileName("SongcastSenderIcon");

ExampleMediaPlayer::ExampleMediaPlayer(HWND hwnd,
                                       Net::DvStack& aDvStack,
                                       const Brx& aUdn,
                                       const TChar* aRoom,
                                       const TChar* aProductName,
                                       const Brx& aUserAgent)
    : iSemShutdown("TMPS", 0)
    , iDisabled("test", 0)
    , iHwnd(hwnd)
    , iCpProxy(NULL)
    , iTxTimestamper(NULL)
    , iRxTimestamper(NULL)
    , iTxTsMapper(NULL)
    , iRxTsMapper(NULL)
    , iUserAgent(aUserAgent)
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

    // create read/write store.  This creates a number of static (constant)
    // entries automatically
    iRamStore = new RamStore();

    // create a read/write store using the new config framework
    iConfigRegStore = new ConfigRegStore();

    iConfigRegStore->Write(Brn("Product.Room"), Brn(aRoom));
    iConfigRegStore->Write(Brn("Product.Name"), Brn(aProductName));

    // Volume Control
    VolumeProfile  volumeProfile;
    VolumeConsumer volumeInit;
    volumeInit.SetVolume(iVolume);
    volumeInit.SetBalance(iVolume);
    volumeInit.SetFade(iVolume);

    // Set pipeline thread priority just below the pipeline animator.
    iInitParams = PipelineInitParams::New();
    iInitParams->SetThreadPriorityMax(kPriorityHighest);
    iInitParams->SetStarvationMonitorMaxSize(Jiffies::kPerMs * 100);

    // create MediaPlayer
    iMediaPlayer = new MediaPlayer( aDvStack, *iDevice, *iRamStore,
                                   *iConfigRegStore, iInitParams,
                                    volumeInit, volumeProfile, aUdn, Brn(aRoom),
                                    Brn(aProductName));

    iPipelineStateLogger = new LoggingPipelineObserver();
    iMediaPlayer->Pipeline().AddObserver(*iPipelineStateLogger);

    // Set up config app.
    static const TUint addr = 0;    // Bind to all addresses.
    static const TUint port = 0;    // Bind to whatever free port the OS
                                    // allocates to the framework server.

    iAppFramework = new WebAppFramework(aDvStack.Env(),
                                        addr,
                                        port,
                                        kMaxUiTabs,
                                        kUiSendQueueSize);
}

ExampleMediaPlayer::~ExampleMediaPlayer()
{
    ASSERT(!iDevice->Enabled());
    delete iAppFramework;
    delete iCpProxy;
    delete iPipelineStateLogger;
    delete iMediaPlayer;
    delete iDevice;
    delete iDeviceUpnpAv;
    delete iRamStore;
    delete iConfigRegStore;
}

Environment& ExampleMediaPlayer::Env()
{
    return iMediaPlayer->Env();
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
    iMediaPlayer->Start();
    iAppFramework->Start();
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();

    iCpProxy = new ControlPointProxy(iHwnd,
                                     aCpStack,
                                     *(Device()),
                                     *(UpnpAvDevice()),
                                     iMediaPlayer->Pipeline());

    iSemShutdown.Wait();
}

void ExampleMediaPlayer::SetSongcastTimestampers(
                                              IOhmTimestamper& aTxTimestamper,
                                              IOhmTimestamper& aRxTimestamper)
{
    iTxTimestamper = &aTxTimestamper;
    iRxTimestamper = &aRxTimestamper;
}

void ExampleMediaPlayer::SetSongcastTimestampMappers(
                                              IOhmTimestampMapper& aTxTsMapper,
                                              IOhmTimestampMapper& aRxTsMapper)
{
    iTxTsMapper = &aTxTsMapper;
    iRxTsMapper = &aRxTsMapper;
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
    // Add codecs
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewWav(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc(iMediaPlayer->MimeTypes()));

#ifdef ENABLE_AAC
    // Disabled by default - requires patent license
    iMediaPlayer->Add(Codec::CodecFactory::NewAac(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAdts(iMediaPlayer->MimeTypes()));
#endif // ENABLE_AAC
    iMediaPlayer->Add(Codec::CodecFactory::NewAlac(iMediaPlayer->MimeTypes()));
#ifdef ENABLE_MP3
    // Disabled by default - requires patent and copyright licenses
    iMediaPlayer->Add(Codec::CodecFactory::NewMp3(iMediaPlayer->MimeTypes()));
#endif // ENABLE_MP3

    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis(iMediaPlayer->MimeTypes()));

    // Add protocol modules
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, iUserAgent));
    iMediaPlayer->Add(ProtocolFactory::NewHls(aEnv, iUserAgent));

    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer));
    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer, *iDeviceUpnpAv));
    iMediaPlayer->Add(SourceFactory::NewReceiver(*iMediaPlayer,
                                                  NULL,
                                                  iTxTimestamper,
                                                  iTxTsMapper,
                                                  iRxTimestamper,
                                                  iRxTsMapper,
                                                  kSongcastSenderIconFileName));

#ifdef ENABLE_TIDAL
    // You must define your Tidal token
    iMediaPlayer->Add(ProtocolFactory::NewTidal(
                                            aEnv,
                                            Brn(TIDAL_TOKEN),
                                            iMediaPlayer->CredentialsManager(),
                                            iMediaPlayer->ConfigInitialiser()));
#endif  // ENABLE_TIDAL

#ifdef ENABLE_QOBUZ
    // You must define your QOBUZ appId and secret key
    iMediaPlayer->Add(ProtocolFactory::NewQobuz(
                                            aEnv,
                                            Brn(QOBUZ_APPID),
                                            Brn(QOBUZ_SECRET),
                                            iMediaPlayer->CredentialsManager(),
                                            iMediaPlayer->ConfigInitialiser()));
#endif  // ENABLE_QOBUZ

#ifdef ENABLE_RADIO
    // Radio is disabled by default as many stations depend on AAC
    iMediaPlayer->Add(SourceFactory::NewRadio(*iMediaPlayer,
                                              NULL,
                                              Brn(TUNEIN_PARTNER_ID)));
#endif  // ENABLE_RADIO
}

void ExampleMediaPlayer::WriteResource(const Brx&          aUriTail,
                                       TIpAddress          /*aInterface*/,
                                       std::vector<char*>& /*aLanguageList*/,
                                       IResourceWriter&    aResourceWriter)
{
    if (aUriTail == kSongcastSenderIconFileName)
    {
        aResourceWriter.WriteResourceBegin(sizeof(kIconDriverSongcastSender),
                                           kIconDriverSongcastSenderMimeType);
        aResourceWriter.WriteResource(kIconDriverSongcastSender,
                                      sizeof(kIconDriverSongcastSender));
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

    iConfigApp = new ConfigAppMediaPlayer(iMediaPlayer->ConfigManager(),
                                          sourcesBufs,
                                          Brn("Softplayer"),
                                          Brn("res/"),
                                          kMaxUiTabs,
                                          kUiSendQueueSize);

    iAppFramework->Add(iConfigApp,              // iAppFramework takes ownership
                       MakeFunctorGeneric(*this, &ExampleMediaPlayer::PresentationUrlChanged));

    for (TUint i=0;i<sourcesBufs.size(); i++) {
        delete sourcesBufs[i];
    }
}

void ExampleMediaPlayer::PresentationUrlChanged(const Brx& aUrl)
{
    if (!iDevice->Enabled()) {
        // FIXME - can only set Product attribute once (meaning no updates on subnet change)
        const TBool firstChange = (iPresentationUrl.Bytes() == 0);
        iPresentationUrl.Replace(aUrl);
        iDevice->SetAttribute("Upnp.PresentationUrl", iPresentationUrl.PtrZ());
        if (firstChange) {
            Bws<128> configAtt("App:Config=");
            configAtt.Append(iPresentationUrl);
            iMediaPlayer->Product().AddAttribute(configAtt);
        }
    }
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

OpenHome::Net::Library* ExampleMediaPlayerInit::CreateLibrary(TUint32 preferredSubnet)
{
    TUint                 index         = 0;
    InitialisationParams *initParams    = InitialisationParams::Create();
    TIpAddress            lastSubnet    = InitArgs::NO_SUBNET;;
    const TChar          *lastSubnetStr = "Subnet.LastUsed";

    //initParams->SetDvEnableBonjour();

    Net::Library* lib = new Net::Library(initParams);
    Debug::SetLevel(/*Debug::kError | */Debug::kPipeline);

    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();

    if (subnetList->size() == 0)
    {
        Log::Print("ERROR: No adapters found\n");
        ASSERTS();
    }

    Configuration::ConfigRegStore iConfigRegStore;

    // Check the configuration store for the last subnet joined.
    try
    {
        Bwn lastSubnetBuf = Bwn((TByte *)&lastSubnet, sizeof(lastSubnet));

        iConfigRegStore.Read(Brn(lastSubnetStr), lastSubnetBuf);
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

        // If the requested subnet is available, choose it.
        if (subnet == preferredSubnet)
        {
            index = i;
            break;
        }

        // If the last used subnet is available, note it.
        // We'll fall back to it if the requested subnet is not available.
        if (subnet == lastSubnet)
        {
            index = i;
        }
    }

    // Choose the required adapter.
    TIpAddress subnet = (*subnetList)[index]->Subnet();

    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);

    // Store the selected subnet in persistent storage.
    iConfigRegStore.Write(Brn(lastSubnetStr),
                          Brn((TByte *)&subnet, sizeof(subnet)));

    Log::Print("Using Subnet %d.%d.%d.%d\n", subnet&0xff, (subnet>>8)&0xff,
                                             (subnet>>16)&0xff,
                                             (subnet>>24)&0xff);

    return lib;
}

