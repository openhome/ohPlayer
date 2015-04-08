#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/OsWrapper.h>

#include "AudioDriver.h"

#include <avrt.h>
#include <stdlib.h>

using namespace OpenHome;
using namespace OpenHome::Media;

DataBuffer::DataBuffer(TUint bufferSize) :
    _Buffer(NULL),
    _Datap(NULL)
{
    _Buffer = new TByte[bufferSize];

    if (_Buffer != NULL)
    {
        _Datap = _Buffer;
        _BufferLength = bufferSize;
    }
}

DataBuffer::~DataBuffer()
{
    delete(_Buffer);
    _Buffer = NULL;
    _Datap  = NULL;
}

TByte *DataBuffer::GetBufferPtr()
{
    return _Datap;
}

TUint DataBuffer::GetBufferLength()
{
    return _BufferLength;
}

bool DataBuffer::ConsumeBufferData(TUint bytes)
{
    // Sanity check.
    if (bytes > _BufferLength)
    {
        return false;
    }

    _Datap        += bytes;
    _BufferLength -= bytes;

    return true;
}

TUint DataBuffer::CopyToBuffer(const TByte *srcBuffer, TUint size)
{
    TUint amount = size;

    if (_BufferLength < size)
    {
        amount = _BufferLength;
    }

    TByte *ptr  = (TByte *)(srcBuffer + 0);
    TByte *ptr1 = (TByte *)_Datap;
    TByte *endp = _Buffer + amount;

    ASSERT(amount % 2 == 0);

    // Little endian byte order required by native audio.
    while (ptr1 < endp)
    {
        *ptr1++ = *(ptr+1);
        *ptr1++ = *(ptr);
        ptr +=2;
    }

    // Return amount copied
    return amount;
}

AudioDriver::AudioDriver(Environment& aEnv) :
    _AudioEndpoint(NULL),
    _AudioClient(NULL),
    _RenderClient(NULL),
    _DeviceEnumerator(NULL),
    _MixFormat(NULL),
    _AudioSamplesReadyEvent(NULL),
    _StreamSwitchEvent(NULL),
    _StreamSwitchCompleteEvent(NULL),
    _ShutdownEvent(NULL),
    _RenderThread(NULL),
    _EngineLatencyInMS(1000),
    _BufferSize(0),
    _RenderBufferSize(0),
    _CachedDataBuffer(NULL),
    _AudioEngineInitialised(false),

    Thread("PipelineAnimator", kPrioritySystemHighest),
    iPipeline(NULL),
    iSem("DRVB", 0),
    iOsCtx(aEnv.OsCtx()),
    iPlayable(NULL),
    iPullLock("DBPL"),
    iPullValue(kClockPullDefault),
    iQuit(false)
{
}

AudioDriver::~AudioDriver()
{
    Join();
}

void AudioDriver::SetPipeline(IPipelineElementUpstream& aPipeline)
{
    iPipeline = &aPipeline;
    Start();
}

