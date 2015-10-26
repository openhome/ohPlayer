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
   TInt64           *filePos;
   TBool            *readBufferFull;
} OpaqueType;

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
    static const TUint   kInBufBytes = 4096;
    static const TInt32 kInt24Max    = 8388607L;
    static const TInt32 kInt24Min    = -8388608L;

    static int     avCodecRead(void* ptr, TUint8* buf, TInt buf_size);
    static TInt64  avCodecSeek(void* ptr, TInt64 offset, TInt whence);
    static TBool   isPlatformBigEndian(void);

    void processPCM(TInt plane_size, TInt inSampleBytes, TInt outSampleBytes);

    TUint64                      iTotalSamples;
    TUint64                      iTrackLengthJiffies;
    TUint64                      iTrackOffset;
    Bws<DecodedAudio::kMaxBytes> iOutput;
    Bws<32*1024>                 iRecogBuf;

    AVInputFormat   *iFormat;
    AVIOContext     *iAvioCtx;
    AVFormatContext *iAvFormatCtx;
    AVCodecContext  *iAvCodecContext;
    AVPacket         iAvPacket;
    AVFrame         *iAvFrame;
    TInt             iStreamId;
    TUint            iBitDepth;
    TInt64           iBytePos;
    TBool            iReadBufferFull;
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
    , iAvFrame(NULL)
    , iStreamId(-1)
    , iBitDepth(0)
    , iBytePos(-1)
    , iReadBufferFull(false)
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

#ifdef DEUG
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
    TBool            *readBufferFull  = classData->readBufferFull;
    Bwn               inputBuffer(buf, buf_size);

    inputBuffer.SetBytes(0);

    // Read the requested amount of data.
    controller->Read(inputBuffer, buf_size);

    *readBufferFull = ((TUint)buf_size == inputBuffer.Bytes());

    return inputBuffer.Bytes();
}

// AVCodec callback to seek to a position in the input stream.
//
// We don't actually seek here, but use this callback as a mechanism to
// determine the location the stream of a given frame (specified as a
// time offset).
TInt64 CodecLibAV::avCodecSeek(void* ptr, TInt64 offset, TInt whence)
{
    OpaqueType       *classData  = (OpaqueType *)ptr;
    ICodecController *controller = classData->controller;;
    TInt64           *bytePos    = classData->filePos;

    *bytePos = -1;

    // Ignore the force bit.
    whence = whence & ~AVSEEK_FORCE;

    switch (whence)
    {
        case SEEK_SET:
            Log::Print("Seek [SET] ");
            *bytePos = offset;
            break;
        case SEEK_CUR:
            Log::Print("Seek [CUR] ");
            break;
        case SEEK_END:
            Log::Print("Seek [END] ");
            *bytePos = controller->StreamLength() + offset;
            break;
        case AVSEEK_SIZE:
            Log::Print("Seek [Size] %d\n", controller->StreamLength());
            return controller->StreamLength();
        default:
            Log::Print("UNSUPPORTED SEEK\n");
            return -1;
    }

    Log::Print("[%jd]\n", offset);

    if (*bytePos < 0)
    {
        return -1;
    }
    else
    {
        return 0;
    }
}

TBool CodecLibAV::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.RawPcm())
    {
        return false;
    }

    // Initialise and fill the recognise buffer.
    iRecogBuf.SetBytes(0);
    iRecogBuf.FillZ();

    iController->Read(iRecogBuf, iRecogBuf.MaxBytes() - AVPROBE_PADDING_SIZE);

    // Attempt to detect the stream format.
    AVProbeData probeData;

    probeData.filename  = "";
    probeData.buf       = (unsigned char *)iRecogBuf.Ptr();
    probeData.buf_size  = iRecogBuf.Bytes();

    iFormat = av_probe_input_format(&probeData, 1);

    if (iFormat == NULL)
    {
        Log::Print("ERROR: Recognise() probe failed\n");
        return false;
    }

    return true;
}

