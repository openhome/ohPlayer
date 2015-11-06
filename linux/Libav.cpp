#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <stdlib.h>
#include <string.h>

//#define BUFFER_GUARD_CHECK

#ifdef BUFFER_GUARD_CHECK
#include <assert.h>
#endif // BUFFER_GUARD_CHECK

extern "C"
{
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libavformat/avformat.h"
}

#include "OptionalFeatures.h"

#ifdef USE_LIBAVCODEC
namespace OpenHome {
namespace Media {
namespace Codec {

// Data passed to the libavcodec callbacks.
typedef struct
{
   ICodecController *controller;
   TBool            *streamStart;
   TBool            *streamEnded;
   TUint             streamId;
   TBool            *seekExpected;
   TBool            *seekExecuted;
   TBool            *seekSuccess;
} OpaqueType;

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

class CodecLibAV : public CodecBase
{
public:
    CodecLibAV(IMimeTypeList& aMimeTypeList);
private: // from CodecBase
    ~CodecLibAV();
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();
private:
    static const TUint   kInBufBytes      = 4096;
    static const TInt32  kInt24Max        = 8388607L;
    static const TInt32  kInt24Min        = -8388608L;
    static const TInt    kDurationRoundUp = 50000;

    const TChar         *kFmtMp3;
    const TChar         *kFmtAac;

    static int     avCodecRead(void* ptr, TUint8* buf, TInt buf_size);
    static TInt64  avCodecSeek(void* ptr, TInt64 offset, TInt whence);
    static TBool   isPlatformBigEndian(void);

    void processPCM(TInt plane_size, TInt inSampleBytes, TInt outSampleBytes);

    TUint64                      iTotalSamples;
    TUint64                      iTrackLengthJiffies;
    TUint64                      iTrackOffset;
    Bws<DecodedAudio::kMaxBytes> iOutput;

    AVInputFormat   *iFormat;
    AVIOContext     *iAvioCtx;
    AVFormatContext *iAvFormatCtx;
    AVCodecContext  *iAvCodecContext;
    TBool            iAvPacketCached;
    AVPacket         iAvPacket;
    AVFrame         *iAvFrame;
    TInt             iStreamId;
    const TChar     *iStreamFormat;
    TUint            iBitDepth;
    TBool            iStreamStart;
    TBool            iStreamEnded;
    TBool            iSeekExpected;
    TBool            iSeekExecuted;
    TBool            iSeekSuccess;
    OpaqueType       iClassData;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewMp3(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecLibAV(aMimeTypeList);
}

// CodecLibAV

CodecLibAV::CodecLibAV(IMimeTypeList& aMimeTypeList)
    : CodecBase("LIBAV")
    , kFmtMp3("Mp3")
    , kFmtAac("Aac")
    , iTotalSamples(0)
    , iTrackLengthJiffies(0)
    , iTrackOffset(0)
    , iFormat(NULL)
    , iAvioCtx(NULL)
    , iAvFormatCtx(NULL)
    , iAvCodecContext(NULL)
    , iAvPacketCached(false)
    , iAvFrame(NULL)
    , iStreamId(-1)
    , iStreamFormat(NULL)
    , iBitDepth(0)
    , iStreamStart(false)
    , iStreamEnded(false)
    , iSeekExpected(false)
    , iSeekExecuted(false)
    , iSeekSuccess(false)
{
#ifdef ENABLE_MP3
    aMimeTypeList.Add("audio/mpeg");
    aMimeTypeList.Add("audio/x-mpeg");
    aMimeTypeList.Add("audio/mp1");
#endif // ENABLE_MP3

#ifdef ENABLE_AAC
    aMimeTypeList.Add("audio/aac");
    aMimeTypeList.Add("audio/aacp");
#endif // ENABLE_AAC

    av_register_all();

    // Initialise our encoded packet container.
    av_init_packet(&iAvPacket);
}

CodecLibAV::~CodecLibAV()
{
}

#ifdef DEBUG
void printBuf(TChar *buf, TInt bufLen)
{
    Log::Print("Buffer Contents: %d\n", bufLen);

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

            Log::Print("%02x ", buf[(i*20) + j]);
        }

