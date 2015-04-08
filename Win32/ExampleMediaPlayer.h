#ifndef HEADER_EXAMPLEMEDIAPLAYER
#define HEADER_EXAMPLEMEDIAPLAYER

#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Utils/DriverSongcastSender.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/Tests/VolumeUtils.h>

namespace OpenHome {
namespace Net {
    class DviServerUpnp;
    class DvStack;
    class DvDevice;
}
namespace Media {
    class PipelineManager;
    class DriverSongcastSender;
}
namespace Configuration {
    class ConfigRamStore;
    class ConfigManager;
}
namespace Av {
    class RamStore;

class ExampleMediaPlayer : private Net::IResourceManager,
                           private Media::IPipelineObserver
{
    static const Brn kSongcastSenderIconFileName;
public:
    ExampleMediaPlayer(Net::DvStack& aDvStack, const Brx& aUdn,
                       const TChar* aRoom, const TChar* aProductName,
                       const Brx& aUserAgent,
                       Media::IPipelineDriver& aPipelineDriver);
    virtual ~ExampleMediaPlayer();
    void StopPipeline();
    void PlayPipeline();
    void PausePipeline();
    void HaltPipeline();
    void AddAttribute(const TChar* aAttribute); // FIXME - only required by Songcasting driver
    virtual void RunWithSemaphore();
    Media::PipelineManager& Pipeline();
    Net::DvDeviceStandard* Device();
protected:
    virtual void RegisterPlugins(Environment& aEnv);
    void DoRegisterPlugins(Environment& aEnv, const Brx& aSupportedProtocols);
private: // from Net::IResourceManager
    void WriteResource(const Brx& aUriTail, TIpAddress aInterface,
                       std::vector<char*>& aLanguageList,
                       Net::IResourceWriter& aResourceWriter) override;
private:
    static TUint Hash(const Brx& aBuf);
    TBool TryDisable(Net::DvDevice& aDevice);
    void Disabled();
protected:
    MediaPlayer* iMediaPlayer;
    Net::DvDeviceStandard* iDevice;
    Net::DvDevice* iDeviceUpnpAv;
    RamStore* iRamStore;
    Configuration::ConfigRamStore* iConfigRamStore;
    Semaphore iSemShutdown;
private:
    Semaphore iDisabled;
    Media::VolumePrinter iVolume;
    Media::EPipelineState pState;
    const Brx& iUserAgent;

private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyTrack(Media::Track& aTrack, const Brx& aMode, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
};

class ExampleMediaPlayerInit
{
public:
    static OpenHome::Net::Library* CreateLibrary();
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_EXAMPLEMEDIAPLAYER
