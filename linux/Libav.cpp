#ifdef USE_LIBAVCODEC

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

// Uncomment to enable out of bounds checking in OpenHome buffers.
//#define BUFFER_GUARD_CHECK

#ifdef BUFFER_GUARD_CHECK
#include <assert.h>
#endif // BUFFER_GUARD_CHECK

// Uncomment to enable timestamping of log messages
//#define TIMESTAMP_LOGGING

#ifdef TIMESTAMP_LOGGING
#include <chrono>

#define DBUG_F(...)                                                            \
    Log::Print("[%jd] ",                                                       \
        std::chrono::high_resolution_clock::now().time_since_epoch().count()); \
    Log::Print(__VA_ARGS__)
#else
#define DBUG_F(...) Log::Print(__VA_ARGS__)
#endif

extern "C"
{
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavformat/avformat.h"
#include <libswresample/swresample.h>
}

#include "OptionalFeatures.h"

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
   TUint64          *byteTotal;
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
    TBool InitAVIOContext();
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void  StreamInitialise();
    void  Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void  StreamCompleted();
private:
    static const TUint   kInBufBytes      = 4096;
    static const TInt32  kInt24Max        = 8388607L;
    static const TInt32  kInt24Min        = -8388608L;
    static const TInt    kDurationRoundUp = 50000;


    static int     avCodecRead(void* ptr, TUint8* buf, TInt buf_size);
    static TInt64  avCodecSeek(void* ptr, TInt64 offset, TInt whence);
    static TBool   isPlatformBigEndian(void);
    static TBool   isFormatPlanar(AVSampleFormat fmt);

    void processPCM(TUint8 **pcmData, AVSampleFormat fmt, TInt plane_size);

    TUint64                      iTotalSamples;
    TUint64                      iTrackLengthJiffies;
    TUint64                      iTrackOffset;
    Bws<DecodedAudio::kMaxBytes> iOutput;

    AVInputFormat          *iFormat;
    AVIOContext            *iAvioCtx;
    AVFormatContext        *iAvFormatCtx;
    AVCodecContext         *iAvCodecContext;
    TBool                   iAvPacketCached;
    AVPacket                iAvPacket;
    AVFrame                *iAvFrame;
    SwrContext       *iSwrResampleCtx;
    TInt             iStreamId;
    const TChar     *iStreamFormat;
    TUint            iOutputBitDepth;
    AVSampleFormat   iConvertedFormat;
    TBool            iStreamStart;
    TBool            iStreamEnded;
    TBool            iSeekExpected;
    TBool            iSeekExecuted;
    TBool            iSeekSuccess;
    TUint64          iByteTotal;
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
    , iTotalSamples(0)
    , iTrackLengthJiffies(0)
    , iTrackOffset(0)
    , iFormat(NULL)
    , iAvioCtx(NULL)
    , iAvFormatCtx(NULL)
    , iAvCodecContext(NULL)
    , iAvPacketCached(false)
    , iAvFrame(NULL)
    , iSwrResampleCtx(NULL)
    , iStreamId(-1)
    , iStreamFormat(NULL)
    , iOutputBitDepth(0)
    , iConvertedFormat(AV_SAMPLE_FMT_NONE)
    , iStreamStart(false)
    , iStreamEnded(false)
    , iSeekExpected(false)
    , iSeekExecuted(false)
    , iSeekSuccess(false)
    , iByteTotal(0)
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
    
    // av_register_all() got deprecated in lavf 58.9.100
    // It is now useless
    // https://github.com/FFmpeg/FFmpeg/blob/70d25268c21cbee5f08304da95be1f647c630c15/doc/APIchanges#L86
    //
    #if ( LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58,9,100) ) 
    av_register_all();
    #endif
    // Initialise our encoded packet container.
    av_init_packet(&iAvPacket);
}

CodecLibAV::~CodecLibAV()
{
}

#ifdef DEBUG
void printBuf(TChar *buf, TInt bufLen)
{
    DBUG_F("Buffer Contents: %d\n", bufLen);

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

            DBUG_F("%02x ", buf[(i*20) + j]);
        }

        DBUG_F("\n");
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

// Is the supplied format planar.
TBool CodecLibAV::isFormatPlanar(AVSampleFormat fmt)
{
    return ((fmt == AV_SAMPLE_FMT_U8P)  || (fmt == AV_SAMPLE_FMT_S16P) ||
            (fmt == AV_SAMPLE_FMT_S32P) || (fmt == AV_SAMPLE_FMT_FLTP) ||
            (fmt == AV_SAMPLE_FMT_DBLP));
}