Msg* AudioDriver::ProcessMsg(MsgMode* aMsg)
{
    Log::Print("Pipeline Mode Msg\n");

    iPullLock.Wait();
    iPullValue = kClockPullDefault;
    iPullLock.Signal();
    aMsg->RemoveRef();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgSession* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgTrack* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

void AudioDriver::PullClock(TInt32 aValue)
{
    AutoMutex _(iPullLock);
    iPullValue += aValue;
    Log::Print("AudioDriver::PullClock now at %u%%\n", iPullValue / (1<<29));
}

TUint AudioDriver::PipelineDriverDelayJiffies(TUint /*aSampleRateFrom*/,
                                              TUint /*aSampleRateTo*/)
{
    return 0;
}


Msg* AudioDriver::ProcessMsg(MsgDecodedStream* aMsg)
{
    Log::Print("Pipeline Decoded Stream Msg\n");

    // We only expect to receive this message once per session.
    if (_AudioEngineInitialised)
    {
        Log::Print("ERROR: Cannot reinitialise Audio Engine.\n");
        aMsg->RemoveRef();
        return NULL;
    }

    // Obtain the audio stream parameters.
    const DecodedStreamInfo& stream = aMsg->StreamInfo();

    iSampleRate  = stream.SampleRate();
    iNumChannels = stream.NumChannels();
    iBitDepth    = stream.BitDepth();
    iJiffiesPerSample = Jiffies::JiffiesPerSample(iSampleRate);

    Log::Print("Audio Pipeline Stream Configuration:\n");
    Log::Print("\tSample Rate:        %6u\n", iSampleRate);
    Log::Print("\tNumber Of Channels: %6u\n", iNumChannels);
    Log::Print("\tBit Depth:          %6u\n", iBitDepth);
    Log::Print("\tJiffies Per Sample: %6u\n", iJiffiesPerSample);

    // We should already have obtained the mix format from the system
    if (_MixFormat == NULL)
    {
        Log::Print("ERROR: Audio Engine Mix Format Not Available.\n");
        aMsg->RemoveRef();
        return NULL;
    }

    WAVEFORMATEX *closestMix;
    WAVEFORMATEX savedMixFormat = *_MixFormat;

    // Verify the Audio Engine supports the pipeline format.
    _MixFormat->wFormatTag      = WAVE_FORMAT_PCM;
    _MixFormat->nChannels       = (WORD)iNumChannels;
    _MixFormat->nSamplesPerSec  = iSampleRate;
    _MixFormat->nBlockAlign     = WORD((iNumChannels * iBitDepth)/8);
    _MixFormat->nAvgBytesPerSec = DWORD(iSampleRate * _MixFormat->nBlockAlign);
    _MixFormat->wBitsPerSample  = (WORD)iBitDepth;
    _MixFormat->cbSize          = 0;

    HRESULT hr = _AudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                 _MixFormat,
                                                 &closestMix);

    CoTaskMemFree(closestMix);

    if (SUCCEEDED(hr))
    {
        // Now that we know the stream format and that it is viable
        // fire up the Audio Engine with the stream specifics.
        //
        // Any allocated resources will be freed on exit of pipeline processing
        // loop in Run().
        if (InitializeAudioEngine())
        {
            // Calculate the buffer size for our queue of data read from the
            // pipeline, to be consumed on demand to the renderer thread.
            _RenderBufferSize = BufferSizePerPeriod() * _MixFormat->nBlockAlign;

            // Create the rendering thread.
            _RenderThread = CreateThread(NULL, 0, WASAPIRenderThread,
                                         this, 0, NULL);
            if (_RenderThread == NULL)
            {
                Log::Print("Unable to create transport thread: %x.",
                           GetLastError());
            }
            else
            {
                _AudioEngineInitialised = true;
            }
        }
    }
    else
    {
        Log::Print("ERROR: Stream Format Cannot Be Played.\n");

        Log::Print("Windows Shared Audio Engine Configuration:\n");

        switch (savedMixFormat.wFormatTag)
        {
            case WAVE_FORMAT_PCM:
                Log::Print("PCM STREAM\n");
                break;
            case WAVE_FORMAT_EXTENSIBLE:
                Log::Print("EXTENSIBLE STREAM\n");
                break;
            case WAVE_FORMAT_MPEG:
                Log::Print("MPEG1 STREAM\n");
                break;
            case WAVE_FORMAT_MPEGLAYER3:
                Log::Print("MPEG3 STREAM\n");
                break;
            default:
                Log::Print("UNKNOWN STREAM\n");
                break;
        }

        Log::Print("\tSample Rate:        %6u\n",
                   savedMixFormat.nSamplesPerSec);
        Log::Print("\tNumber Of Channels: %6u\n",
                   savedMixFormat.nChannels);
        Log::Print("\tBit Depth:          %6u\n",
                   savedMixFormat.wBitsPerSample);
    }

    aMsg->RemoveRef();
    return NULL;
}

// Dump the contents of a data buffer to stdout. For debug purposes only.
void AudioDriver::DumpDataBuffer(TByte* buf, TUint length)
{
    TUint i;

    printf_s("DumpDataBuffer: %u\n\n", length);

    for (i=0; i<length; i++)
    {
        printf_s("%02x ", buf[i]);

        if (i % 25 == 0 && i != 0)
        {
            printf_s("\n");
        }
    }

    printf_s("\n\n");
}

