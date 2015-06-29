#include <OpenHome/Private/Printer.h>
#include <OpenHome/Net/Private/Globals.h>
#include <OpenHome/OsWrapper.h>
#include <alsa/asoundlib.h>
#include <memory>

#include "DriverAlsa.h"

using namespace OpenHome;
using namespace OpenHome::Media;

class IDataSink
{
public:
    virtual void Write(const Brx& aData) = 0;
    virtual ~IDataSink() {}
};

// PcmProcessorBase

class PcmProcessorBase : public IPcmProcessor
{
protected:
    PcmProcessorBase(IDataSink& aDataSink, Bwx& aBuffer);
public: // IPcmProcessor
    virtual void BeginBlock();
    virtual void EndBlock();
protected:
    void Append(const TByte* aData, TUint aBytes);
    void Flush();
protected:
    IDataSink& iSink;
    Bwx& iBuffer;
};

PcmProcessorBase::PcmProcessorBase(IDataSink& aDataSink, Bwx& aBuffer)
: iSink(aDataSink)
, iBuffer(aBuffer)
{
}

void PcmProcessorBase::Append(const TByte* aData, TUint aBytes)
{
    if ( iBuffer.BytesRemaining() < aBytes )
        Flush();

    iBuffer.Append(aData, aBytes);
}

void PcmProcessorBase::Flush()
{
    if ( iBuffer.Bytes() != 0 )
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

// PcmProcessorPass

class PcmProcessorPass : public PcmProcessorBase
{
public:
    PcmProcessorPass(IDataSink& aSink, Bwx& aBuffer);
public: // IPcmProcessor
    virtual TBool ProcessFragment8(const Brx& aData, TUint aNumChannels);
    virtual TBool ProcessFragment16(const Brx& aData, TUint aNumChannels);
    virtual TBool ProcessFragment24(const Brx& aData, TUint aNumChannels);
    virtual void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample24(const TByte* aSample, TUint aNumChannels);
};

PcmProcessorPass::PcmProcessorPass(IDataSink& aSink, Bwx& aBuffer)
: PcmProcessorBase(aSink, aBuffer)
{
}

TBool PcmProcessorPass::ProcessFragment8(const Brx& aData, TUint aNumChannels)
{
    Flush();
    iSink.Write(aData);
    return true;
}

TBool PcmProcessorPass::ProcessFragment16(const Brx& aData, TUint aNumChannels)
{
    Flush();
    iSink.Write(aData);
    return true;
}

TBool PcmProcessorPass::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    Flush();
    iSink.Write(aData);
    return true;
}

void PcmProcessorPass::ProcessSample8(const TByte* aSample, TUint aNumChannels)
{
    Append(aSample, aNumChannels);
}

void PcmProcessorPass::ProcessSample16(const TByte* aSample, TUint aNumChannels)
{
    Append(aSample, 2*aNumChannels);
}

void PcmProcessorPass::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    Append(aSample, 3*aNumChannels);
}

// PcmProcessorLe

class PcmProcessorLe : public PcmProcessorBase
{
public:
    PcmProcessorLe(IDataSink& aSink, Bwx& aBuffer);
public: // IPcmProcessor
    virtual TBool ProcessFragment8(const Brx& aData, TUint aNumChannels);
    virtual TBool ProcessFragment16(const Brx& aData, TUint aNumChannels);
    virtual TBool ProcessFragment24(const Brx& aData, TUint aNumChannels);
    virtual void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample24(const TByte* aSample, TUint aNumChannels);
};

PcmProcessorLe::PcmProcessorLe(IDataSink& aSink, Bwx& aBuffer)
: PcmProcessorBase(aSink, aBuffer)
{
}

TBool PcmProcessorLe::ProcessFragment8(const Brx& aData, TUint aNumChannels)
{
    Append(aData.Ptr(), aData.Bytes());
    return true;
}

TBool PcmProcessorLe::ProcessFragment16(const Brx& aData, TUint aNumChannels)
{
    return false;
}

TBool PcmProcessorLe::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    return false;
}

void PcmProcessorLe::ProcessSample8(const TByte* aSample, TUint aNumChannels)
{
    Append(aSample, aNumChannels);
}

void PcmProcessorLe::ProcessSample16(const TByte* aSample, TUint aNumChannels)
{
    TUint byteIndex = 0;
    TByte subsample[2];
    for ( TUint i = 0 ; i < aNumChannels ; ++i )
    {
        subsample[0] = aSample[byteIndex+1];
        subsample[1] = aSample[byteIndex+0];
        Append(subsample, 2);
        byteIndex += 2;
    }
}