        Log::Print("\n");
    }
}
#endif

// Is the current platform big endian.
TBool CodecLibAV::isPlatformBigEndian(void)
{
    union
    {
        TUint32 i;
        TChar   c[4];
    } bint = {0x01020304};

    return bint.c[0] == 1;
}

// AVCodec callback to read stream data into avcodec buffer.
TInt CodecLibAV::avCodecRead(void* ptr, TUint8* buf, TInt buf_size)
{
    OpaqueType       *classData       = (OpaqueType *)ptr;
    ICodecController *controller      = classData->controller;;
    TBool            *streamStart     = classData->streamStart;
    TBool            *streamEnded     = classData->streamEnded;
    Bwn               inputBuffer(buf, buf_size);

    inputBuffer.SetBytes(0);

    try
    {
        // Read the requested amount of data.
        controller->Read(inputBuffer, inputBuffer.MaxBytes());
    }
    catch(CodecStreamStart&)
    {
#ifdef DEBUG
        Log::Print("Info: [CodecLibAV]: avCodecRead - CodecStreamStart "
                   "Exception Caught\n");
#endif // DEBUG
        *streamStart = true;
    }
    catch(CodecStreamEnded&)
    {
#ifdef DEBUG
        Log::Print("Info: [CodecLibAV] avCodecRead - CodecStreamEnded "
                   "Exception Caught\n");
#endif // DEBUG
        *streamEnded = true;
    }
    catch(CodecStreamStopped&)
    {
#ifdef DEBUG
        Log::Print("Info: [CodecLibAV] avCodecRead - CodecStreamStopped "
                   "Exception Caught\n");
#endif // DEBUG
        *streamEnded = true;
    }

    return inputBuffer.Bytes();
}

// AVCodec callback to seek to a position in the input stream.
//
// We don't actually seek here, but use this callback as a mechanism to
// determine the location the stream of a given frame (specified as a
// time offset).
TInt64 CodecLibAV::avCodecSeek(void* ptr, TInt64 offset, TInt whence)
{
    OpaqueType       *classData    = (OpaqueType *)ptr;
    ICodecController *controller   = classData->controller;;
    TUint             streamId     = classData->streamId;
    TBool            *seekExpected = classData->seekExpected;
    TBool            *seekExecuted = classData->seekExecuted;
    TBool            *seekSuccess   = classData->seekSuccess;

    // Ignore the force bit.
    whence = whence & ~AVSEEK_FORCE;

    switch (whence)
    {
        case SEEK_SET:
        {
#ifdef DEBUG
            Log::Print("Info: [CodecLibAV] avCodecSeek Seek [SET] [%jd]\n",
                       offset);
#endif // DEBUG

            // Avoid attempting to seek when not initiated from TrySeek()
            if (! *seekExpected)
            {
#ifdef DEBUG
                Log::Print("Info: [CodecLibAV] avCodecSeek Seek [SET] "
                           "Ignored\n");
#endif // DEBUG
                return -1;
            }

            *seekExpected = false;
            *seekExecuted = true;
            *seekSuccess  = false;

            if (controller->TrySeekTo(streamId, offset))
            {
#ifdef DEBUG
                Log::Print("Info: [CodecLibAV] avCodecSeek Seek [SET] "
                           "Succeeded\n");
#endif // DEBUG

                *seekSuccess = true;
                return offset;
            }
            else
            {
#ifdef DEBUG
                Log::Print("Info: [CodecLibAV] avCodecSeek Seek [SET] "
                           "Failed\n");
#endif // DEBUG

                return -1;
            }
        }
        case SEEK_CUR:
        {
            Log::Print("[CodecLibAV] avCodecSeek Unsupported Seek "
                       "[CUR] [%jd]\n", offset);
            return -1;
        }
        case SEEK_END:
        {
            Log::Print("[CodecLibAV] avCodecSeek Unsupported Seek "
                       "[END] [%jd]\n", offset);
            return -1;
        }
        case AVSEEK_SIZE:
        {
#ifdef DEBUG
            Log::Print("[CodecLibAV] avCodecSeek Seek [SIZE] [%jd]\n",
                       controller->StreamLength());
#endif // DEBUG

            return controller->StreamLength();
        }
        default:
        {
            Log::Print("[CodecLibAV] avCodecSeek Unsupported Seek\n");
            return -1;
        }
    }
}

TBool CodecLibAV::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    Bws<16*1024> recogBuf;