// Queue the audio data obtained from the pipeline ready for consumption
// by the renderer thread.
void AudioDriver::QueueData(MsgPlayable* aMsg, TUint bytes)
{
    // If the native audio system failed to initialise just throw
    // the data away.
    if (! _AudioEngineInitialised)
    {
        return;
    }

    // Get the message data.
    ProcessorPcmBufPacked pcmProcessor;

    aMsg->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());

    const TByte* ptr = buf.Ptr();

    // Create a new data buffer.
    DataBuffer *buffer = new DataBuffer(bytes);

    // Copy the data into the buffer, changing the byte order to suite.
    buffer->CopyToBuffer((const TByte *)(ptr), bytes);

    // Queue buffer.
    _RenderQueue.push(buffer);
}

void AudioDriver::ProcessAudio(MsgPlayable* aMsg)
{
    iPlayable              = NULL;
    const TUint numSamples = aMsg->Bytes() / ((iBitDepth/8) * iNumChannels);
    TUint jiffies          = numSamples * iJiffiesPerSample;

    if (jiffies > iPendingJiffies) {
        // More data available in this message than is currently required.
        jiffies = iPendingJiffies;
        const TUint bytes =
            Jiffies::BytesFromJiffies(jiffies,
                                      iJiffiesPerSample,
                                      iNumChannels,
                                      (iBitDepth/8));

        if (bytes == 0) {
            iPendingJiffies = 0;
            iPlayable = aMsg;
            return;
        }

        // Copy 'bytes' into our new buffer and queue when full.
        QueueData(aMsg, bytes);

        iPlayable = aMsg->Split(bytes);
    }
    else {
        // Deal with all data in this message.
        QueueData(aMsg, aMsg->Bytes());
    }

    iPendingJiffies -= jiffies;

    aMsg->RemoveRef();
}

Msg* AudioDriver::ProcessMsg(MsgPlayable* aMsg)
{
    ProcessAudio(aMsg);
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgQuit* aMsg)
{
    // Terminate input pipeline processing and shutdown native audio.
    Log::Print("Quit\n");

    iQuit = true;
    iPendingJiffies = 0;
    iNextTimerDuration = 0;
    aMsg->RemoveRef();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgHalt* aMsg)
{
    Log::Print("Pipeline Halt Msg\n");

    // TBD: Not sure if anything needs done here for Audio Engine/Client
    //
    // Halt input audio processing from the pipeline.
    iPendingJiffies = 0;
    iNextTimerDuration = 0;
    aMsg->RemoveRef();
    return NULL;
}

// Obtain the use of the native multimedia device.
bool AudioDriver::GetMultimediaDevice(IMMDevice **DeviceToUse)
{
    HRESULT hr;
    bool retValue = true;

    IMMDeviceEnumerator *deviceEnumerator = NULL;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&deviceEnumerator));
    if (FAILED(hr))
    {
        Log::Print("Unable to instantiate device enumerator: %x\n", hr);
        retValue = false;
        goto Exit;
    }

    IMMDevice *device      = NULL;
    ERole      deviceRole  = eMultimedia;

    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender,
                                                   deviceRole,
                                                  &device);
    if (FAILED(hr))
    {
        Log::Print("Unable to get default multimedia device: %x\n", hr);
        retValue = false;
        goto Exit;
    }

    *DeviceToUse       = device;
    retValue           = true;
Exit:
    SafeRelease(&deviceEnumerator);

    return retValue;
}

// Initialise the native audio engine.
bool AudioDriver::InitializeAudioEngine()
{
    HRESULT hr;

    hr = _AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                  AUDCLNT_STREAMFLAGS_NOPERSIST,
                                  _EngineLatencyInMS * 10000,
                                  0,
                                  _MixFormat,
                                  NULL);

    if (FAILED(hr))
    {
        Log::Print("Unable to initialize audio client: %x.\n", hr);
        return false;
    }

    //
    //  Retrieve the buffer size, in frames,  for the audio client.
    //
    hr = _AudioClient->GetBufferSize(&_BufferSize);
    if(FAILED(hr))
    {
        Log::Print("Unable to get audio client buffer: %x. \n", hr);
        return false;
    }

    hr = _AudioClient->SetEventHandle(_AudioSamplesReadyEvent);
    if (FAILED(hr))
    {
        Log::Print("Unable to set ready event: %x.\n", hr);
        return false;
    }

    hr = _AudioClient->GetService(IID_PPV_ARGS(&_RenderClient));
    if (FAILED(hr))
    {
        Log::Print("Unable to get new render client: %x.\n", hr);
        return false;
    }

    return true;
}