void PcmProcessorLe::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    TUint byteIndex = 0;
    TByte subsample[3];
    for ( TUint i = 0 ; i < aNumChannels ; ++i )
    {
        subsample[0] = aSample[byteIndex+2];
        subsample[1] = aSample[byteIndex+1];
        subsample[2] = aSample[byteIndex+0];
        Append(subsample, 3);
        byteIndex += 3;
    }
}

// PcmProcessorLe32

class PcmProcessorLe32 : public PcmProcessorBase
{
public:
    PcmProcessorLe32(IDataSink& aSink, Bwx& aBuffer);
public: // IPcmProcessor
    virtual TBool ProcessFragment8(const Brx& aData, TUint aNumChannels);
    virtual TBool ProcessFragment16(const Brx& aData, TUint aNumChannels);
    virtual TBool ProcessFragment24(const Brx& aData, TUint aNumChannels);
    virtual void ProcessSample8(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample16(const TByte* aSample, TUint aNumChannels);
    virtual void ProcessSample24(const TByte* aSample, TUint aNumChannels);
};

PcmProcessorLe32::PcmProcessorLe32(IDataSink& aSink, Bwx& aBuffer)
: PcmProcessorBase(aSink, aBuffer)
{
}

TBool PcmProcessorLe32::ProcessFragment8(const Brx& aData, TUint aNumChannels)
{
    Append(aData.Ptr(), aData.Bytes());
    return true;
}

TBool PcmProcessorLe32::ProcessFragment16(const Brx& aData, TUint aNumChannels)
{
    return false;
}

TBool PcmProcessorLe32::ProcessFragment24(const Brx& aData, TUint aNumChannels)
{
    return false;
}

void PcmProcessorLe32::ProcessSample8(const TByte* aSample, TUint aNumChannels)
{
    Append(aSample, aNumChannels);
}

void PcmProcessorLe32::ProcessSample16(const TByte* aSample, TUint aNumChannels)
{
    TUint byteIndex = 0;
    TByte subsample[2];
    for ( TUint i = 0 ; i < aNumChannels ; ++i )
    {
        subsample[0] = aSample[byteIndex+1];
        subsample[1] = aSample[byteIndex+0];
        Append(subsample, 2);
        byteIndex += 2;
    }
}

void PcmProcessorLe32::ProcessSample24(const TByte* aSample, TUint aNumChannels)
{
    // Expand 24bits to 32bits, LE
    TUint byteIndex = 0;
    TByte subsample[4];
    for ( TUint i = 0 ; i < aNumChannels ; ++i )
    {
        subsample[0] = 0;
        subsample[1] = aSample[byteIndex+2];
        subsample[2] = aSample[byteIndex+1];
        subsample[3] = aSample[byteIndex+0];
        Append(subsample, 4);
        byteIndex += 3;
    }
}
typedef std::pair<snd_pcm_format_t, TUint> OutputFormat;

class Profile
{
public:
    Profile(IPcmProcessor* aPcmProcessor, OutputFormat aFormat24, OutputFormat aFormat16, OutputFormat aFormat8);
    OutputFormat GetFormat(TUint aBitDepth) const;
    IPcmProcessor& GetPcmProcessor() const;
private:
    std::unique_ptr<IPcmProcessor> iPcmProcessor;
    OutputFormat iOutputDesc[3];
};

Profile::Profile(IPcmProcessor* aPcmProcessor, OutputFormat aFormat24, OutputFormat aFormat16, OutputFormat aFormat8)
: iPcmProcessor(aPcmProcessor)
{
    iOutputDesc[0] = aFormat24;
    iOutputDesc[1] = aFormat16;
    iOutputDesc[2] = aFormat8;
}

OutputFormat Profile::GetFormat(TUint aBitDepth) const
{
    switch ( aBitDepth )
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
    void ProcessHalt(MsgHalt* aMsg);
    TUint DriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo);
public:
    virtual void Write(const Brx& aData);
private:
    TBool TryProfile(Profile& aProfile, TUint aBitDepth, TUint aNumChannels, TUint aSampleRate, TUint aBufferUs);
private:
    snd_pcm_t* iHandle;
    Bwh iSampleBuffer;  // buffer ProcessSampleX data
    TUint iSampleBytes;
    std::vector<Profile> iProfiles;
    TInt iProfileIndex;
    TBool iDitch;
    TUint iBytesSent;
    TUint iBufferUs;
    TUint iByteRate;
    Bwh iZeroBuffer;
    TBool iHalted;
    snd_pcm_sw_params_t* iSwpPlay;
    snd_pcm_sw_params_t* iSwpHalt;

