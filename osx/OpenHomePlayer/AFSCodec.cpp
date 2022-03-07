// Native MP3/AAC codec implemented using an AudioFileStrean and
// AudioQueueOfflineRender.

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <AudioToolbox/AudioToolbox.h>

#include "OptionalFeatures.h"

// Uncomment to enable time stamping of log messages
//#define TIMESTAMP_LOGGING

#ifdef TIMESTAMP_LOGGING
#include <chrono>

#define DBUG_F(...)                                                            \
    Log::Print("[%llu] [CodecAFS] ",                                           \
        std::chrono::high_resolution_clock::now().time_since_epoch().count()); \
    Log::Print(__VA_ARGS__)
#else
#define DBUG_F(...) Log::Print("[CodecAFS]" __VA_ARGS__)
#endif

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecAFS : public CodecBase
{
public:
    CodecAFS(IMimeTypeList& aMimeTypeList);
private: // from CodecBase
    ~CodecAFS();
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void  StreamInitialise();
    void  Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void  StreamCompleted();
private: // AudioFileStream/AudioQueue callback functions

    // Called when a property is found in the data stream.
    static void AFSPropertyListenerProc(
                              void                      *inClientData,
                              AudioFileStreamID          inAudioFileStream,
                              AudioFileStreamPropertyID  inPropertyID,
                              TUint32                   *ioFlags);

    // Called when audio data is found in the data stream.
    static void AFSPacketsProc(
                          void                          *inClientData,
                          TUint32                        inNumberBytes,
                          TUint32                        inNumberPackets,
                          const void                    *inInputData,
                          AudioStreamPacketDescription  *inPacketDescriptions);

    // Called when the AudioQueue has finished with a buffer.
    static void AudioQueueOutputCallback(
                                    void                *inClientData,
                                    AudioQueueRef        inAQ,
                                    AudioQueueBufferRef  inBuffer);

    // Called when the AudioQeuue starts/stops.
    static void AudioQueueIsRunningCallback(
                                   void                 *inUserData,
                                   AudioQueueRef         inAQ,
                                   AudioQueuePropertyID  inID);
private: // Utility functions
    // Create the AudioQueue
    TBool    CreateAudioQueue(AudioFileStreamID inAudioFileStream);
    // Start the AudioQueue in Offline Render mode, if not already running.
    OSStatus StartAudioQueueIfNeeded();
    // Add stream audio data to the AudioQueue.
    OSStatus EnqueueAudioData();
    // Decode the audio stream data.
    TBool    DecodeAudioData();
    // Get the index of the supplied AudioQueue buffer in the buffer array.
    TInt     GetBufferIndex(AudioQueueBufferRef inBuffer);
    // Wait for a free AudioQueue buffer to become available.
    void     WaitForFreeBuffer();
    // Initialise/Reset the object data.
    void     InitialiseData();
    // Obtain the audio stream format details.
    void     GetInputFormat(AudioFileStreamID inAudioFileStream);
    // Calculate the buffer size required to hold 'inSeconds' of audio data.
    void     CalculateBufferSize(TUint32  inMaxPacketSize,
                                 Float64  inSeconds,
                                 TUint32 *outBufferSize,
                                 TUint32 *outNumPackets);
private:
    // Audio File Stream
    AudioFileStreamID iAudioFileStream;         // Audio File Stream parser

    AudioStreamBasicDescription   iAsbd;        // Audio Stream Basic Descriptor
    AudioStreamPacketDescription *iPacketDescs; // Audio Stream Pkt Descriptors

    // Audio Queue
    static const TUint           kNumAQBufs = 1;// Num of audio queue buffers

    AudioQueueRef                iAudioQueue;
    AudioQueueBufferRef          iAudioQueueBuffer[kNumAQBufs];
    TBool                        iAudioQueueStarted;
    TUint32                      iNumPacketsToRead;// Packets per AQ buffer
    AudioTimeStamp               iTimeStamp;       // Audio queue timestamp

    // Audio Queue Offline Render
    AudioStreamBasicDescription  iPcmFormat;
    AudioQueueBufferRef          iPcmBuffer;

    TUint iFillBufferIndex;   // Index of current audioQueueBuffer
    TUint iBytesFilled;       // Bytes in current audioQueueBuffer
    TUint iPacketsFilled;     // Packets in current audioQueueBuffer

    TBool     iErrorStatus;       // Decode error status.
    TBool     iRecogComplete;     // Recognition phase complete
    TBool     iProcessing;        // Decoding in progress
    TBool     iInUse[kNumAQBufs]; // Buffer array inuse status
    Semaphore iSemInUse;          // Semaphore to protect the inuse flags
private: // Stream format data
    const TChar *kFmtMp3;
    const TChar *kFmtAac;
    const TChar *iStreamFormat;

    TUint64 iTotalSamples;
    TUint64 iTrackLengthJiffies;
    TUint64 iTrackOffset;
    TInt64  iDataOffset;
    TUint64 iPktCnt;
    TBool   iDiscontinuous;

    TUint32 iChannels;
    Float64 iSampleRate;
    TUint32 iBitDepth;
    TUint32 iBitRate;
    Float64 iDuration;
    
    SpeakerProfile* iSpeakerProfile;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewMp3(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecAFS(aMimeTypeList);
}

// CodecAFS

CodecAFS::CodecAFS(IMimeTypeList& aMimeTypeList)
    : CodecBase("MMF")
    , iSemInUse("INUSE", 0)
    , kFmtMp3("Mp3")
    , kFmtAac("Aac")
    , iBitDepth(24)      // Everything is decoded to 24 bit PCM
{
    // Initialise the object data.
    InitialiseData();
    
    iSpeakerProfile = new SpeakerProfile();

#ifdef ENABLE_MP3
    aMimeTypeList.Add("audio/mpeg");
    aMimeTypeList.Add("audio/x-mpeg");
    aMimeTypeList.Add("audio/mp1");
#endif // ENABLE_MP3

#ifdef ENABLE_AAC
    aMimeTypeList.Add("audio/aac");
    aMimeTypeList.Add("audio/aacp");
#endif // ENABLE_AAC
}

CodecAFS::~CodecAFS()
{
    delete iSpeakerProfile;
}

TBool CodecAFS::DecodeAudioData()
{
    OSStatus err;

    // Add audio stream data to the AudioQueue.
    err = EnqueueAudioData();

    if (err != noErr)
    {
        return false;
    }

    // Calculate the number of PCM frames the buffer can store.
    TUint32 reqFrames = iPcmBuffer->mAudioDataBytesCapacity /
                        iPcmFormat.mBytesPerFrame;

    // Obtain the decoded audio data via an AudioQueue Offline Render, then
    // pass it on to the next pipeline element.
    while (true)
    {
        err = AudioQueueOfflineRender(iAudioQueue,
                                     &iTimeStamp,
                                      iPcmBuffer,
                                      reqFrames);

        if (err != noErr)
        {
            DBUG_F("ERROR: DecodeAudioData: Offline render failed [%u]\n", err);
            return false;
        }

        // Exit loop when no more PCM is available.
        if (iPcmBuffer->mAudioDataByteSize == 0)
        {
            break;
        }

        // Split the available PCM into chunks and pass to the next
        // pipeline element.
        TUint bytesLeft = (TUint)iPcmBuffer->mAudioDataByteSize;
        TUint offset    = 0;

        while (bytesLeft > 0)
        {
            TUint leftToRead =
                (bytesLeft < DecodedAudio::kMaxBytes) ? bytesLeft :
                                                        DecodedAudio::kMaxBytes;

            Bwn outputBuffer = Bwn((TByte *)(iPcmBuffer->mAudioData) + offset,
                                   leftToRead);

            outputBuffer.SetBytes(leftToRead);

            try
            {
                iTrackOffset +=
                    iController->OutputAudioPcm(outputBuffer,
                                                iChannels,
                                                static_cast<TUint>(iSampleRate),
                                                iBitDepth,
                                                AudioDataEndian::Big,
                                                iTrackOffset);

            }
            catch (AssertionFailed&)
            {
                DBUG_F("ERROR: DecodeAudioData: OutputAudioPcm Assert "
                       "Exception\n");

                return false;
            }

            iTimeStamp.mSampleTime += leftToRead / iPcmFormat.mBytesPerFrame;
            bytesLeft              -= leftToRead;
            offset                 += leftToRead;
        }
    }

    return true;
}

// Initialise/Reset the object data.
void CodecAFS::InitialiseData()
{
    iAudioFileStream   = NULL;
    iAsbd              = {0};
    iPacketDescs       = NULL;

    iAudioQueue        = NULL;
    iAudioQueueStarted = false;
    iNumPacketsToRead  = 0;
    iTimeStamp         = {0};

    iPcmFormat = {0};
    iPcmBuffer = NULL;

    iFillBufferIndex = 0;
    iBytesFilled     = 0;
    iPacketsFilled   = 0;

    iErrorStatus     = false;
    iRecogComplete   = false;
    iProcessing      = false;

    for (TInt i=0; i<kNumAQBufs; i++)
    {
        iInUse[i] = false;
    }

    iTotalSamples       = 0;
    iTrackLengthJiffies = 0;
    iTrackOffset        = 0;
    iDataOffset         = 0;
    iPktCnt             = 0;
    iDiscontinuous      = false;

    iChannels           = 0;
    iSampleRate         = 0.0f;
    iBitRate            = 0;
    iDuration           = 0.0f;

    iStreamFormat = NULL;
}

// Obtain the audio stream format.
void CodecAFS::GetInputFormat(AudioFileStreamID inAudioFileStream)
{
    switch (iAsbd.mFormatID)
    {
        case kAudioFormatMPEG4AAC:
            DBUG_F("Info: GetInputFormat: AAC Encoded Stream\n");
            iStreamFormat = kFmtAac;
            break;
        case kAudioFormatMPEGLayer3:
            DBUG_F("Info: GetInputFormat: MP3 Encoded Stream\n");
            iStreamFormat = kFmtMp3;
            break;
        default:
            // All other formats are unsupported.
            return;
    }

    // Channels
    iChannels = iAsbd.mChannelsPerFrame;
    DBUG_F("Info: GetInputFormat: Channels [%u]\n", iChannels);

    // Sample Rate
    iSampleRate = iAsbd.mSampleRate;
    DBUG_F("Info: GetInputFormat: Sample Rate [%f]\n", iSampleRate);

    // Duration NOT available with AudioFileStream we CAN however
    // estimate this for Playlist source (using the same algorithm used in
    // AudioFile Services.
    OSStatus err;
    TUint32  propertySize = sizeof(iPktCnt);

    err = AudioFileStreamGetProperty(
                                  inAudioFileStream,
                                  kAudioFileStreamProperty_AudioDataPacketCount,
                                 &propertySize,
                                 &iPktCnt);

    if (err != noErr)
    {
        DBUG_F("Info: GetInputFormat: Failed to obtain packet cnt [%d]\n", err);
    }
    else
    {
        DBUG_F("Info: GetInputFormat: Pkt Cnt [%llu]\n", iPktCnt);
    }

    iDuration  = (iAsbd.mFramesPerPacket != 0) ?
                              (iPktCnt * iAsbd.mFramesPerPacket) /
                              iAsbd.mSampleRate : 0.0;

    DBUG_F("Info: GetInputFormat: Duration [%g]\n", iDuration);

    // Bit Rate
    propertySize = sizeof(iBitRate);

    err = AudioFileStreamGetProperty(inAudioFileStream,
                                     kAudioFileStreamProperty_BitRate,
                                    &propertySize,
                                    &iBitRate );

    if (err != noErr)
    {
        // Estimate bit rate as "(audio bytes * 8) / seconds"
        if (iDuration > 0)
        {
            iBitRate = (TInt)(iController->StreamLength() * 8 / iDuration);

            DBUG_F("Info: GetInputFormat: Encoded Bitrate (Estimated) "
                   "[%d] kbs\n", iBitRate/1000);
        }
        else
        {
            DBUG_F("Info: GetInputFormat: Encoded Bitrate Unavailable\n");
        }
    }
    else
    {
        iBitRate *= 1000;

        DBUG_F("Info: GetInputFormat: Bit Rate [%u] kbs\n",
               (TUint)iBitRate/1000);
    }
}

// Calculate the buffer size required to hold a given amount (in seconds)
// of audio data from the current stream.
void CodecAFS::CalculateBufferSize(TUint32  inMaxPacketSize,
                                   Float64  inSeconds,
                                   TUint32 *outBufferSize,
                                   TUint32 *outNumPackets)
{
    static const TInt maxBufferSize = 0x10000;
    static const TInt minBufferSize = 0x4000;

    // Calculate the buffer size.
    if (iAsbd.mFramesPerPacket)
    {
        Float64 numPacketsForTime =
            iAsbd.mSampleRate / iAsbd.mFramesPerPacket * inSeconds;

        *outBufferSize = numPacketsForTime * inMaxPacketSize;
    }
    else
    {
        // If frames per packet is zero, then the codec has no predictable
        // packet == time so we can't tailor this (we don't know how many
        // packets represent a time period we'll just return a default buffer
        // size
        *outBufferSize =
            maxBufferSize > inMaxPacketSize ? maxBufferSize : inMaxPacketSize;
    }

    // Apply our limits.
    if (*outBufferSize > maxBufferSize && *outBufferSize > inMaxPacketSize)
    {
        *outBufferSize = maxBufferSize;
    }
    else
    {
        if (*outBufferSize < minBufferSize)
        {
            *outBufferSize = minBufferSize;
        }
    }

    // Calculate the number of packets that will fit in the buffer.
    *outNumPackets = *outBufferSize / inMaxPacketSize;
}

void CodecAFS::AudioQueueOutputCallback(void                *inClientData,
                                        AudioQueueRef        inAQ,
                                        AudioQueueBufferRef  inBuffer)
{
    // The AudioQueue has finished with this buffer. It is available for reuse.
    CodecAFS *myData = static_cast<CodecAFS *>(inClientData);

    TUint bufIndex = myData->GetBufferIndex(inBuffer);

    if (bufIndex == -1)
    {
        DBUG_F("ERROR: AudioQueueOutputCallback: Cannot locate "
               "buffer in the buffer array\n");
        return;
    }

    // Signal waiting thread that the buffer is free.
    myData->iInUse[bufIndex] = false;
    myData->iSemInUse.Signal();
}

// Called when AudioQueue is started/stopped
void CodecAFS::AudioQueueIsRunningCallback(
                                        void                 * /*inClientData*/,
                                        AudioQueueRef         inAQ,
                                        AudioQueuePropertyID  inID)
{
    TUint32 running;
    TUint32 size = sizeof(running);

    OSStatus err = AudioQueueGetProperty(inAQ,
                                         kAudioQueueProperty_IsRunning,
                                        &running,
                                        &size);

    if (err)
    {
        DBUG_F("ERROR: AudioQueueIsRunningCallback: Cannot read the "
               "kAudioQueueProperty_IsRunning property\n");
        return;
    }

#ifdef DEBUG
    DBUG_F("Info: AudioQueueIsRunningCallback: Running [%u]\n", running);
#endif // DEBUG
}

// Create the AudioQueue and configure the offline decode.
TBool CodecAFS::CreateAudioQueue(AudioFileStreamID inAudioFileStream)
{
    OSStatus err;

    err = AudioQueueNewOutput(&iAsbd,
                               AudioQueueOutputCallback,
                               static_cast<void *>(this),
                               NULL,
                               NULL,
                               0,
                              &iAudioQueue);

    if (err != noErr)
    {
        DBUG_F("ERROR: CreateAudioQueue AudioQueueNewOutput failed [%d]\n",
               err);
        return false;
    }

    TUint32 bufferByteSize;
    TUint32 size;

    // Calculate how many packets to read at a time and how big a buffer
    // is required.
    //
    // This is based on the size of the packets in the file and an
    // approximate duration for each buffer
    TBool isFormatVBR = (iAsbd.mBytesPerPacket  == 0 ||
                         iAsbd.mFramesPerPacket == 0);

    // The formats we support are all VBR so return an error if this is
    // not the case.
    if (! isFormatVBR)
    {
        DBUG_F("ERROR: CreateAudioQueue: CBR format unsupported\n");
        return false;
    }

    // First check to see what the max size of a packet is - if
    // it is bigger than our allocation default size, that needs
    // to become larger
    TUint32 maxPacketSize = 0;

    size = sizeof(maxPacketSize);

    // Obtain the max packet size from the AudioQueue as this information is
    // not available from the AudioFileStream.
    err = AudioQueueGetProperty(iAudioQueue,
                                kAudioConverterPropertyMaximumInputPacketSize,
                               &maxPacketSize,
                               &size);

    if (err != noErr)
    {
        DBUG_F("ERROR: CreateAudioQueue: Cannot read input packet size "
               "upper bound [%d]\n", err);

        return false;
    }

    // Calculate the  buffer size required to capture around 1 second of
    // audio based on the current format
    CalculateBufferSize(maxPacketSize,
                        1.0,           // seconds
                       &bufferByteSize,
                       &iNumPacketsToRead);

    iPacketDescs = new AudioStreamPacketDescription[iNumPacketsToRead];

    // Create an array of AudioQueue buffers of the required size.
    for (TUint i=0; i<kNumAQBufs; ++i)
    {
        // Allocate the read buffer
        err = AudioQueueAllocateBuffer(iAudioQueue,
                                       bufferByteSize,
                                      &iAudioQueueBuffer[i]);

        if (err != noErr)
        {
            DBUG_F("ERROR: CreateAudioQueue:  Cannot allocate AudioQueue  "
                   "buffer [%d]\n", err);

            return false;
        }
    }

    // Get the cookie size
    TUint32 cookieSize;
    Boolean writable;

    err = AudioFileStreamGetPropertyInfo(
                                   inAudioFileStream,
                                   kAudioFileStreamProperty_MagicCookieData,
                                  &cookieSize,
                                  &writable);

    if (err == noErr)
    {
        // Get the cookie data
        void* cookieData = calloc(1, cookieSize);

        err = AudioFileStreamGetProperty(
                                   inAudioFileStream,
                                   kAudioFileStreamProperty_MagicCookieData,
                                  &cookieSize,
                                   cookieData);

        if (err)
        {
            DBUG_F("ERROR: CreateAudioQueue: Cannot read "
                   "kAudioFileStreamProperty_MagicCookieData property\n");

            free(cookieData);
            return false;
        }

        // Set the cookie on the queue.
        err = AudioQueueSetProperty(iAudioQueue,
                                    kAudioQueueProperty_MagicCookie,
                                    cookieData,
                                    cookieSize);
        free(cookieData);

        if (err)
        {
            DBUG_F("ERROR: CreateAudioQueue: Cannot set "
                   "kAudioFileStreamProperty_MagicCookie property\n");
            return false;
        }
    }


    // Channel layout
    AudioChannelLayout *acl = NULL;

    err = AudioFileStreamGetPropertyInfo(
                                    inAudioFileStream,
                                    kAudioFileStreamProperty_ChannelLayout,
                                   &size,
                                    NULL);

    if ((err == noErr) && (size > 0))
    {
        acl = (AudioChannelLayout *)malloc(size);

        err = AudioFileStreamGetProperty(
                                      inAudioFileStream,
                                      kAudioFileStreamProperty_ChannelLayout,
                                     &size,
                                     acl);

        if (err != noErr)
        {
            DBUG_F("ERROR: CreateAudioQueue: Cannot read channel layout "
                   "[%d]\n", err);

            free(acl);
            return false;
        }

        err = AudioQueueSetProperty(iAudioQueue,
                                    kAudioQueueProperty_ChannelLayout,
                                    acl,
                                    size);

        if (err != noErr)
        {
            DBUG_F("ERROR: CreateAudioQueue: Cannot set channel layout "
                   "[%d]\n", err);

            free(acl);
            return false;
        }
    }

    // Specify the required PCM format.
    iPcmFormat.mFormatID         = kAudioFormatLinearPCM;
    iPcmFormat.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger |
                                   kLinearPCMFormatFlagIsBigEndian     |
                                   kAudioFormatFlagIsPacked;

    iPcmFormat.mSampleRate       = iSampleRate;
    iPcmFormat.mBitsPerChannel   = iBitDepth;
    iPcmFormat.mChannelsPerFrame = iChannels;
    iPcmFormat.mBytesPerFrame    = iChannels * (iPcmFormat.mBitsPerChannel / 8);

    iPcmFormat.mFramesPerPacket  = 1;
    iPcmFormat.mBytesPerPacket   = iPcmFormat.mBytesPerFrame *
                                   iPcmFormat.mFramesPerPacket;

    err = AudioQueueSetOfflineRenderFormat(iAudioQueue, &iPcmFormat, acl);

    // Free channel layout buffer.
    free(acl);

    if (err != noErr)
    {
        DBUG_F("ERROR: CreateAudioQueue: Cannot set offline render format "
               "[%d]\n", err);
    }

    const TUint32 pcmBufferByteSize = bufferByteSize / 2;

    err = AudioQueueAllocateBuffer(iAudioQueue, pcmBufferByteSize, &iPcmBuffer);

    if (err != noErr)
    {
        DBUG_F("ERROR: CreateAudioQueue Cannot allocate capture buffer "
               "[%d]\n", err);

        return false;
    }

    // Add a kAudioQueueProperty_IsRunning listener
    err = AudioQueueAddPropertyListener(iAudioQueue,
                                        kAudioQueueProperty_IsRunning,
                                        AudioQueueIsRunningCallback,
                                        static_cast<void *>(this));

    if (err != noErr)
    {
        DBUG_F("ERROR: CreateAudioQueue: Failed to set a "
               "kAudioQueueProperty_IsRunning listener\n", err);
        return false;
    }

    return true;
}

// Start the AudioQueue in Offline Render Mode, if not already running.
OSStatus CodecAFS::StartAudioQueueIfNeeded()
{
    OSStatus err = noErr;

    // Start the AudioQueue if not already running
    if (! iAudioQueueStarted)
    {
        err = AudioQueueStart(iAudioQueue, NULL);

        if (err != noErr)
        {
            DBUG_F("ERROR: StartAudioQueueIfNeeded: AudioQueueStart failed "
                   "[%d]\n", err);

            return err;
        }

        iTimeStamp.mFlags      = kAudioTimeStampSampleTimeValid;
        iTimeStamp.mSampleTime = 0.0f;

        // Enable offline render by requesting 0 frames.
        err = AudioQueueOfflineRender(iAudioQueue,
                                     &iTimeStamp,
                                      iPcmBuffer,
                                      0);

        if (err != noErr)
        {
            DBUG_F("ERROR: StartAudioQueueIfNeeded: Initial offline render "
                   "failed [%d]\n", err);
            return err;
        }

        iAudioQueueStarted = true;
    }

    return err;
}

// Enqueue the current AudioQueue buffer.
OSStatus CodecAFS::EnqueueAudioData()
{
    OSStatus err;

    // Start the AudioQueue, if not already running.
    err = StartAudioQueueIfNeeded();

    if (err != noErr)
    {
        return err;
    }

    // Mark the buffer as in use.
    iInUse[iFillBufferIndex] = true;

    // Obtain a reference to the buffer.
    AudioQueueBufferRef fillBuf = iAudioQueueBuffer[iFillBufferIndex];

    // Set the amount of data in the buffer.
    fillBuf->mAudioDataByteSize = (TUint32)iBytesFilled;

    // Enqueue the buffer on the AudioQueue.
    err = AudioQueueEnqueueBuffer(iAudioQueue,
                                  fillBuf,
                                  (TUint32)iPacketsFilled,
                                  iPacketDescs);

    if (err != noErr)
    {
        DBUG_F("ERROR: EnqueueAudioData: AudioQueueEnqueueBuffer failed "
               "[%d]\n", err);
        return err;
    }

    return err;
}

// Wait for the next buffer to become available.
void CodecAFS::WaitForFreeBuffer()
{
    // Go to the next buffer
    if (++iFillBufferIndex >= kNumAQBufs)
    {
        iFillBufferIndex = 0;
    }

    iBytesFilled   = 0;
    iPacketsFilled = 0;

    while (iInUse[iFillBufferIndex])
    {
#ifdef DEBUG
        DBUG_F("Info: WaitForFreeBuffer ... WAITING ...\n");
#endif // DEBUG
        iSemInUse.Wait();
    }
}

// Get the index of the supplied buffer in the buffer array.
TInt CodecAFS::GetBufferIndex(AudioQueueBufferRef inBuffer)
{
    for (TUint i=0; i<kNumAQBufs; ++i)
    {
        if (inBuffer == iAudioQueueBuffer[i])
        {
            return i;
        }
    }

    return -1;
}

// Called when a property is found in the data stream.
void CodecAFS::AFSPropertyListenerProc(
                                   void                      *inClientData,
                                   AudioFileStreamID          inAudioFileStream,
                                   AudioFileStreamPropertyID  inPropertyID,
                                   TUint32                   *ioFlags)
{
    CodecAFS *myData = static_cast<CodecAFS *>(inClientData);

#ifdef DEBUG
    DBUG_F("Info: AFSPropertyListenerProc Property '%c%c%c%c'\n",
           (char)(inPropertyID>>24)&255,
           (char)(inPropertyID>>16)&255,
           (char)(inPropertyID>>8)&255,
           (char)inPropertyID&255);
#endif // DEBUG

    switch (inPropertyID)
    {
        case kAudioFileStreamProperty_DataOffset:
        {
            OSStatus err;
            TUint32  dataOffsetSize = sizeof(myData->iDataOffset);

            err = AudioFileStreamGetProperty(
                                         inAudioFileStream,
                                         kAudioFileStreamProperty_DataOffset,
                                        &dataOffsetSize,
                                        &myData->iDataOffset);

            if (err)
            {
                DBUG_F("ERROR: AFSPropertyListenerProc: Cannot read the "
                       "kAudioFileStreamProperty_DataOffset property\n");

                myData->iErrorStatus = true;
            }

            break;
        }
        // The file stream parser is now ready to produce audio packets.
        // We now know as much as we can about the stream format.
        case kAudioFileStreamProperty_ReadyToProducePackets :
        {
            OSStatus err = noErr;

            // The stream format recognition phase is complete.
            myData->iRecogComplete = true;

            // Obtain the basic descriptor for the stream.
            TUint32 asbdSize = sizeof(myData->iAsbd);

            err = AudioFileStreamGetProperty(
                                         inAudioFileStream,
                                         kAudioFileStreamProperty_DataFormat,
                                        &asbdSize,
                                        &myData->iAsbd);

            if (err)
            {
                DBUG_F("ERROR: AFSPropertyListenerProc: Cannot read the "
                       "kAudioFileStreamProperty_DataFormat property\n");

                myData->iErrorStatus = true;
                break;
            }

            // Obtain the required stream format information.
            myData->GetInputFormat(inAudioFileStream);

            // Leave Audio Queue setup until we're done with the recognition
            // phase as it is not required until this codec is chosen for
            // decoding the stream.
            if (! myData->iProcessing)
            {
                return;
            }

            if (myData->iDuration > 0)
            {
                myData->iTotalSamples       =
                    myData->iDuration * static_cast<TUint>(myData->iSampleRate);
                myData->iTrackLengthJiffies =
                    myData->iDuration * Jiffies::kPerSecond;
            }
            else
            {
                // Handle the case where a stream does not have a duration.
                // eg. A radio stream.
                myData->iTotalSamples       = 0;
                myData->iTrackLengthJiffies = 0;
            }

            // Setup the decoded PCM format specifics.
            myData->iController->OutputDecodedStream(
                                       myData->iBitRate,
                                       myData->iBitDepth,
                                       static_cast<TUint>(myData->iSampleRate),
                                       myData->iChannels,
                                       Brn(myData->iStreamFormat),
                                       myData->iTrackLengthJiffies,
                                       0,
                                       false,
                                       *(myData->iSpeakerProfile));

            // Create the audio queue, setting up the required PCM format.
            if (! myData->CreateAudioQueue(inAudioFileStream))
            {
                myData->iErrorStatus = true;
            }

            break;
        }
    }
}

// Called when audio data is found in the data stream.
void CodecAFS::AFSPacketsProc(
                        void                          *inClientData,
                        TUint32                        inNumberBytes,
                        TUint32                        inNumberPackets,
                        const void                    *inInputData,
                        AudioStreamPacketDescription  *inPacketDescriptions)
{
    CodecAFS *myData = static_cast<CodecAFS *>(inClientData);

    // Ignore audio data during the recognition phase
    if (! myData->iProcessing)
    {
#ifdef DEBUG
        DBUG_F("Info: AFSPacketsProc IGNORING audio data during Recognition "
               "phase\n");
#endif // DEBUG

        return;
    }

    for (int i = 0; i < inNumberPackets; ++i)
    {
        SInt64 packetOffset = inPacketDescriptions[i].mStartOffset;
        SInt64 packetSize   = inPacketDescriptions[i].mDataByteSize;

        TUint bufSpaceRemaining =
            myData->iAudioQueueBuffer[0]->mAudioDataBytesCapacity -
            myData->iBytesFilled;

        // Check if this packet can fit in the current AudioQueue buffer.
        if (bufSpaceRemaining < packetSize)
        {
            // Not enough room. Proceed to decode the current buffer.
            if (! myData->DecodeAudioData())
            {
                goto failure;
            }

            // Obtain an empty buffer before proceeding.
            myData->WaitForFreeBuffer();
        }

        // Copy the packet data into the buffer.
        AudioQueueBufferRef fillBuf =
            myData->iAudioQueueBuffer[myData->iFillBufferIndex];

        memcpy((TChar*)fillBuf->mAudioData + myData->iBytesFilled,
               (const TChar*)inInputData + packetOffset,
               packetSize);

        // Fill out the packet descriptor
        myData->iPacketDescs[myData->iPacketsFilled] = inPacketDescriptions[i];
        myData->iPacketDescs[myData->iPacketsFilled].mStartOffset =
                                                       myData->iBytesFilled;

        // Keep track of bytes filled and packets filled
        myData->iBytesFilled   += packetSize;
        myData->iPacketsFilled += 1;

        // If that was the last free packet descriptor, proceed to decode the
        // current buffer.
        TUint packetsDescsRemaining =
                             myData->iNumPacketsToRead - myData->iPacketsFilled;

        if (packetsDescsRemaining == 0)
        {
            // Decode the current buffer.
            if (! myData->DecodeAudioData())
            {
                goto failure;
            }

            // Obtain an empty buffer before proceeding.
            myData->WaitForFreeBuffer();
        }
    }

    return;

failure:
    myData->iErrorStatus = true;
}

TBool CodecAFS::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    const TUint       bufferLimit = 4 * 1024; // Use 4K chunks
    TBool             retVal      = false;
    Bws<bufferLimit>  tmpBuffer;

    // Create an audio file stream parser
    OSStatus err = AudioFileStreamOpen(static_cast<void *>(this),
                                       AFSPropertyListenerProc,
                                       AFSPacketsProc,
                                       kAudioFileMP1Type,
                                      &iAudioFileStream);

    if (err != noErr)
    {
        DBUG_F("ERROR: Recognise: AudioFileStreamOpen Failed [%d]\n", err);
        return false;
    }

    // Supply as much data as required for stream format identification
    while (! iRecogComplete)
    {
        try
        {
            iController->Read(tmpBuffer, bufferLimit);
        }
        catch (...)
        {
            DBUG_F("ERROR: Recognise: Read Exception\n");
            throw;
        }

        // Pass the data to the file stream parser to extract the stream
        // format
        err = AudioFileStreamParseBytes(iAudioFileStream,
                                        tmpBuffer.Bytes(),
                                        tmpBuffer.Ptr(),
                                        0);

        if (err != noErr)
        {
            DBUG_F("ERROR: Recognise: AudioFileStreamParseBytes Failed [%d]\n",
                   err);
            break;
        }

        tmpBuffer.SetBytes(0);
    }

    // Tear down the audio file stream.
    err = AudioFileStreamClose(iAudioFileStream);

    if (err != noErr)
    {
        DBUG_F("ERROR: Recognise: AudioFileStreamClose failed [%d]\n", err);
    }

    if (iStreamFormat != NULL)
    {
        retVal = true;
    }

    // Reset the object data
    InitialiseData();

    return retVal;
}