    if (aStreamInfo.RawPcm())
    {
        return false;
    }

    // Initialise and fill the recognise buffer.
    recogBuf.SetBytes(0);
    recogBuf.FillZ();

#ifdef BUFFER_GUARD_CHECK
    SetGuardBytes(recogBuf);

    iController->Read(recogBuf, recogBuf.MaxBytes() - AVPROBE_PADDING_SIZE -
                                kGuardSize);
#else // BUFFER_GUARD_CHECK
    iController->Read(recogBuf, recogBuf.MaxBytes() - AVPROBE_PADDING_SIZE);
#endif // BUFFER_GUARD_CHECK

    // Attempt to detect the stream format.
    AVProbeData probeData;

    probeData.filename  = "";
    probeData.buf       = (unsigned char *)recogBuf.Ptr();
#ifdef BUFFER_GUARD_CHECK
    probeData.buf_size  = recogBuf.MaxBytes() - kGuardSize;
#else // BUFFER_GUARD_CHECK
    probeData.buf_size  = recogBuf.MaxBytes();
#endif // BUFFER_GUARD_CHECK

    iFormat = av_probe_input_format(&probeData, 1);

#ifdef BUFFER_GUARD_CHECK
    CheckGuardBytes(recogBuf);
#endif // BUFFER_GUARD_CHECK

    if (iFormat == NULL)
    {
        Log::Print("[CodecLibAV] Recognise - Probe Failed\n");
        return false;
    }

    return true;
}

void CodecLibAV::StreamInitialise()
{
#ifdef DEBUG
    Log::Print("[CodecLibAV] StreamInitialise\n");
#endif

    // Initialise PCM buffer.
    iOutput.SetBytes(0);
#ifdef BUFFER_GUARD_CHECK
    SetGuardBytes(iOutput);
#endif // BUFFER_GUARD_CHECK

    // Initialise the track offset in jiffies.
    iTrackOffset = 0;

    iAvPacketCached = false;

    // Initialise Stream State
    iStreamStart  = false;
    iStreamEnded  = false;

    iSeekExpected  = false;
    iSeekExecuted  = false;
    iSeekSuccess   = false;

    // Initialise the codec data buffer.
    //
    // NB. This may be free'd/realloced out with our control.
    unsigned char *avcodecBuf = (unsigned char *)av_malloc(kInBufBytes);

    if (avcodecBuf == NULL)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Cannot allocate AV "
                   "buffer\n");
        goto failure;
    }

    // Data to be passed to the AVCodec callbacks.
    iClassData.controller     = iController;
    iClassData.streamStart    = &iStreamStart;
    iClassData.streamEnded    = &iStreamEnded;
    iClassData.streamId       = 0;
    iClassData.seekExpected   = &iSeekExpected;
    iClassData.seekExecuted   = &iSeekExecuted;
    iClassData.seekSuccess    = &iSeekSuccess;

    // Manually create AVIO context, supplying our own read/seek functions.
    iAvioCtx = avio_alloc_context(avcodecBuf,
                                  kInBufBytes,
                                  0,
                                  (void *)&iClassData,
                                  avCodecRead,
                                  NULL,
                                  avCodecSeek);

    if (iAvioCtx == NULL)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Cannot allocate AV IO "
                   "Context\n");
        goto failure;
    }

    // Allocate an AC Format context.
    iAvFormatCtx = avformat_alloc_context();

    if (iAvFormatCtx == NULL)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Cannot allocate AV Format "
                   "Context\n");
        goto failure;
    }

    // Add our AVIO context to the AC Format context
    iAvFormatCtx->pb      = iAvioCtx;
    iAvFormatCtx->iformat = iFormat;
    iAvFormatCtx->flags   = AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&iAvFormatCtx, "", 0, 0) != 0)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Could not open AV "
                   "input stream");
        goto failure;
    }

    if (avformat_find_stream_info(iAvFormatCtx, NULL) < 0)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Could not find AV "
                   "stream info");
        goto failure;
    }

