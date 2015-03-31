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


static void PlayCallback(void *inUserData, AudioQueueRef inAudioQueue, AudioQueueBufferRef inBuffer)
{
    if(inUserData == nil)
        return;
    
    OsxAudio *hostAudio = (OsxAudio *)inUserData;

    hostAudio->fillBuffer(inBuffer);
    AudioQueueEnqueueBuffer(inAudioQueue, inBuffer, 0, NULL);
}

OsxAudio::OsxAudio() : Thread("OsxAudio", kPriorityVeryHigh), iAudioAvailable("AUAV",0)
{
    Start();
}

OsxAudio::~OsxAudio()
{
    Join();
}

void OsxAudio::fillBuffer(AudioQueueBufferRef inBuffer)
{
    /* copy from the litepipe cache buffer */
    iPcmHandler->fillBuffer(inBuffer);
}

void OsxAudio::initialise(OsxPcmProcessor *aPcmHandler, AudioStreamBasicDescription *aFormat)
{
    iPcmHandler = aPcmHandler;
    iAudioFormat = *aFormat;
    
    iPlaying = false;
    initAudioQueue();
    initAudioBuffers();
}

void OsxAudio::finalise()
{
    /* iPcmHandler is owned by someone else - just null our reference */
    iPcmHandler = NULL;
    
    finaliseAudioQueue();
}

void OsxAudio::initAudioQueue()
{
    AudioQueueNewOutput(
                        &iAudioFormat,
                        PlayCallback,
                        this,
                        NULL,                   // run loop
                        kCFRunLoopCommonModes,  // run loop mode
                        0,                      // flags
                        &iAudioQueue);
    
    iVolume = 1.0;
    
    // FIXME: hook up the volume control
    setVolume(iVolume);
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
    for (int index = 0; index < kNumDataBuffers; ++index)
    {
        PlayCallback(this, iAudioQueue, iAudioQueueBuffers[index]);
    }
}

void OsxAudio::startQueue()
{
    primeAudioBuffers();
    AudioQueueStart(iAudioQueue, NULL);
    iPlaying = true;
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

void OsxAudio::notifyAudioAvailable()
{
    iAudioAvailable.Clear();
    iAudioAvailable.Signal();    
}

void OsxAudio::Run()
{
    /* wait for audio available signal */
    while(1)
    {
        iAudioAvailable.Wait();
        iAudioAvailable.Clear();
        if(!iPlaying)
            startQueue();
    }
}


