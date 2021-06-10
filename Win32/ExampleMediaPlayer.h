#pragma once

#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/FriendlyNameAdapter.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Av/RebootHandler.h>
#include <OpenHome/Av/Songcast/OhmTimestamp.h>
#include <OpenHome/Av/UpnpAv/FriendlyNameUpnpAv.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Web/ConfigUi/FileResourceHandler.h>
#include <OpenHome/Web/WebAppFramework.h>

#include <Windows.h>

#include "Volume.h"

namespace OpenHome {
    class Shell;
    class ShellCommandDebug;
namespace Net {
    class DviServerUpnp;
    class DvStack;
    class DvDevice;
    class CpStack;
}
namespace Media {
    class PipelineManager;
    class DriverSongcastSender;
    class AllocatorInfoLogger;
}
namespace Configuration {
    class ConfigRegStore;
    class ConfigManager;
}
namespace Web {
    class ConfigAppMediaPlayer;
}
namespace Av {
    class RamStore;
    class ControlPointProxy;


// Helpers
static TBool CompareIPv4Addrs(const TIpAddress addr1,
                              const TIpAddress addr2)
{
    return addr1.iFamily == kFamilyV4
        && addr2.iFamily == kFamilyV4
        && addr1.iV4 == addr2.iV4;
}

static TBool CompareIPv6Addrs(const TIpAddress addr1,
                              const TIpAddress addr2)
{
    return addr1.iFamily == kFamilyV6
        && addr2.iFamily == kFamilyV6
        && memcmp((TByte*)addr1.iV6[0], (TByte*)addr2.iV6[0], 16) == 0;
}


class ExampleMediaPlayer : private Net::IResourceManager
{
    static const Brn   kIconOpenHomeFileName;
    static const TUint kMaxUiTabs       = 4;
    static const TUint kUiSendQueueSize = kMaxUiTabs * 200;
    static const TUint kShellPort       = 2323;

public:
    ExampleMediaPlayer(HWND hwnd, Net::DvStack& aDvStack, Net::CpStack&, 
                       const Brx& aUdn, const TChar* aRoom, const TChar* aProductName,
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
    void                    SetSongcastTimestampers(
                                               IOhmTimestamper& aTxTimestamper,
                                               IOhmTimestamper& aRxTimestamper);
    void                    SetSongcastTimestampMappers(
                                              IOhmTimestamper& aTxTsMapper,
                                              IOhmTimestamper& aRxTsMapper);

    void DebugLogOutput(const char* aMessage);

    Media::PipelineManager           &Pipeline();
    Net::DvDeviceStandard            *Device();
    Net::DvDevice                    *UpnpAvDevice();
    Av::FriendlyNameAttributeUpdater *iFnUpdaterStandard;
    FriendlyNameManagerUpnpAv        *iFnManagerUpnpAv;
    Av::FriendlyNameAttributeUpdater *iFnUpdaterUpnpAv;
private: // from Net::IResourceManager
    void WriteResource(const Brx& aUriTail, 
                       const TIpAddress& aInterface,
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
#ifdef _DEBUG
    Media::IPipelineObserver      *iPipelineStateLogger;
#endif // _DEBUG
    Media::PipelineInitParams     *iInitParams;
    Media::AllocatorInfoLogger    *iInfoLogger;
    Net::DvDeviceStandard         *iDevice;
    Net::DvDevice                 *iDeviceUpnpAv;
    RamStore                      *iRamStore;
    Configuration::ConfigRegStore *iConfigRegStore;
    Semaphore                      iSemShutdown;
    Web::WebAppFramework          *iAppFramework;
    RebootLogger                   iRebootHandler;
private:
    Semaphore                  iDisabled;
    Av::VolumeControl          iVolume;
    ControlPointProxy         *iCpProxy;
    IOhmTimestamper           *iTxTimestamper;
    IOhmTimestamper           *iRxTimestamper;
    IOhmTimestamper           *iTxTsMapper;
    IOhmTimestamper           *iRxTsMapper;
    const Brx                 &iUserAgent;
    Web::FileResourceHandlerFactory iFileResourceHandlerFactory;
    Web::ConfigAppMediaPlayer *iConfigApp;
    HWND                       iHwnd; // Main window handle
    Bws<Uri::kMaxUriBytes+1>   iPresentationUrl;
    Shell* iShell;
    ShellCommandDebug* iShellDebug;
};

class ExampleMediaPlayerInit
{
public:
    static OpenHome::Net::Library* CreateLibrary(TIpAddress preferredSubnet);
};

} // namespace Av
} // namespace OpenHome