#ifdef DEBUG
    av_dump_format(iAvFormatCtx, 0, "", false);
#endif // DEBUG

    // Identify the audio stream.
    for (TInt i=0; i<(TInt)iAvFormatCtx->nb_streams; i++)
    {
        if (iAvFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            iStreamId = i;

            switch (iAvFormatCtx->streams[i]->codec->codec_id)
            {
                case AV_CODEC_ID_AAC:
                    iStreamFormat = kFmtAac;
                    break;
                case AV_CODEC_ID_MP3:
                    iStreamFormat = kFmtMp3;
                    break;
                default:
                    Log::Print("[CodecLibAV] StreamInitialise - AUDIO FORMAT: "
                               "UNKNOWN\n");
                    break;
            }

            break;
        }
    }

    if (iStreamId == -1)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Could not find Audio "
                   "Stream");
        goto failure;
    }

    // Identify and open the correct codec for the audio stream.
    AVCodec *codec;

    iAvCodecContext = iAvFormatCtx->streams[iStreamId]->codec;
    codec           = avcodec_find_decoder(iAvCodecContext->codec_id);

    if (codec == NULL)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Cannot find codec!");
        goto failure;
    }

    if (avcodec_open2(iAvCodecContext,codec,NULL) < 0)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Codec cannot be opened");
        goto failure;
    }

    switch (iAvCodecContext->sample_fmt)
    {
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_U8P:
            iBitDepth = 8;
            break;
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S16P:
            iBitDepth = 16;
            break;
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S32P:
            iBitDepth = 24;
            break;
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_FLT:
            iBitDepth = 24;
            break;
        case AV_SAMPLE_FMT_DBL:
            Log::Print("[CodecLibAV] StreamInitialise - Format "
                       "'AV_SAMPLE_FMT_DBL' unsupported\n");
            goto failure;
        case AV_SAMPLE_FMT_DBLP:
            Log::Print("[CodecLibAV] StreamInitialise - Format "
                       "'AV_SAMPLE_FMT_DBLP' unsupported\n");
            goto failure;
        default:
            Log::Print("[CodecLibAV] StreamInitialise - Unknown Sample "
                       "Format\n");
            goto failure;
    }

    if (iAvFormatCtx->duration != AV_NOPTS_VALUE)
    {
        TInt64 duration;

        duration = iAvFormatCtx->duration + kDurationRoundUp;

        if (iAvFormatCtx->start_time != AV_NOPTS_VALUE)
        {
            duration -= iAvFormatCtx->start_time;
        }

        iTotalSamples = duration * iAvCodecContext->sample_rate / AV_TIME_BASE;
        iTrackLengthJiffies = duration * Jiffies::kPerSecond / AV_TIME_BASE;
    }
    else
    {
        // Handle the case where a stream does not have a duration.
        // eg. a radio stream.
        iTotalSamples       = 0;
        iTrackLengthJiffies = 0;
    }

    iController->OutputDecodedStream(iAvCodecContext->bit_rate,
                                     iBitDepth,
                                     iAvCodecContext->sample_rate,
                                     iAvCodecContext->channels,
                                     Brn(iStreamFormat),
                                     iTrackLengthJiffies,
                                     0,
                                     false);

    // Create a frame to hold the decoded packets.
    iAvFrame = avcodec_alloc_frame();

    if (iAvFrame == NULL)
    {
        Log::Print("[CodecLibAV] StreamInitialise - Cannot create iAvFrame\n");
        goto failure;
    }

    return;

failure:
    // Resources are tidied up in StreamCompleted
    THROW(CodecStreamCorrupt);
}

