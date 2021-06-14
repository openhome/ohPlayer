#include <OpenHome/Private/Printer.h>
#include <OpenHome/Net/Private/Globals.h>
#include <OpenHome/OsWrapper.h>
#include <alsa/asoundlib.h>
#include <memory>

#include "DriverAlsa.h"

using namespace OpenHome;
using namespace OpenHome::Media;


PriorityArbitratorDriver::PriorityArbitratorDriver(TUint aOpenHomeMax)
: iOpenHomeMax(aOpenHomeMax)
{
}

TUint PriorityArbitratorDriver::Priority(const TChar* /*aId*/, TUint aRequested, TUint aHostMax)
{
    ASSERT(aRequested == iOpenHomeMax);
    return aHostMax;
}

TUint PriorityArbitratorDriver::OpenHomeMin() const
{
    return iOpenHomeMax;
}

TUint PriorityArbitratorDriver::OpenHomeMax() const
{
    return iOpenHomeMax;
}

TUint PriorityArbitratorDriver::HostRange() const
{
    return 1;
}

class IDataSink
{
public:
    virtual void Write(const Brx& aData) = 0;
    virtual     ~IDataSink() {}
};

// PcmProcessorBase

class PcmProcessorBase : public IPcmProcessor
{
protected:
    PcmProcessorBase(IDataSink& aDataSink, Bwx& aBuffer);
public: // IPcmProcessor
    virtual void BeginBlock();
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    virtual void EndBlock();
    virtual void Flush();
public:
    void SetDuplicateChannel(TBool duplicateChannel);
    void SetBitDepth(TUint bitDepth);
protected:
    void Append(const TByte* aData, TUint aBytes);

    virtual void ProcessFragment8(const Brx& aData, TUint aNumChannels) = 0;
    virtual void ProcessFragment16(const Brx& aData, TUint aNumChannels) = 0;
    virtual void ProcessFragment24(const Brx& aData, TUint aNumChannels) = 0;
    virtual void ProcessFragment32(const Brx& aData, TUint aNumChannels) = 0;
protected:
    IDataSink& iSink;
    Bwx&       iBuffer;
    TBool      iDuplicateChannel;
    TUint      iBitDepth;
};

PcmProcessorBase::PcmProcessorBase(IDataSink& aDataSink, Bwx& aBuffer)
: iSink(aDataSink)
, iBuffer(aBuffer)
, iDuplicateChannel(false)
, iBitDepth(0)
{
}

void PcmProcessorBase::SetDuplicateChannel(TBool duplicateChannel)
{
    iDuplicateChannel = duplicateChannel;
}

void PcmProcessorBase::SetBitDepth(TUint bitDepth)
{
    iBitDepth = bitDepth;
}

void PcmProcessorBase::Append(const TByte* aData, TUint aBytes)
{
    if (iBuffer.BytesRemaining() < aBytes)
        Flush();

    iBuffer.Append(aData, aBytes);
}

void PcmProcessorBase::Flush()
{
    if (iBuffer.Bytes() != 0)
    {
        iSink.Write(iBuffer);
        iBuffer.SetBytes(0);
    }
}

void PcmProcessorBase::BeginBlock()
{
    ASSERT(iBuffer.Bytes() == 0);
}

void PcmProcessorBase::EndBlock()
{
    Flush();
}

void PcmProcessorBase::ProcessSilence(const Brx& aData, 
                                      TUint aNumChannels,
                                      TUint aNumSampleBytes)
{
    ProcessFragment(aData, aNumChannels, aNumSampleBytes);
}

void PcmProcessorBase::ProcessFragment(const Brx& aData,
                                       TUint aNumChannels,
                                       TUint /*aNumSampleBytes*/)
{
    switch (iBitDepth)
    {
        case 8: {
            ProcessFragment8(aData, aNumChannels);
            break;
        }
        case 16: {
            ProcessFragment16(aData, aNumChannels);
            break;
        }
        case 24: {
            ProcessFragment24(aData, aNumChannels);
            break;
        }
        case 32: {
            ProcessFragment32(aData, aNumChannels);
            break;
        }
        default: {
            ASSERT_VA(false, "%s", "Unknown bit depth.");
            break; // NOT REACHED
        }
    }

}


