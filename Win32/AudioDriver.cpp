#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/OsWrapper.h>

#include "AudioDriver.h"
#include "CustomMessages.h"
#include "MemoryCheck.h"
#include "ProcessorPcmWASAPI.h"
#include "WWMFResampler.h"

#include <avrt.h>
#include <stdlib.h>

#pragma warning(disable : 4091 ) // Disable warning C4091: Typedef ignored on left of... (Inside the Windows SDKs - ksmedia.h)


using namespace OpenHome;
using namespace OpenHome::Media;

// Static data
TBool AudioDriver::iVolumeChanged = false;
float AudioDriver::iVolumeLevel   = 100.0f;
const TUint AudioDriver::kSupportedMsgTypes =   eMode
                                              | eDrain
                                              | eHalt
                                              | eDecodedStream
                                              | ePlayable
                                              | eQuit;

AudioDriver::AudioDriver(Environment& /*aEnv*/, IPipeline& aPipeline, HWND hwnd) :
    PipelineElement(kSupportedMsgTypes),
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
    iResamplingInput(false),
    iResampleInputBps(1),
    iResampleOutputBps(1),

    iPipeline(aPipeline),
    iPlayable(NULL),
    iQuit(false)
{
    iPipeline.SetAnimator(*this);
    iThread = new ThreadFunctor("PipelineAnimator", MakeFunctor(*this, &AudioDriver::AudioThread), kPrioritySystemHighest);
    iThread->Start();
}

AudioDriver::~AudioDriver()
{
    delete iThread;
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

Msg* AudioDriver::ProcessMsg(MsgDrain* aMsg)
{
    TUint32        padding;
    REFERENCE_TIME defaultPeriod;
    DWORD          periodMs;

    if (iAudioClient->GetDevicePeriod(&defaultPeriod, NULL) == S_OK)
    {
        // Convert the period from 100ns units to ms.
        periodMs = (DWORD)((defaultPeriod / 10000) + 1);

        // Loop until the render buffer is empty.
        while (iAudioClient->GetCurrentPadding(&padding) == S_OK)
        {
            if (padding == 0)
            {
                break;
            }

            // Check again in the next device period.
            Sleep(periodMs);
        }
    }

    aMsg->ReportDrained();
    aMsg->RemoveRef();

    return NULL;
}

TUint AudioDriver::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/,
                                                TUint /*aSampleRate*/,
                                                TUint /*aBitDepth*/,
                                                TUint /*aNumChannels*/) const
{
    return 0;
}

TUint AudioDriver::PipelineAnimatorBufferJiffies() const
{
    return 0;
}

TUint AudioDriver::PipelineAnimatorDsdBlockSizeWords() const
{
    return 0;
}

TUint AudioDriver::PipelineAnimatorMaxBitDepth() const
{
    return 0;
}

void AudioDriver::PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const
{
    aPcm = 0;
    aDsd = 0;
}


