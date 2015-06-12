#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/OsWrapper.h>

#include "AudioDriver.h"
#include "CustomMessages.h"
#include "MemoryCheck.h"
#include "ProcessorPcmWASAPI.h"

#include <avrt.h>
#include <stdlib.h>


using namespace OpenHome;
using namespace OpenHome::Media;

// Static data
TBool AudioDriver::iVolumeChanged = false;
float AudioDriver::iVolumeLevel   = 100.0f;

AudioDriver::AudioDriver(Environment& /*aEnv*/, IPipeline& aPipeline, HWND hwnd) :
    iHwnd(hwnd),
    iAudioEndpoint(NULL),
    iAudioClient(NULL),
    iRenderClient(NULL),
    iMixFormat(NULL),
    iAudioSessionControl(NULL),
    iAudioSessionVolume(NULL),
    iAudioSessionEvents(NULL),
    iAudioSamplesReadyEvent(NULL),
    iAudioSessionDisconnectedEvent(NULL),
    iEngineLatencyInMS(25),
    iBufferSize(0),
    iStreamFormatSupported(false),
    iAudioSessionDisconnected(false),
    iAudioEngineInitialised(false),
    iAudioClientStarted(false),
    iRenderBytesThisPeriod(0),
    iRenderBytesRemaining(0),
    iFrameSize(0),
    iDuplicateChannel(false),

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

void AudioDriver::SetVolume(float level)
{
    iVolumeChanged = true;
    iVolumeLevel   = level;
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
    HRESULT hr = iAudioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr))
    {
        Log::Print("Warning Audio Endpoint mix format unknown\n",
                   aSampleRateTo);
        return 0;
    }

    // Plug the requested sample rate into the existing mix format and
    // query the Audio Engine.
    mixFormat->nSamplesPerSec = aSampleRateTo;

    hr = iAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                         mixFormat,
                                         &closestMix);

    CoTaskMemFree(closestMix);
    CoTaskMemFree(mixFormat);

    if (hr != S_OK)
    {
        iStreamFormatSupported = false;

        Log::Print("Warning sample rate not supported [%u]\n", aSampleRateTo);
        THROW(SampleRateUnsupported);
    }

    return 0;
}