bool AudioDriver::InitializeAudioClient()
{
    HRESULT hr;

    // Initialize COM
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        return false;
    }

    //
    //  Create our shutdown and samples ready events- we want auto reset
    //  events that start in the not-signaled state.
    //
    _ShutdownEvent = CreateEventEx(NULL, NULL, 0,
                                   EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_ShutdownEvent == NULL)
    {
        Log::Print("Unable to create shutdown event: %d.\n", GetLastError());
        goto Exit;
    }

    _AudioSamplesReadyEvent = CreateEventEx(NULL, NULL, 0,
                                            EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_AudioSamplesReadyEvent == NULL)
    {
        Log::Print("Unable to create samples ready event: %d.\n",
                   GetLastError());

        goto Exit;
    }

    //
    //  Create our stream switch event- we want auto reset events that start
    //  in the not-signaled state.
    //  Note that we create this event even if we're not going to stream
    //  switch - that's because the event is used in the main loop of the
    //  renderer and thus it has to be set.
    //
    //
    //  FIXME - We don't intend to support stream switching.
    _StreamSwitchEvent = CreateEventEx(NULL, NULL, 0,
                                       EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (_StreamSwitchEvent == NULL)
    {
        Log::Print("Unable to create stream switch event: %d.\n",
                   GetLastError());

        goto Exit;
    }

    if (!GetMultimediaDevice(&_AudioEndpoint))
    {
        goto Exit;
    }

    _AudioEndpoint->AddRef();

    //
    // Now activate an IAudioClient object on our preferred endpoint and
    // retrieve the mix format for that endpoint.
    //
    hr = _AudioEndpoint->Activate(__uuidof(IAudioClient),
                                  CLSCTX_INPROC_SERVER,
                                  NULL,
                                  reinterpret_cast<void **>(&_AudioClient));
    if (FAILED(hr))
    {
        goto Exit;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
                          CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&_DeviceEnumerator));
    if (FAILED(hr))
    {
        goto Exit;
    }

    //
    // Load the MixFormat. This may differ depending on the shared mode used
    //
    hr = _AudioClient->GetMixFormat(&_MixFormat);
    if (FAILED(hr))
    {
        goto Exit;
    }

    return true;

Exit:
    ShutdownAudioEngine();

    CoUninitialize();

    return false;
}

// Shutdown the Audio Engine, freeing associated resources.
void AudioDriver::ShutdownAudioEngine()
{
    Log::Print("ShutdownAudioEngine: Starting ...\n");

    if (_RenderThread)
    {
        SetEvent(_ShutdownEvent);
        WaitForSingleObject(_RenderThread, INFINITE);
        CloseHandle(_RenderThread);
        _RenderThread = NULL;
    }

    if (_ShutdownEvent)
    {
        CloseHandle(_ShutdownEvent);
        _ShutdownEvent = NULL;
    }

    if (_AudioSamplesReadyEvent)
    {
        CloseHandle(_AudioSamplesReadyEvent);
        _AudioSamplesReadyEvent = NULL;
    }

    if (_StreamSwitchEvent)
    {
        CloseHandle(_StreamSwitchEvent);
        _StreamSwitchEvent = NULL;
    }

    SafeRelease(&_AudioEndpoint);
    SafeRelease(&_AudioClient);
    SafeRelease(&_RenderClient);

    if (_MixFormat)
    {
        CoTaskMemFree(_MixFormat);
        _MixFormat = NULL;
    }

    Log::Print("ShutdownAudioEngine: Complete ...\n");
}

//
// Stop the native renderer.
//
void AudioDriver::StopAudioEngine()
{
    HRESULT hr;

    Log::Print("StopAudioEngine: Starting ...\n");

    //
    // Tell the render thread to shut down, wait for the thread to
    // complete then clean up all the stuff we allocated in initialisation.
    //
    if (_ShutdownEvent)
    {
        SetEvent(_ShutdownEvent);
    }

    hr = _AudioClient->Stop();
    if (FAILED(hr))
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }

    if (_RenderThread)
    {
        WaitForSingleObject(_RenderThread, INFINITE);

        CloseHandle(_RenderThread);
        _RenderThread = NULL;
    }

    //
    //  Drain the buffers in the render buffer queue.
    //
    while (! _RenderQueue.isEmpty())
    {
        DataBuffer *renderBuffer = _RenderQueue.pop();
        delete renderBuffer;
    }

    Log::Print("StopAudioEngine: Complete\n");
}

