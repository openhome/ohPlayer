#pragma once

#include <Mmdeviceapi.h>

#include "AudioSessionEvents.h"
#include "WWMFResampler.h"
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Standard.h>

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

class AudioDriver : public PipelineElement, public IPipelineAnimator, private INonCopyable
{
    static const TInt64 kClockPullDefault = (1 << 29) * 100LL;
    static const TUint kSupportedMsgTypes;
public:
    AudioDriver(Environment& aEnv, IPipeline& aPipeline, HWND hwnd);
    ~AudioDriver();

    static void SetVolume(float level);

private: // Data set by SetVolume()
    static TBool iVolumeChanged;
    static float iVolumeLevel;
private:
    void AudioThread();
    void ProcessAudio(MsgPlayable* aMsg);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override;
    TUint PipelineAnimatorDelayJiffies(AudioFormat aForamt, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const override;
    TUint PipelineAnimatorDsdBlockSizeWords() const override;
    TUint PipelineAnimatorMaxBitDepth() const override;
    void PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const override;

private:
    IPipeline     &iPipeline;
    TUint          iSampleRate;
    TUint          iNumChannels;
    TUint          iBitDepth;
    MsgPlayable   *iPlayable;
    TBool          iQuit;
    ThreadFunctor *iThread;
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