// AVCodec callback to read stream data into avcodec buffer.
TInt CodecLibAV::avCodecRead(void* ptr, TUint8* buf, TInt buf_size)
{
    OpaqueType       *classData       = (OpaqueType *)ptr;
    ICodecController *controller      = classData->controller;;
    TBool            *streamStart     = classData->streamStart;
    TBool            *streamEnded     = classData->streamEnded;
    TUint64          *byteTotal       = classData->byteTotal;

    TUint             bytesLeft       = (TUint)buf_size;
    const TUint       bufferLimit     = 32 * 1024; // Use 32K chunks
    Bws<bufferLimit>  tmpBuffer;
    Bwn               inputBuffer(buf, buf_size);

    inputBuffer.SetBytes(0);

    // Read the required amount of data in chunks.
    while (bytesLeft > 0)
    {
        TUint leftToRead = (bytesLeft < bufferLimit) ? bytesLeft : bufferLimit;

        // Reset the chunk buffer.
        tmpBuffer.SetBytes(0);

        try
        {
            // Read a chunk of data.
            controller->Read(tmpBuffer, leftToRead);

            // Append the chunk to the output buffer.
            if (! inputBuffer.TryAppend(tmpBuffer))
            {
                DBUG_F("Info: [CodecLibAV]: avCodecRead - TryAppend Failed\n ");
                break;
            }

            bytesLeft -= tmpBuffer.Bytes();
        }
        catch(CodecStreamStart&)
        {
#ifdef DEBUG
            DBUG_F("Info: [CodecLibAV]: avCodecRead - CodecStreamStart "
                   "Exception Caught\n");
#endif // DEBUG
            *streamStart = true;
            break;
        }
        catch(CodecStreamEnded&)
        {
#ifdef DEBUG
            DBUG_F("Info: [CodecLibAV] avCodecRead - CodecStreamEnded "
                   "Exception Caught\n");
#endif // DEBUG
            *streamEnded = true;
            break;
        }
        catch(CodecStreamStopped&)
        {
#ifdef DEBUG
            DBUG_F("Info: [CodecLibAV] avCodecRead - CodecStreamStopped "
                   "Exception Caught\n");
#endif // DEBUG
            *streamEnded = true;
            break;
        }
        catch(CodecRecognitionOutOfData&)
        {
#ifdef DEBUG
            DBUG_F("Info: [CodecLibAV] avCodecRead - CodecRecognitionOutOfData "
                   "Exception Caught\n");
#endif // DEBUG
            break;
        }
    }

    *byteTotal += inputBuffer.Bytes();

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
    TBool            *seekSuccess  = classData->seekSuccess;
    TUint64          *byteTotal    = classData->byteTotal;

    // Ignore the force bit.
    whence = whence & ~AVSEEK_FORCE;

    switch (whence)
    {
        case SEEK_SET:
        {
#ifdef DEBUG
            DBUG_F("Info: [CodecLibAV] avCodecSeek Seek [SET] [%jd]\n",
                   offset);
#endif // DEBUG

            // Avoid attempting to seek when not initiated from TrySeek()
            if (! *seekExpected)
            {
#ifdef DEBUG
                DBUG_F("Info: [CodecLibAV] avCodecSeek Seek [SET] Ignored\n");
#endif // DEBUG
                return -1;
            }

            *seekExpected = false;
            *seekExecuted = true;
            *seekSuccess  = false;

            if (controller->TrySeekTo(streamId, offset))
            {
#ifdef DEBUG
                DBUG_F("Info: [CodecLibAV] avCodecSeek Seek [SET] Succeeded\n");
#endif

                *byteTotal   = offset;
                *seekSuccess = true;
                return offset;
            }
            else
            {
#ifdef DEBUG
                DBUG_F("Info: [CodecLibAV] avCodecSeek Seek [SET] Failed\n");
#endif // DEBUG

                return -1;
            }
        }
        case SEEK_CUR:
        {
            DBUG_F("[CodecLibAV] avCodecSeek Unsupported Seek "
                   "[CUR] [%jd]\n", offset);
            return -1;
        }
        case SEEK_END:
        {
            DBUG_F("[CodecLibAV] avCodecSeek Unsupported Seek "
                   "[END] [%jd]\n", offset);
            return -1;
        }
        case AVSEEK_SIZE:
        {
#ifdef DEBUG
            DBUG_F("[CodecLibAV] avCodecSeek Seek [SIZE] [%jd]\n",
                   controller->StreamLength());
#endif // DEBUG

            return controller->StreamLength();
        }
        default:
        {
            DBUG_F("[CodecLibAV] avCodecSeek Unsupported Seek\n");
            return -1;
        }
    }
}

