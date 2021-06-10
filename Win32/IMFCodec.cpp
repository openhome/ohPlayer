//
// Native MP3/AAC codec implemented using a Media Foundation
// SourceReader and custom IMFByteStream.
//

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Propvarutil.h>

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/MimeTypeList.h>

#include "OHPlayerByteStream.h"
#include "OptionalFeatures.h"

// Uncomment to enable out of bounds checking in OpenHome buffers.
//#define BUFFER_GUARD_CHECK

#ifdef BUFFER_GUARD_CHECK
#include <assert.h>
#endif // BUFFER_GUARD_CHECK

// Uncomment to enable time stamping of log messages
//#define TIMESTAMP_LOGGING

#ifdef TIMESTAMP_LOGGING
#include <chrono>

#define DBUG_F(...)                                                            \
    Log::Print("[%llu] [CodecIMF] ",                                                      \
        std::chrono::high_resolution_clock::now().time_since_epoch().count()); \
    Log::Print(__VA_ARGS__)
#else
#define DBUG_F(...) Log::Print("[CodecIMF]" __VA_ARGS__)
#endif

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

#ifdef USE_IMFCODEC

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecIMF : public CodecBase
{
public:
    CodecIMF(IMimeTypeList& aMimeTypeList);
private: // from CodecBase
    ~CodecIMF();
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void  StreamInitialise();
    void  Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void  StreamCompleted();
private:
    void  FlushPCM();
    void  ProcessPCM(TByte *aBuffer, TInt aLength);
    void  GetEncodedBitrate(IMFSourceReader *aSourceReader);
    void  GetSourceDuration(IMFSourceReader *aSourceReader);
    TBool VerifyStreamType(IMFSourceReader *aSourceReader);
    TBool ConfigureAudioStream(IMFSourceReader *aSourceReader);
private:
    const TChar         *kFmtMp3;
    const TChar         *kFmtAac;

    TUint64                      iTotalSamples;
    TUint64                      iTrackLengthJiffies;
    TUint64                      iTrackOffset;
    Bws<DecodedAudio::kMaxBytes> iOutput;

    TUint64      iStreamLength;
    TInt         iChannels;
    TInt         iSampleRate;
    TInt         iBitDepth;
    TUint        iBitRate;
    TUint64      iDuration;
    TInt         iStreamId;
    const TChar *iStreamFormat;
    TBool        iStreamStart;
    TBool        iStreamEnded;

    OHPlayerByteStream *iByteStream;
    IMFSourceReader    *iSourceReader;
    SpeakerProfile     *iSpeakerProfile;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

#ifdef _DEBUG
// Format and log the contents of the supplied buffer.
void printBuf(TByte *buf, TInt bufLen)
{
    printf("Buffer Contents: %d\n", bufLen);

    TInt lines = bufLen/20;
    if (bufLen % 20 != 0)
    {
        lines++;
    }

    for (TInt i=0; i<lines; i++)
    {
        for (TInt j=0; j<20; j++)
        {
            if (i*20 + j >= bufLen)
            {
                break;
            }

            printf("%02x ", buf[(i*20) + j]);
        }

        printf("\n");
    }
}
#endif

#ifdef BUFFER_GUARD_CHECK
static TInt kGuardSize = 4;

void SetGuardBytes(Bwx& buffer)
{
    TUint8 *ptr;

    ptr = (TUint8 *)buffer.Ptr();

    ptr = ptr + buffer.MaxBytes() - kGuardSize;

    *ptr++ = 0xde;
    *ptr++ = 0xad;
    *ptr++ = 0xbe;
    *ptr++ = 0xef;
}

void CheckGuardBytes(Bwx& buffer)
{
    TUint8 *ptr;

    ptr = (TUint8 *)buffer.Ptr();

    ptr = ptr + buffer.MaxBytes() - kGuardSize;

    assert (*ptr++ == 0xde);
    assert (*ptr++ == 0xad);
    assert (*ptr++ == 0xbe);
    assert (*ptr++ == 0xef);
}
#endif // BUFFER_GUARD_CHECK


CodecBase* CodecFactory::NewMp3(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecIMF(aMimeTypeList);
}

// CodecIMF

#pragma warning(push)
#pragma warning(disable : 4100)
CodecIMF::CodecIMF(IMimeTypeList& aMimeTypeList)
    : CodecBase("MMF")
    , kFmtMp3("Mp3")
    , kFmtAac("Aac")
    , iTotalSamples(0)
    , iTrackLengthJiffies(0)
    , iTrackOffset(0)
    , iStreamLength(0)
    , iChannels(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iBitRate(0)
    , iDuration(0)
    , iStreamId(-1)
    , iStreamFormat(NULL)
    , iStreamStart(false)
    , iStreamEnded(false)
    , iByteStream(NULL)
    , iSourceReader(NULL)
{
#pragma warning(pop)


#ifdef ENABLE_MP3
    aMimeTypeList.Add("audio/mpeg");
    aMimeTypeList.Add("audio/x-mpeg");
    aMimeTypeList.Add("audio/mp1");
#endif // ENABLE_MP3

#ifdef ENABLE_AAC
    aMimeTypeList.Add("audio/aac");
    aMimeTypeList.Add("audio/aacp");
#endif // ENABLE_AAC

    HRESULT hr;

    // Initialise COM on this thread.
    hr = CoInitializeEx(NULL,
                        COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (FAILED(hr))
    {
        DBUG_F("Failed to initialise COM\n");
        return;
    }

    // Initialise the Media Foundation platform.
    hr = MFStartup(MF_VERSION);

    if (FAILED(hr))
    {
        DBUG_F("Failed to initialise Media Foundation\n");
        return;
    }

    iSpeakerProfile = new SpeakerProfile();
}

CodecIMF::~CodecIMF()
{
    // Shutdown the Media Foundation
    MFShutdown();

    // Finalise COM
    CoUninitialize();

    delete iSpeakerProfile;
}

// Get the encoded bit rate of the stream from the SourceReader, if available.
void CodecIMF::GetEncodedBitrate(IMFSourceReader *aSourceReader)
{
    PROPVARIANT prop;

    iBitRate = 0;

    HRESULT hr =
        aSourceReader->GetPresentationAttribute(
                                            (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                                            MF_PD_AUDIO_ENCODING_BITRATE,
                                           &prop);

    if (SUCCEEDED(hr))
    {
        hr = PropVariantToUInt32(prop, &iBitRate);
        PropVariantClear(&prop);

        iBitRate /= 1000;
    }
}

// Get the stream duration, in seconds, from the SourceReader, if available.
void CodecIMF::GetSourceDuration(IMFSourceReader *aSourceReader)
{
    PROPVARIANT  prop;
    MFTIME       duration;

    iDuration = 0;

    HRESULT hr =
        aSourceReader->GetPresentationAttribute(
                                            (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                                            MF_PD_DURATION,
                                           &prop);

    if (SUCCEEDED(hr))
    {
        hr = PropVariantToInt64(prop, &duration);
        PropVariantClear(&prop);

        // Convert from 100ns units to seconds
        duration += 2500000;
        duration /= 10000000;

        iDuration = duration;
    }
}

// Determine the audio format of the stream from the SourceReader.
//
// Returns true if the format is supported, false otherwise.
TBool CodecIMF::VerifyStreamType(IMFSourceReader *aSourceReader)
{
    HRESULT       hr        = S_OK;
    IMFMediaType *mediaType = NULL;
    TBool         retVal    = false;

    hr = aSourceReader->GetNativeMediaType(
                                     (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                     0,
                                    &mediaType);

    if (SUCCEEDED(hr))
    {
        GUID audioFormat;

        hr = mediaType->GetGUID(MF_MT_SUBTYPE, &audioFormat);

        if (hr == S_OK)
        {
            if (audioFormat == MFAudioFormat_AAC)
            {
#ifdef _DEBUG
                DBUG_F("Audio Format: [AAC]\n");
#endif

                iStreamFormat = kFmtAac;
                retVal        = TRUE;
            }
            else if (audioFormat == MFAudioFormat_MP3)
            {
#ifdef _DEBUG
                DBUG_F("Audio Format: [Mp3]\n");
#endif

                iStreamFormat = kFmtMp3;
                retVal        = TRUE;
            }
        }

        mediaType->Release();
    }
    else
    {
        DBUG_F("Media Type unavailable\n");
    }

    return retVal;
}

// Setup the output format to uncompressed PCM and note the incoming
// stream details.
TBool CodecIMF::ConfigureAudioStream(IMFSourceReader *aSourceReader)
{
    HRESULT hr = S_OK;

    IMFMediaType *uncompressedAudioType = NULL;
    IMFMediaType *partialType           = NULL;

    // Create a partial media type that specifies uncompressed PCM audio.
    hr = MFCreateMediaType(&partialType);

    if (FAILED(hr))
    {
        DBUG_F("Failed to create partial media type\n");
        goto failure;
    }

    hr = partialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);

    if (FAILED(hr))
    {
        DBUG_F("Failed to target audio stream\n");
        goto failure;
    }

    hr = partialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

    if (FAILED(hr))
    {
        DBUG_F("Failed to set PCM output format\n");
        goto failure;
    }

    // Set this type on the SourceReader.
    // The SourceReader will load the necessary decoder.
    hr = aSourceReader->SetCurrentMediaType(
                                     (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                     NULL,
                                     partialType);

    if (FAILED(hr))
    {
        DBUG_F("Failed to set media type on SourceReader\n");
        goto failure;
    }

    hr = aSourceReader->GetCurrentMediaType(
                                     (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                     &uncompressedAudioType);

    if (FAILED(hr))
    {
        DBUG_F("Failed to get uncompressed media type from SourceReader\n");
        goto failure;
    }

    // Ensure the audio stream is selected.
    if (SUCCEEDED(hr))
    {
        hr = aSourceReader->SetStreamSelection(
                                     (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                     TRUE);
    }

    hr = iByteStream->GetLength((QWORD *)&iStreamLength);

    if (FAILED(hr))
    {
        DBUG_F("Stream Length Unavailable\n");
    }
    else
    {
#ifdef _DEBUG
        DBUG_F("Stream Length [%llu]\n", iStreamLength);
#endif
    }

    iChannels = MFGetAttributeUINT32(uncompressedAudioType,
                                     MF_MT_AUDIO_NUM_CHANNELS,
                                     0);

#ifdef _DEBUG
    DBUG_F("Channels %d\n", iChannels);
#endif

    iSampleRate = MFGetAttributeUINT32(uncompressedAudioType,
                                       MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                       0);

#ifdef _DEBUG
    DBUG_F("Sample Rate %d\n", iSampleRate);
#endif

    iBitDepth = MFGetAttributeUINT32(uncompressedAudioType,
                                     MF_MT_AUDIO_BITS_PER_SAMPLE,
                                     0);

#ifdef _DEBUG
    DBUG_F("Bit Depth %d\n", iBitDepth);
#endif

    // Get Source File Duration (in seconds)
    GetSourceDuration(iSourceReader);

#ifdef _DEBUG
    DBUG_F("Duration %lld\n", iDuration);
#endif

    GetEncodedBitrate(iSourceReader);

    if (iBitRate != 0)
    {
#ifdef _DEBUG
        DBUG_F("Encoded Bitrate %u kbs\n", iBitRate);
#endif
    }
    else
    {
        // Estimate bit rate if we can.
        if (iDuration > 0)
        {
            iBitRate = (TInt)(iStreamLength / 1000 * 8 / iDuration);

#ifdef _DEBUG
            DBUG_F("Encoded Bitrate (Estimated) %d kbs\n",
                   iBitRate);
#endif
        }
        else
        {
            DBUG_F("Encoded Bitrate Unavailable\n");
        }
    }

    SafeRelease(&uncompressedAudioType);
    SafeRelease(&partialType);

    return TRUE;

failure:
    SafeRelease(&uncompressedAudioType);
    SafeRelease(&partialType);

    return false;
}

TBool CodecIMF::Recognise(const EncodedStreamInfo& /*aStreamInfo*/)
{
    HRESULT hr;
    TBool   retVal = false;

#ifdef _DEBUG
    DBUG_F("Recognise\n");
#endif

    // Initialise Stream State
    iStreamStart  = false;
    iStreamEnded  = false;

    try
    {
        // Initialise the byte stream.
        iByteStream = new OHPlayerByteStream(iController,
                                             (TBool *)&iStreamStart,
                                             (TBool *)&iStreamEnded);
    }
    catch (CodecStreamEnded&)
    {
        DBUG_F("Recognise: Failed To Create OHPlayerByteStream\n");

        SafeRelease(&iByteStream);
        return retVal;
    }

    // Create the SourceReader
    hr = MFCreateSourceReaderFromByteStream(iByteStream, NULL, &iSourceReader);

    if (FAILED(hr))
    {
        DBUG_F("Recognise: MFCreateSourceReaderFromByteStream Failed\n");    
        goto failure;
    }

    // Check if the stream format is one we are interested in.
    if (VerifyStreamType(iSourceReader))
    {
        return true;
    }

failure:
    // Tear down the SourceReader
    SafeRelease(&iByteStream);
    SafeRelease(&iSourceReader);

    return false;;
}

void CodecIMF::StreamInitialise()
{

#ifdef _DEBUG
    DBUG_F("StreamInitialise\n");
#endif

    // Initialise the track offset in jiffies.
    iTrackOffset = 0;

    // Initialise Stream State
    iStreamStart  = false;
    iStreamEnded  = false;

    // Initialise PCM buffer.
    iOutput.SetBytes(0);
#ifdef BUFFER_GUARD_CHECK
    SetGuardBytes(iOutput);
#endif // BUFFER_GUARD_CHECK

    // Identify and open the correct codec for the audio stream.
    if (! ConfigureAudioStream(iSourceReader))
    {
        goto failure;
    }

    // Remove the recognition cache and start operating on the actual stream.
    if (iStreamFormat == kFmtMp3)
    {
        // Move the stream position to that reached in Recognise().
        iByteStream->DisableRecogCache(true);
    }
    else
    {
        // Leave the stream position at 0.
        iByteStream->DisableRecogCache(false);
    }

    // Note that the recognition phase is complete.
    iByteStream->RecognitionComplete();

    if (iDuration > 0)
    {
        iTotalSamples       = iDuration * iSampleRate;
        iTrackLengthJiffies = iDuration * Jiffies::kPerSecond;
    }
    else
    {
        // Handle the case where a stream does not have a duration.
        // eg. a radio stream.
        iTotalSamples       = 0;
        iTrackLengthJiffies = 0;
    }

    iController->OutputDecodedStream(iBitRate,
                                     iBitDepth,
                                     iSampleRate,
                                     iChannels,
                                     Brn(iStreamFormat),
                                     iTrackLengthJiffies,
                                     0,
                                     false,
                                     *iSpeakerProfile);

    return;

failure:
    // Resources are tidied up in StreamCompleted
    THROW(CodecStreamCorrupt);
}

void CodecIMF::StreamCompleted()
{
#ifdef _DEBUG
    DBUG_F("StreamCompleted\n");
#endif

    SafeRelease(&iByteStream);
    SafeRelease(&iSourceReader);
}

TBool CodecIMF::TrySeek(TUint aStreamId, TUint64 aSample)
{
#ifdef _DEBUG
    DBUG_F("TrySeek - StreamId [%d] Sample[%llu]\n",
           aStreamId, aSample);
#else
    // Keep compiler happy
    aStreamId = 0;
    aSample   = 0LLU;
#endif // _DEBUG

    // Ditch any PCM we have buffered.
    iOutput.SetBytes(0);

    // Seeking disabled for now,
    return false;
}

// Flush any outstanding PCM data
void CodecIMF::FlushPCM()
{
    iTrackOffset +=
        iController->OutputAudioPcm(iOutput,
                                    iChannels,
                                    iSampleRate,
                                    iBitDepth,
                                    AudioDataEndian::Big,
                                    iTrackOffset);

    iOutput.SetBytes(0);
}

// Convert a decoded PCM chunk to big endian and add to the output buffer.
//
// The output buffer will be flushed when full.
void CodecIMF::ProcessPCM(TByte *aBuffer, TInt aLength)
{
    TInt   frameBytes  = iChannels * iBitDepth / 8;
    TUint  bufferLimit = iOutput.MaxBytes() - (iOutput.MaxBytes() % frameBytes);
    TByte *buffPtr     = (TByte *)(iOutput.Ptr() + 0);
    TUint  outIndex;
    TInt   inIndex     = 0;

#ifdef BUFFER_GUARD_CHECK
    bufferLimit -=
        (frameBytes > kGuardSize) ? frameBytes : kGuardSize;
#endif // BUFFER_GUARD_CHECK

    // Locate the first free byte in the output buffer.
    outIndex = iOutput.Bytes();

    // Process the full input buffer.
    while (inIndex < aLength)
    {
        // Flush the output buffer when full.
        if (outIndex == bufferLimit)
        {
            iTrackOffset +=
                iController->OutputAudioPcm(iOutput,
                                            iChannels,
                                            iSampleRate,
                                            iBitDepth,
                                            AudioDataEndian::Big,
                                            iTrackOffset);

            iOutput.SetBytes(0);
            outIndex = 0;
        }

        // Convert each sample to big endian PCM and store in the output buffer
        switch (iBitDepth)
        {
            case 8:
            {
                buffPtr[outIndex++] = aBuffer[inIndex++];

                break;
            }

            case 16:
            {
                buffPtr[outIndex++] = aBuffer[inIndex + 1];
                buffPtr[outIndex++] = aBuffer[inIndex];

                inIndex += 2;

                break;
            }

            case 24:
            {
                buffPtr[outIndex++] = aBuffer[inIndex + 2];
                buffPtr[outIndex++] = aBuffer[inIndex + 1];
                buffPtr[outIndex++] = aBuffer[inIndex];

                inIndex += 3;

                break;
            }

            default:
            {
                DBUG_F("ProcessPCM - Unsupported bit depth [%d]\n", iBitDepth);
                break;
            }
        }

#ifdef BUFFER_GUARD_CHECK
        CheckGuardBytes(iOutput);
#endif // BUFFER_GUARD_CHECK

        iOutput.SetBytes(outIndex);
    }

    // Flush the output if full.
    if (outIndex == bufferLimit)
    {
        iTrackOffset +=
            iController->OutputAudioPcm(iOutput,
                                        iChannels,
                                        iSampleRate,
                                        iBitDepth,
                                        AudioDataEndian::Big,
                                        iTrackOffset);

        iOutput.SetBytes(0);
    }
}

void CodecIMF::Process()
{
    HRESULT         hr;
    DWORD           flags        = 0;
    IMFSample      *sampleBuf    = NULL;
    IMFMediaBuffer *mediaBuf     = NULL;
    DWORD           decodedBytes = 0;
    BYTE           *decodedBuf   = NULL;

    // Read the next decoded sample chunk from the SourceReader.
    hr = iSourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                   0,
                                   NULL,
                                  &flags,
                                   NULL,
                                  &sampleBuf);

    if (iStreamStart)
    {
        DBUG_F("SourceReader ReadSample CodecStreamStart\n");

        THROW(CodecStreamStart);
    }

    if (iStreamEnded)
    {
        DBUG_F("SourceReader ReadSample CodecStreamEnded\n");

        THROW(CodecStreamEnded);
    }

    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
    {
        DBUG_F("Type change not supported.\n");

        SafeRelease(&sampleBuf);
        THROW(CodecStreamCorrupt);
    }

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
    {
        DBUG_F("End of input stream\n");

        SafeRelease(&sampleBuf);
        THROW(CodecStreamEnded);
    }

    if (FAILED(hr))
    {
        DBUG_F("ReadSample Failed\n");

        SafeRelease(&sampleBuf);
        THROW(CodecStreamEnded);
    }

    if (sampleBuf == NULL)
    {
        DBUG_F("No sample read\n");

        THROW(CodecStreamEnded);
    }

    // Get a pointer to the audio data in the sample.
    hr = sampleBuf->ConvertToContiguousBuffer(&mediaBuf);

    if (FAILED(hr))
    {
        DBUG_F("Failed to convert sample to contiguous buffer\n");

        SafeRelease(&sampleBuf);
        return;
    }

    hr = mediaBuf->Lock(&decodedBuf, NULL, &decodedBytes);

    if (FAILED(hr))
    {
        DBUG_F("Failed to lock contiguous buffer\n");

        SafeRelease(&sampleBuf);
        SafeRelease(&mediaBuf);
        return;
    }

    // Convert the sample data to the required format and pass on to the
    // next pipeline element.
    ProcessPCM((TByte *)decodedBuf, (TInt)decodedBytes);

    if (decodedBuf)
    {
        mediaBuf->Unlock();
    }

    SafeRelease(&sampleBuf);
    SafeRelease(&mediaBuf);
}
#endif // USE_IMFCODEC
