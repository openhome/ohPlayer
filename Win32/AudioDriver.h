#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>

// WASAPI headers
#include <Mmdeviceapi.h>
#include <AudioClient.h>
#include <AudioPolicy.h>

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

namespace OpenHome {
    class Environment;

namespace Media {

class AudioDriver : public Thread, private IMsgProcessor, public IPipelineAnimator
{
    static const TInt64 kClockPullDefault = (1 << 29) * 100LL;
public:
    AudioDriver(Environment& aEnv, IPipeline& aPipeline);
    ~AudioDriver();

private: // from Thread
    void Run();
private:
    void ProcessAudio(MsgPlayable* aMsg);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgSession* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private: // from IPipelineDriver
    TUint PipelineDriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo) override;
private:
    IPipeline& iPipeline;
    TUint iSampleRate;
    TUint iNumChannels;
    TUint iBitDepth;
    MsgPlayable* iPlayable;
    TBool iQuit;
private:
    // WASAPI Related
    IMMDevice            *_AudioEndpoint;
    IAudioClient         *_AudioClient;
    IAudioRenderClient   *_RenderClient;
    IMMDeviceEnumerator  *_DeviceEnumerator;
    WAVEFORMATEX         *_MixFormat;

private:
    // Audio Client Events
    HANDLE              _AudioSamplesReadyEvent;
    HANDLE              _StreamSwitchEvent;
    HANDLE              _ShutdownEvent;

    // Internal Data

    // The buffer shared with the audio engine should be at least big enough
    // to buffer enough data to cover this time frame.
    LONG                _EngineLatencyInMS;
    // Max audio Frames in Audio Client buffer.
    TUint32             _BufferSize;
    // Set when native audio is initialised successfully.
    bool                _AudioEngineInitialised;
    // Amount of space in the render buffer this render period.
    TUint32             _RenderBytesThisPeriod;
    // Amount of remaining space in the render buffer this render period.
    TUint32             _RenderBytesRemaining;

private:
    // Utility functions
    bool GetMultimediaDevice(IMMDevice **DeviceToUse);
    bool InitializeAudioClient();
    bool InitializeAudioEngine();
    bool InitializeStreamSwitch();
    void StopAudioEngine();
    void ShutdownAudioEngine();
    TUint32 BufferSizePerPeriod();
};
} // namespace Media
} // namespace OpenHome