void CodecLibAV::StreamInitialise()
{
    // Initialise PCM buffer.
    iOutput.SetBytes(0);

    // Initialise the track offset in jiffies.
    iTrackOffset = 0;

    // Initialise the codec data buffer.
    //
    // NB. This may be free'd/realloced out with our control.
    unsigned char *avcodecBuf = (unsigned char *)av_malloc(kInBufBytes);

    if (avcodecBuf == NULL)
    {
        Log::Print("ERROR: Cannot allocate AV buffer\n");
        goto failure;
    }

    // Data to be passed to the AVCodec callbacks.
    iClassData.controller     = iController;
    iClassData.filePos        = &iBytePos;
    iClassData.readBufferFull = &iReadBufferFull;

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
        Log::Print("ERROR: Cannot allocate AV IO Context\n");
        goto failure;
    }

    // Allocate an AC Format context.
    iAvFormatCtx = avformat_alloc_context();

    if (iAvFormatCtx == NULL)
    {
        Log::Print("ERROR: Cannot allocate AV Format Context\n");
        goto failure;
    }

    // Add our AVIO context to the AC Format context
    iAvFormatCtx->pb      = iAvioCtx;
    iAvFormatCtx->iformat = iFormat;
    iAvFormatCtx->flags   = AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&iAvFormatCtx, "", 0, 0) != 0)
    {
        Log::Print("ERROR: Could not open AV input stream");
        goto failure;
    }

    if (avformat_find_stream_info(iAvFormatCtx, NULL) < 0)
    {
        Log::Print("ERROR: Could not find AV stream info");
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

            break;
        }
    }

    if (iStreamId == -1)
    {
        Log::Print("ERROR: Could not find Audio Stream");
        goto failure;
    }

    // Identify and open the correct codec for the audio stream.
    AVCodec *codec;

    iAvCodecContext = iAvFormatCtx->streams[iStreamId]->codec;
    codec           = avcodec_find_decoder(iAvCodecContext->codec_id);

    if (codec == NULL)
    {
        Log::Print("ERROR: Cannot find codec!");
        goto failure;
    }

    if (avcodec_open2(iAvCodecContext,codec,NULL) < 0)
    {
        Log::Print("ERROR: Codec cannot be opened");
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
            Log::Print("ERROR: Format 'AV_SAMPLE_FMT_DBL' unsupported\n");
            goto failure;
        case AV_SAMPLE_FMT_DBLP:
            Log::Print("ERROR: Format 'AV_SAMPLE_FMT_DBLP' unsupported\n");
            goto failure;
        default:
            Log::Print("ERROR: Unknown Sample Format\n");
            goto failure;
    }

    if (iAvFormatCtx->duration != AV_NOPTS_VALUE)
    {
        TInt64 duration;

        duration = iAvFormatCtx->duration + 5000;

        if (iAvFormatCtx->start_time != AV_NOPTS_VALUE)
        {
            duration -= iAvFormatCtx->start_time;
        }

        iTotalSamples       = duration * iAvCodecContext->sample_rate / AV_TIME_BASE;
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
                                     Brn(iFormat->name),
                                     iTrackLengthJiffies,
                                     0,
                                     false);

    // Create a frame to hold the decoded packets.
    iAvFrame = avcodec_alloc_frame();

    if (iAvFrame == NULL)
    {
        Log::Print("ERROR: Cannot create iAvFrame\n");
        goto failure;
    }

    return;

failure:
    // Resources are tidied up in StreamCompleted
    THROW(CodecStreamCorrupt);
}

void CodecLibAV::StreamCompleted()
{
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
    Log::Print("CodecLibAV::TrySeek Sample[%jd]\n", aSample);

    double  frac       = (double)aSample / (double)iTotalSamples;
    TInt64 seekTarget  = TInt64(frac * (iAvFormatCtx->duration + 5000));

    Log::Print("CodecLibAV::TrySeek Target Timestamp [%jd]\n", seekTarget);
    Log::Print("CodecLibAV::TrySeek Fraction Of Duration [%G]\n", frac);

    if (iAvFormatCtx->start_time != AV_NOPTS_VALUE)
        seekTarget += iAvFormatCtx->start_time;

    if (av_seek_frame(iAvFormatCtx, -1, seekTarget, AVSEEK_FLAG_ANY) < 0)
    {
        return false;
    }

    // keep seek within file bounds
    if (iBytePos >= (TInt64)iController->StreamLength())
    {
        iBytePos = iController->StreamLength() - 1;
    }

    TBool canSeek = iController->TrySeekTo(aStreamId, iBytePos);
    if (canSeek)
    {
        iTrackOffset =
            (aSample * Jiffies::kPerSecond) / iAvCodecContext->sample_rate;

        iController->OutputDecodedStream(iAvCodecContext->bit_rate,
                                         iBitDepth,
                                         iAvCodecContext->sample_rate,
                                         iAvCodecContext->channels,
                                         Brn(iFormat->name),
                                         iTrackLengthJiffies,
                                         aSample,
                                         false);
    }
    else
    {
        Log::Print("CodecLibAV::TrySeek: Failed To Seek To [%jd]\n", iBytePos);
    }

    return canSeek;
}

// Convert native endian interleaved/planar PCM to interleaved big endian PCM and
// output.
void CodecLibAV::processPCM(TInt plane_size, TInt inSampleBytes, TInt outSampleBytes)
{
    TInt    outIndex = 0;
    TUint8 *out      = (TUint8 *)(iOutput.Ptr() + iOutput.Bytes());
    TInt   planeSamples;
    TInt   planes;

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

    for (TInt ps=0; ps<planeSamples; ps++)
    {
        for (TInt plane=0; plane<planes; plane++)
        {
            // Flush the output buffer when full.
            if (iOutput.Bytes() >= iOutput.MaxBytes() -
                    (outSampleBytes * iAvCodecContext->channels))
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
                    Log::Print("ERROR: Unsupported bit depth [%d]\n");
                    break;
                }
            }

            iOutput.SetBytes(iOutput.Bytes() + outSampleBytes);
        }
    }
}

void CodecLibAV::Process()
{
    TInt frameFinished = 0;
    TInt plane_size;

    if (av_read_frame(iAvFormatCtx,&iAvPacket) < 0)
    {
#ifdef DEBUG
        Log::Print("Info: Frame read error or EOF\n");
#endif // DEBUG
        THROW(CodecStreamCorrupt);
    }

    if (iAvPacket.stream_index == iStreamId)
    {
        avcodec_decode_audio4(iAvCodecContext,
                              iAvFrame,
                             &frameFinished,
                              &iAvPacket);

        if (! frameFinished)
        {
#ifdef DEBUG
            Log::Print("Info: Error decoding frame\n");
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

    if (! iReadBufferFull)
    {
#ifdef DEBUG
        Log::Print("Info: EOF ?\n");
#endif // DEBUG

        // Flush PCM buffer.
        iTrackOffset +=
            iController->OutputAudioPcm(
                            iOutput,
                            iAvCodecContext->channels,
                            iAvCodecContext->sample_rate,
                            iBitDepth,
                            EMediaDataEndianBig,
                            iTrackOffset);

        THROW(CodecStreamEnded);
    }
}
#endif // USE_LIBAVCODEC