TBool AudioDriver::CheckMixFormat(TUint iSampleRate,
                                  TUint iNumChannels,
                                  TUint iBitDepth)
{
    HRESULT       hr;
    WAVEFORMATEX *closestMix;
    WAVEFORMATEX  savedMixFormat;
    TBool         retVal = false;

    iDuplicateChannel = false;

    if (iMixFormat == NULL)
    {
        hr = iAudioClient->GetMixFormat(&iMixFormat);
        if (FAILED(hr))
        {
            Log::Print("ERROR: Could not obtain mix system format.\n");
            return false;
        }
    }

    savedMixFormat = *iMixFormat;

    // Verify the Audio Engine supports the pipeline format.
    iMixFormat->wFormatTag      = WAVE_FORMAT_PCM;
    iMixFormat->nChannels       = (WORD)iNumChannels;
    iMixFormat->nSamplesPerSec  = iSampleRate;
    iMixFormat->nBlockAlign     = WORD((iNumChannels * iBitDepth)/8);
    iMixFormat->nAvgBytesPerSec = DWORD(iSampleRate * iMixFormat->nBlockAlign);
    iMixFormat->wBitsPerSample  = (WORD)iBitDepth;
    iMixFormat->cbSize          = 0;

    hr = iAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                         iMixFormat,
                                        &closestMix);

    if (hr != S_OK)
    {
        // The stream format isn't suitable as it stands.

        // Check to see if the issue is the number of channels.
        // We can duplicate a channel to play mono as stereo.
        if (iNumChannels == 1 && closestMix->nChannels == 2)
        {
            iMixFormat->nChannels   = closestMix->nChannels;
            iMixFormat->nBlockAlign =
                                   WORD((iMixFormat->nChannels * iBitDepth)/8);
            iMixFormat->nAvgBytesPerSec =
                                   DWORD(iSampleRate * iMixFormat->nBlockAlign);

            CoTaskMemFree(closestMix);

            hr = iAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                 iMixFormat,
                                                &closestMix);

            if (hr == S_OK)
            {
                Log::Print("Converting mono input to stereo\n");
                iDuplicateChannel = true;
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

    iStreamFormatSupported = false;
    iFrameSize             = 0;

    if (CheckMixFormat(iSampleRate, iNumChannels, iBitDepth))
    {
        //
        // Now that we know the stream format and that it is viable
        // fire up the Audio Engine with the stream specifics.
        //
        // Any allocated resources will be freed on exit of pipeline
        // processing loop in Run().
        if (iAudioEngineInitialised)
        {
            if (!RestartAudioEngine())
            {
                iAudioEngineInitialised = false;
            }
        }
        else
        {
            if (InitializeAudioEngine())
            {
                iAudioEngineInitialised = true;
            }
        }

        if (iAudioEngineInitialised)
        {
            iStreamFormatSupported = true;
            iFrameSize             = iMixFormat->nBlockAlign;
        }
        else
        {
            PostMessage(iHwnd, WM_APP_AUDIO_INIT_ERROR, NULL, NULL);
        }
    }
    else
    {
        //
        // We can't play the audio stream, most likely due to the number
        // of channels changing, as a sample rate change would have been
        // caught in PipelineDriverDelayJiffies().
        //
        // There is no way to report this upstream, so we:
        // - Halt the Audio Engine
        // - Pull and discard the audio data from the pipeline.
        //
        if (iAudioClientStarted)
        {
            Log::Print("Stopping Audio Client.\n");

            iAudioClient->Stop();
            iAudioClient->Reset();
            iAudioClientStarted = false;
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
    if (! iStreamFormatSupported || iAudioSessionDisconnected)
    {
        aMsg->RemoveRef();
        return;
    }

    bytes = aMsg->Bytes();

    if (iDuplicateChannel)
    {
        bytes *= 2;
    }

    TUint framesToWrite = bytes / iFrameSize;

    if (bytes > iRenderBytesRemaining)
    {
        // We've passed enough data for this period. Hold on to the data
        // for the next render period.
        iPlayable = aMsg;
        return;
    }

    hr = iRenderClient->GetBuffer(framesToWrite, &pData);
    if (! SUCCEEDED(hr))
    {
        Log::Print("ERROR: Can't get render buffer\n");

        // Can't get render buffer. Hold on to the data for the next
        // render period.
        iPlayable = aMsg;
        iRenderClient->ReleaseBuffer(0, 0);
        return;
    }

    // Get the message data. This converts the pipeline data into a format
    // suitable for the native audio system.
    ProcessorPcmBufWASAPI pcmProcessor(iDuplicateChannel);
    aMsg->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());

    // Copy to the render buffer.
    CopyMemory(pData, buf.Ptr(), bytes);

    // Release the render buffer.
    iRenderClient->ReleaseBuffer(framesToWrite, 0);

    iRenderBytesRemaining -= bytes;

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

    aMsg->RemoveRef();
    return NULL;
}

// Obtain the use of the native multimedia device.
TBool AudioDriver::GetMultimediaDevice(IMMDevice **DeviceToUse)
{
    HRESULT hr;
    TBool   retValue = true;

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

    *DeviceToUse = device;
    retValue     = true;
Exit:
    SafeRelease(&deviceEnumerator);

    return retValue;
}

// Restart the audio engine with a new mix format.
TBool AudioDriver::RestartAudioEngine()
{
    HRESULT hr;

    iAudioClientStarted     = false;
    iAudioEngineInitialised = false;
    iPlayable               = NULL;

#ifdef _TIMINGS_DEBUG
    LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
    LARGE_INTEGER Frequency;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&StartingTime);
#endif /* _TIMINGS_DEBUG */

    // Shutdown audio client.
    hr = iAudioClient->Stop();
    if (FAILED(hr))
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }
    iAudioClient->Reset();

    SafeRelease(&iAudioClient);
    SafeRelease(&iRenderClient);
    SafeRelease(&iAudioSessionControl);
    SafeRelease(&iAudioSessionVolume);

    // Restart the audio client with latest mix format.
    hr = iAudioEndpoint->Activate(__uuidof(IAudioClient),
                                  CLSCTX_INPROC_SERVER,
                                  NULL,
                                  reinterpret_cast<void **>(&iAudioClient));
    if (FAILED(hr))
    {
        Log::Print("Unable to activate endpoint\n");
        return false;
    }

    hr = iAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                  AUDCLNT_STREAMFLAGS_NOPERSIST,
                                  iEngineLatencyInMS * 10000,
                                  0,
                                  iMixFormat,
                                  NULL);

    if (FAILED(hr))
    {
        Log::Print("Unable to initialize audio client: %x.\n", hr);
        return false;
    }

    //
    //  Retrieve the buffer size, in frames,  for the audio client.
    //
    hr = iAudioClient->GetBufferSize(&iBufferSize);
    if(FAILED(hr))
    {
        Log::Print("Unable to get audio client buffer: %x. \n", hr);
        return false;
    }

    //
    // Setup the maximum amount of data to buffer prior to starting the
    // audio client, to avoid glitches on startup.
    //
    iRenderBytesThisPeriod = iBufferSize * iFrameSize;
    iRenderBytesRemaining  = iRenderBytesThisPeriod;

    hr = iAudioClient->SetEventHandle(iAudioSamplesReadyEvent);
    if (FAILED(hr))
    {
        Log::Print("Unable to set ready event: %x.\n", hr);
        return false;
    }

    hr = iAudioClient->GetService(IID_PPV_ARGS(&iRenderClient));
    if (FAILED(hr))
    {
        Log::Print("Unable to get new render client: %x.\n", hr);
        return false;
    }

    // Get Audio Session Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionControl));
    if (FAILED(hr))
    {
        Log::Print("Unable to retrieve session control: %x\n", hr);
        return false;
    }

    //
    //  Register for session change notifications.
    //
    //  A stream switch is initiated when we receive a session disconnect
    //  notification or we receive a default device changed notification.
    //
    hr = iAudioSessionControl->RegisterAudioSessionNotification(iAudioSessionEvents);

    if (FAILED(hr))
    {
        Log::Print("Unable to register for audio session notifications: %x\n",
                   hr);

        return false;
    }

    // Get Volume Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionVolume));
    if (FAILED(hr))
    {
        Log::Print("Unable to retrieve volume control: %x\n", hr);
        return false;
    }

    iAudioEngineInitialised = true;

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
TBool AudioDriver::InitializeAudioEngine()
{
    HRESULT hr;

    Log::Print("Initializing Audio Engine\n");

    hr = iAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                  AUDCLNT_STREAMFLAGS_NOPERSIST,
                                  iEngineLatencyInMS * 10000,
                                  0,
                                  iMixFormat,
                                  NULL);

    if (FAILED(hr))
    {
        Log::Print("Unable to initialize audio client: %x.\n", hr);
        return false;
    }

    //
    //  Retrieve the buffer size, in frames,  for the audio client.
    //
    hr = iAudioClient->GetBufferSize(&iBufferSize);
    if(FAILED(hr))
    {
        Log::Print("Unable to get audio client buffer: %x. \n", hr);
        return false;
    }

    //
    // Setup the maximum amount of data to buffer prior to starting the
    // audio client, to avoid glitches on startup.
    //
    iRenderBytesThisPeriod = iBufferSize * iFrameSize;
    iRenderBytesRemaining  = iRenderBytesThisPeriod;

    hr = iAudioClient->SetEventHandle(iAudioSamplesReadyEvent);
    if (FAILED(hr))
    {
        Log::Print("Unable to set ready event: %x.\n", hr);
        return false;
    }

    hr = iAudioClient->GetService(IID_PPV_ARGS(&iRenderClient));
    if (FAILED(hr))
    {
        Log::Print("Unable to get new render client: %x.\n", hr);
        return false;
    }

    // Get Audio Session Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionControl));
    if (FAILED(hr))
    {
        Log::Print("Unable to retrieve session control: %x\n", hr);
        return false;
    }

    iAudioSessionEvents = new AudioSessionEvents(iHwnd,
                                                 iAudioSessionDisconnectedEvent);

    //
    //  Register for session change notifications.
    //
    //  A stream switch is initiated when we receive a session disconnect
    //  notification or we receive a default device changed notification.
    //
    hr = iAudioSessionControl->RegisterAudioSessionNotification(iAudioSessionEvents);

    if (FAILED(hr))
    {
        Log::Print("Unable to register for audio session notifications: %x\n",
                   hr);

        return false;
    }

    // Get Volume Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionVolume));
    if (FAILED(hr))
    {
        Log::Print("Unable to retrieve volume control: %x\n", hr);
        return false;
    }

    return true;
}