// PcmProcessorLe

class PcmProcessorLe : public PcmProcessorBase
{
public:
    PcmProcessorLe(IDataSink& aSink, Bwx& aBuffer);
public: // IPcmProcessor
    virtual void ProcessFragment8(const Brx& aData, TUint aNumChannels);
    virtual void ProcessFragment16(const Brx& aData, TUint aNumChannels);
    virtual void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    virtual void ProcessFragment32(const Brx& aData, TUint aNumChannels);
};

PcmProcessorLe::PcmProcessorLe(IDataSink& aSink, Bwx& aBuffer)
: PcmProcessorBase(aSink, aBuffer)
{
}

void PcmProcessorLe::ProcessFragment8(const Brx& aData, TUint aNumChannels)
{
    TByte *nData;
    TUint  bytes;

    // The input data is converted from unsigned 8 bit to signed 16 bit.
    // to removes poor audio quality and glitches when part of a playlist
    // with tracks of a different bit depth.
    //
    // Accordingly the amount of data is doubled.
    bytes = aData.Bytes() * 2;

    // If we are manually converting mono to stereo the data will double.
    if (iDuplicateChannel)
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    while (ptr1 < endp)
    {
        // Convert U8 to S16 data in little endian format.
        *ptr1++ = 0x00;
        *ptr1++ = *ptr - 0x80;

        if (iDuplicateChannel)
        {
            *ptr1++ = 0x00;
            *ptr1++ = *ptr - 0x80;
        }

        ptr++;
    }

    Brn fragment(nData, bytes);
    Flush();
    iSink.Write(fragment);
    delete[] nData;
}

void PcmProcessorLe::ProcessFragment16(const Brx& aData, TUint aNumChannels)
{
    TByte *nData;
    TUint  bytes;

    bytes = aData.Bytes();

    // If we are manually converting mono to stereo the data will double.
    if (iDuplicateChannel)
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    ASSERT(bytes % 2 == 0);

    while (ptr1 < endp)
    {
        // Store the S16 data in little endian format.
        *ptr1++ = *(ptr+1);
        *ptr1++ = *(ptr);

        if (iDuplicateChannel)
        {
            *ptr1++ = *(ptr+1);
            *ptr1++ = *(ptr);
        }

        ptr +=2;
    }

    Brn fragment(nData, bytes);
    Flush();
    iSink.Write(fragment);
    delete[] nData;
}

void PcmProcessorLe::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    TByte *nData;
    TUint  bytes;

    // 24 bit audio is not supported on the platform so it is converted
    // to signed 16 bit audio for playback.
    //
    // Accordingly one third of the input data is discarded.
    bytes = (aData.Bytes() * 2) / 3;

    // If we are manually converting mono to stereo the data will double.
    if (iDuplicateChannel)
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    ASSERT(bytes % 2 == 0);

    while (ptr1 < endp)
    {
        // Store the data in little endian format.
        *ptr1++ = *(ptr+1);
        *ptr1++ = *(ptr+0);

        if (iDuplicateChannel)
        {
            *ptr1++ = *(ptr+1);
            *ptr1++ = *(ptr+0);
        }

        ptr += 3;
    }

    Brn fragment(nData, bytes);
    Flush();
    iSink.Write(fragment);
    delete[] nData;
}

