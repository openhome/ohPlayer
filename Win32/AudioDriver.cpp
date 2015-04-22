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
#include "ProcessorPcmWASAPI.h"

#include <avrt.h>
#include <stdlib.h>

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifdef _DEBUG
   #ifndef DBG_NEW
      #define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
      #define new DBG_NEW
   #endif
#endif  // _DEBUG

using namespace OpenHome;
using namespace OpenHome::Media;

AudioDriver::AudioDriver(Environment& /*aEnv*/, IPipeline& aPipeline) :
    _AudioEndpoint(NULL),
    _AudioClient(NULL),
    _RenderClient(NULL),
    _MixFormat(NULL),
    _AudioSamplesReadyEvent(NULL),
    _StreamSwitchEvent(NULL),
    _EngineLatencyInMS(25),
    _BufferSize(0),
    _StreamFormatSupported(false),
    _AudioEngineInitialised(false),
    _AudioClientStarted(false),
    _RenderBytesThisPeriod(0),
    _RenderBytesRemaining(0),
    _FrameSize(0),
    _DuplicateChannel(false),

    Thread("PipelineAnimator", kPrioritySystemHighest),
    iPipeline(aPipeline),
    iPlayable(NULL),
    iQuit(false)
{
    iPipeline.SetAnimator(*this);
    Start();
}

AudioDriver::~AudioDriver()
{
    Join();
}

Msg* AudioDriver::ProcessMsg(MsgMode* aMsg)
{
    Log::Print("Pipeline Mode Msg\n");

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

TUint AudioDriver::PipelineDriverDelayJiffies(TUint /*aSampleRateFrom*/,
                                              TUint aSampleRateTo)
{
    WAVEFORMATEX *mixFormat;
    WAVEFORMATEX *closestMix;

    //
    // Load the MixFormat. This may differ depending on the shared mode used
    //
    HRESULT hr = _AudioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr))
    {
        Log::Print("Warning Audio Endpoint mix format unknown\n",
                   aSampleRateTo);
        return 0;
    }

    // Plug the requested sample rate into the existing mix format and
    // query the Audio Engine.
    mixFormat->nSamplesPerSec = aSampleRateTo;

    hr = _AudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                         mixFormat,
                                         &closestMix);

    CoTaskMemFree(closestMix);
    CoTaskMemFree(mixFormat);

    if (hr != S_OK)
    {
        _StreamFormatSupported = false;

        Log::Print("Warning sample rate not supported [%u]\n", aSampleRateTo);
        THROW(SampleRateUnsupported);
    }

    return 0;
}

bool AudioDriver::CheckMixFormat(TUint iSampleRate,
                                 TUint iNumChannels,
                                 TUint iBitDepth)
{
    HRESULT       hr;
    WAVEFORMATEX *closestMix;
    WAVEFORMATEX  savedMixFormat;
    bool          retVal = false;

    _DuplicateChannel = false;

    if (_MixFormat == NULL)
    {
        hr = _AudioClient->GetMixFormat(&_MixFormat);
        if (FAILED(hr))
        {
            Log::Print("ERROR: Could not obtain mix system format.\n");
            return false;
        }
    }

    savedMixFormat = *_MixFormat;

    // Verify the Audio Engine supports the pipeline format.
    _MixFormat->wFormatTag      = WAVE_FORMAT_PCM;
    _MixFormat->nChannels       = (WORD)iNumChannels;
    _MixFormat->nSamplesPerSec  = iSampleRate;
    _MixFormat->nBlockAlign     = WORD((iNumChannels * iBitDepth)/8);
    _MixFormat->nAvgBytesPerSec = DWORD(iSampleRate * _MixFormat->nBlockAlign);
    _MixFormat->wBitsPerSample  = (WORD)iBitDepth;
    _MixFormat->cbSize          = 0;

    hr = _AudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                         _MixFormat,
                                        &closestMix);

    if (hr != S_OK)
    {
        // The stream format isn't suitable as it stands.

        // Check to see if the issue is the number of channels.
        // We can duplicate a channel to play mono as stereo.
        if (iNumChannels == 1 && closestMix->nChannels == 2)
        {
            _MixFormat->nChannels   = closestMix->nChannels;
            _MixFormat->nBlockAlign =
                                   WORD((_MixFormat->nChannels * iBitDepth)/8);
            _MixFormat->nAvgBytesPerSec =
                                   DWORD(iSampleRate * _MixFormat->nBlockAlign);

            CoTaskMemFree(closestMix);

            hr = _AudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                 _MixFormat,
                                                &closestMix);

            if (hr == S_OK)
            {
                Log::Print("Converting mono input to stereo\n");
                _DuplicateChannel = true;
                retVal            = true;
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

                Log::Print("Closest Mix\n\n");

                Log::Print("\tSample Rate:        %6u\n",
                           closestMix->nSamplesPerSec);
                Log::Print("\tNumber Of Channels: %6u\n",
                           closestMix->nChannels);
                Log::Print("\tBit Depth:          %6u\n",
                           closestMix->wBitsPerSample);
            }

            CoTaskMemFree(closestMix);
        }
    }
    else
    {
        retVal = true;
    }


    return retVal;
}

