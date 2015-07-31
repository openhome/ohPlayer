#pragma once

#include <Mmdeviceapi.h>

#include "AudioSessionEvents.h"
#include "WWMFResampler.h"
#include <OpenHome/Media/Utils/Aggregator.h>

namespace OpenHome {
    class Environment;

namespace Media {

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

class AudioDriver : public Thread, private IMsgProcessor, public IPipelineAnimator
{
    static const TInt64 kClockPullDefault = (1 << 29) * 100LL;
public:
    AudioDriver(Environment& aEnv, IPipeline& aPipeline, HWND hwnd);
    ~AudioDriver();

    static void SetVolume(float level);

private: // Data set by SetVolume()
    static TBool iVolumeChanged;
    static float iVolumeLevel;
private: // from Thread
    void Run();
private:
    void ProcessAudio(MsgPlayable* aMsg);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgSession* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgChangeInput* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
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
    IPipeline   &iPipeline;
    TUint        iSampleRate;
    TUint        iNumChannels;
    TUint        iBitDepth;
    MsgPlayable *iPlayable;
    TBool        iQuit;
private:
    // WASAPI Related
    IMMDevice            *iAudioEndpoint;
    IAudioClient         *iAudioClient;
    IAudioRenderClient   *iRenderClient;
    WAVEFORMATEX         *iMixFormat;
    IAudioSessionControl *iAudioSessionControl;
    AudioSessionEvents   *iAudioSessionEvents;
    ISimpleAudioVolume   *iAudioSessionVolume;
    WWMFResampler         iResampler;

private:
    // Audio Client Events
    HANDLE iAudioSamplesReadyEvent;
    HANDLE iAudioSessionDisconnectedEvent;

    // Internal Data

    // Main window handle.
    HWND    iHwnd;
    // The buffer shared with the audio engine should be at least big enough
    // to buffer enough data to cover this time frame.
    LONG    iEngineLatencyInMS;
    // Max audio Frames in Audio Client buffer.
    TUint32 iBufferSize;
    // Set when the audio stream has benn verified as playable
    TBool   iStreamFormatSupported;
    // Set when the audio session has been disconnected.
    TBool   iAudioSessionDisconnected;
    // Set when native audio is initialised successfully to the stream format.
    TBool   iAudioEngineInitialised;
    // Set when native audio client has been started successfully.
    TBool   iAudioClientStarted;
    // Amount of space in the render buffer this render period.
    TUint32 iRenderBytesThisPeriod;
    // Amount of remaining space in the render buffer this render period.
    TUint32 iRenderBytesRemaining;
    // Audio renderer frame size.
    TUint32 iFrameSize;
    // Resample the input stream
    TBool   iResamplingInput;
    // Bytes Per Second of resampled input/output streams
    TUint32 iResampleInputBps;
    TUint32 iResampleOutputBps;

private:
    // Utility functions
    TBool GetMultimediaDevice(IMMDevice **DeviceToUse);
    TBool CheckMixFormat(TUint iSampleRate, TUint iNumChannels, TUint iBitDepth);
    TBool InitializeAudioClient();
    TBool InitializeAudioEngine();
    TBool RestartAudioEngine();
    void  StopAudioEngine();
    void  ShutdownAudioEngine();
};
} // namespace Media
} // namespace OpenHome