void CodecAFS::StreamInitialise()
{
    const TUint       bufferLimit = 4 * 1024; // Use 4K chunks
    Bws<bufferLimit>  tmpBuffer;

    // Re-Create an audio file stream parser
    OSStatus err = AudioFileStreamOpen(static_cast<void *>(this),
                                       AFSPropertyListenerProc,
                                       AFSPacketsProc,
                                       kAudioFileMP1Type,
                                      &iAudioFileStream);

    if (err != noErr)
    {
        DBUG_F("ERROR: StreamInitialise: AudioFileStreamOpen Failed [%d]\n",
               err);
        goto failure;
    }

    // This codec has now been selected to process the stream.
    // Allow audio data to be decoded and passed on.
    iProcessing = true;

    // Redo stream recognition so that we are in the correct state
    // in the event a seek is required imeediately upon return.
    while (! iRecogComplete)
    {
        try
        {
            iController->Read(tmpBuffer, bufferLimit);
        }
        catch (...)
        {
            DBUG_F("ERROR: StreamInitialise: Read Exception\n");
            throw;
        }

        // Pass the data to the file stream parser to extract the stream
        // format
        err = AudioFileStreamParseBytes(iAudioFileStream,
                                        tmpBuffer.Bytes(),
                                        tmpBuffer.Ptr(),
                                        0);

        if (err != noErr)
        {
            DBUG_F("ERROR: StreamInitialise: AudioFileStreamParseBytes "
                   "Failed [%d]\n", err);
            goto failure;;
        }

        tmpBuffer.SetBytes(0);
    }

    return;

failure:
    // Resources are tidied up in StreamCompleted
    THROW(CodecStreamCorrupt);
}