Msg* AudioDriver::ProcessMsg(MsgDecodedStream* aMsg)
{
    // Obtain the audio stream parameters.
    const DecodedStreamInfo& stream = aMsg->StreamInfo();

    iSampleRate  = stream.SampleRate();
    iNumChannels = stream.NumChannels();
    iBitDepth    = stream.BitDepth();

    Log::Print("OpenHome Pipeline Stream Configuration:\n");
    Log::Print("\tSample Rate:        %6u\n", iSampleRate);
    Log::Print("\tNumber Of Channels: %6u\n", iNumChannels);
    Log::Print("\tBit Depth:          %6u\n", iBitDepth);

    if (CheckMixFormat(iSampleRate, iNumChannels, iBitDepth))
    {
        _StreamFormatSupported = true;
        _FrameSize             = _MixFormat->nBlockAlign;

        //
        // Now that we know the stream format and that it is viable
        // fire up the Audio Engine with the stream specifics.
        //
        // Any allocated resources will be freed on exit of pipeline
        // processing loop in Run().
        if (_AudioEngineInitialised)
        {
            if (!RestartAudioEngine())
            {
                _AudioEngineInitialised = false;
            }
        }
        else
        {
            if (InitializeAudioEngine())
            {
                _AudioEngineInitialised = true;
            }
        }
    }
    else
    {
        _StreamFormatSupported = false;
        _FrameSize             = 0;

        //
        // We can't play the audio stream, most likely due to the number
        // of channels changing, as a sample rate change would have been
        // caught in PipelineDriverDelayJiffies().
        //
        // There is no way to report this upstream, so we:
        // - Halt the Audio Engine
        // - Pull and discard the audio data from the pipeline.
        //
        if (_AudioClientStarted)
        {
            Log::Print("Stopping Audio Client.\n");

            _AudioClient->Stop();
            _AudioClient->Reset();
            _AudioClientStarted = false;
        }
    }

    aMsg->RemoveRef();
    return NULL;
}

void AudioDriver::ProcessAudio(MsgPlayable* aMsg)
{
    BYTE    *pData;
    HRESULT  hr;
    TUint    bytes;

    iPlayable = NULL;

    // If the native audio system is not available yet just throw
    // the data away.
    if (! _StreamFormatSupported)
    {
        aMsg->RemoveRef();
        return;
    }

    bytes = aMsg->Bytes();

    if (_DuplicateChannel)
    {
        bytes *= 2;
    }

    TUint framesToWrite = bytes / _FrameSize;

    if (bytes > _RenderBytesRemaining)
    {
        // We've passed enough data for this period. Hold on to the data
        // for the next render period.
        iPlayable = aMsg;
        return;
    }

    hr = _RenderClient->GetBuffer(framesToWrite, &pData);
    if (! SUCCEEDED(hr))
    {
        Log::Print("ERROR: Can't get render buffer\n");

        // Can't get render buffer. Hold on to the data for the next
        // render period.
        iPlayable = aMsg;
        _RenderClient->ReleaseBuffer(0, 0);
        return;
    }

    // Get the message data. This converts the pipeline data into a format
    // suitable for the native audio system.
    ProcessorPcmBufWASAPI pcmProcessor(_DuplicateChannel);
    aMsg->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());

    // Copy to the render buffer.
    CopyMemory(pData, buf.Ptr(), bytes);

    // Release the render buffer.
    _RenderClient->ReleaseBuffer(framesToWrite, 0);

    _RenderBytesRemaining -= bytes;

    // Release the source buffer.
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
    StopAudioEngine();
    aMsg->RemoveRef();
    return NULL;
}