void PcmProcessorLe::ProcessFragment32(const Brx& aData, TUint aNumChannels)
{
    TByte *nData;
    TUint  bytes;

    // Currently the only 32 bit pcm in the pipeline is auto-generated by
    // the ramper.
    //
    // This may differ from the stream format so we must do the conversion
    // here.
    bytes = aData.Bytes();

    // If we are manually converting mono to stereo the data will double.
    //
    // aNumChannels must be checked as the ramper can inject 32 bit
    // stereo into the pipeline.
    if (iDuplicateChannel && (aNumChannels != 2))
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *endp = ptr + aData.Bytes();
    TByte *ptr1 = (TByte *)nData;

    TUint outBytes = 0;

    while (ptr < endp)
    {
        switch (iBitDepth)
        {
            // The system only supports upto 16 bit.
            //
            // Convert everything above that to 16 bit.
            case 32:
            // Fallthrough
            case 24:
            // Fallthrough
            case 16:
            {
                // Store the data in little endian format.
                *ptr1++ = *(ptr+1);
                *ptr1++ = *(ptr+0);
                outBytes += 2;

                if (iDuplicateChannel && (aNumChannels != 2))
                {
                    *ptr1++ = *(ptr+1);
                    *ptr1++ = *(ptr+0);
                    outBytes += 2;
                }

                break;
            }
            // The platform is configured for 8 bit. Convert.
            case 8:
            {
                *ptr1++ = *(ptr+0);
                outBytes += 1;

                if (iDuplicateChannel && (aNumChannels != 2))
                {
                    *ptr1++ = *(ptr+0);
                    outBytes += 1;
                }

                break;
            }
        }

        ptr += 4;
    }

    Brn fragment(nData, outBytes);
    Flush();
    iSink.Write(fragment);
    delete[] nData;
}

// PcmProcessorLe32

class PcmProcessorLe32 : public PcmProcessorLe
{
public:
    PcmProcessorLe32(IDataSink& aSink, Bwx& aBuffer);
public: // IPcmProcessor
    void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessFragment32(const Brx& aData, TUint aNumChannels);
};

PcmProcessorLe32::PcmProcessorLe32(IDataSink& aSink, Bwx& aBuffer)
: PcmProcessorLe(aSink, aBuffer)
{
}

void PcmProcessorLe32::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    TByte *nData;
    TUint  bytes;

    // 24 bit audio is not supported on the platform so it is converted
    // to signed 32 bit audio for playback.
    //
    // Accordingly we allocate room for 4 byte samples.
    bytes = (aData.Bytes() * 4) / 3;

    // If we are manually converting mono to stereo the data will double.
    if (iDuplicateChannel)
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *ptr1 = (TByte *)nData;
    TByte *endp = ptr1 + bytes;

    ASSERT(bytes % 4 == 0);

    while (ptr1 < endp)
    {
        // Store the data in little endian format.
        *ptr1++ = 0;
        *ptr1++ = *(ptr+2);
        *ptr1++ = *(ptr+1);
        *ptr1++ = *(ptr+0);

        if (iDuplicateChannel)
        {
            *ptr1++ = 0;
            *ptr1++ = *(ptr+2);
            *ptr1++ = *(ptr+1);
            *ptr1++ = *(ptr+0);
        }

        ptr += 3;
    }

    Brn fragment(nData, bytes);
    Flush();
    iSink.Write(fragment);
    delete[] nData;
}

void PcmProcessorLe32::ProcessFragment32(const Brx& aData, TUint aNumChannels)
{
    TByte *nData;
    TUint  bytes;

    // Currently the only 32 bit pcm in the pipeline is auto-generated by
    // the ramper.
    //
    // This may differ from the stream format so we must do the conversion
    // here.
    bytes = aData.Bytes();

    // If we are manually converting mono to stereo the data will double.
    //
    // aNumChannels must be checked as the ramper can inject 32 bit
    // stereo into the pipeline.
    if (iDuplicateChannel && (aNumChannels != 2))
    {
        bytes *= 2;
    }

    nData = new TByte[bytes];
    ASSERT(nData != NULL);

    TByte *ptr  = (TByte *)(aData.Ptr() + 0);
    TByte *endp = ptr + aData.Bytes();
    TByte *ptr1 = (TByte *)nData;

    TUint outBytes = 0;

    while (ptr < endp)
    {
        switch (iBitDepth)
        {
            // The platform supports and is configured for 32 bit audio.
            case 32:
            // Fallthrough
            case 24:
            {
                *ptr1++ = *(ptr+3);
                *ptr1++ = *(ptr+2);
                *ptr1++ = *(ptr+1);
                *ptr1++ = *(ptr+0);
                outBytes += 4;

                if (iDuplicateChannel && (aNumChannels != 2))
                {
                    *ptr1++ = *(ptr+3);
                    *ptr1++ = *(ptr+2);
                    *ptr1++ = *(ptr+1);
                    *ptr1++ = *(ptr+0);
                    outBytes += 4;
                }

                break;
            }
            // The platform is configured for 16 bit. Convert.
            case 16:
            {
                *ptr1++ = *(ptr+1);
                *ptr1++ = *(ptr+0);
                outBytes += 2;

                if (iDuplicateChannel && (aNumChannels != 2))
                {
                    *ptr1++ = *(ptr+1);
                    *ptr1++ = *(ptr+0);
                    outBytes += 2;
                }

                break;
            }
            // The platform is configured for 8 bit. Convert.
            case 8:
            {
                *ptr1++ = *(ptr+1);
                *ptr1++ = *(ptr+0);
                outBytes += 2;

                if (iDuplicateChannel && (aNumChannels != 2))
                {
                    *ptr1++ = *(ptr+1);
                    *ptr1++ = *(ptr+0);
                    outBytes += 2;
                }

                break;
            }
        }

        ptr += 4;
    }

    Brn fragment(nData, outBytes);
    Flush();
    iSink.Write(fragment);
    delete[] nData;
}