void CodecAFS::StreamCompleted()
{
    OSStatus err;

    if (iAudioQueueStarted)
    {
        err = AudioQueueStop(iAudioQueue, true);

        if (err != noErr)
        {
            DBUG_F("ERROR: StreamCompleted:  AudioQueueStop [%d]\n", err);
        }

        err = AudioQueueDispose(iAudioQueue, true);

        if (err != noErr)
        {
            DBUG_F("ERROR: StreamCompleted: AudioQueueDispose [%d]\n", err);
        }
    }

    if (iAudioFileStream != NULL)
    {
        err = AudioFileStreamClose(iAudioFileStream);

        if (err != noErr)
        {
            DBUG_F("ERROR: StreamCompleted:  AudioFileStreamClose [%d]\n", err);
        }
    }

    delete[] iPacketDescs;

    // Reset the object data.
    InitialiseData();
}

TBool CodecAFS::TrySeek(TUint aStreamId, TUint64 aSample)
{
#ifdef DEBUG
    DBUG_F("TrySeek - StreamId [%d] Sample[%llu]\n",
           aStreamId, aSample);
#endif // DEBUG

    OSStatus                 err;
    AudioFileStreamSeekFlags ioFlags;
    TInt64                   byteOffset;
    TInt64                   pktOffset;

    if (iAudioQueueStarted)
    {
        err = AudioQueueStop(iAudioQueue, true);

        if (err != noErr)
        {
            DBUG_F("ERROR: TrySeek: AudioQueueStop failed [%d]\n", err);
            return false;
        }
    }

    // Calculate the packet to seek to.
    pktOffset = aSample /iAsbd.mFramesPerPacket;

    err = AudioFileStreamSeek(iAudioFileStream,
                              pktOffset,
                             &byteOffset,
                             &ioFlags);

    if (err != noErr)
    {
        DBUG_F("ERROR: TrySeek: AudioFileStreamSeek failed [%d]\n", err);
        return false;
    }

    iTimeStamp.mSampleTime = byteOffset / iPcmFormat.mBytesPerFrame;

    err = AudioQueueStart(iAudioQueue, NULL);

    if (err != noErr)
    {
        DBUG_F("ERROR: TrySeek: AudioQueueStart failed [%d]\n", err);
        return false;
    }

    iDiscontinuous  = true;          // Note an audio discontinuity is expected.
    byteOffset     += iDataOffset;   // Calculate the absolute stream offset.

    // Attempt the stream seek.
    if (! iController->TrySeekTo(aStreamId, byteOffset))
    {
        return false;
    }

    iTrackOffset =
        (aSample * Jiffies::kPerSecond) / iSampleRate;

    iController->OutputDecodedStream(iBitRate,
                                     iBitDepth,
                                     iSampleRate,
                                     iChannels,
                                     Brn(iStreamFormat),
                                     iTrackLengthJiffies,
                                     aSample,
                                     false,
                                     *iSpeakerProfile);

    return true;
}