void CodecLibAV::StreamCompleted()
{
#ifdef DEBUG
    Log::Print("[CodecLibAV] StreamCompleted\n");
#endif

    if (iAvPacketCached)
    {
        iAvPacketCached = false;
        av_free_packet(&iAvPacket);
    }

    if (iAvFrame != NULL)
    {
        avcodec_free_frame(&iAvFrame);
        iAvFrame = NULL;
    }

    if (iAvCodecContext != NULL)
    {
        avcodec_close(iAvCodecContext);
        iAvCodecContext = NULL;
    }

    if (iAvFormatCtx != NULL)
    {
        avformat_close_input(&iAvFormatCtx);
        iAvFormatCtx = NULL;
    }

    if (iAvioCtx != NULL)
    {
        if (iAvioCtx->buffer != NULL)
        {
            av_free(iAvioCtx->buffer);
        }

        av_free(iAvioCtx);
        iAvioCtx = NULL;
    }
}

TBool CodecLibAV::TrySeek(TUint aStreamId, TUint64 aSample)
{
#ifdef DEBUG
    Log::Print("[CodecLibAV] TrySeek - StreamId [%d] Sample[%jd]\n",
               aStreamId, aSample);
#endif // DEBUG

    double frac        = (double)aSample / (double)iTotalSamples;
    TInt64 seekTarget  = TInt64(frac *
                                (iAvFormatCtx->duration + kDurationRoundUp));

    if (iAvFormatCtx->start_time != AV_NOPTS_VALUE)
        seekTarget += iAvFormatCtx->start_time;

    iClassData.streamId = aStreamId;

    iSeekExpected = true;
    iSeekExecuted = false;
    iSeekSuccess  = false;

    TInt ret = avformat_seek_file(iAvFormatCtx, -1, seekTarget, seekTarget,
                           seekTarget, AVSEEK_FLAG_ANY);

    if (iSeekSuccess)
    {
        avcodec_flush_buffers(iAvCodecContext);
    }

    if (ret < 0)
    {
#ifdef DEBUG
        Log::Print("[CodecLibAV] TrySeek - av_seek_frame failed\n");
#endif // DEBUG

        return false;
    }

    // It is not guaranteed the av codec 'seek' operation will have been
    // executed yet.
    //
    // It must be executed before this function returns so we attempt to force
    // the issue by executing a read.
    if (! iSeekExecuted)
    {
#ifdef DEBUG
        Log::Print("[CodecLibAV] TrySeek - Calling Read Frame\n");
#endif // DEBUG

        av_read_frame(iAvFormatCtx,&iAvPacket);

        if (iSeekSuccess)
        {
            avcodec_flush_buffers(iAvCodecContext);
        }

        if (! iSeekExecuted)
        {
#ifdef DEBUG
            Log::Print("[CodecLibAV] TrySeek - Failed To Execute Seek\n");
#endif // DEBUG

            iAvPacketCached = true;
            iSeekExpected   = false;

            // Give up and throw an error.
            THROW(CodecStreamCorrupt);
        }
    }

    if (iSeekSuccess == false)
    {
        return false;
    }

    iSeekExpected = false;

    iTrackOffset =
        (aSample * Jiffies::kPerSecond) / iAvCodecContext->sample_rate;

    iController->OutputDecodedStream(iAvCodecContext->bit_rate,
                                     iBitDepth,
                                     iAvCodecContext->sample_rate,
                                     iAvCodecContext->channels,
                                     Brn(iStreamFormat),
                                     iTrackLengthJiffies,
                                     aSample,
                                     false);

    // Ditch any PCM we have buffered.
    iOutput.SetBytes(0);

    return true;
}