TBool AudioDriver::CheckMixFormat(TUint aSampleRate,
                                  TUint aNumChannels,
                                  TUint aBitDepth)
{
    HRESULT       hr;
    WAVEFORMATEX *closestMix;
    WAVEFORMATEX  savedMixFormat;
    TBool         retVal = false;

    // Complete any previous resampling session.
    if (iResamplingInput)
    {
        WWMFSampleData sampleData;

        hr = iResampler.Drain((iBufferSize * iFrameSize), &sampleData);

        if (hr == S_OK)
        {
            Log::Print("Resampler drained correctly [%d bytes].\n",
                       sampleData.bytes);

            sampleData.Release();
        }
        else
        {
            Log::Print("Resample drain failed.\n");
        }

        iResampler.Finalize();
    }

    iResamplingInput  = false;

    // Verify the Audio Engine supports the stream format.
    if (iMixFormat == NULL)
    {
        hr = iAudioClient->GetMixFormat(&iMixFormat);
        if (hr != S_OK)
        {
            Log::Print("ERROR: Could not obtain mix system format.\n");
            return false;
        }
    }

    savedMixFormat = *iMixFormat;

    iMixFormat->wFormatTag      = WAVE_FORMAT_PCM;
    iMixFormat->nChannels       = (WORD)aNumChannels;
    iMixFormat->nSamplesPerSec  = aSampleRate;
    iMixFormat->nBlockAlign     = WORD((aNumChannels * aBitDepth)/8);
    iMixFormat->nAvgBytesPerSec = DWORD(aSampleRate * iMixFormat->nBlockAlign);
    iMixFormat->wBitsPerSample  = (WORD)aBitDepth;
    iMixFormat->cbSize          = 0;

    hr = iAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                         iMixFormat,
                                        &closestMix);

    if (hr != S_OK)
    {
        // The stream format isn't suitable as it stands.
        //
        // Use a media foundation translation to convert to the current
        // mix format.

        //
        // Load the active mix format.
        //
        CoTaskMemFree(iMixFormat);

        hr = iAudioClient->GetMixFormat(&iMixFormat);

        if (hr == S_OK)
        {
            iMixFormat->wFormatTag = WAVE_FORMAT_PCM;
            iMixFormat->cbSize     = 0;

            // Confirm the mix format s valid.
            CoTaskMemFree(closestMix);

            hr = iAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                 iMixFormat,
                                                &closestMix);

            if (hr != S_OK)
            {
                Log::Print("ERROR: Cannot obtain valid mix format for stream "
                           "translation\n");

                retVal = false;

                goto end;
            }

            // Setup the translation.

            // Input stream format.
            WWMFPcmFormat inputFormat;

            inputFormat.sampleFormat       = WWMFBitFormatInt;
            inputFormat.nChannels          = (WORD)aNumChannels;
            inputFormat.sampleRate         = aSampleRate;
            inputFormat.bits               = (WORD)aBitDepth;
            inputFormat.validBitsPerSample = (WORD)aBitDepth;

            // System mix format.
            WWMFPcmFormat outputFormat;

            outputFormat.sampleFormat       = WWMFBitFormatInt;
            outputFormat.nChannels          = iMixFormat->nChannels;
            outputFormat.sampleRate         = iMixFormat->nSamplesPerSec;
            outputFormat.bits               = iMixFormat->wBitsPerSample;
            outputFormat.validBitsPerSample = iMixFormat->wBitsPerSample;

            // Store bytes per second values for later calculations around
            // the amount of data generated by the translation.
            iResampleInputBps  = inputFormat.BytesPerSec();
            iResampleOutputBps = outputFormat.BytesPerSec();

            if (iResampler.Initialize(inputFormat,
                                      outputFormat, 60) == S_OK)
            {
                iResamplingInput  = true;
                retVal            = true;
            }
            else
            {
                Log::Print("ERROR: Stream Transaltion Failed.\n");

                Log::Print("Transalte From:\n\n");

                Log::Print("\tSample Rate:        %6u\n", aSampleRate);
                Log::Print("\tNumber Of Channels: %6u\n", aNumChannels);
                Log::Print("\tBit Depth:          %6u\n", aBitDepth);

                Log::Print("Translate To:\n\n");

                Log::Print("\tSample Rate:        %6u\n",
                           iMixFormat->nSamplesPerSec);
                Log::Print("\tNumber Of Channels: %6u\n",
                           iMixFormat->nChannels);
                Log::Print("\tBit Depth:          %6u\n",
                           iMixFormat->wBitsPerSample);
            }
        }
    }
    else
    {
        retVal = true;
    }

