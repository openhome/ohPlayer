#pragma once

#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Utils/DriverSongcastSender.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Av/Songcast/OhmTimestamp.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Web/WebAppFramework.h>

#include "Volume.h"

namespace OpenHome {
namespace Net {
    class DviServerUpnp;
    class DvStack;
    class DvDevice;
    class Shell;
    class ShellCommandDebug;
}
namespace Media {
    class PipelineManager;
    class DriverSongcastSender;
}
namespace Configuration {
    class ConfigGTKKeyStore;
    class ConfigManager;
}
namespace Web {
    class ConfigAppMediaPlayer;
}
namespace Av {
    class RamStore;
    class ControlPointProxy;

class ExampleMediaPlayer : private Net::IResourceManager
{
    static const Brn   kSongcastSenderIconFileName;
    static const TUint kMaxUiTabs       = 4;
    static const TUint kUiSendQueueSize = 32;
    static const TUint kShellPort       = 2323;
public:
    ExampleMediaPlayer(Net::DvStack& aDvStack, const Brx& aUdn,
                       const TChar* aRoom, const TChar* aProductName,
                       const Brx& aUserAgent);
    virtual ~ExampleMediaPlayer();

    Environment            &Env();
    void                    StopPipeline();
    TBool                   CanPlay();
    void                    PlayPipeline();
    TBool                   CanPause();
    void                    PausePipeline();
    TBool                   CanHalt();
    void                    HaltPipeline();
    void                    AddAttribute(const TChar* aAttribute);
    virtual void            RunWithSemaphore(Net::CpStack& aCpStack);
    void                    SetSongcastTimestampers(IOhmTimestamper& aTxTimestamper, IOhmTimestamper& aRxTimestamper);
    void                    SetSongcastTimestampMappers(IOhmTimestampMapper& aTxTsMapper, IOhmTimestampMapper& aRxTsMapper);
    Media::PipelineManager &Pipeline();
    Net::DvDeviceStandard  *Device();
    Net::DvDevice          *UpnpAvDevice();
private: // from Net::IResourceManager
    void WriteResource(const Brx& aUriTail, TIpAddress aInterface,
                       std::vector<char*>& aLanguageList,
                       Net::IResourceWriter& aResourceWriter) override;
private:
    void  RegisterPlugins(Environment& aEnv);
    void  AddConfigApp();
    void  PresentationUrlChanged(const Brx& aUrl);
    TBool TryDisable(Net::DvDevice& aDevice);
    void  Disabled();
protected:
    MediaPlayer                   *iMediaPlayer;
    Media::IPipelineObserver      *iPipelineStateLogger;
    Media::PipelineInitParams     *iInitParams;
    Net::DvDeviceStandard         *iDevice;
    Net::DvDevice                 *iDeviceUpnpAv;
    RamStore                      *iRamStore;
    Configuration::ConfigGTKKeyStore *iConfigStore;
    Semaphore                      iSemShutdown;
    Web::WebAppFramework          *iAppFramework;
private:
    Semaphore                  iDisabled;
    Av::VolumeControl          iVolume;
    ControlPointProxy         *iCpProxy;
    IOhmTimestamper           *iTxTimestamper;
    IOhmTimestamper           *iRxTimestamper;
    IOhmTimestampMapper       *iTxTsMapper;
    IOhmTimestampMapper       *iRxTsMapper;
    const Brx                 &iUserAgent;
    Web::ConfigAppMediaPlayer *iConfigApp;
    Bws<Uri::kMaxUriBytes+1>   iPresentationUrl;
    Net::Shell* iShell;
    Net::ShellCommandDebug* iShellDebug;
};

class ExampleMediaPlayerInit
{
public:
    static OpenHome::Net::Library* CreateLibrary(TIpAddress preferredSubnet);
};

} // namespace Av
} // namespace OpenHome