// Convert native endian interleaved/planar PCM to interleaved big endian PCM
// and output.
void CodecLibAV::processPCM(TInt plane_size, TInt inSampleBytes, TInt outSampleBytes)
{
    TInt    outIndex = 0;
    TUint8 *out      = (TUint8 *)(iOutput.Ptr() + iOutput.Bytes());
    TInt    planeSamples;
    TInt    planes;

    if (plane_size == -1)
    {
        // For Interleaved PCM the frames are delivered in a single plane.
        planeSamples = iAvFrame->linesize[0]/inSampleBytes;
        planes       = 1;
    }
    else
    {
        // For Planar PCM there is a plane for each channel.
        planeSamples = plane_size/inSampleBytes;
        planes       = iAvCodecContext->channels;
    }

    TUint frameSize   = outSampleBytes * iAvCodecContext->channels;
    TUint bufferLimit =  iOutput.MaxBytes() - (iOutput.MaxBytes() % frameSize);

#ifdef BUFFER_GUARD_CHECK
    bufferLimit -=
        (frameSize > (TUint)kGuardSize) ? frameSize : (TUint)kGuardSize;
#endif // BUFFER_GUARD_CHECK

    for (TInt ps=0; ps<planeSamples; ps++)
    {
        for (TInt plane=0; plane<planes; plane++)
        {
            // Flush the output buffer when full.
            if (iOutput.Bytes() == bufferLimit)
            {
                iTrackOffset +=
                    iController->OutputAudioPcm(
                                    iOutput,
                                    iAvCodecContext->channels,
                                    iAvCodecContext->sample_rate,
                                    iBitDepth,
                                    EMediaDataEndianBig,
                                    iTrackOffset);

                iOutput.SetBytes(0);
                out        = (TUint8 *)iOutput.Ptr();
                outIndex   = 0;
            }

            // Switch on required output PCM bit depth.
            switch (iBitDepth)
            {
                case 8:
                {
                    TUint8  sample =
                        ((TUint8 *)iAvFrame->extended_data[plane])[ps];
                    TUint8 *samplePtr = (TUint8 *)&sample;

                    out[outIndex++] = *samplePtr;

                    break;
                }

                case 16:
                {
                    TUint16  sample =
                        ((TUint16 *)iAvFrame->extended_data[plane])[ps];
                    TUint8  *samplePtr = (TUint8 *)&sample;

                    if (isPlatformBigEndian())
                    {
                        // No conversion required.
                        out[outIndex++] = *(samplePtr+0);
                        out[outIndex++] = *(samplePtr+1);
                    }
                    else
                    {
                        // Convert to big endian
                        out[outIndex++] = *(samplePtr+1);
                        out[outIndex++] = *(samplePtr+0);
                    }

                    break;
                }

                case 24:
                {
                    TUint8   *samplePtr;
                    TUint32   sample;

                    // FLTP/FLT is converted to 24 bit PCM for our purposes.
                    if (iAvCodecContext->sample_fmt == AV_SAMPLE_FMT_FLTP ||
                        iAvCodecContext->sample_fmt == AV_SAMPLE_FMT_FLT)
                    {
                        float sampleFloat =
                            ((float *)iAvFrame->extended_data[plane])[ps];

                        sampleFloat *= (kInt24Max + 1);
                        if (sampleFloat > kInt24Max)
                        {
                            sampleFloat = kInt24Max;
                        }

                        if (sampleFloat < kInt24Min)
                        {
                            sampleFloat = kInt24Min;
                        }

                        sample    = (TUint32)sampleFloat;
                        samplePtr = (TUint8 *)&sample;
                    }
                    else
                    {
                        sample    = ((TUint32 *)iAvFrame->extended_data[plane])[ps];
                        samplePtr = (TUint8 *)&sample;
                    }

                    if (isPlatformBigEndian())
                    {
                        // No conversion required.
                        out[outIndex++] = *(samplePtr+0);
                        out[outIndex++] = *(samplePtr+1);
                        out[outIndex++] = *(samplePtr+2);
                    }
                    else
                    {
                        // Convert to big endian
                        out[outIndex++] = *(samplePtr+2);
                        out[outIndex++] = *(samplePtr+1);
                        out[outIndex++] = *(samplePtr+0);
                    }

                    break;
                }

                default:
                {
                    Log::Print("[CodecLibAV] processPCM - Unsupported bit "
                               "depth [%d]\n", iBitDepth);
                    break;
                }
            }

#ifdef BUFFER_GUARD_CHECK
            CheckGuardBytes(iOutput);
#endif // BUFFER_GUARD_CHECK

            iOutput.SetBytes(iOutput.Bytes() + outSampleBytes);
        }
    }
}

