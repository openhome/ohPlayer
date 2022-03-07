#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>

#include <string>
#include <limits>

#include "DriverOsx.h"

using namespace OpenHome;
using namespace OpenHome::Media;

#define DBG(_x)
//#define DBG(_x)   Log::Print(_x)

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

#undef TEST_BUFFER
#ifdef TEST_BUFFER


// Enable this code to circumvent the buffer filling and output
// a test sine wave at 200Hz every time a buffer fill request
// is received

// indicate whether we are currently playing a test tone
bool            isPlaying;

// specify the frequency of the tone (Hz)
double          outputFrequency = 200.0;

// specify the sample rate of our test tone (Hz)
double          sampleRate = 44100.0;

// the queue we use for output
AudioQueueRef   outputQueue;

// generate a buffer filled with a test tone (sine wave)
//
// Given an AudioQueue buffer deduce the number of samples which
// will fill the buffer and generate a test tone of the specified
// frequency and sample rate (200Hz 44kHz sample rate tone)
void generateTone (AudioQueueBufferRef buffer)
{
    if (outputFrequency == 0.0)
    {
        memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    }
    else
    {
        // Make the buffer length a multiple of the wavelength for the output frequency.
        int sampleCount = buffer->mAudioDataBytesCapacity / sizeof (SInt16);
        sampleCount -= 500 * sizeof (SInt16);

        double bufferLength = sampleCount;
        double wavelength = sampleRate / outputFrequency;
        double repetitions = floor (bufferLength / wavelength);
        if (repetitions > 0.0)
        {
            sampleCount = round (wavelength * repetitions);
        }

        double      x, y;
        double      sd = 1.0 / sampleRate;
        double      amp = 0.9;
        double      max16bit = SHRT_MAX;
        int i;
        SInt16 *p = (SInt16 *)buffer->mAudioData;

        // loop round for each sample generating a point on our sine wave
        for (i = 0; i < sampleCount; i++)
        {
            x = i * sd * outputFrequency;
            y = sin (x * 2.0 * M_PI);
            p[i] = y * max16bit * amp;
        }

        // set the size of the wave data as the buffer data size
        buffer->mAudioDataByteSize = sampleCount * sizeof (SInt16);
        DBG(("generateTone() wrote %u bytes to buffer of size %u\n", buffer->mAudioDataByteSize, buffer->mAudioDataBytesCapacity ));
    }
}

void processOutputBuffer( AudioQueueBufferRef buffer, AudioQueueRef queue)
{
    OSStatus err;
    isPlaying = true;
    if (isPlaying)
    {
        // we're in play mode so fill the buffer with a test tone
        generateTone(buffer);

        // enqueue the buffer with our AudioQueue
        err = AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
        if (err == 560030580)
        { // Queue is not active due to Music being started or other reasons
            isPlaying = false;
        }
        else if (err != noErr)
        {
            DBG(("AudioQueueEnqueueBuffer() error %d\n", err));
        }
    }
    else
    {
        /* we're not playing so ensure we stop the hst audio queue */
        err = AudioQueueStop (queue, false);
        if (err != noErr) DBG(("AudioQueueStop() error: %d\n", err));
    }
}


static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;

    OsxAudio *hostAudio = (OsxAudio *)inUserData;

    // fill the audio buffer with a 200Hz test tone
    processOutputBuffer(inBuffer, inAudioQueue);
}

#else /* TEST_BUFFER */

static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;

    DriverOsx *hostAudio = (DriverOsx *)inUserData;

    DBG(("PlayCallback()\n"));

    // fill the host buffer with data from the pipeline
    hostAudio->fillBuffer(inBuffer);
    DBG(("PlayCallback() filled buffer\n"));
    //DBG(("  First 50 bytes are:\n   "));
    //for(int i=0; i< 50; i++)
    //{
    //    DBG(("%.2x",((char*)inBuffer->mAudioData)[i]));
    //}

    // the buffer is full so enqueue it with the Audio subsystem
    AudioQueueEnqueueBuffer(inAudioQueue, inBuffer, 0, NULL);
    DBG(("PlayCallback() enqueued buffer\n"));

}
#endif /* TEST_BUFFER */

const TUint DriverOsx::kSupportedMsgTypes = PipelineElement::MsgType::eMode
| PipelineElement::MsgType::eDrain
| PipelineElement::MsgType::eHalt
| PipelineElement::MsgType::eDecodedStream
| PipelineElement::MsgType::ePlayable
| PipelineElement::MsgType::eQuit;

DriverOsx::DriverOsx(Environment& aEnv, IPipeline& aPipeline) :
      PipelineElement(kSupportedMsgTypes)
    , iPipeline(aPipeline)
    , iOsCtx(aEnv.OsCtx())
    , iQuit(false)
    , iHostLock("HLCK")
    , iAudioQueue(nil)
{
    iPipeline.SetAnimator(*this);
    iThread = new ThreadFunctor("PipelineAnimator", MakeFunctor(*this, &DriverOsx::AudioThread), kPrioritySystemHighest);
    iThread->Start();
}