TBool AudioDriver::InitializeAudioClient()
{
    HRESULT hr;

    iAudioSamplesReadyEvent = CreateEventEx(NULL, NULL, 0,
                                            EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (iAudioSamplesReadyEvent == NULL)
    {
        Log::Print("Unable to create samples ready event: %d.\n",
                   GetLastError());

        goto Exit;
    }

    // Create our session disconnected event.
    //
    // This will be triggered when the audio session is disconnected.
    iAudioSessionDisconnectedEvent = CreateEventEx(NULL, NULL, 0,
                                       EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (iAudioSessionDisconnectedEvent == NULL)
    {
        Log::Print("Unable to create stream switch event: %d.\n",
                   GetLastError());

        goto Exit;
    }

    if (!GetMultimediaDevice(&iAudioEndpoint))
    {
        goto Exit;
    }

    iAudioEndpoint->AddRef();

    //
    // Now activate an IAudioClient object on the multimedia endpoint and
    // retrieve the mix format for that endpoint.
    //
    hr = iAudioEndpoint->Activate(__uuidof(IAudioClient),
                                  CLSCTX_INPROC_SERVER,
                                  NULL,
                                  reinterpret_cast<void **>(&iAudioClient));
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
    if (iAudioSamplesReadyEvent)
    {
        CloseHandle(iAudioSamplesReadyEvent);
        iAudioSamplesReadyEvent = NULL;
    }

    if (iAudioSessionDisconnectedEvent)
    {
        CloseHandle(iAudioSessionDisconnectedEvent);
        iAudioSessionDisconnectedEvent = NULL;
    }

    SafeRelease(&iAudioEndpoint);
    SafeRelease(&iAudioClient);
    SafeRelease(&iRenderClient);
    SafeRelease(&iAudioSessionControl);
    SafeRelease(&iAudioSessionVolume);

    delete (iAudioSessionEvents);

    if (iMixFormat)
    {
        CoTaskMemFree(iMixFormat);
        iMixFormat = NULL;
    }
}

//
// Stop the native renderer.
//
void AudioDriver::StopAudioEngine()
{
    HRESULT hr;

    Log::Print("StopAudioEngine: Starting ...\n");

    hr = iAudioClient->Stop();
    if (FAILED(hr))
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }

    iAudioEngineInitialised = false;

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
    HANDLE waitArray[2] = {iAudioSessionDisconnectedEvent,
                           iAudioSamplesReadyEvent};

    // Pipeline processing loop.
    try {
        for (;;) {
#ifdef _TIMINGS_DEBUG
            LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
            LARGE_INTEGER Frequency;

            QueryPerformanceFrequency(&Frequency);
            QueryPerformanceCounter(&StartingTime);
#endif /* _TIMINGS_DEBUG */

            TUint32 padding                  = 0;

            //
            //  Calculate the number of bytes in the render buffer
            //  for this period.
            //
            //  This is the maximum we will pull from the pipeline.
            //
            //  If the Audio Engine has not been initialized yet stick with
            //  the default value.
            //
            if (iAudioEngineInitialised && ! iAudioSessionDisconnected)
            {
                hr = iAudioClient->GetCurrentPadding(&padding);
                if (SUCCEEDED(hr))
                {
                    iRenderBytesThisPeriod = (iBufferSize - padding) *
                                              iFrameSize;
                }
                else
                {
                    Log::Print("ERROR: Couldn't read render buffer padding\n");
                    iRenderBytesThisPeriod = 0;
                }

                iRenderBytesRemaining = iRenderBytesThisPeriod;
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
            if (iRenderBytesThisPeriod * 0.5 < iRenderBytesRemaining)
            {
                Log::Print("Audio period: Requested Bytes [%u] : Returned Bytes"
                           " [%u]\n",
                           iRenderBytesThisPeriod,
                           iRenderBytesThisPeriod - iRenderBytesRemaining);

                if (iPlayable)
                {
                    TUint bytes = iPlayable->Bytes();

                    if (iDuplicateChannel)
                    {
                        bytes *= 2;
                    }

                    Log::Print("  Available Bytes [%u]\n", bytes);
                }
                else
                {
                    Log::Print("  Available Bytes [0]\n");
                }

                if (iAudioEngineInitialised)
                {
                    Log::Print(" Period Start Frames In Buffer [%u]\n",
                               padding);

                    hr = iAudioClient->GetCurrentPadding(&padding);
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
            if (! iStreamFormatSupported)
            {
                continue;
            }

            // The audio session has been disconnected.
            // Continue to pull from pipeline until we are instructed to quit.
            if (iAudioSessionDisconnected)
            {
                continue;
            }

            //
            // Start the Audio client once we have pre-loaded some
            // data to the render buffer.
            //
            // This will prevent any initial audio glitches..
            //
            if (! iAudioClientStarted)
            {
                // There was no data read this period so try again next period.
                if (iRenderBytesThisPeriod == iRenderBytesRemaining)
                {
                    continue;
                }

                hr = iAudioClient->Start();
                if (FAILED(hr))
                {
                    Log::Print("Unable to start render client: %x.\n", hr);
                    break;
                }

                iAudioClientStarted = true;
            }

            // Apply any volume changes
            if (iAudioClientStarted && iVolumeChanged)
            {
                iAudioSessionVolume->SetMasterVolume(iVolumeLevel, NULL);
                iVolumeChanged = false;
            }

            // Wait for a kick from the native audio engine.
            DWORD waitResult =
                WaitForMultipleObjects(2, waitArray, FALSE, INFINITE);

            switch (waitResult) {
                case WAIT_OBJECT_0 + 0:     // iAudioSessionDisconnectedEvent

                    // Stop the audio client
                    iAudioClient->Stop();
                    iAudioClient->Reset();
                    iAudioClientStarted = false;

                    iAudioSessionDisconnected = true;
                    break;
                case WAIT_OBJECT_0 + 1:     // iAudioSamplesReadyEvent
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