Msg* AudioDriver::ProcessMsg(MsgHalt* aMsg)
{
    Log::Print("Pipeline Halt Msg\n");

    // TBD: Not sure if anything needs done here for Audio Engine/Client
    //
    // Halt input audio processing from the pipeline.
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

// Restart the audio engine with a new mix format.
bool AudioDriver::RestartAudioEngine()
{
    HRESULT hr;

    _AudioClientStarted     = false;
    _AudioEngineInitialised = false;
    iPlayable               = NULL;

#ifdef _TIMINGS_DEBUG
    LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
    LARGE_INTEGER Frequency;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&StartingTime);
#endif /* _TIMINGS_DEBUG */

    // Shutdown audio client.
    hr = _AudioClient->Stop();
    if (FAILED(hr))
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }
    _AudioClient->Reset();

    SafeRelease(&_AudioClient);
    SafeRelease(&_RenderClient);

    // Restart the audio client with latest mix format.
    hr = _AudioEndpoint->Activate(__uuidof(IAudioClient),
                                  CLSCTX_INPROC_SERVER,
                                  NULL,
                                  reinterpret_cast<void **>(&_AudioClient));
    if (FAILED(hr))
    {
        Log::Print("Unable to activate endpoint\n");
        return false;
    }

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

    //
    // Setup the maximum amount of data to buffer prior to starting the
    // audio client, to avoid glitches on startup.
    //
    _RenderBytesThisPeriod = _BufferSize * _FrameSize;
    _RenderBytesRemaining  = _RenderBytesThisPeriod;

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

    _AudioEngineInitialised = true;

#ifdef _TIMINGS_DEBUG
    QueryPerformanceCounter(&EndingTime);
    ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;

    //
    // We now have the elapsed number of ticks, along with the
    // number of ticks-per-second. We use these values
    // to convert to the number of elapsed microseconds.
    // To guard against loss-of-precision, we convert
    // to microseconds *before* dividing by ticks-per-second.
    //
    ElapsedMicroseconds.QuadPart *= 1000000;
    ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

    Log::Print("Time To Restart Audio Client [%lld us]\n",
               ElapsedMicroseconds.QuadPart);
#endif /* _TIMINGS_DEBUG */

    return true;
}

// Initialise the native audio engine.
bool AudioDriver::InitializeAudioEngine()
{
    HRESULT hr;

    Log::Print("Initializing Audio Engine\n");

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

    //
    // Setup the maximum amount of data to buffer prior to starting the
    // audio client, to avoid glitches on startup.
    //
    _RenderBytesThisPeriod = _BufferSize * _FrameSize;
    _RenderBytesRemaining  = _RenderBytesThisPeriod;

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
    // Now activate an IAudioClient object on the multimedia endpoint and
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

    return true;

Exit:
    ShutdownAudioEngine();

    return false;
}

// Shutdown the Audio Engine, freeing associated resources.
void AudioDriver::ShutdownAudioEngine()
{
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
}

//
// Stop the native renderer.
//
void AudioDriver::StopAudioEngine()
{
    HRESULT hr;

    Log::Print("StopAudioEngine: Starting ...\n");

    hr = _AudioClient->Stop();
    if (FAILED(hr))
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }

    _AudioEngineInitialised = false;

    Log::Print("StopAudioEngine: Complete\n");
}