//
//  Render thread - processes samples from the audio engine
//
DWORD AudioDriver::WASAPIRenderThread(LPVOID Context)
{
    AudioDriver *renderer = static_cast<AudioDriver *>(Context);

    DWORD threadPriority = GetThreadPriority(GetCurrentThread());
    Log::Print("Audio Driver  Thread Priority [%d]\n", threadPriority);

    threadPriority = GetPriorityClass(GetCurrentProcess());
    Log::Print("Run Thread Class [%x]\n", threadPriority);

    return renderer->DoRenderThread();
}

DWORD AudioDriver::DoRenderThread()
{
    bool stillPlaying    = true;
    bool DisableMMCSS    = false;
    HANDLE waitArray[3]  = {_ShutdownEvent,
                            _StreamSwitchEvent,
                            _AudioSamplesReadyEvent};
    HANDLE mmcssHandle   = NULL;
    DWORD  mmcssTaskIndex = 0;
    BYTE  *pData;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        Log::Print("Unable to initialize COM in render thread: %x\n", hr);
        return hr;
    }

    if (!DisableMMCSS)
    {
        mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);
        if (mmcssHandle == NULL)
        {
            Log::Print("Unable to enable MMCSS on render thread: %d\n",
                       GetLastError());
        }
    }

    // Pre-populate the render buffer prior to starting the audio client
    // to avoid initial glitches.
    {
        hr = _RenderClient->GetBuffer(_BufferSize, &pData);
        if (SUCCEEDED(hr))
        {
            UINT32      framesToWrite = 0;
            UINT32      bufferFrames  = 0;
            UINT32      offset        = 0;
            DataBuffer *renderBuffer;

            if (_CachedDataBuffer != NULL)
            {
                renderBuffer = _CachedDataBuffer;
                _CachedDataBuffer = NULL;
            }
            else
            {
                renderBuffer = _RenderQueue.pop();
            }

            bufferFrames = renderBuffer->GetBufferLength() /
                           _MixFormat->nBlockAlign;

            while (framesToWrite + bufferFrames <= _BufferSize)
            {
                //
                //  Copy data from the render buffer to the output
                //  buffer and bump our render pointer.
                //
                CopyMemory(pData + offset, renderBuffer->GetBufferPtr(),
                           renderBuffer->GetBufferLength());

                framesToWrite += bufferFrames;
                offset += renderBuffer->GetBufferLength();

                delete renderBuffer;
                renderBuffer = NULL;

                //
                // Check for empty queue to avoid the potential for the
                // pop operation to block.
                if (_RenderQueue.isEmpty())
                {
                    break;
                }

                renderBuffer = _RenderQueue.pop();

                bufferFrames = renderBuffer->GetBufferLength() /
                               _MixFormat->nBlockAlign;
            }

            _CachedDataBuffer = renderBuffer;

            hr = _RenderClient->ReleaseBuffer(framesToWrite, 0);
            if (!SUCCEEDED(hr))
            {
                Log::Print("Unable to release buffer: %x\n", hr);
                stillPlaying = false;
            }

            Log::Print("Audio Client - Pre-Loaded Frames %d\n", framesToWrite);
        }
        else
        {
            Log::Print("Unable to obtain buffer: %x\n", hr);
            stillPlaying = false;
        }
    }

    // Start the Audio client
    hr = _AudioClient->Start();
    if (FAILED(hr))
    {
        Log::Print("Unable to start render client: %x.\n", hr);
        stillPlaying = false;
    }

    while (stillPlaying)
    {
        DWORD waitResult =
            WaitForMultipleObjects(3, waitArray, FALSE, INFINITE);

        switch (waitResult)
        {
        case WAIT_OBJECT_0 + 0:     // _ShutdownEvent
            stillPlaying = false;       // We're done, exit the loop.
            break;
        case WAIT_OBJECT_0 + 1:     // _StreamSwitchEvent
            // FIXME - Not supported.
            break;
        case WAIT_OBJECT_0 + 2:     // _AudioSamplesReadyEvent
            //
            //  We need to provide the next buffer of samples to the
            //  audio renderer.
            //
            BYTE    *pData;
            UINT32   padding;
            UINT32   framesAvailable;
            HRESULT  hr;

            //
            //  We want to find out how much of the buffer *isn't* available
            //  (is padding).
            //
            hr = _AudioClient->GetCurrentPadding(&padding);
            if (SUCCEEDED(hr))
            {
                //
                //  Calculate the number of frames available.  We'll render
                //  that many frames or the number of frames left in the buffer,
                //  whichever is smaller.
                //
                framesAvailable = _BufferSize - padding;

                // No data to write.
                if (_RenderQueue.isEmpty() && _CachedDataBuffer == NULL)
                {
                    Log::Print("_AudioSamplesReadyEvent: Yikes No Data\n");
                    break;
                }

                // Get the maximum buffer available and write as much
                // as we can into it.
                hr = _RenderClient->GetBuffer(framesAvailable, &pData);
                if (SUCCEEDED(hr))
                {
                    UINT32      framesToWrite = 0;
                    UINT32      bufferFrames  = 0;
                    UINT32      offset        = 0;
                    DataBuffer *renderBuffer  = NULL;

                    if (_CachedDataBuffer != NULL)
                    {
                        renderBuffer = _CachedDataBuffer;
                        _CachedDataBuffer = NULL;
                    }
                    else
                    {
                        renderBuffer = _RenderQueue.pop();
                    }

                    bufferFrames = renderBuffer->GetBufferLength() /
                                   _MixFormat->nBlockAlign;

                    while (framesToWrite + bufferFrames <= framesAvailable)
                    {
                        //
                        //  Copy data from the render buffer to the output
                        //  buffer and bump our render pointer.
                        //
                        CopyMemory(pData + offset, renderBuffer->GetBufferPtr(),
                                   renderBuffer->GetBufferLength());

                        framesToWrite += bufferFrames;
                        offset        += renderBuffer->GetBufferLength();

                        delete renderBuffer;
                        renderBuffer = NULL;

                        if (_RenderQueue.isEmpty())
                        {
                            break;
                        }

                        renderBuffer = _RenderQueue.pop();

                        bufferFrames = renderBuffer->GetBufferLength() /
                                       _MixFormat->nBlockAlign;
                    }

                    _CachedDataBuffer = renderBuffer;

                    // Split queued buffer is not enough room in render buffer.
                    if (_CachedDataBuffer != NULL)
                    {
                        if (framesToWrite + bufferFrames > framesAvailable)
                        {
                            TUint bytes = (framesAvailable - framesToWrite) *
                                          _MixFormat->nBlockAlign;

                            CopyMemory(pData + offset,
                                       _CachedDataBuffer->GetBufferPtr(),
                                       bytes);

                            framesToWrite = framesAvailable;

                            // Remove the consumed data from the buffer.
                            _CachedDataBuffer->ConsumeBufferData(bytes);
                        }
                    }

#if 0
                    // Debugging purposes only.
                    if (framesAvailable * 0.75 > framesToWrite)
                    {
                        Log::Print("Frames Written %u:%u\n",
                                   framesToWrite,
                                   framesAvailable);
                    }
#endif

                    hr = _RenderClient->ReleaseBuffer(framesToWrite, 0);
                    if (!SUCCEEDED(hr))
                    {
                        Log::Print("Unable to release buffer: %x\n", hr);
                        stillPlaying = false;
                    }
                }
                else
                {
                    Log::Print("Unable to obtain buffer: %x\n", hr);
                    stillPlaying = false;
                }
            }

            break;
        }
    }

    Log::Print("Exited Render Loop\n");

    //
    //  Unhook from MMCSS.
    //
    if (!DisableMMCSS)
    {
        AvRevertMmThreadCharacteristics(mmcssHandle);
    }

    CoUninitialize();
    return 0;
}