end:
    CoTaskMemFree(closestMix);

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

    // This does not take into account the fact that this message may be
    // auto-generated 32 bit PCM and not the expected pcm format.
    //
    // At worst this should result in us asking for a larger render buffer
    // than is actually required.
    bytes = aMsg->Bytes();

    if (iResamplingInput)
    {
        // Calculate the bytes that will be generated by the translation.
        long long tmp = (long long)bytes * (long long)iResampleOutputBps /
                        (long long)iResampleInputBps;

        bytes = TUint(tmp);

        // Round up to the nearest frame.
        bytes += iMixFormat->nBlockAlign;
        bytes -= bytes % iMixFormat->nBlockAlign;
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
    if (hr != S_OK)
    {
        Log::Print("ERROR: Can't get render buffer");

        switch (hr)
        {
            case AUDCLNT_E_BUFFER_ERROR:
                Log::Print("[AUDCLNT_E_BUFFER_ERROR]\n");
                break;
            case AUDCLNT_E_BUFFER_TOO_LARGE:
                Log::Print("[AUDCLNT_E_BUFFER_TOO_LARGE]: %d\n", framesToWrite);
                break;
            case AUDCLNT_E_BUFFER_SIZE_ERROR:
                Log::Print("[AUDCLNT_E_BUFFER_SIZE_ERROR]\n");
                break;
            case AUDCLNT_E_OUT_OF_ORDER:
                Log::Print("[AUDCLNT_E_OUT_OF_ORDER]\n");
                break;
            case AUDCLNT_E_DEVICE_INVALIDATED:
                Log::Print("[AUDCLNT_E_DEVICE_INVALIDATED]\n");
                break;
            case AUDCLNT_E_BUFFER_OPERATION_PENDING:
                Log::Print("[AUDCLNT_E_BUFFER_OPERATION_PENDING]\n");
                break;
            case AUDCLNT_E_SERVICE_NOT_RUNNING:
                Log::Print("[AUDCLNT_E_SERVICE_NOT_RUNNING]\n");
                break;
            case E_POINTER:
                Log::Print("[E_POINTER]\n");
                break;
            default:
                Log::Print("[UNKNOWN]\n");
                break;
        }

        // Can't get render buffer. Hold on to the data for the next
        // render period.
        iPlayable = aMsg;
        iRenderClient->ReleaseBuffer(0, 0);
        return;
    }

    // Get the message data. This converts the pipeline data into a format
    // suitable for the native audio system.
    ProcessorPcmBufWASAPI pcmProcessor;
    pcmProcessor.SetBitDepth(iBitDepth);

    aMsg->Read(pcmProcessor);

    // Modify sample rate/bit to match system mix format, if required.
    if (iResamplingInput)
    {
        WWMFSampleData sampleData;
        Brn buf(pcmProcessor.Buf());

        hr = iResampler.Resample(buf.Ptr(),
                                 buf.Bytes(),
                                 &sampleData);

        if (hr == S_OK)
        {
            // Copy to the render buffer.
            CopyMemory(pData, sampleData.data, sampleData.bytes);

            framesToWrite = sampleData.bytes / iFrameSize;

            // Release the render buffer.
            hr = iRenderClient->ReleaseBuffer(framesToWrite, 0);

            if (hr != S_OK)
            {
                Log::Print("ReleaseBuffer failed Reserved [%d] Written [%d]\n",
                           bytes, sampleData.bytes);
                Log::Print("aMsg [%d] InBps [%d] OutBps [%d]\n",
                            buf.Bytes(),
                            iResampleInputBps ,
                            iResampleOutputBps);
            }

            iRenderBytesRemaining -= sampleData.bytes;

            sampleData.Release();
        }
        else
        {
            Log::Print("ERROR: ProcessFragment16: Resample failed.\n");
        }
    }
    else
    {
        Brn buf(pcmProcessor.Buf());

        // Copy to the render buffer.
        CopyMemory(pData, buf.Ptr(), buf.Bytes());

        framesToWrite = buf.Bytes() / iFrameSize;

        // Release the render buffer.
        hr = iRenderClient->ReleaseBuffer(framesToWrite, 0);

        if (hr != S_OK)
        {
            Log::Print("ReleaseBuffer failed Reserverd [%d] Written [%d]\n",
                       bytes, buf.Bytes());
        }

        iRenderBytesRemaining -= buf.Bytes();
    }

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

    aMsg->ReportHalted();
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
    if (hr != S_OK)
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
    if (hr != S_OK)
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
    if (hr != S_OK)
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }
    iAudioClient->Reset();

    if (iAudioSessionEvents)
    {
        iAudioSessionControl->UnregisterAudioSessionNotification(iAudioSessionEvents);
        delete iAudioSessionEvents;
    }

    SafeRelease(&iAudioClient);
    SafeRelease(&iRenderClient);
    SafeRelease(&iAudioSessionControl);
    SafeRelease(&iAudioSessionVolume);

    // Restart the audio client with latest mix format.
    hr = iAudioEndpoint->Activate(__uuidof(IAudioClient),
                                  CLSCTX_INPROC_SERVER,
                                  NULL,
                                  reinterpret_cast<void **>(&iAudioClient));
    if (hr != S_OK)
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

    if (hr != S_OK)
    {
        Log::Print("Unable to initialize audio client: %x.\n", hr);
        return false;
    }

    //
    //  Retrieve the buffer size, in frames, for the audio client.
    //
    hr = iAudioClient->GetBufferSize(&iBufferSize);
    if(hr != S_OK)
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
    if (hr != S_OK)
    {
        Log::Print("Unable to set ready event: %x.\n", hr);
        return false;
    }

    hr = iAudioClient->GetService(IID_PPV_ARGS(&iRenderClient));
    if (hr != S_OK)
    {
        Log::Print("Unable to get new render client: %x.\n", hr);
        return false;
    }

    // Get Audio Session Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionControl));
    if (hr != S_OK)
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

    if (hr != S_OK)
    {
        Log::Print("Unable to register for audio session notifications: %x\n",
                   hr);

        return false;
    }

    // Get Volume Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionVolume));
    if (hr != S_OK)
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

    if (hr != S_OK)
    {
        Log::Print("Unable to initialize audio client: %x.\n", hr);
        return false;
    }

    //
    //  Retrieve the buffer size, in frames,  for the audio client.
    //
    hr = iAudioClient->GetBufferSize(&iBufferSize);
    if (hr != S_OK)
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
    if (hr != S_OK)
    {
        Log::Print("Unable to set ready event: %x.\n", hr);
        return false;
    }

    hr = iAudioClient->GetService(IID_PPV_ARGS(&iRenderClient));
    if (hr != S_OK)
    {
        Log::Print("Unable to get new render client: %x.\n", hr);
        return false;
    }

    // Get Audio Session Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionControl));
    if (hr != S_OK)
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

    if (hr != S_OK)
    {
        Log::Print("Unable to register for audio session notifications: %x\n",
                   hr);

        return false;
    }

    // Get Volume Control
    hr = iAudioClient->GetService(IID_PPV_ARGS(&iAudioSessionVolume));
    if (hr != S_OK)
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
    if (hr != S_OK)
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

    if (iAudioSessionEvents)
    {
        iAudioSessionControl->UnregisterAudioSessionNotification(iAudioSessionEvents);
        delete iAudioSessionEvents;
    }

    SafeRelease(&iAudioEndpoint);
    SafeRelease(&iAudioClient);
    SafeRelease(&iRenderClient);
    SafeRelease(&iAudioSessionControl);
    SafeRelease(&iAudioSessionVolume);


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
    if (hr != S_OK)
    {
        Log::Print("Unable to stop audio client: %x\n", hr);
    }

    iAudioEngineInitialised = false;

    Log::Print("StopAudioEngine: Complete\n");
}

