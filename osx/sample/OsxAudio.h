#ifndef HEADER_PIPELINE_OSX_AUDIO
#define HEADER_PIPELINE_OSX_AUDIO

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioQueue.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
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
    // number of OS buffers to allocate for AudioQueue
    static const TInt32 kNumDataBuffers = 3;
    
    // number of milliseconds for semaphores to wait between
    // checks for thread shutdown requests
    static const TInt32 kWaitTime = 250;
    
public:
    OsxAudio();
    ~OsxAudio();
    
    // Fill a host buffer with PCM pipeline data
    //
    // Called by the host audio thread when it needs an audio buffer filled.
    // This is also called on stream startup to prime buffers prior to
    // starting playback
    //
    // Params:
    //   inBuffer - the buffer to fill
    
    void fillBuffer(AudioQueueBufferRef inBuffer);
    
    // Initialise the AudioQueue
    //
    // Set up an audio queue based on the stream format described in 'format'.
    // Identify buffering requirements and allocate buffers for the AudioQueue
    //
    // Params:
    //   aPcmHandler - the PCM reader component used to parse PCM data into the host buffers
    //   format - a description of the audio stream data including encoding type and sample format
    
    void initialise(OsxPcmProcessor *aPcmHandler, AudioStreamBasicDescription *format);
    
    // finalise the Host Audio processing
    void finalise();
    
    // Start playing the Host Audio
    void startQueue();
    
    // Pause the Host Audio playback
    void pauseQueue();
    
    // Pause the Host Audio playback
    void resumeQueue();
    
    // Flush any outstanding host audio buffers
    void flushQueue();
    
    // Stop playing the Host Audio
    void stopQueue();
    
    // Set the audio stream volume.
    void setVolume(Float32 volume);
    
    // quit the Host Audio processor
    void quit();

private: // from Thread
    void Run();
    
private:
    // Initialise the OSX AudioQueue
    void initAudioQueue();
    
    // Initialise a set of AudioQueueBuffers for use with our AudioQueue
    // This method allocates kNumDataBuffers buffers (default is 3)
    void initAudioBuffers();
    
    // Finalise the OSX AudioQueue
    // The AudioQueue will be stopped if necessary and all used resources released
    void finaliseAudioQueue();

    // Finalise the AudioQueue's buffers, releasing system resources
    void finaliseAudioBuffers();
    
    // Prime the allocated AudioQueueBuffers with data from the pipeline
    void primeAudioBuffers();
    
private:
    // The PCM handler object which will parse PCM data into host buffers
    OsxPcmProcessor *   iPcmHandler;
    
    // Semaphore to indicate to the main loop when stream initialisation has completed
    Semaphore           iStreamInitialised;
    
    // Semaphore to indicate to the main loop when the current stream has completed
    Semaphore           iStreamCompleted;
    
    // Mutex to ensure serialised access to host buffers
    Mutex               iHostLock;
    
    // Flag to indicate when the stream is in 'play' mode
    bool                iPlaying;
    
    // Flag to indicate when the main thread should exit
    bool                iQuit;
    
    // Define the relative audio level of the output stream. Defaults to 1.0f.
    Float32             iVolume;
    
    // describe the audio format of the active stream
    AudioStreamBasicDescription iAudioFormat;
    
    // the host audio queue object being used for playback
    AudioQueueRef               iAudioQueue;
    
    // the audio queue buffers for the host playback audio queue
    AudioQueueBufferRef         iAudioQueueBuffers[kNumDataBuffers];
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_OSX_AUDIO
