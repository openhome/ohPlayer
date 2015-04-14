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

AudioDriver::AudioDriver(Environment& /*aEnv*/, IPipeline& aPipeline) :
    _AudioEndpoint(NULL),
    _AudioClient(NULL),
    _RenderClient(NULL),
    _DeviceEnumerator(NULL),
    _MixFormat(NULL),
    _AudioSamplesReadyEvent(NULL),
    _StreamSwitchEvent(NULL),
    _ShutdownEvent(NULL),
    _EngineLatencyInMS(100),
    _BufferSize(0),
    _AudioEngineInitialised(false),
    _RenderBytesThisPeriod(0),
    _RenderBytesRemaining(0),

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
                                              TUint /*aSampleRateTo*/)
{
    TUint dummy = 0;

    Log::Print("PipelineDriverDelayJiffies\n");

    // Throw an exception here if the sample rate cannot be supported.
    if (dummy == 1)
    {
        THROW(SampleRateUnsupported);
    }

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

    Log::Print("Audio Pipeline Stream Configuration:\n");
    Log::Print("\tSample Rate:        %6u\n", iSampleRate);
    Log::Print("\tNumber Of Channels: %6u\n", iNumChannels);
    Log::Print("\tBit Depth:          %6u\n", iBitDepth);

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

    if (SUCCEEDED(hr))
    {
        // Now that we know the stream format and that it is viable
        // fire up the Audio Engine with the stream specifics.
        //
        // Any allocated resources will be freed on exit of pipeline processing
        // loop in Run().
        if (InitializeAudioEngine())
        {
            _AudioEngineInitialised = true;
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

    CoTaskMemFree(closestMix);

    aMsg->RemoveRef();
    return NULL;
}

void AudioDriver::ProcessAudio(MsgPlayable* aMsg)
{
    BYTE *pData;

    iPlayable = NULL;

    // If the native audio system is not available yet. just
    // the data away.
    if (! _AudioEngineInitialised)
    {
        return;
    }

    TUint framesToWrite = aMsg->Bytes() / _MixFormat->nBlockAlign;
    HRESULT hr;

    if (aMsg->Bytes() > _RenderBytesRemaining)
    {
        // We've passed enough data for this period. Hold on to the data
        // for the next render period.
        iPlayable = aMsg;
        return;
    }

    hr = _RenderClient->GetBuffer(framesToWrite, &pData);
    if (! SUCCEEDED(hr))
    {
        // Can't get render buffer. Hold on to the data for the next
        // render period.
        iPlayable = aMsg;
        _RenderClient->ReleaseBuffer(0, 0);
        return;
    }

    // Get the message data.
    ProcessorPcmBufPacked pcmProcessor;

    aMsg->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());

    // Swap Big Endian to Little endian and write into native render buffer.
    {
        // Copy the pipeline data in the render buffer. converting to
        // little endian in the process.
        TByte *ptr  = (TByte *)(buf.Ptr() + 0);
        TByte *ptr1 = (TByte *)pData;
        TByte *endp = ptr1 + aMsg->Bytes();

        ASSERT(aMsg->Bytes() % 2 == 0);

        // Little endian byte order required by native audio.
        while (ptr1 < endp)
        {
            *ptr1++ = *(ptr+1);
            *ptr1++ = *(ptr);
            ptr +=2;
        }

        // Release the render buffer.
        _RenderClient->ReleaseBuffer(framesToWrite, 0);

        _RenderBytesRemaining -= aMsg->Bytes();

        // Release the source buffer.
        aMsg->RemoveRef();
    }
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
    // Setup the maximum amout of data to buffer prior to starting the
    // audio client, to avoid glitches on startup.
    _RenderBytesThisPeriod = _BufferSize * _MixFormat->nBlockAlign;
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

    Log::Print("StopAudioEngine: Complete\n");
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

    Log::Print("Device Period: %f\n", devicePeriodInSeconds);

    return static_cast<UINT32>(_MixFormat->nSamplesPerSec *
                               devicePeriodInSeconds + 0.5);
}

void AudioDriver::Run()
{
    HANDLE mmcssHandle    = NULL;
    DWORD  mmcssTaskIndex = 0;

    // Gain access to the system multimedia audio endpoint and associate an
    // audio client object with it..
    if (InitializeAudioClient() == false)
    {
        return;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        Log::Print("Unable to initialize COM in render thread: %x\n", hr);
    }

    // Hook up to the Multimedia Class Scheduler Service to prioritise
    // our render activities.
    mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &mmcssTaskIndex);
    if (mmcssHandle == NULL)
    {
        Log::Print("Unable to enable MMCSS on render thread: %d\n",
                   GetLastError());
    }

    // Events used to unblock this thread.
    HANDLE waitArray[3]   = {_ShutdownEvent,
                             _StreamSwitchEvent,
                             _AudioSamplesReadyEvent};

    // Pipeline processing loop.
    try {
        bool audioClientStarted = false;

        for (;;) {

            if (_AudioEngineInitialised)
            {
                TUint32 padding;

                //
                //  Calculate the number of bytes in the render buffer
                //  for this period.
                //
                //  This is the maximum we will pull from the pipeline
                //  this period.
                //
                hr = _AudioClient->GetCurrentPadding(&padding);
                if (SUCCEEDED(hr))
                {
                    _RenderBytesThisPeriod = (_BufferSize - padding) *
                                        _MixFormat->nBlockAlign;
                }
                else
                {
                    Log::Print("Couldn't read render buffer padding\n");
                    _RenderBytesThisPeriod = 0;
                }

                _RenderBytesRemaining = _RenderBytesThisPeriod;
            }

            // Process pipeline messages until we've reached the maximum for
            // this period.
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

                // Have we reached the data limit for this period or been told
                // to exit.
                if (iPlayable != NULL || iQuit)
                {
                    break;
                }
            }

            if (iQuit)
            {
                break;
            }

            // Start the Audio client once we have pre-loaded some
            // data to the render buffer. This will prevent any initial
            // audio abnormalities.
            if (! audioClientStarted)
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

                audioClientStarted = true;
            }

            // Wait for a kick from the native audio engine or quit handler.
            DWORD waitResult =
                WaitForMultipleObjects(3, waitArray, FALSE, INFINITE);

            switch (waitResult) {
                case WAIT_OBJECT_0 + 0:     // _ShutdownEvent
                    Log::Print("Shutdown Audio Engine\n");
                    break;
                case WAIT_OBJECT_0 + 1:     // _StreamSwitchEvent
                    // FIXME - Not supported.
                    break;
                case WAIT_OBJECT_0 + 2:     // _AudioSamplesReadyEvent
                    break;
                default:
                    Log::Print("Error unexpected event received  [%d]\n",
                               waitResult);
            }

            if (iQuit) {
                break;
            }
        }
    }
    catch (ThreadKill&) {}

    // Pull until the pipeline is emptied or we're told to quit.
    while (!iQuit) {
        Msg* msg = iPipeline.Pull();
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

    //
    //  Unhook from MMCSS.
    //
    AvRevertMmThreadCharacteristics(mmcssHandle);

    CoUninitialize();
}