void AudioDriver::Run()
{
    HANDLE mmcssHandle    = NULL;
    DWORD  mmcssTaskIndex = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        Log::Print("Unable to initialize COM in render thread: %x\n", hr);
        return;
    }

    // Gain access to the system multimedia audio endpoint and associate an
    // audio client object with it.
    if (InitializeAudioClient() == false)
    {
        goto Exit;
    }

    // Hook up to the Multimedia Class Scheduler Service to prioritise
    // our render activities.
    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);
    if (mmcssHandle == NULL)
    {
        Log::Print("Unable to enable MMCSS on render thread: %d\n",
                   GetLastError());

        goto Exit;
    }

    // Native events waited on in this thread.
    HANDLE waitArray[2] = {_StreamSwitchEvent, _AudioSamplesReadyEvent};

    // Pipeline processing loop.
    try {
        for (;;) {
#ifdef _TIMINGS_DEBUG
            LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
            LARGE_INTEGER Frequency;

            QueryPerformanceFrequency(&Frequency);
            QueryPerformanceCounter(&StartingTime);
#endif /* _TIMINGS_DEBUG */

            TUint32 padding = 0;

            //
            //  Calculate the number of bytes in the render buffer
            //  for this period.
            //
            //  This is the maximum we will pull from the pipeline.
            //
            //  If the Audio Engine has not been initialized yet stick with
            //  the default value.
            //
            if (_AudioEngineInitialised)
            {
                hr = _AudioClient->GetCurrentPadding(&padding);
                if (SUCCEEDED(hr))
                {
                    _RenderBytesThisPeriod = (_BufferSize - padding) *
                                              _FrameSize;
                }
                else
                {
                    Log::Print("ERROR: Couldn't read render buffer padding\n");
                    _RenderBytesThisPeriod = 0;
                }

                _RenderBytesRemaining = _RenderBytesThisPeriod;
            }

            //
            // Process pipeline messages until we've reached the maximum for
            // this period.
            //
            // The pull will block if there are no messages.
            //
            for(;;)
            {
                if (iPlayable != NULL) {
                    ProcessAudio(iPlayable);
                }
                else {
                    Msg* msg = iPipeline.Pull();
                    (void)msg->Process(*this);
                    ASSERT(msg != NULL);
                }

                //
                // Have we reached the data limit for this period or been told
                // to exit ?
                //
                if (iPlayable != NULL || iQuit)
                {
                    break;
                }
            }

            // Log some interesting data if we can't fill at least half
            // of the available space in the render buffer.
            if (_RenderBytesThisPeriod * 0.5 < _RenderBytesRemaining)
            {
                Log::Print("Audio period: Requested Bytes [%u] : Returned Bytes"
                           " [%u]\n",
                           _RenderBytesThisPeriod,
                           _RenderBytesThisPeriod - _RenderBytesRemaining);

                if (iPlayable)
                {
                    TUint bytes = iPlayable->Bytes();

                    if (_DuplicateChannel)
                    {
                        bytes *= 2;
                    }

                    Log::Print("  Available Bytes [%u]\n", bytes);
                }
                else
                {
                    Log::Print("  Available Bytes [0]\n");
                }

                if (_AudioEngineInitialised)
                {
                    Log::Print(" Period Start Frames In Buffer [%u]\n",
                               padding);

                    hr = _AudioClient->GetCurrentPadding(&padding);
                    if (SUCCEEDED(hr))
                    {
                        Log::Print(" Current Frames In Buffer [%u]\n",
                                   padding);
                    }
                }
            }

#ifdef _TIMINGS_DEBUG
            QueryPerformanceCounter(&EndingTime);
            ElapsedMicroseconds.QuadPart = EndingTime.QuadPart -
                                           StartingTime.QuadPart;

            //
            // We now have the elapsed number of ticks, along with the
            // number of ticks-per-second. We use these values
            // to convert to the number of elapsed microseconds.
            // To guard against loss-of-precision, we convert
            // to microseconds *before* dividing by ticks-per-second.
            //
            ElapsedMicroseconds.QuadPart *= 1000000;
            ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

            Log::Print("Time To Process Messages This Audio Period [%lld us]\n",
                       ElapsedMicroseconds.QuadPart);
#endif /* _TIMINGS_DEBUG */

            if (iQuit)
            {
                break;
            }

            // The audio client isn't capable of playing this stream.
            // Continue to pull from pipeline until the next playable
            // stream is available.
            if (! _StreamFormatSupported)
            {
                continue;
            }

            //
            // Start the Audio client once we have pre-loaded some
            // data to the render buffer.
            //
            // This will prevent any initial audio glitches..
            //
            if (! _AudioClientStarted)
            {
                // There was no data read this period so try again next period.
                if (_RenderBytesThisPeriod == _RenderBytesRemaining)
                {
                    continue;
                }

                hr = _AudioClient->Start();
                if (FAILED(hr))
                {
                    Log::Print("Unable to start render client: %x.\n", hr);
                    break;
                }

                _AudioClientStarted = true;
            }

            // Wait for a kick from the native audio engine.
            DWORD waitResult =
                WaitForMultipleObjects(2, waitArray, FALSE, INFINITE);

            switch (waitResult) {
                case WAIT_OBJECT_0 + 0:     // _StreamSwitchEvent
                    // FIXME - Not supported.
                    break;
                case WAIT_OBJECT_0 + 1:     // _AudioSamplesReadyEvent
                    break;
                default:
                    Log::Print("ERROR: Unexpected event received  [%d]\n",
                               waitResult);
            }
        }
    }
    catch (ThreadKill&) {}

Exit:
    // Now we've stopped reading the pipeline, stop the native audio.
    StopAudioEngine();

    // Free up native resources.
    ShutdownAudioEngine();

    //  Unhook from MMCSS.
    AvRevertMmThreadCharacteristics(mmcssHandle);

    CoUninitialize();
}
