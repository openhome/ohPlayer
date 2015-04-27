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


#include "AudioSessionEvents.h"

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
    AudioDriver(Environment& aEnv, IPipeline& aPipeline, LPVOID lpParam);
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
    WAVEFORMATEX         *_MixFormat;
    IAudioSessionControl * _AudioSessionControl;
    AudioSessionEvents   *_AudioSessionEvents;

private:
    // Audio Client Events
    HANDLE              _AudioSamplesReadyEvent;
    HANDLE              _AudioSessionDisconnectedEvent;

    // Internal Data

    // Main window handle.
    HWND                _Hwnd;
    // The buffer shared with the audio engine should be at least big enough
    // to buffer enough data to cover this time frame.
    LONG                _EngineLatencyInMS;
    // Max audio Frames in Audio Client buffer.
    TUint32             _BufferSize;
    // Set when the audio stream has benn verified as playable
    bool                _StreamFormatSupported;
    // Set when the audio session has been disconnected.
    bool                _AudioSessionDisconnected;
    // Set when native audio is initialised successfully to the stream format.
    bool                _AudioEngineInitialised;
    // Set when native audio client has been started successfully.
    bool                _AudioClientStarted;
    // Amount of space in the render buffer this render period.
    TUint32             _RenderBytesThisPeriod;
    // Amount of remaining space in the render buffer this render period.
    TUint32             _RenderBytesRemaining;
    // Audio renderer frame size.
    TUint32             _FrameSize;
    // Duplicate a chaneel when rendering (mono->stereo).
    bool                _DuplicateChannel;

private:
    // Utility functions
    bool GetMultimediaDevice(IMMDevice **DeviceToUse);
    bool CheckMixFormat(TUint iSampleRate, TUint iNumChannels, TUint iBitDepth);
    bool InitializeAudioClient();
    bool InitializeAudioEngine();
    bool RestartAudioEngine();
    void StopAudioEngine();
    void ShutdownAudioEngine();
};
} // namespace Media
} // namespace OpenHome