    static const TUint kSampleBufSize = 16 * 1024;
};

DriverAlsa::Pimpl::Pimpl(const TChar* aAlsaDevice, TUint aBufferUs)
: iHandle(nullptr)
, iSampleBuffer(kSampleBufSize)
, iSampleBytes(0)
, iProfileIndex(-1)
, iDitch(false)
, iBytesSent(0)
, iBufferUs(aBufferUs)
, iByteRate(0)
, iHalted(false)
{
    auto err = snd_pcm_open(&iHandle, aAlsaDevice, SND_PCM_STREAM_PLAYBACK, 0);
    ASSERT(err == 0);

    iProfiles.emplace_back(new PcmProcessorPass(*this, iSampleBuffer),
            OutputFormat(SND_PCM_FORMAT_S24_3BE, 3),
            OutputFormat(SND_PCM_FORMAT_S16_BE, 2),
            OutputFormat(SND_PCM_FORMAT_S8, 1)
            );
    iProfiles.emplace_back(new PcmProcessorLe(*this, iSampleBuffer),
            OutputFormat(SND_PCM_FORMAT_S24_3LE, 3),
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),
            OutputFormat(SND_PCM_FORMAT_S8, 1)
            );
    iProfiles.emplace_back(new PcmProcessorLe32(*this, iSampleBuffer),
            OutputFormat(SND_PCM_FORMAT_S32_LE, 4),
            OutputFormat(SND_PCM_FORMAT_S16_LE, 2),
            OutputFormat(SND_PCM_FORMAT_S8, 1)
            );

    snd_pcm_sw_params_malloc(&iSwpPlay);
    snd_pcm_sw_params_malloc(&iSwpHalt);
}

DriverAlsa::Pimpl::~Pimpl()
{
    snd_pcm_sw_params_free(iSwpPlay);
    snd_pcm_sw_params_free(iSwpHalt);
    auto err = snd_pcm_close(iHandle);
    ASSERT(err == 0);
}

void DriverAlsa::Pimpl::ProcessHalt(MsgHalt* aMsg)
{
    int err;

    if ( iProfileIndex != -1 )
    {
        Log::Print("DriverAlsa: got halt, applying 'halt' S/W params\n");
        err = snd_pcm_sw_params(iHandle, iSwpHalt);
        if (err < 0) {
            Log::Print("DriverAlsa: snd_pcm_sw_params() error : %s\n", snd_strerror(err));
        }

        iHalted = true;
    }
}

void DriverAlsa::Pimpl::ProcessPlayable(MsgPlayable* aMsg)
{
    if ( ! iDitch )
    	aMsg->Read(iProfiles[iProfileIndex].GetPcmProcessor());
}

void DriverAlsa::Pimpl::Write(const Brx& aData)
{
    int err;

    if (iHalted) {
        Log::Print("DriverAlsa: applying 'play' S/W params\n");
        err = snd_pcm_sw_params(iHandle, iSwpPlay);
        if (err < 0) {
            Log::Print("DriverAlsa: snd_pcm_sw_params() error : %s\n", snd_strerror(err));
        }
        iHalted = false;
    }

    err = snd_pcm_writei(iHandle, aData.Ptr(), aData.Bytes() / iSampleBytes);

    // Handle underrun/broken pipe errors.
    if(err == -EPIPE) {
        snd_pcm_prepare(iHandle);
        err = snd_pcm_writei(iHandle,
                             aData.Ptr(),
                             aData.Bytes() / iSampleBytes);
    }

    if ( err < 0 )
    {
        Log::Print("DriverAlsa: snd_pcm_writei() got error %s\n", snd_strerror(err));
        err = snd_pcm_recover(iHandle, err, 0);
        if ( err < 0 )
        {
            Log::Print("DriverAlsa: failed to snd_pcm_recover with %s\n", snd_strerror(err));
            ASSERTS();
        }
    }
    else
    {
        iBytesSent += aData.Bytes();
    }
}