typedef std::pair<snd_pcm_format_t, TUint> OutputFormat;

class Profile
{
public:
    Profile(IPcmProcessor* aPcmProcessor, OutputFormat aFormat32,
                                          OutputFormat aFormat24,
                                          OutputFormat aFormat16,
                                          OutputFormat aFormat8);
public:
    OutputFormat   GetFormat(TUint aBitDepth) const;
    IPcmProcessor& GetPcmProcessor() const;
private:
    std::unique_ptr<IPcmProcessor> iPcmProcessor;
    OutputFormat                   iOutputDesc[4];
};

Profile::Profile(IPcmProcessor* aPcmProcessor, OutputFormat aFormat32,
                                               OutputFormat aFormat24,
                                               OutputFormat aFormat16,
                                               OutputFormat aFormat8)
: iPcmProcessor(aPcmProcessor)
{
    iOutputDesc[0] = aFormat32;
    iOutputDesc[1] = aFormat24;
    iOutputDesc[2] = aFormat16;
    iOutputDesc[3] = aFormat8;
}

OutputFormat Profile::GetFormat(TUint aBitDepth) const
{
    switch (aBitDepth)
    {
        case 32:
            return iOutputDesc[0];
        case 24:
            return iOutputDesc[1];
        case 16:
            return iOutputDesc[2];
        case 8:
            return iOutputDesc[3];
        default:
            ASSERTS();
            return iOutputDesc[0];
    }
}

IPcmProcessor& Profile::GetPcmProcessor() const
{
    return *iPcmProcessor;
}

/*  Pimpl

    Private implementation of ALSA output. Takes MsgPlayable
    and plays it.
*/

class DriverAlsa::Pimpl : public IDataSink
{
public:
    Pimpl(const TChar* aAlsaDevice, TUint aBufferUs);
    virtual ~Pimpl();
    void ProcessDecodedStream(MsgDecodedStream* aMsg);
    void ProcessPlayable(MsgPlayable* aMsg);
    void ProcessDrain();
    void LogPCMState();
    TUint DriverDelayJiffies(TUint aSampleRate);
public:
    virtual void Write(const Brx& aData);
private:
    TBool TryProfile(Profile& aProfile, TUint aBitDepth, TUint aNumChannels,
                     TUint aSampleRate, TUint aBufferUs);
private:
    snd_pcm_t* iHandle;
    Bwh iSampleBuffer;  // buffer ProcessSampleX data
    TUint iSampleBytes;
    TBool iDuplicateChannel;
    std::vector<Profile> iProfiles;
    TInt iProfileIndex;
    TBool iDitch;
    TUint iBytesSent;
    TUint iBufferUs;

    static const TUint kSampleBufSize = 16 * 1024;
};

