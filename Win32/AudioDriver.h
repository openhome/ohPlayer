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

#include "Queue.h"

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

// Buffer to hold audio data being transferred between the audio pipeline
// and the native audio client.
class DataBuffer {
public:
    DataBuffer(TUint bufferSize);
    ~DataBuffer();

    TUint  GetBufferLength();
    TByte *GetBufferPtr();
    TUint  CopyToBuffer(const TByte *srcBuffer, TUint size);
    bool   ConsumeBufferData(TUint bytes);
private:
    TUint  _BufferLength;
    TByte* _Buffer;
    TByte* _Datap;
};

namespace Media {

class AudioDriver : public Thread, private IMsgProcessor, public IPullableClock, public IPipelineDriver
{
    static const TUint kTimerFrequencyMs = 5;
    static const TInt64 kClockPullDefault = (1 << 29) * 100LL;
public:
    AudioDriver(Environment& aEnv);
    ~AudioDriver();
    void SetPipeline(IPipelineElementUpstream& aPipeline);

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
private: // from IPullableClock
    void PullClock(TInt32 aValue);
private: // from IPipelineDriver
    TUint PipelineDriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo) override;
private:
    IPipelineElementUpstream* iPipeline;
    Semaphore iSem;
    OsContext* iOsCtx;
    TUint iSampleRate;
    TUint iJiffiesPerSample;
    TUint iNumChannels;
    TUint iBitDepth;
    TUint iPendingJiffies;
    TUint64 iLastTimeUs;
    TUint iNextTimerDuration;
    MsgPlayable* iPlayable;
    Mutex iPullLock;
    TInt64 iPullValue;
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
    HANDLE              _StreamSwitchCompleteEvent;
    HANDLE              _ShutdownEvent;

    // Internal Data

    // Render thread id.
    HANDLE              _RenderThread;
    // The buffer shared with the audio engine should be at least big enough
    // to buffer enough data to cover this time frame.
    LONG                _EngineLatencyInMS;
    // Max audio Frames in Audio Client buffer.
    TUint32             _BufferSize;
    // Render buffer size in bytes.
    // Calculated to supply the required amoutn of data per render cycle.
    TUint32             _RenderBufferSize;
    // Buffer removed from queue, but pending write to render buffer.
    DataBuffer         *_CachedDataBuffer;
    // Queue of data, produced by pipeline, consumed by renderer.
    Queue<DataBuffer*>  _RenderQueue;
    // Set when native audio is initialised successfully.
    bool _AudioEngineInitialised;

private:
    // Utility functions
    bool GetMultimediaDevice(IMMDevice **DeviceToUse);
    bool InitializeAudioClient();
    bool InitializeAudioEngine();
    bool InitializeStreamSwitch();
    void StopAudioEngine();
    void ShutdownAudioEngine();

    TUint32 BufferSizePerPeriod();

    DWORD DoRenderThread();
    static DWORD __stdcall WASAPIRenderThread(LPVOID Context);
private:
    void QueueData(Media::MsgPlayable* aMsg, TUint bytes);
    void DumpDataBuffer(TByte* buf, TUint length);
};
} // namespace Media
} // namespace OpenHome