TBool CodecLibAV::InitAVIOContext()
{
    // Initialise Stream State
    iStreamStart  = false;
    iStreamEnded  = false;

    iSeekExpected  = false;
    iSeekExecuted  = false;
    iSeekSuccess   = false;

    iByteTotal     = 0;

    // Initialise the codec data buffer.
    //
    // NB. This may be free'd/realloced out with our control.
    unsigned char *avcodecBuf = (unsigned char *)av_malloc(kInBufBytes);

    if (avcodecBuf == NULL)
    {
        DBUG_F("[CodecLibAV] InitAVIOContext - Cannot allocate AV buffer\n");
        return false;
    }

    // Data to be passed to the AVCodec callbacks.
    iClassData.controller     = iController;
    iClassData.streamStart    = &iStreamStart;
    iClassData.streamEnded    = &iStreamEnded;
    iClassData.streamId       = 0;
    iClassData.seekExpected   = &iSeekExpected;
    iClassData.seekExecuted   = &iSeekExecuted;
    iClassData.seekSuccess    = &iSeekSuccess;
    iClassData.byteTotal      = &iByteTotal;

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
        DBUG_F("[CodecLibAV] InitAVIOContext - Cannot allocate AV IO Context\n");

        av_free(avcodecBuf);
        return false;
    }

    return true;
}

TBool CodecLibAV::Recognise(const EncodedStreamInfo& aStreamInfo)
{
#ifdef DEBUG
    DBUG_F("[CodecLibAV] Recognise\n");
#endif

    if (aStreamInfo.StreamFormat()==EncodedStreamInfo::Format::Pcm)
    {
        return false;
    }

    if (!InitAVIOContext())
    {
        return false;
    }

    // Read as much data as required from the pipeline to ascertain the
    // format of the stream.
    av_probe_input_buffer(iAvioCtx,   // AVIOContext
                          &iFormat,   // AVInputFormat
                          "",         // Filename
                          NULL,       // Logctx
                          0,          // Offset
                          0);         // Default max probe data

    if (iFormat == NULL)
    {
        DBUG_F("[CodecLibAV] Recognise Probe Failed.\n");

        // Free up the iAvioCtx created for the recognition process.
        if (iAvioCtx != NULL)
        {
            if (iAvioCtx->buffer != NULL)
            {
                av_free(iAvioCtx->buffer);
            }

            av_free(iAvioCtx);
            iAvioCtx = NULL;
        }

        return false;
    }

    return true;
}

void CodecLibAV::StreamInitialise()
{
#ifdef DEBUG
    DBUG_F("[CodecLibAV] StreamInitialise\n");
#endif

    // Initialise the track offset in jiffies.
    iTrackOffset = 0;

    iAvPacketCached  = false;
    iConvertedFormat = AV_SAMPLE_FMT_NONE;

    // The stream position is 'rewound' after Recognise() succeeds.
    // Libav does not expect/handle this, thus we must read and discard data
    // until we get back to the expected position.
    TUint bytesLeft   = (TUint)iByteTotal;
    TUint bufferLimit = iOutput.MaxBytes();

#ifdef BUFFER_GUARD_CHECK
    bufferLimit -= (TUint)kGuardSize;
#endif // BUFFER_GUARD_CHECK

    // Initialise the output buffer for usage as a temp buffer.
    iOutput.SetBytes(0);

#ifdef BUFFER_GUARD_CHECK
    SetGuardBytes(iOutput);
#endif // BUFFER_GUARD_CHECK

    while (bytesLeft > 0)
    {
        TUint leftToRead = (bytesLeft < bufferLimit) ? bytesLeft : bufferLimit;

        try
        {
            iController->Read(iOutput, leftToRead);

            bytesLeft -= iOutput.Bytes();

#ifdef BUFFER_GUARD_CHECK
            CheckGuardBytes(iOutput);
#endif // BUFFER_GUARD_CHECK

            iOutput.SetBytes(0);
        }
        catch (...)
        {
            DBUG_F("[CodecLibAV] StreamInitialise - Unexpected exception "
                   "while advancing to offset [%llu]\n", iByteTotal);

            break;
        }
    }

    // Initialise the output buffer to hold decoded PCM.
    iOutput.SetBytes(0);

    // Allocate an AC Format context.
    iAvFormatCtx = avformat_alloc_context();

    if (iAvFormatCtx == NULL)
    {
        DBUG_F("[CodecLibAV] StreamInitialise - Cannot allocate AV Format "
               "Context\n");
        goto failure;
    }

    // Add our AVIO context to the AC Format context
    iAvFormatCtx->pb      = iAvioCtx;
    iAvFormatCtx->iformat = iFormat;
    iAvFormatCtx->flags   = AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&iAvFormatCtx, "", 0, 0) != 0)
    {
        DBUG_F("[CodecLibAV] StreamInitialise - Could not open AV input "
               "stream\n");
        goto failure;
    }

    if (avformat_find_stream_info(iAvFormatCtx, NULL) < 0)
    {
        DBUG_F("[CodecLibAV] StreamInitialise - Could not find AV stream "
               "info\n");
        goto failure;
    }