void CodecLibAV::Process()
{
    TInt frameFinished = 0;
    TInt plane_size;

    if (! iAvPacketCached)
    {
        if (av_read_frame(iAvFormatCtx,&iAvPacket) < 0)
        {
    #ifdef DEBUG
            Log::Print("Info: [CodecLibAV] Process - Frame read error or EOF\n");
    #endif // DEBUG

            THROW(CodecStreamEnded);
        }
    }

    iAvPacketCached = false;

    if (iAvPacket.stream_index == iStreamId)
    {
        avcodec_decode_audio4(iAvCodecContext,
                              iAvFrame,
                             &frameFinished,
                              &iAvPacket);

        if (! frameFinished)
        {
            // Couldn't decode a full frame.
            // This can happen after seek or on initial switch to radio so
            // don't throw an exception.
#ifdef DEBUG
        Log::Print("Info: [CodecLibAV] Process - Error Decoding Frame\n");
#endif // DEBUG

            av_free_packet(&iAvPacket);

            return;
        }

        TInt data_size =
            av_samples_get_buffer_size(&plane_size,
                                        iAvCodecContext->channels,
                                        iAvFrame->nb_samples,
                                        iAvCodecContext->sample_fmt,
                                        1);

        if (data_size <= 0)
        {
            Log::Print("ERROR:  Cannot obtain frame plane size\n");

            av_free_packet(&iAvPacket);
            THROW(CodecStreamCorrupt);
        }

        TInt    inSampleBytes  = iBitDepth/8;
        TInt    outSampleBytes = inSampleBytes;

        switch (iAvCodecContext->sample_fmt)
        {
            // Planar formats.
            case AV_SAMPLE_FMT_FLTP:
            {
                // Convert FLTP to S24
                inSampleBytes  = sizeof(float);
                outSampleBytes = 3;

                processPCM(plane_size, inSampleBytes, outSampleBytes);
                break;
            }
            case AV_SAMPLE_FMT_S32P:
            {
                // Convert S32P to S24
                outSampleBytes = 3;

                processPCM(plane_size, inSampleBytes, outSampleBytes);
                break;
            }
            case AV_SAMPLE_FMT_S16P:
            {
                processPCM(plane_size, inSampleBytes, outSampleBytes);
                break;
            }
            case AV_SAMPLE_FMT_U8P:
            {
                processPCM(plane_size, inSampleBytes, outSampleBytes);
                break;
            }

            // Interleaved formats
            case AV_SAMPLE_FMT_FLT:
            {
                // Convert FLT to S24
                inSampleBytes  = sizeof(float);
                outSampleBytes = 3;

                processPCM(-1, inSampleBytes, outSampleBytes);
                break;
            }
            case AV_SAMPLE_FMT_S32:
            {
                // Convert S32 to S24
                outSampleBytes = 3;

                processPCM(-1, inSampleBytes, outSampleBytes);
                break;
            }
            case AV_SAMPLE_FMT_S16:
            {
                processPCM(-1, inSampleBytes, outSampleBytes);
                break;
            }
            case AV_SAMPLE_FMT_U8:
            {
                processPCM(-1, inSampleBytes, outSampleBytes);
                break;
            }
            default:
            {
                Log::Print("ERROR: Format Not Supported Yet\n");
                break;
            }
        }
    }

    av_free_packet(&iAvPacket);

    if (iStreamStart || iStreamEnded)
    {
        if (iOutput.Bytes() > 0)
        {
            // Flush PCM buffer.
            iTrackOffset +=
                iController->OutputAudioPcm(
                                iOutput,
                                iAvCodecContext->channels,
                                iAvCodecContext->sample_rate,
                                iBitDepth,
                                EMediaDataEndianBig,
                                iTrackOffset);

            iOutput.SetBytes(0);
        }

        if (iStreamStart)
        {
            Log::Print("[CodecLibAV] Process - Throw CodecStreamStart\n");
            THROW(CodecStreamStart);
        }

        Log::Print("[CodecLibAV] Process - Throw CodecStreamEnded\n");
        THROW(CodecStreamEnded);
    }
}
#endif // USE_LIBAVCODEC