void DriverAlsa::Pimpl::ProcessDecodedStream(MsgDecodedStream* aMsg)
{
    if ( iProfileIndex != -1 )
    {
        auto err = snd_pcm_drain(iHandle);
        if ( err < 0 )
        {
            Log::Print("DriverAlsa: snd_pcm_drain() error : %s\n", snd_strerror(err));
            ASSERTS();
        }
    }

    auto decodedStreamInfo = aMsg->StreamInfo();

    Log::Print("DriverAlsa: Bytes Sent since last MsgDecodedStream = %d\n", iBytesSent);
    iBytesSent = 0;

    iByteRate = decodedStreamInfo.BitRate() / 8;

    Log::Print("DriverAlsa: Finding PcmProcessor for stream: BitDepth = %d, SampleRate = %d, Channels = %d\n",
        decodedStreamInfo.BitDepth(), decodedStreamInfo.SampleRate(), decodedStreamInfo.NumChannels());
    for ( TUint i = 0 ; i < iProfiles.size() ; ++i )
    {
        if ( TryProfile(iProfiles[i], decodedStreamInfo.BitDepth(), decodedStreamInfo.NumChannels(), decodedStreamInfo.SampleRate(), iBufferUs) )
        {
            iProfileIndex = i;
            iSampleBytes = decodedStreamInfo.NumChannels() * iProfiles[i].GetFormat(decodedStreamInfo.BitDepth()).second;
            iDitch = false;
            Log::Print("Found PcmProcessor %d\n", iProfileIndex);

            int ret;
            snd_pcm_uframes_t bnd;

            ret = snd_pcm_sw_params_current(iHandle, iSwpPlay);
            if (ret < 0) {
                Log::Print("DriverAlsa: snd_pcm_sw_params_current() error : %s\n", snd_strerror(ret));
            }

            // Create parameter set for halt mode, to force output of silence
            snd_pcm_sw_params_copy(iSwpHalt, iSwpPlay);
            snd_pcm_sw_params_get_boundary(iSwpHalt, &bnd);
            snd_pcm_sw_params_set_stop_threshold(iHandle, iSwpHalt, bnd);
            snd_pcm_sw_params_set_silence_threshold(iHandle, iSwpHalt, 0);
            snd_pcm_sw_params_set_silence_size(iHandle, iSwpHalt, bnd);
            return;
        }
    }
    Log::Print("DriverAlsa: Could not find a PcmProcessor for stream! BitDepth = %d, SampleRate = %d, Channels = %d\n",
        decodedStreamInfo.BitDepth(), decodedStreamInfo.SampleRate(), decodedStreamInfo.NumChannels());
    iDitch = true;
}

TBool DriverAlsa::Pimpl::TryProfile(Profile& aProfile, TUint aBitDepth, TUint aNumChannels, TUint aSampleRate, TUint aBufferUs)
{
    auto outputFormat = aProfile.GetFormat(aBitDepth);

    auto err = snd_pcm_set_params(  iHandle,
                                    outputFormat.first,
                                    SND_PCM_ACCESS_RW_INTERLEAVED,
                                    aNumChannels,
                                    aSampleRate,
                                    0,  // no soft-resample
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
        Log::Print("DriverAlsa: snd_pcm_delay() error : %s\n", snd_strerror(ret));
        return 0;
    }

    Log::Print("DriverAlsa: snd_pcm_delay() : %u\n", dp);
    return dp * Jiffies::JiffiesPerSample(aSampleRateFrom);
}


// DriverAlsa

DriverAlsa::DriverAlsa(IPipeline& aPipeline, TUint aBufferUs)
    : Thread("alsa", kPriorityHighest)
    , iPimpl(new Pimpl("default", aBufferUs))
    , iPipeline(aPipeline)
    , iMutex("alsa")
    , iQuit(false)
{
    iPipeline.SetAnimator(*this);
    Start();
}

DriverAlsa::~DriverAlsa()
{
    Join();
    delete iPimpl;
}

void DriverAlsa::Run()
{
    for(;;)
    {
        CheckForKill();

        iPipeline.Pull()->Process(*this)->RemoveRef();

        AutoMutex am(iMutex);
        if ( iQuit )
            break;
    }
}

TUint DriverAlsa::PipelineDriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo)
{
    return iPimpl->DriverDelayJiffies(aSampleRateFrom, aSampleRateTo);
}

Msg* DriverAlsa::ProcessMsg(MsgHalt* aMsg)
{
    iPimpl->ProcessHalt(aMsg);
    return aMsg;    // ignore
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

// Unimplemented Msg handlers

Msg* DriverAlsa::ProcessMsg(MsgTrack* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverAlsa::ProcessMsg(MsgSession* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