#ifdef DEBUG
    av_dump_format(iAvFormatCtx, 0, "", false);
#endif // DEBUG

    // Identify the audio stream.
    iStreamId = av_find_best_stream(iAvFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (iStreamId == -1)
    {
        DBUG_F("[CodecLibAV] StreamInitialise - Could not find Audio Stream\n");
        goto failure;
    }

    // Disable all other streams
    for (TInt i=0; i<(TInt)iAvFormatCtx->nb_streams; i++)
    {
        if(i!=iStreamId) iAvFormatCtx->streams[i]->discard = AVDISCARD_ALL;
    }

    // Identify and open the correct codec for the audio stream.
    AVCodec *codec; 
    AVCodecParameters *origin_par;
    
    origin_par = iAvFormatCtx->streams[iStreamId]->codecpar;

    codec = avcodec_find_decoder(origin_par->codec_id);
    if (codec == NULL)
    {
        DBUG_F("[CodecLibAV] StreamInitialise - Cannot find codec!\n");
        goto failure;
    }

    iAvCodecContext = avcodec_alloc_context3(codec);
    if (!iAvCodecContext) {
        DBUG_F("[CodecLibAV] StreamInitialise - Can't allocate decoder context\n");
        goto failure;
    }

    if (avcodec_parameters_to_context(iAvCodecContext, origin_par) < 0) {
        DBUG_F("[CodecLibAV] StreamInitialise - Can't copy decoder context\n");
        goto failure;
    }

   if (avcodec_open2(iAvCodecContext,codec,NULL) < 0)
   {
        DBUG_F("[CodecLibAV] StreamInitialise - Codec cannot be opened\n");
        goto failure;
    }

    switch (iAvCodecContext->sample_fmt)
    {
        case AV_SAMPLE_FMT_U8:
        case AV_SAMPLE_FMT_U8P:
            iOutputBitDepth = 8;
            break;
        case AV_SAMPLE_FMT_S16:
        case AV_SAMPLE_FMT_S16P:
            iOutputBitDepth = 16;
            break;
        case AV_SAMPLE_FMT_S32:
        case AV_SAMPLE_FMT_S32P:
            iOutputBitDepth = 24;
            break;
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_FLT:
            // For best playback quality use 'libavresample' to convert this
            // format to a PCM format we can handle.

            iSwrResampleCtx = swr_alloc();

            if (iSwrResampleCtx != NULL)
            {
                TInt64 channelLayout = AV_CH_LAYOUT_STEREO;

                if (iAvCodecContext->channels == 1)
                {
                    channelLayout = AV_CH_LAYOUT_MONO;
                }

                av_opt_set_int(iSwrResampleCtx, "in_channel_layout",
                               channelLayout, 0);
                av_opt_set_int(iSwrResampleCtx, "out_channel_layout",
                               channelLayout, 0);
                av_opt_set_int(iSwrResampleCtx, "in_sample_rate",
                               iAvCodecContext->sample_rate, 0);
                av_opt_set_int(iSwrResampleCtx, "out_sample_rate",
                               iAvCodecContext->sample_rate, 0);
                av_opt_set_int(iSwrResampleCtx, "in_sample_fmt",
                               iAvCodecContext->sample_fmt, 0);

                // Convert to S32P (this will be sampled down manually to 24
                // bit for output)
                iOutputBitDepth  = 24;
                iConvertedFormat = AV_SAMPLE_FMT_S32P;

                av_opt_set_int(iSwrResampleCtx, "out_sample_fmt",
                               iConvertedFormat, 0);

                if (swr_init(iSwrResampleCtx) < 0)
                {
                    DBUG_F("[CodecLibAV] StreamInitialise - Cannot "
                           "Open Resampler\n");

                    goto failure;
                }
            }
            else
            {
                DBUG_F("[CodecLibAV] StreamInitialise - Cannot "
                       "Create Resampler Context\n");

                goto failure;
            }

            break;
        case AV_SAMPLE_FMT_DBL:
            DBUG_F("[CodecLibAV] StreamInitialise - Format "
                   "'AV_SAMPLE_FMT_DBL' unsupported\n");
            goto failure;
        case AV_SAMPLE_FMT_DBLP:
            DBUG_F("[CodecLibAV] StreamInitialise - Format "
                   "'AV_SAMPLE_FMT_DBLP' unsupported\n");
            goto failure;
        default:
            DBUG_F("[CodecLibAV] StreamInitialise - Unknown Sample Format\n");
            goto failure;
    }

    if (iAvFormatCtx->duration != (TInt64)AV_NOPTS_VALUE)
    {
        TInt64 duration;

        duration = iAvFormatCtx->duration + kDurationRoundUp;

        if (iAvFormatCtx->start_time != (TInt64)AV_NOPTS_VALUE)
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
                                     iOutputBitDepth,
                                     iAvCodecContext->sample_rate,
                                     iAvCodecContext->channels,
                                     Brn(codec->name),
                                     iTrackLengthJiffies,
                                     0,
                                     false,
				                     DeriveProfile(iAvCodecContext->channels));

    // Create a frame to hold the decoded packets.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 45, 101)
    iAvFrame = av_frame_alloc();
#else // LIBAVCODEC_VERSION_INT
    iAvFrame = avcodec_alloc_frame();
#endif // LIBAVCODEC_VERSION_INT

    if (iAvFrame == NULL)
    {
        DBUG_F("[CodecLibAV] StreamInitialise - Cannot create iAvFrame\n");
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
    DBUG_F("[CodecLibAV] StreamCompleted\n");
#endif

    iFormat = NULL;

    if (iSwrResampleCtx != NULL)
    {
        swr_free(&iSwrResampleCtx);
        iSwrResampleCtx = NULL;
    }

    if (iAvPacketCached)
    {
        iAvPacketCached = false;
        av_packet_unref(&iAvPacket);
    }

    if (iAvFrame != NULL)
    {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 45, 101)
        av_frame_free(&iAvFrame);
#else // LIBAVCODEC_VERSION_INT
        avcodec_free_frame(&iAvFrame);
#endif // LIBAVCODEC_VERSION_INT

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
    DBUG_F("[CodecLibAV] TrySeek - StreamId [%d] Sample[%jd]\n",
           aStreamId, aSample);
#endif // DEBUG

    double frac        = (double)aSample / (double)iTotalSamples;
    TInt64 seekTarget  = TInt64(frac *
                                (iAvFormatCtx->duration + kDurationRoundUp));

    if (iAvFormatCtx->start_time != (TInt64)AV_NOPTS_VALUE)
        seekTarget += iAvFormatCtx->start_time;

#ifdef DEBUG
    DBUG_F("[CodecLibAV] TrySeek - SeekTarget [%jd]\n", seekTarget);
#endif // DEBUG

    iClassData.streamId = aStreamId;

    iSeekExpected = true;
    iSeekExecuted = false;
    iSeekSuccess  = false;

    TUint64 currentPos = iByteTotal;
    TInt ret = av_seek_frame(iAvFormatCtx, -1, seekTarget, AVSEEK_FLAG_ANY);

    if ((ret < 0) && iSeekExecuted)
    {
         // It looks like the seek operation involved a number of stream
         // reads followed by a seek, which failed.
         //
         // In this event we pretend the seek operation succeeded.
         // Playback will proceed from the current position.
        if (currentPos != iByteTotal)
        {
#ifdef DEBUG
            DBUG_F("[CodecLibAV] TrySeek - Read/Failed Seek Case\n");
#endif

            iSeekSuccess = true;
        }
    }

    if (iSeekSuccess)
    {
        avcodec_flush_buffers(iAvCodecContext);
    }

    // It is not guaranteed the av codec 'seek' operation will have been
    // executed yet.
    //
    // We attempt to force the issue by executing a read.
    if (! iSeekExecuted)
    {
        if (av_read_frame(iAvFormatCtx,&iAvPacket) >= 0)
        {
            iAvPacketCached = true;
        }
        else
        {
            DBUG_F("[CodecLibAV] av_read_frame problem\n");
        }
        

        if (iSeekSuccess)
        {
            avcodec_flush_buffers(iAvCodecContext);
        }

        if (! iSeekExecuted)
        {
            // In the event that a we are unable to force a seek we assume
            // the seek operation was achieved via a sequence of stream
            // reads.
#ifdef DEBUG
            DBUG_F("[CodecLibAV] TrySeek - Acheved Via Stream Reads\n");
#endif // DEBUG

            iSeekExpected = false;
            iSeekSuccess  = true;
        }
    }

    if (iSeekSuccess == false)
    {
        return false;
    }

    iTrackOffset =
        (aSample * Jiffies::kPerSecond) / iAvCodecContext->sample_rate;

    iController->OutputDecodedStream(iAvCodecContext->bit_rate,
                                     iOutputBitDepth,
                                     iAvCodecContext->sample_rate,
                                     iAvCodecContext->channels,
                                     Brn(iStreamFormat),
                                     iTrackLengthJiffies,
                                     aSample,
                                     false,
				                     DeriveProfile(iAvCodecContext->channels)
				     );

    // Ditch any PCM we have buffered.
    iOutput.SetBytes(0);

    return true;
}

// Convert native endian interleaved/planar PCM to interleaved big endian PCM
// and output.
void CodecLibAV::processPCM(TUint8 **pcmData, AVSampleFormat fmt,
                            TInt plane_size)
{
    TInt    outIndex       = 0;
    TUint8 *out            = (TUint8 *)(iOutput.Ptr() + iOutput.Bytes());
    TInt    outSampleBytes = iOutputBitDepth/8;
    TInt    planeSamples;
    TInt    planes;

    planeSamples = plane_size/av_get_bytes_per_sample(fmt);

    if (isFormatPlanar(fmt))
    {
        // For Planar PCM there is a plane for each channel.
        planes = iAvCodecContext->channels;
    }
    else
    {
        // For Interleaved PCM the frames are delivered in a single plane.
        planes = 1;
    }

    TUint frameSize   = outSampleBytes * iAvCodecContext->channels;
    TUint bufferLimit = iOutput.MaxBytes() - (iOutput.MaxBytes() % frameSize);

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
                                    iOutputBitDepth,
                                    AudioDataEndian::Big,
                                    iTrackOffset);

                iOutput.SetBytes(0);
                out        = (TUint8 *)iOutput.Ptr();
                outIndex   = 0;
            }

            // Switch on required output PCM bit depth.
            switch (iOutputBitDepth)
            {
                case 8:
                {
                    TUint8  sample   = ((TUint8 *)pcmData[plane])[ps];
                    TUint8 *samplePtr = (TUint8 *)&sample;

                    out[outIndex++] = *samplePtr;

                    break;
                }

                case 16:
                {
                    TUint16  sample    = ((TUint16 *)pcmData[plane])[ps];
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
                    TUint32  sample    = ((TUint32 *)pcmData[plane])[ps];
                    TUint8  *samplePtr = (TUint8 *)&sample;

                    if (isPlatformBigEndian())
                    {
                        // No conversion required.
                        out[outIndex++] = *(samplePtr+1);
                        out[outIndex++] = *(samplePtr+2);
                        out[outIndex++] = *(samplePtr+3);
                    }
                    else
                    {
                        // Convert to big endian
                        out[outIndex++] = *(samplePtr+3);
                        out[outIndex++] = *(samplePtr+2);
                        out[outIndex++] = *(samplePtr+1);
                    }

                    break;
                }

                default:
                {
                    DBUG_F("[CodecLibAV] processPCM - Unsupported bit "
                           "depth [%d]\n", iOutputBitDepth);
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
    TInt plane_size;
    TInt ret ;

    if (! iAvPacketCached)
    {
        if (av_read_frame(iAvFormatCtx,&iAvPacket) < 0)
        {
#ifdef DEBUG
            DBUG_F("Info: [CodecLibAV] Process - Frame read error or EOF\n");
#endif // DEBUG

            THROW(CodecStreamEnded);
        }
    }
    if (iAvPacket.stream_index != iStreamId)
    {
        DBUG_F("[CodecLibAV] Process - ERROR: Skip Packet with Stream %d\n",iAvPacket.stream_index);
	    return;
    }

    iAvPacketCached = false;

    ret = avcodec_send_packet(iAvCodecContext, &iAvPacket);
    if(ret < 0)
    {
#ifdef DEBUG
        DBUG_F("Info: [CodecLibAV] Process - Error Decoding Frame\n");
#endif // DEBUG

        av_packet_unref(&iAvPacket);

        return;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(iAvCodecContext, iAvFrame);
        if (ret < 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) 
        {
            av_packet_unref(&iAvPacket);
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
            DBUG_F("ERROR:  Cannot obtain frame plane size\n");

            av_packet_unref(&iAvPacket);
            THROW(CodecStreamCorrupt);
        }

        switch (iAvCodecContext->sample_fmt)
        {
            case AV_SAMPLE_FMT_FLTP:
            case AV_SAMPLE_FMT_FLT:
            {
                // Use 'libavresample' to convert FLT[P] to a more
                // usable format.
                //
                // The transform is setup in StreamInitialise()
                TInt    outSamples;
                TInt    outLinesize;
                TUint8 *convertedData;
                TInt    ret;

                // The number of samples expected in the converted buffer.
                //
                // This should equal the number in the input buffer as the
                // sample rate is not being modified.
                outSamples =
                    av_rescale_rnd(swr_get_delay(iSwrResampleCtx, iAvCodecContext->sample_rate) +
                                iAvFrame->nb_samples,
                                iAvCodecContext->sample_rate,
                                iAvCodecContext->sample_rate,
                                AV_ROUND_UP);

                // Allocate a buffer for the conversion output.
                ret = av_samples_alloc((TUint8 **)&convertedData,
                                    &outLinesize,
                                    iAvCodecContext->channels,
                                    outSamples,
                                    iConvertedFormat,
                                    0);

                if (ret < 0)
                {
                    DBUG_F("[CodecLibAV] Process - ERROR: Cannot "
                        "Allocate Sample Conversion Buffer\n");
                    THROW(CodecStreamEnded);
                }
                swr_convert(iSwrResampleCtx,
                                (TUint8 **)&convertedData,
                                outSamples,
                                (const uint8_t**)iAvFrame->extended_data,
                                iAvFrame->nb_samples);

                processPCM((TUint8 **)&convertedData, iConvertedFormat,
                        outLinesize);

                av_freep(&convertedData);

                break;
            }
            case AV_SAMPLE_FMT_S32P:
                // Fallthrough
            case AV_SAMPLE_FMT_S16P:
                // Fallthrough
            case AV_SAMPLE_FMT_U8P:
            {
                processPCM(iAvFrame->extended_data, iAvCodecContext->sample_fmt,
                        plane_size);
                break;
            }

            case AV_SAMPLE_FMT_S32:
                // Fallthrough
            case AV_SAMPLE_FMT_S16:
                // Fallthrough
            case AV_SAMPLE_FMT_U8:
            {
                processPCM(iAvFrame->extended_data, iAvCodecContext->sample_fmt,
                        iAvFrame->linesize[0]);
                break;
            }
            default:
            {
                DBUG_F("[CodecLibAV] Process - ERROR: Format Not "
                    "Supported Yet\n");
                break;
            }
        }
    }

    av_packet_unref(&iAvPacket);

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
                                iOutputBitDepth,
                                AudioDataEndian::Big,
                                iTrackOffset);

            iOutput.SetBytes(0);
        }

        if (iStreamStart)
        {
            DBUG_F("[CodecLibAV] Process - Throw CodecStreamStart\n");
            THROW(CodecStreamStart);
        }

        DBUG_F("[CodecLibAV] Process - Throw CodecStreamEnded\n");
        THROW(CodecStreamEnded);
    }
}
#endif // USE_LIBAVCODEC
