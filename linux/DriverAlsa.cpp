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
    virtual void EndBlock();
    virtual void Flush();
public:
    void SetDuplicateChannel(TBool duplicateChannel);
protected:
    void Append(const TByte* aData, TUint aBytes);
protected:
    IDataSink& iSink;
    Bwx&       iBuffer;
    TBool      iDuplicateChannel;
};

PcmProcessorBase::PcmProcessorBase(IDataSink& aDataSink, Bwx& aBuffer)
: iSink(aDataSink)
, iBuffer(aBuffer)
, iDuplicateChannel(false)
{
}

void PcmProcessorBase::SetDuplicateChannel(TBool duplicateChannel)
{
    iDuplicateChannel = duplicateChannel;
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
    virtual void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample24(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample32(const TByte* aSample, TUint aNumChannels);
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
    ASSERTS();
}

void PcmProcessorLe::ProcessSample8(const TByte* aSample, TUint aNumChannels)
{
    TByte subsample[2];  // Store for S16 sample data.

    for (TUint i = 0; i < aNumChannels; ++i)
    {
        //  Convert the input unsigned 8 bit data to signed 16 bit (little
        //  endian).
        subsample[0] =  0x00;
        subsample[1] =  aSample[i] - 0x80;

        Append(subsample, 2);

        if (iDuplicateChannel)
        {
            Append(subsample, 2);
        }
    }
}

void PcmProcessorLe::ProcessSample16(const TByte* aSample, TUint aNumChannels)
{
    TUint byteIndex = 0;
    TByte subsample[2];

    for (TUint i = 0; i < aNumChannels; ++i)
    {
        // Store the S16 data in little endian format.
        subsample[0] = aSample[byteIndex+1];
        subsample[1] = aSample[byteIndex+0];

        Append(subsample, 2);

        if (iDuplicateChannel)
        {
            Append(subsample, 2);
        }

        byteIndex += 2;
    }
}

void PcmProcessorLe::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    TUint byteIndex = 0;
    TByte subsample[2];   // Store for S16 sample data.

    for (TUint i = 0; i < aNumChannels; ++i)
    {
        // Store the S16 data in little endian format.
        subsample[0] = aSample[byteIndex+1];
        subsample[1] = aSample[byteIndex+0];

        Append(subsample, 2);

        if (iDuplicateChannel)
        {
            Append(subsample, 2);
        }

        byteIndex += 3;
    }
}

void PcmProcessorLe::ProcessSample32(const TByte* aSample, TUint aNumChannels)
{
    ASSERTS();
}

// PcmProcessorLe32

class PcmProcessorLe32 : public PcmProcessorLe
{
public:
    PcmProcessorLe32(IDataSink& aSink, Bwx& aBuffer);
public: // IPcmProcessor
    void ProcessFragment24(const Brx& aData, TUint aNumChannels);
    void ProcessSample24(const TByte* aSample, TUint aNumChannels);
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

    ASSERT(bytes % 2 == 0);

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

void PcmProcessorLe32::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    TUint byteIndex = 0;
    TByte subsample[4];   // Store for S32 sample data.

    for (TUint i = 0; i < aNumChannels; ++i)
    {
        // Store the S32 data in little endian format.
        subsample[0] = 0;
        subsample[1] = aSample[byteIndex+2];
        subsample[2] = aSample[byteIndex+1];
        subsample[3] = aSample[byteIndex+0];

        Append(subsample, 4);

        if (iDuplicateChannel)
        {
            Append(subsample, 4);
        }

        byteIndex += 3;
    }
}

typedef std::pair<snd_pcm_format_t, TUint> OutputFormat;

class Profile
{
public:
    Profile(IPcmProcessor* aPcmProcessor, OutputFormat aFormat24,
            OutputFormat aFormat16, OutputFormat aFormat8);
public:
    OutputFormat   GetFormat(TUint aBitDepth) const;
    IPcmProcessor& GetPcmProcessor() const;
private:
    std::unique_ptr<IPcmProcessor> iPcmProcessor;
    OutputFormat                   iOutputDesc[3];
};

Profile::Profile(IPcmProcessor* aPcmProcessor, OutputFormat aFormat24,
                 OutputFormat aFormat16, OutputFormat aFormat8)
: iPcmProcessor(aPcmProcessor)
{
    iOutputDesc[0] = aFormat24;
    iOutputDesc[1] = aFormat16;
    iOutputDesc[2] = aFormat8;
}

OutputFormat Profile::GetFormat(TUint aBitDepth) const
{
    switch (aBitDepth)
    {
        case 24:
            return iOutputDesc[0];
        case 16:
            return iOutputDesc[1];
        case 8:
            return iOutputDesc[2];
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
    TUint DriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo);
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
            OutputFormat(SND_PCM_FORMAT_S32_LE, 4),  // S24 -> S32
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),  // S16
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2)); // U8 -> S16

    // PcmProcessorLe without S32 support
    iProfiles.emplace_back(new PcmProcessorLe(*this, iSampleBuffer),
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

TUint DriverAlsa::Pimpl::DriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo)
{
    snd_pcm_sframes_t dp;
    int ret;

    switch (aSampleRateTo) {
    // 48 KHz family rates
    case 192000:
    case 96000:
    case 64000:
    case 48000:
    case 32000:
    case 16000:
    case 8000:
    // 44.1 KHz family rates
    case 176400:
    case 88200:
    case 44100:
    case 22050:
    case 11025:
        break;

    default:
        ASSERTS();
    }

    if (!aSampleRateFrom) {
        return 0;
    }

    ret = snd_pcm_delay(iHandle, &dp);
    if (ret < 0) {
        Log::Print("DriverAlsa: snd_pcm_delay() error : %s\n",
                   snd_strerror(ret));
        return 0;
    }

    Log::Print("DriverAlsa: snd_pcm_delay() : %u\n", dp);
    return dp * Jiffies::JiffiesPerSample(aSampleRateFrom);
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

TUint DriverAlsa::PipelineDriverDelayJiffies(TUint aSampleRateFrom,
                                             TUint aSampleRateTo)
{
    return iPimpl->DriverDelayJiffies(aSampleRateFrom, aSampleRateTo);
}

Msg* DriverAlsa::ProcessMsg(MsgHalt* aMsg)
{
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
