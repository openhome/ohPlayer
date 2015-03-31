#ifndef HEADER_PIPELINE_OSX_AUDIO
#define HEADER_PIPELINE_OSX_AUDIO

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioQueue.h>
#include <OpenHome/Types.h>
#include <OpenHome/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>

#include "PcmHandler.h"

namespace OpenHome {
    class Environment;
namespace Media {

class OsxAudio : public Thread
{
    static const TInt32 kNumDataBuffers = 3;
    
public:
    OsxAudio();
    ~OsxAudio();
    
    void fillBuffer(AudioQueueBufferRef inBuffer);
    void fillBuffer2(AudioQueueBufferRef inBuffer);
    
    void initialise(OsxPcmProcessor *iPcmHandler, AudioStreamBasicDescription *format);
    void finalise();
    void startQueue();
    void stopQueue();
    void setVolume(Float32 volume);
    void notifyAudioAvailable();
    
private: // from Thread
    void Run();
    
private:
    void initAudioQueue();
    void initAudioBuffers();
    void finaliseAudioQueue();
    void finaliseAudioBuffers();
    void primeAudioBuffers();
    
private:
    OsxPcmProcessor *iPcmHandler;
    Semaphore   iAudioAvailable;
    bool        iPlaying;
    
    /* Define the relative audio level of the output stream. Defaults to 1.0f. */
    Float32 iVolume;
    
    /* describe the audio format of the active stream */
    AudioStreamBasicDescription iAudioFormat;
    
    // the audio queue object being used for playback
    AudioQueueRef iAudioQueue;
    
    // the audio queue buffers for the playback audio queue
    AudioQueueBufferRef iAudioQueueBuffers[kNumDataBuffers];
    
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_OSX_AUDIO