DriverAlsa::Pimpl::Pimpl(const TChar* aAlsaDevice, TUint aBufferUs)
: iHandle(nullptr)
, iSampleBuffer(kSampleBufSize)
, iSampleBytes(0)
, iDuplicateChannel(false)
, iProfileIndex(-1)
, iDitch(false)
, iBytesSent(0)
, iBufferUs(aBufferUs)
{
    auto err = snd_pcm_open(&iHandle, aAlsaDevice, SND_PCM_STREAM_PLAYBACK, 0);
    ASSERT(err == 0);

    // PcmProcessorLe with S32 support
    iProfiles.emplace_back(new PcmProcessorLe32(*this, iSampleBuffer),
            OutputFormat(SND_PCM_FORMAT_S32_LE, 4),  // S32 -> S32
            OutputFormat(SND_PCM_FORMAT_S32_LE, 4),  // S24 -> S32
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),  // S16
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2)); // U8 -> S16

    // PcmProcessorLe without S32 support
    iProfiles.emplace_back(new PcmProcessorLe(*this, iSampleBuffer),
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),  // S32 -> S16
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),  // S24 -> S16
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),  // S16
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2)); // U8 -> S16
}

DriverAlsa::Pimpl::~Pimpl()
{
    auto err = snd_pcm_close(iHandle);
    ASSERT(err == 0);
}

void DriverAlsa::Pimpl::ProcessPlayable(MsgPlayable* aMsg)
{
    if (! iDitch)
    	aMsg->Read(iProfiles[iProfileIndex].GetPcmProcessor());
}

void DriverAlsa::Pimpl::ProcessDrain()
{
    // Wait for the native audio buffers to empty.
    if (iProfileIndex != -1)
    {
        // Drain the PCM buffers.
        auto err = snd_pcm_drain(iHandle);
        if (err < 0)
        {
            Log::Print("DriverAlsa: snd_pcm_drain() error : %s\n",
                       snd_strerror(err));
            ASSERTS();
        }

        // Prepare the PCM to accept new data.
        err = snd_pcm_prepare(iHandle);

        if (err < 0)
        {
            Log::Print("DriverAlsa: snd_pcm_prepare() error : %s\n",
                       snd_strerror(err));
            ASSERTS();
        }
    }
}

void DriverAlsa::Pimpl::Write(const Brx& aData)
{
    int err;

    err = snd_pcm_writei(iHandle, aData.Ptr(), aData.Bytes() / iSampleBytes);

    // Handle underrun errors.
    if(err == -EPIPE) {
        err = snd_pcm_prepare(iHandle);

        if (err < 0)
        {
            Log::Print("DriverAlsa: failed to snd_pcm_recover with %s\n",
                       snd_strerror(err));
            ASSERTS();
        }

        err = snd_pcm_writei(iHandle,
                             aData.Ptr(),
                             aData.Bytes() / iSampleBytes);
    }


    if (err < 0)
    {
        Log::Print("DriverAlsa: snd_pcm_writei() got error %s\n",
                   snd_strerror(err));
    }
    else
    {
        iBytesSent += aData.Bytes();
    }
}

#ifdef DEBUG
void DriverAlsa::Pimpl::LogPCMState()
{
    switch (snd_pcm_state(iHandle))
    {
        case SND_PCM_STATE_OPEN:
            Log::Print("PCM STATE: SND_PCM_STATE_OPEN\n");
            break;
        case SND_PCM_STATE_SETUP:
            Log::Print("PCM STATE: SND_PCM_STATE_SETUP\n");
            break;
        case SND_PCM_STATE_PREPARED:
            Log::Print("PCM STATE: SND_PCM_STATE_PREPARED\n");
            break;
        case SND_PCM_STATE_RUNNING:
            Log::Print("PCM STATE: SND_PCM_STATE_RUNNING\n");
            break;
        case SND_PCM_STATE_XRUN:
            Log::Print("PCM STATE: SND_PCM_STATE_XRUN\n");
            break;
        case SND_PCM_STATE_DRAINING:
            Log::Print("PCM STATE: SND_PCM_STATE_DRAINING\n");
            break;
        case SND_PCM_STATE_PAUSED:
            Log::Print("PCM STATE: SND_PCM_STATE_PAUSED\n");
            break;
        case SND_PCM_STATE_SUSPENDED:
            Log::Print("PCM STATE: SND_PCM_STATE_SUSPENDED\n");
            break;
        case SND_PCM_STATE_DISCONNECTED:
            Log::Print("PCM STATE: SND_PCM_STATE_DISCONNECTED\n");
            break;
        default:
            Log::Print("PCM STATE: UNKNOWN\n");
            break;
    }
}
#endif