void CodecAFS::Process()
{
    try
    {
        Bws<32*1024> inputBuffer;
        OSStatus     err;

        // Read the requested amount of data from the physical stream.
        iController->Read(inputBuffer, inputBuffer.MaxBytes());

        // Pass the data to the file stream parser to extract the encoded
        // audio packets.
        //
        // From there it will be decoded via an audio queue offline render.
        err = AudioFileStreamParseBytes(
                               iAudioFileStream,
                               inputBuffer.Bytes(),
                               inputBuffer.Ptr(),
                               (iDiscontinuous) ?
                                  kAudioFileStreamParseFlag_Discontinuity : 0);

        iDiscontinuous = false;

        if (err != noErr)
        {
            DBUG_F("ERROR:Process: AudioFileStreamParseBytes [%d]\n", err);
            THROW(CodecStreamCorrupt);
        }

        if (iErrorStatus)
        {
            DBUG_F("ERROR:Process: Audio Decode Error\n");
            THROW(CodecStreamCorrupt);
        }
    }
    catch(CodecStreamStart&)
    {
        DBUG_F("Info: Process: CodecStreamStart Exception Caught.\n");

        THROW(CodecStreamStart);
    }
    catch(CodecStreamEnded&)
    {
        DBUG_F("Info: Process: CodecStreamEnded Exception Caught\n");

        THROW(CodecStreamEnded);
    }
    catch(CodecStreamStopped&)
    {
        DBUG_F("Info: Process: CodecStreamStopped Exception Caught\n");

        // Decode the last of the buffered audio.
        if (iBytesFilled > 0)
        {
            OSStatus err;

            DecodeAudioData();

            err = AudioQueueFlush(iAudioQueue);

            if (err != noErr)
            {
                DBUG_F("Info: Process: AudioQueueFlush Failed [%d]\n", err);
            }
        }

        THROW(CodecStreamEnded);
    }
}