//
// The Event Driven renderer will be woken up every defaultDevicePeriod
// hundred-nano-seconds.
//
// Convert that time into a number of frames.
//
TUint32 AudioDriver::BufferSizePerPeriod()
{
    REFERENCE_TIME defaultDevicePeriod, minimumDevicePeriod;

    HRESULT hr = _AudioClient->GetDevicePeriod(&defaultDevicePeriod,
                                               &minimumDevicePeriod);
    if (FAILED(hr))
    {
        Log::Print("Unable to retrieve device period: %x\n", hr);
        return 0;
    }

    double devicePeriodInSeconds = defaultDevicePeriod / (10000.0*1000.0);

    return static_cast<UINT32>(_MixFormat->nSamplesPerSec *
                               devicePeriodInSeconds + 0.5);
}

void AudioDriver::Run()
{
    DWORD threadPriority = GetThreadPriority(GetCurrentThread());
    Log::Print("Run Thread Priority [%d]\n", threadPriority);

    if(!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
    {
        Log::Print("Can't up the priority of Run()\n");
    }

    threadPriority = GetThreadPriority(GetCurrentThread());
    Log::Print("Run Thread Priority [%d]\n", threadPriority);

    threadPriority = GetPriorityClass(GetCurrentProcess());
    Log::Print("Run Thread Class [%x]\n", threadPriority);

    // Initialise the audio client and the audio engine.
    if (InitializeAudioClient() == false)
    {
        return;
    }

    // For the moment use the default mechanism for reading the PCM data.

    // pull the first (assumed non-audio) msg here so that any delays
    // populating the pipeline don't affect timing calculations below.
    Msg* msg = iPipeline->Pull();
    ASSERT(msg != NULL);
    (void)msg->Process(*this);

    TUint64 now = OsTimeInUs(iOsCtx);
    iLastTimeUs = now;
    iNextTimerDuration = kTimerFrequencyMs;
    iPendingJiffies = kTimerFrequencyMs * Jiffies::kPerMs;
    try {
        for (;;) {
            while (iPendingJiffies > 0) {
                if (iPlayable != NULL) {
                    ProcessAudio(iPlayable);
                }
                else {
                    Msg* msg = iPipeline->Pull();
                    msg = msg->Process(*this);
                    ASSERT(msg == NULL);
                }
            }

            //Log::Print("Escaped Loop: JpMs [%u]\n", Jiffies::kPerMs);

            if (iQuit) {
                break;
            }
            iLastTimeUs = now;
            if (iNextTimerDuration != 0) {
                try {
                    iSem.Wait(iNextTimerDuration);
                }
                catch (Timeout&) {}
            }
            iNextTimerDuration = kTimerFrequencyMs;
            now = OsTimeInUs(iOsCtx);
            const TUint diffMs = ((TUint)(now - iLastTimeUs + 500)) / 1000;

            // assume delay caused by drop-out.  process regular amount of audio
            if (diffMs > 100) {
                iPendingJiffies = kTimerFrequencyMs * Jiffies::kPerMs;
            }
            else {
                iPendingJiffies = diffMs * Jiffies::kPerMs;
                iPullLock.Wait();
                if (iPullValue != kClockPullDefault) {
                    TInt64 pending64 = iPullValue * iPendingJiffies;
                    pending64 /= kClockPullDefault;
                    //Log::Print("iPendingJiffies=%08x, pull=%08x\n", iPendingJiffies, pending64); // FIXME
                    //TInt pending = (TInt)iPendingJiffies + (TInt)pending64;
                    //Log::Print("Pulled clock, now want %u jiffies (%ums, %d%%) extra\n", (TUint)pending, pending/Jiffies::kPerMs, (pending-(TInt)iPendingJiffies)/iPendingJiffies); // FIXME
                    iPendingJiffies = (TUint)pending64;
                }
                iPullLock.Signal();
            }
        }
    }
    catch (ThreadKill&) {}

    // pull until the pipeline is emptied
    while (!iQuit) {
        Msg* msg = iPipeline->Pull();
        msg = msg->Process(*this);
        ASSERT(msg == NULL);
        if (iPlayable != NULL) {
            iPlayable->RemoveRef();
        }
    }

    // Now we've stopped reading the pipeline, stop the native audio.
    StopAudioEngine();

    // Free up native resources.
    ShutdownAudioEngine();

    CoUninitialize();
}