DriverOsx::~DriverOsx()
{
    // Wait for the pipeline animator thread to finish.
    iThread->Join();

    iPcmHandler.quit();

    // we're leaving now, so finalise the previailing AudioQueue
    finaliseAudioQueue(false);

    delete iThread;
}

void DriverOsx::AudioThread()
{
    try {
        /* Loop round processing messages until we are explicitly stopped
         * Note that messages such as MsgPlayable may block waiting for available host buffers
         */
        while (!iQuit) {
            Msg *msg = nil;

            try {
                msg = iPipeline.Pull();
            }
            catch (AssertionFailed &ex)
            {
                iQuit = true;
            }

            if(!iQuit && (msg != nil))
                (void)msg->Process(*this);
        }
    }
    catch (ThreadKill&) {}

    // pull until the pipeline is emptied
    while (!iQuit) {
        Msg* msg = iPipeline.Pull();
        msg = msg->Process(*this);
        ASSERT(msg == NULL);
    }
}

void DriverOsx::ProcessAudio(MsgPlayable* aMsg)
{
    // Resume the AudioQueue, if not already running.
    resumeQueue();

    // process the PCM audio data - this may block
    iPcmHandler.enqueue(aMsg);
}

Msg* DriverOsx::ProcessMsg(MsgMode* aMsg)
{
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgDrain* aMsg)
{
    DBG(("MsgDrain\n"));

    // Terminate playback immediately, flushing any
    // active audio buffers
    flushQueue();

    aMsg->ReportDrained();
    aMsg->RemoveRef();

    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgHalt* aMsg)
{
    DBG(("MsgHalt\n"));

    pauseQueue();

    aMsg->ReportHalted();
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& stream = aMsg->StreamInfo();

    iPlaying = false;
    iVolume = 1.0;

    iAudioFormat.mFormatID         = kAudioFormatLinearPCM;
    iAudioFormat.mSampleRate       = stream.SampleRate();
    iAudioFormat.mChannelsPerFrame = stream.NumChannels();
    iAudioFormat.mBitsPerChannel   = stream.BitDepth();
    iAudioFormat.mFramesPerPacket  = 1;  // uncompressed audio
    iAudioFormat.mBytesPerFrame    = iAudioFormat.mChannelsPerFrame * iAudioFormat.mBitsPerChannel/8;
    iAudioFormat.mBytesPerPacket   = iAudioFormat.mBytesPerFrame * iAudioFormat.mFramesPerPacket;
    iAudioFormat.mFormatFlags      = kLinearPCMFormatFlagIsBigEndian | kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;

    // Notify the PCM handler that the output is changing.
    // It should flush any cached data.
    iPcmHandler.setOutputActive(false);

    // Wait fot the flush to complete before proceeding to restart the audio
    AutoMutex _(iHostLock);

    stopQueue();

    // we may be switching streams to a new stream; finalise the existing
    // AudioQueue
    finaliseAudioQueue(true);

    // Drop remaining data in our intermediate queue
    while (!iPcmHandler.isEmpty())
    {
        MsgPlayable *msg = iPcmHandler.dequeue();
        msg->RemoveRef();
    }

    // set up a new AudioQueue object in the format specified by aFormat
    initAudioQueue();

    // allocate a set of buffers of an appropriate size to handle the new stream
    initAudioBuffers();

    startQueue();

    iPlaying = true;
    iPcmHandler.setStreamFormat(stream.BitDepth()/8, stream.NumChannels());
    iPcmHandler.setOutputActive(true);

    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgPlayable* aMsg)
{
    ProcessAudio(aMsg);
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgQuit* aMsg)
{
    iQuit = true;
    aMsg->RemoveRef();
    return NULL;
}

void DriverOsx::Pause()
{
    pauseQueue();
}

void DriverOsx::Resume()
{
    resumeQueue();
}

TUint DriverOsx::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/,
                                              TUint /*aSampleRate*/,
                                              TUint /*aBitDepth*/,
                                              TUint /*aNumChannels*/) const
{
    return 0;
}

TUint DriverOsx::PipelineAnimatorBufferJiffies() const
{
    return 0;
}

TUint DriverOsx::PipelineAnimatorMaxBitDepth() const
{
    return  0;
}

TUint DriverOsx::PipelineAnimatorDsdBlockSizeWords() const
{
    return 0;
}


void DriverOsx::fillBuffer(AudioQueueBufferRef inBuffer)
{
    // Guard against any callback while we're reconfiguring the audio.
    if (! isPlaying())
    {
        return;
    }

    AutoMutex _(iHostLock);

    DBG(("fillBuffer() populate buffer: %p\n", (inBuffer==iAudioQueueBuffers[0]) ? 1 : 2));

    // set the PcmHandler to use inBuffer as the target for the next read requests
    iPcmHandler.setBuffer(inBuffer);

    DBG(("fillBuffer() buffersize is: %d\n", inBuffer->mAudioDataBytesCapacity));
    // copy from the pipeline cache buffer
    iPcmHandler.fillBuffer(inBuffer);

    DBG(("fillBuffer() datasize is: %d\n", inBuffer->mAudioDataByteSize));
    DBG(("fillBuffer() out\n"));
}


