#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OsWrapper.h>

#include <string>
#include <limits>

#include "OsxAudio.h"

using namespace OpenHome;
using namespace OpenHome::Media;

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
        Log::Print("generateTone() wrote %u bytes to buffer of size %u", buffer->mAudioDataByteSize, buffer->mAudioDataBytesCapacity );
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
            Log::Print("AudioQueueEnqueueBuffer() error %d", err);
        }
    }
    else
    {
        /* we're not playing so ensure we stop the hst audio queue */
        err = AudioQueueStop (queue, false);
        if (err != noErr) Log::Print("AudioQueueStop() error: %d", err);
    }
}


static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;
    
    OsxAudio *hostAudio = (OsxAudio *)inUserData;
    
    // fill the audi buffer with a 200Hz test tone
    processOutputBuffer(inBuffer, inAudioQueue);
}

#else /* TEST_BUFFER */

static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;
    
    OsxAudio *hostAudio = (OsxAudio *)inUserData;

    // fill the host buffer with data from the pipeline
    hostAudio->fillBuffer(inBuffer);

    // the buffer is full so enqueue it with the Audio subsystem
    AudioQueueEnqueueBuffer(inAudioQueue, inBuffer, 0, NULL);
}
#endif /* TEST_BUFFER */

OsxAudio::OsxAudio() : Thread("OsxAudio", kPriorityHigher )
    , iStreamInitialised("TINI", 0)
    , iStreamCompleted("TCOM", 0)
    , iHostLock("HLCK")
{
    iQuit = false;
    
    // start our main thread which will wait initially for the
    // iStreamInitialised semaphore to be signalled when we
    // start processing a decoded stream
    Start();
}

OsxAudio::~OsxAudio()
{
    // indicate to the main loop that we're done
    quit();
    
    // we may be waiting on these signals and they're never
    // going to happen, so signal them and let the thread exit
    iStreamCompleted.Signal();
    iStreamInitialised.Signal();

    // we're done here, so finalise our host audio resources
    finalise();
}

void OsxAudio::fillBuffer(AudioQueueBufferRef inBuffer)
{
    AutoMutex _(iHostLock);
    
    // set the PcmHandler to use inBuffer as the target for the next read requests
    iPcmHandler->setBuffer(inBuffer);

    // copy from the pipeline cache buffer
    while(inBuffer->mAudioDataByteSize < inBuffer->mAudioDataBytesCapacity)
        iPcmHandler->fillBuffer(inBuffer);
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
    
    // signal to the main loop that we have initialised the AudioQueue
    iStreamInitialised.Signal();

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
    AudioQueueDispose(iAudioQueue, true);
    iAudioQueue = NULL;
    // indicate to the main loop that we're finished with this stream
    iStreamCompleted.Signal();
}

void OsxAudio::initAudioBuffers()
{
    // We allocate a number of buffers for the host AudioQueue.
    // Allocate the number of buffers required, and figure out a size for them
    // based on buffering 0.25 seconds of audio per buffer
    for (int t = 0; t < kNumDataBuffers; ++t)
    {
        AudioQueueAllocateBuffer(iAudioQueue,
                                 iAudioFormat.mBytesPerFrame * (iAudioFormat.mSampleRate) / 4,  // 0.25 second
                                 &iAudioQueueBuffers[t]);
    }
}

void OsxAudio::primeAudioBuffers()
{
    // normally the audio buffers will be filled based on callbacks from a host
    // audio thread managing the AudioQueue.
    // When we initialise our AudioQueue we attempt to provide a clean start by priming
    // the host audio buffers with data from the pipeline prior to calling AudioQueueStart()
    // We iterate through the buffer list filling and enqueueing each one in turn
    for (int index = 0; index < kNumDataBuffers; ++index)
        PlayCallback(this, iAudioQueue, iAudioQueueBuffers[index]);
}

void OsxAudio::startQueue()
{
    // we want to strat the audio queue so prime the buffers for a smooth start
    primeAudioBuffers();
    
    // start the host AudioQueue. this will play audio from the buffers then start callbacks
    // to PlayCallback to refill the buffers.
    AudioQueueStart(iAudioQueue, NULL);

    if(iPcmHandler)
        iPcmHandler->setOutputActive(true);
    
}

void OsxAudio::pauseQueue()
{
        AudioQueuePause(iAudioQueue);
}

void OsxAudio::resumeQueue()
{
        AudioQueueStart(iAudioQueue, NULL);
}

void OsxAudio::flushQueue()
{
    // flush host AudioQueue buffers immediately
    // NOTE: this also resets DSP state so resuming audio playback could
    // potentially result in a minor audio glitch
    AudioQueueReset(iAudioQueue);
}

void OsxAudio::stopQueue()
{
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

void OsxAudio::quit()
{
    // we are done here, so set the iQuit flag and signal the main loop
    // that we wish to exit
    iQuit = true;
    iStreamCompleted.Signal();
    iStreamInitialised.Signal();
}

void OsxAudio::Run()
{
    try {
        // run until we're terminated explicitly or by thread shutdown
        while(!iQuit)
        {
            // wait for a start signal
            iStreamInitialised.Wait();
            iStreamInitialised.Clear();
            
            if(!iQuit)
            {
                // start the host audio queue
                startQueue();
                
                // wait until our stream is terminated
                // this can happen due to a halt or when we switch audio formats
                iStreamCompleted.Wait();
                iStreamCompleted.Clear();
            }
        }
    }
    catch (ThreadKill &e) {}
}