void AudioDriver::AudioThread()
{
    HANDLE mmcssHandle    = NULL;
    DWORD  mmcssTaskIndex = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr != S_OK)
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
                if (hr == S_OK)
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
                    ASSERT(msg != NULL);
                    msg = msg->Process(*this);
                    ASSERT(msg == NULL);
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

            if (iQuit)
            {
                break;
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

                    if (iResamplingInput)
                    {
                        // Calculate the bytes that will be generated by the
                        // translation.
                        long long tmp = (long long)bytes *
                                        (long long)iResampleOutputBps /
                                        (long long)iResampleInputBps;

                        bytes = TUint(tmp);

                        // Round up to the nearest frame.
                        bytes += iMixFormat->nBlockAlign;
                        bytes -= bytes % iMixFormat->nBlockAlign;
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
                    if (hr == S_OK)
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
                if (hr != S_OK)
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
    // Complete any previous resampling session.
    if (iResamplingInput)
    {
        WWMFSampleData sampleData;

        hr = iResampler.Drain((iBufferSize * iFrameSize), &sampleData);

        if (hr == S_OK)
        {
            Log::Print("Resampler drained correctly [%d bytes].\n",
                       sampleData.bytes);

            sampleData.Release();
        }
        else
        {
            Log::Print("Resampler drain failed.\n");
        }
    }

    iResampler.Finalize();

    // Now we've stopped reading the pipeline, stop the native audio.
    StopAudioEngine();

    // Free up native resources.
    ShutdownAudioEngine();

    //  Unhook from MMCSS.
    AvRevertMmThreadCharacteristics(mmcssHandle);

    CoUninitialize();
}