void DriverOsx::initAudioQueue()
{
#ifdef TEST_BUFFER
    // Set up stream format fields fro a 16-bit mono LPCM stream
    AudioStreamBasicDescription streamFormat;
    streamFormat.mSampleRate = sampleRate;
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    streamFormat.mBitsPerChannel = 16;
    streamFormat.mChannelsPerFrame = 1;
    streamFormat.mBytesPerPacket = 2 * streamFormat.mChannelsPerFrame;
    streamFormat.mBytesPerFrame = 2 * streamFormat.mChannelsPerFrame;
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mReserved = 0;

    // create a new AudioQueue output stream and supply our PlayCallback
    // function to fill the buffer with a 200Hz sine wave
    AudioQueueNewOutput(
                        &streamFormat,
                        PlayCallback,
                        this,
                        nil,                   // run loop
                        nil,  // run loop mode
                        0,                      // flags
                        &iAudioQueue);
#else /* TEST_BUFFER */

    // create a new AudioQueue output stream and supply our PlayCallback
    // to fill the host buffers with PCM data from the pipeline
    AudioQueueNewOutput(
                        &iAudioFormat,
                        PlayCallback,
                        this,
                        NULL,                   // run loop
                        kCFRunLoopCommonModes,  // run loop mode
                        0,                      // flags
                        &iAudioQueue);
    DBG(("initAudioQueue - created new session\n"));
    DBG(("   sample rate     : %d\n", iAudioFormat.mSampleRate));
    DBG(("   bits per channel: %d\n", iAudioFormat.mBitsPerChannel));
    DBG(("   num channels    : %d\n", iAudioFormat.mChannelsPerFrame));
#endif /* TEST_BUFFER */

    iVolume = 0;

    setVolume(1.0);
}

void DriverOsx::finaliseAudioQueue(TBool synchronous)
{
    // dispose of the prevailing AudioQueue, terminating immediately
    if(iAudioQueue != nil)
    {
        DBG(("finaliseAudioQueue\n"));
        AudioQueueDispose(iAudioQueue, synchronous);
        iAudioQueue = nil;
    }
}

void DriverOsx::initAudioBuffers()
{
    // We allocate a number of buffers for the host AudioQueue.
    // Allocate the number of buffers required, and figure out a size for them
    // based on buffering a minimal number of samples of audio per buffer
    // AudioQueue fails to play audio when the buffer size is below a certain
    // level, but does not generate any errors. This could be a hardware limit
    // but we set the buffer to 500 samples.
    for (int t = 0; t < kNumDataBuffers; ++t)
    {
        AudioQueueAllocateBuffer(iAudioQueue,
#ifdef TEST_BUFFER
                                 iAudioFormat.mBytesPerFrame * 1000,  // 1000 samples
#else
                                 iAudioFormat.mBytesPerFrame * 1000,  // 1000 samples
#endif
                                 &iAudioQueueBuffers[t]);
        iAudioQueueBuffers[t]->mAudioDataByteSize = iAudioFormat.mBytesPerFrame;
        DBG(("initAudioBuffer - allocated buffer %d\n", t+1));
        AudioQueueEnqueueBuffer(iAudioQueue, iAudioQueueBuffers[t], 0, NULL);
        DBG(("initAudioBuffer - enqueued buffer %d\n", t+1));
    }
}

void DriverOsx::startQueue()
{
    if(iAudioQueue != nil)
    {
        DBG(("osxAudio:startQueue\n"));
        AudioQueueStart(iAudioQueue, NULL);
    }
}

void DriverOsx::pauseQueue()
{
    if(iAudioQueue != nil)
    {
        DBG(("osxAudio:pauseQueue\n"));
        AudioQueuePause(iAudioQueue);
    }
}

void DriverOsx::resumeQueue()
{
    if(iAudioQueue != nil)
    {
        DBG(("osxAudio:resumeQueue\n"));
        AudioQueueStart(iAudioQueue, NULL);
    }
}

void DriverOsx::flushQueue()
{
    if(iAudioQueue != nil)
    {
        // flush host AudioQueue buffers immediately
        // NOTE: this also resets DSP state so resuming audio playback could
        // potentially result in a minor audio glitch
        DBG(("osxAudio:flushQueue\n"));
        AudioQueueFlush(iAudioQueue);
    }
}

void DriverOsx::stopQueue()
{
    if(iAudioQueue != nil)
    {
        DBG(("osxAudio:stopQueue\n"));
        AudioQueueStop(iAudioQueue, false);
    }
}

void DriverOsx::setVolume(Float32 volume)
{
    // if the volume has changed then inform our AudioQueue
    if (iVolume != volume)
    {
        iVolume = volume;
        AudioQueueSetParameter(iAudioQueue, kAudioQueueParam_Volume, iVolume);
    }
}

