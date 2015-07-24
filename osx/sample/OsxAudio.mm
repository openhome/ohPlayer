#import <Cocoa/Cocoa.h>

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Private/Printer.h>

#include <string>
#include <limits>

#include "OsxAudio.h"


using namespace OpenHome;
using namespace OpenHome::Media;

#undef CALIBRATE_SETUP

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
        Log::Print("generateTone() wrote %u bytes to buffer of size %u\n", buffer->mAudioDataByteSize, buffer->mAudioDataBytesCapacity );
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
            Log::Print("AudioQueueEnqueueBuffer() error %d\n", err);
        }
    }
    else
    {
        /* we're not playing so ensure we stop the hst audio queue */
        err = AudioQueueStop (queue, false);
        if (err != noErr) Log::Print("AudioQueueStop() error: %d\n", err);
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
    
    OsxAudio *hostAudio = (OsxAudio *)inUserData;

    Log::Print("PlayCallback()\n");

    // fill the host buffer with data from the pipeline
    hostAudio->fillBuffer(inBuffer);
    Log::Print("PlayCallback() filled buffer\n");

    // the buffer is full so enqueue it with the Audio subsystem
    AudioQueueEnqueueBuffer(inAudioQueue, inBuffer, 0, NULL);
    Log::Print("PlayCallback() enqueued buffer\n");

}
#endif /* TEST_BUFFER */

OsxAudio::OsxAudio() : iHostLock("HLCK")
{
}

OsxAudio::~OsxAudio()
{
    // we're done here, so finalise our host audio resources
    finalise();
}

void OsxAudio::fillBuffer(AudioQueueBufferRef inBuffer)
{
    AutoMutex _(iHostLock);
    
    Log::Print("fillBuffer() populate buffer: %p\n", (inBuffer==iAudioQueueBuffers[0]) ? 1 : 2);

    // set the PcmHandler to use inBuffer as the target for the next read requests
    iPcmHandler->setBuffer(inBuffer);

    Log::Print("fillBuffer() buffersize is: %d\n", inBuffer->mAudioDataBytesCapacity);
    // copy from the pipeline cache buffer
    while(inBuffer->mAudioDataByteSize < inBuffer->mAudioDataBytesCapacity)
    {
        iPcmHandler->fillBuffer(inBuffer);
        Log::Print("fillBuffer() datasize is: %d\n", inBuffer->mAudioDataByteSize);
    }
    Log::Print("fillBuffer() out\n");

}

void OsxAudio::initialise(OsxPcmProcessor *aPcmHandler, AudioStreamBasicDescription *aFormat)
{
    // we may be switching streams to a new stream; finalise the existing AudioQueue
    finaliseAudioQueue();
    
    // stash the PcmHandler and audio stream format which will be used to setup and
    // decode the new audio stream
    iPcmHandler = aPcmHandler;
    iAudioFormat = *aFormat;
    
    // set up a new AudioQueue object in the format specified by aFormat
    initAudioQueue();
    
    // allocate a set of buffers of an appropriate size to handle the new stream
    initAudioBuffers();
}


void OsxAudio::finalise()
{
    // iPcmHandler is owned by someone else - just null our reference
    iPcmHandler = NULL;
    
    // we're leaving now, so finalise the previaling AudioQueue
    finaliseAudioQueue();
}

void OsxAudio::initAudioQueue()
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
#endif /* TEST_BUFFER */
    
    iVolume = 0;
    
    // FIXME: hook up the volume control
    setVolume(1.0);
}

void OsxAudio::finaliseAudioQueue()
{
    // dispose of the prevailing AudioQueue, terminating immediately
    Log::Print("finaliseAudioQueue\n");
    AudioQueueDispose(iAudioQueue, true);
    iAudioQueue = NULL;
}

void OsxAudio::initAudioBuffers()
{
    // We allocate a number of buffers for the host AudioQueue.
    // Allocate the number of buffers required, and figure out a size for them
    // based on buffering 250 samples of audio per buffer
    for (int t = 0; t < kNumDataBuffers; ++t)
    {
        AudioQueueAllocateBuffer(iAudioQueue,
#ifdef TEST_BUFFER
                                 iAudioFormat.mBytesPerFrame * 1000,  // 1000 samples
#else
                                 iAudioFormat.mBytesPerFrame * 50,  // 250 samples
#endif
                                 &iAudioQueueBuffers[t]);
        iAudioQueueBuffers[t]->mAudioDataByteSize = iAudioFormat.mBytesPerFrame;
        AudioQueueEnqueueBuffer(iAudioQueue, iAudioQueueBuffers[t], 0, NULL);
    }
}

void OsxAudio::startQueue()
{
    Log::Print("osxAudio:startQueue\n");
    AudioQueueStart(iAudioQueue, NULL);
}

void OsxAudio::pauseQueue()
{
    Log::Print("osxAudio:pauseQueue\n");
    AudioQueuePause(iAudioQueue);
}

void OsxAudio::resumeQueue()
{
    Log::Print("osxAudio:resumeQueue\n");
    AudioQueueStart(iAudioQueue, NULL);
}

void OsxAudio::flushQueue()
{
    // flush host AudioQueue buffers immediately
    // NOTE: this also resets DSP state so resuming audio playback could
    // potentially result in a minor audio glitch
    Log::Print("osxAudio:resumeQueue\n");
    AudioQueueReset(iAudioQueue);
}

void OsxAudio::stopQueue()
{
    Log::Print("osxAudio:stopQueue\n");
    AudioQueueStop(iAudioQueue, false);
}

void OsxAudio::setVolume(Float32 volume)
{
    // if the volume has changed then inform our AudioQueue
    if (iVolume != volume)
    {
        iVolume = volume;
        AudioQueueSetParameter(iAudioQueue, kAudioQueueParam_Volume, iVolume);
    }
}