void DriverAlsa::Pimpl::ProcessDecodedStream(MsgDecodedStream* aMsg)
{
    if (iProfileIndex != -1)
    {
        // Drain and stop the PCM.
        auto err = snd_pcm_drain(iHandle);
        if (err < 0)
        {
            Log::Print("DriverAlsa: snd_pcm_drain() error : %s\n",
                       snd_strerror(err));
            ASSERTS();
        }
    }

    auto decodedStreamInfo = aMsg->StreamInfo();

    Log::Print("DriverAlsa: Bytes Sent since last MsgDecodedStream = %d\n",
               iBytesSent);

    iBytesSent = 0;

    Log::Print("DriverAlsa: Finding PcmProcessor for stream: BitDepth = %d, "
               "SampleRate = %d, Channels = %d\n",
               decodedStreamInfo.BitDepth(), decodedStreamInfo.SampleRate(),
               decodedStreamInfo.NumChannels());

    // Mono plays badly on the Raspberry Pi and causes issues when
    // switching to a stereo track.
    //
    // So we configure the playback for stereo and duplicate the
    // channel data.
    if (decodedStreamInfo.NumChannels() == 1)
    {
        iDuplicateChannel = true;
    }
    else
    {
        iDuplicateChannel = false;
    }

    for (TUint i = 0; i < iProfiles.size(); ++i)
    {
        if (TryProfile(iProfiles[i], decodedStreamInfo.BitDepth(),
                       decodedStreamInfo.NumChannels(),
                       decodedStreamInfo.SampleRate(), iBufferUs))
        {
            iProfileIndex = i;

            PcmProcessorBase& pcmP =
                (PcmProcessorBase&)iProfiles[i].GetPcmProcessor();
            pcmP.SetDuplicateChannel(iDuplicateChannel);
            pcmP.SetBitDepth(decodedStreamInfo.BitDepth());

            iSampleBytes =
                decodedStreamInfo.NumChannels() *
                iProfiles[i].GetFormat(decodedStreamInfo.BitDepth()).second;

            // If we manually converting mono to stereo the sample size doubles.
            if (iDuplicateChannel)
            {
                iSampleBytes *= 2;
            }

            iDitch = false;

            Log::Print("Found PcmProcessor %d\n", iProfileIndex);

            return;
        }
    }

    Log::Print("DriverAlsa: Could not find a PcmProcessor for stream! "
               "BitDepth = %d, SampleRate = %d, Channels = %d\n",
               decodedStreamInfo.BitDepth(), decodedStreamInfo.SampleRate(),
               decodedStreamInfo.NumChannels());

    iDitch = true;
    iProfileIndex = -1;
}

TBool DriverAlsa::Pimpl::TryProfile(Profile& aProfile, TUint aBitDepth,
                                    TUint aNumChannels, TUint aSampleRate,
                                    TUint aBufferUs)
{
    auto outputFormat = aProfile.GetFormat(aBitDepth);

    if (iDuplicateChannel)
    {
        // We are manually converting a mono input to stereo.
        // Configure the stream for stereo.
        aNumChannels *= 2;
    }

    auto err = snd_pcm_set_params(iHandle,
                                  outputFormat.first,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  aNumChannels,
                                  aSampleRate,
                                  0,             // no soft-resample
                                  aBufferUs);
    return err == 0;
}

