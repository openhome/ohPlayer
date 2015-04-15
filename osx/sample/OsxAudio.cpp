#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Env.h>

#include <string>
#include <limits>

#include "OsxAudio.h"

using namespace OpenHome;
using namespace OpenHome::Media;

#undef TEST_BUFFER
#ifdef TEST_BUFFER

/* Enable this code to circumvent the buffer filling and output
 * a test sine wave at 200Hz every time a buffer fill request
 * is received
 */
bool isPlaying;

double outputFrequency = 200.0;
double sampleRate = 44100.0;
AudioQueueRef outputQueue;

void generateTone (AudioQueueBufferRef buffer)
{
    if (outputFrequency == 0.0) {
        memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    } else {
        // Make the buffer length a multiple of the wavelength for the output frequency.
        int sampleCount = buffer->mAudioDataBytesCapacity / sizeof (SInt16);
        sampleCount -= 500 * sizeof (SInt16);
        double bufferLength = sampleCount;
        double wavelength = sampleRate / outputFrequency;
        double repetitions = floor (bufferLength / wavelength);
        if (repetitions > 0.0) {
            sampleCount = round (wavelength * repetitions);
        }
        
        double      x, y;
        double      sd = 1.0 / sampleRate;
        double      amp = 0.9;
        double      max16bit = SHRT_MAX;
        int i;
        SInt16 *p = (SInt16 *)buffer->mAudioData;
        
        for (i = 0; i < sampleCount; i++) {
            x = i * sd * outputFrequency;
            y = sin (x * 2.0 * M_PI);
            p[i] = y * max16bit * amp;
        }
        
        buffer->mAudioDataByteSize = sampleCount * sizeof (SInt16);
        Log::Print("generateTone() wrote %u bytes to buffer of size %u", buffer->mAudioDataByteSize, buffer->mAudioDataBytesCapacity );
    }
}

void processOutputBuffer( AudioQueueBufferRef buffer, AudioQueueRef queue)
{
    OSStatus err;
    isPlaying = true;
    if (isPlaying) {
        generateTone(buffer);
        
        err = AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
        if (err == 560030580) { // Queue is not active due to Music being started or other reasons
            isPlaying = false;
        } else if (err != noErr) {
            Log::Print("AudioQueueEnqueueBuffer() error %d", err);
        }
    } else {
        err = AudioQueueStop (queue, false);
        if (err != noErr) Log::Print("AudioQueueStop() error: %d", err);
    }
}


static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;
    
    Log::Print("OsxAudio::PlayCallback in\n");
    OsxAudio *hostAudio = (OsxAudio *)inUserData;
    
    processOutputBuffer(inBuffer, inAudioQueue);
}
#else /* TEST_BUFFER */

static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;
    
    Log::Print("OsxAudio::PlayCallback in\n");
    OsxAudio *hostAudio = (OsxAudio *)inUserData;

    hostAudio->fillBuffer(inBuffer);
    AudioQueueEnqueueBuffer(inAudioQueue, inBuffer, 0, NULL);
}
#endif /* TEST_BUFFER */

OsxAudio::OsxAudio() : Thread("OsxAudio", kPriorityHigher )
    , iInitialised("TINI", 0)
    , iHostLock("HLCK")
{
    iQuit = false;
    Start();
}

OsxAudio::~OsxAudio()
{
    Quit();
    Join();
}

void OsxAudio::fillBuffer(AudioQueueBufferRef inBuffer)
{
    AutoMutex _(iHostLock);
    
    Log::Print("OsxAudio::fillBuffer in\n");
    
    /* set the PcmHandler to use inBuffer for the next read requests */
    iPcmHandler->setBuffer(inBuffer);

    /* copy from the litepipe cache buffer */
    while(inBuffer->mAudioDataByteSize < inBuffer->mAudioDataBytesCapacity)
        iPcmHandler->fillBuffer(inBuffer);
    Log::Print("OsxAudio::fillBuffer out\n");
}

void OsxAudio::initialise(OsxPcmProcessor *aPcmHandler, AudioStreamBasicDescription *aFormat)
{
    iPcmHandler = aPcmHandler;
    iAudioFormat = *aFormat;
    
    Log::Print("OsxAudio::initialise in\n");
    iPlaying = false;
    initAudioQueue();
    initAudioBuffers();
    
    iInitialised.Signal();
    Log::Print("OsxAudio::initialise out\n");

}


void OsxAudio::finalise()
{
    /* iPcmHandler is owned by someone else - just null our reference */
    iPcmHandler = NULL;
    
    finaliseAudioQueue();
}

void OsxAudio::initAudioQueue()
{
#ifdef TEST_BUFFER
    // Set up stream format fields
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
    
    AudioQueueNewOutput(
                        &streamFormat,
                        PlayCallback,
                        this,
                        nil,                   // run loop
                        nil,  // run loop mode
                        0,                      // flags
                        &iAudioQueue);
#else /* TEST_BUFFER */
    
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
    AudioQueueDispose(iAudioQueue, true);
    iAudioQueue = NULL;
    iPlaying = false;
}

void OsxAudio::initAudioBuffers()
{
    for (int t = 0; t < kNumDataBuffers; ++t)
    {
        AudioQueueAllocateBuffer(iAudioQueue,
                                 iAudioFormat.mBytesPerFrame * (iAudioFormat.mSampleRate / 5),  // 24-bit stereo 44kHz 0.2s
                                 &iAudioQueueBuffers[t]);
    }
}

void OsxAudio::primeAudioBuffers()
{
    Log::Print("OsxAudio::prime in\n");
    for (int index = 0; index < kNumDataBuffers; ++index)
    {
        Log::Print("OsxAudio::prime buffer %u\n", index);

        PlayCallback(this, iAudioQueue, iAudioQueueBuffers[index]);
    }
    Log::Print("OsxAudio::prime out\n");
}

void OsxAudio::startQueue()
{
    Log::Print("OsxAudio::startQueue in\n");
    primeAudioBuffers();
    AudioQueueStart(iAudioQueue, NULL);
    iPlaying = true;
    Log::Print("OsxAudio::startQueue out\n");
}

void OsxAudio::stopQueue()
{
    AudioQueueStop(iAudioQueue, TRUE);
    iPlaying = false;
}

void OsxAudio::setVolume(Float32 volume)
{
    if (iVolume != volume)
    {
        iVolume = volume;
        AudioQueueSetParameter(iAudioQueue, kAudioQueueParam_Volume, iVolume);
    }
}

void OsxAudio::Quit()
{
    iQuit = true;
}

void OsxAudio::Run()
{
    try {
        /* wait for audio available signal */
        while(!iQuit)
        {
            iInitialised.Wait();
            if(!iPlaying)
            {
                startQueue();
            }
        }
    }
    catch (ThreadKill &e) {}
    
    Log::Print("EXIT OsxAudio Thread\n");
}