TUint DriverAlsa::Pimpl::DriverDelayJiffies(TUint aSampleRate)
{
    snd_pcm_sframes_t dp;
    int ret;

    if (!aSampleRate) {
        return 0;
    }

    // Verify the supplied sample rate is supported.
    snd_pcm_hw_params_t *hwParams;
    TUint                err;

    snd_pcm_hw_params_alloca(&hwParams);
    err = snd_pcm_hw_params_any(iHandle, hwParams);
    if (err < 0)
    {
        Log::Print("DriverAlsa: Cannot get hardware parameters: %s\n",
                   snd_strerror(err));

        THROW(SampleRateUnsupported);
    }

    if (snd_pcm_hw_params_test_rate(iHandle, hwParams, aSampleRate, 0) < 0)
    {
        THROW(SampleRateUnsupported);
    }

    ret = snd_pcm_delay(iHandle, &dp);
    if (ret < 0) {
        Log::Print("DriverAlsa: snd_pcm_delay() error : %s\n",
                   snd_strerror(ret));
        return 0;
    }

    Log::Print("DriverAlsa: snd_pcm_delay() : %u\n", dp);
    return dp * Jiffies::PerSample(aSampleRate);
}


// DriverAlsa

const TUint DriverAlsa::kSupportedMsgTypes = PipelineElement::MsgType::eMode
| PipelineElement::MsgType::eDrain
| PipelineElement::MsgType::eHalt
| PipelineElement::MsgType::eDecodedStream
| PipelineElement::MsgType::ePlayable
| PipelineElement::MsgType::eQuit;

DriverAlsa::DriverAlsa(IPipeline& aPipeline, TUint aBufferUs)
    : PipelineElement(kSupportedMsgTypes)
    , iPimpl(new Pimpl("default", aBufferUs))
    , iPipeline(aPipeline)
    , iMutex("alsa")
    , iQuit(false)
{
    iPipeline.SetAnimator(*this);

    iThread = new ThreadFunctor("PipelineAnimator",
                                MakeFunctor(*this, &DriverAlsa::AudioThread),
                                kPrioritySystemHighest);
    iThread->Start();
}

DriverAlsa::~DriverAlsa()
{
    delete iThread;
    delete iPimpl;
}

void DriverAlsa::AudioThread()
{
    try
    {
        for (;;)
        {
            Msg* msg = iPipeline.Pull();
            msg = msg->Process(*this);
            if (msg != NULL)
            {
                msg->RemoveRef();
            }

            AutoMutex am(iMutex);
            if (iQuit)
                break;
        }
    }
    catch (ThreadKill&) {}
}

TUint DriverAlsa::PipelineAnimatorBufferJiffies() const
{
	return 0;
}

TUint DriverAlsa::PipelineAnimatorDelayJiffies(AudioFormat aFormat,
											   TUint aSampleRate,
                                               TUint /*aBitDepth*/,
                                               TUint /*aNumChannels*/) const
{
	if (aFormat == AudioFormat::Dsd) {
		THROW(FormatUnsupported);
	}
    return iPimpl->DriverDelayJiffies(aSampleRate);
}

TUint DriverAlsa::PipelineAnimatorDsdBlockSizeWords() const
{
	return 0;
}

TUint DriverAlsa::PipelineAnimatorMaxBitDepth() const
{
    return 0;
}

Msg* DriverAlsa::ProcessMsg(MsgHalt* aMsg)
{
    aMsg->ReportHalted();

    return aMsg;
}

Msg* DriverAlsa::ProcessMsg(MsgDecodedStream* aMsg)
{
    iPimpl->ProcessDecodedStream(aMsg);
    return aMsg;
}

Msg* DriverAlsa::ProcessMsg(MsgPlayable* aMsg)
{
    iPimpl->ProcessPlayable(aMsg);
    return aMsg;
}

Msg* DriverAlsa::ProcessMsg(MsgQuit* aMsg)
{
    AutoMutex am(iMutex);
    iQuit = true;
    return aMsg;
}

Msg* DriverAlsa::ProcessMsg(MsgMode* aMsg)
{
    // TODO
    return aMsg;
}

Msg* DriverAlsa::ProcessMsg(MsgDrain* aMsg)
{
    // Ensure the ALSA audio buffer is emptied.
    iPimpl->ProcessDrain();

    aMsg->ReportDrained();

    return aMsg;
}
