#ifndef HEADER_PIPELINE_DRIVER_OSX
#define HEADER_PIPELINE_DRIVER_OSX

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioQueue.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/MuteManager.h>

#include "PcmHandler.h"

namespace OpenHome {
    class Environment;

namespace Media {

class PriorityArbitratorDriver : public IPriorityArbitrator, private INonCopyable
{
public:
    PriorityArbitratorDriver(TUint aOpenHomeMax);
private: // from IPriorityArbitrator
    TUint Priority(const TChar* aId, TUint aRequested, TUint aHostMax) override;
    TUint OpenHomeMin() const override;
    TUint OpenHomeMax() const override;
    TUint HostRange() const override;
private:
    const TUint iOpenHomeMax;
};

// DriverOsx is a pipeline animator which renders audio messages
// via the Osx AudioKit
//
// The driver is used by MediaPlayer to animate the pipeline
// extracting MsgDecodedAudio messgaes to configure the host audio stream
// and feeding MsgPlayable messages to the host audio buffer(s)
//
// Our OSX Driver implementation consists of 3 main classes operating on 2 threads.
// DriverOsx runs a pipeline animator thread, pulling data from pipeline
// on request, and enqueuing the data in a queue managed by the PcmHandler class.
// Pipeline pull requests are throttled by the OsxAudio thread which handles the
// host AudioQueue and associated buffers.
// PcmHandler is derived from IPcmProcessor and provides a transfer of PCM data
// from MsgPlayable messages to the host audio buffers without additional buffering.
// the processor is called on-demand and transfers the PCM data directly from the
// MsgPlayable to the host audio buffers.
//
// On the Host audio side on OSX we used AudioQueue from the AudioToolkit.
// In this model we allocate a number of buffers (in the default case
// we use 3 buffers as recommended by Apple) which are filled and
// enqueued on demand as the host exhausts them.
// On acquisition of a MsgDecodedStream message we retrieve the stream
// audio format and create an AudioQueue of corresponding configuration.
// We then prime our buffers to ensure a glitch free stream start.
// Once we have primed the buffers with our initial data we start the
// OSX AudioQueue which continues playing the buffers until exhausted then
// calls a buffer-fill callback where we pull more audio data from our
// pipeline.

    
class DriverOsx : public Thread, private IMsgProcessor, public IPipelineAnimator
{
    // number of OS buffers to allocate for AudioQueue
    static const TInt32 kNumDataBuffers = 3;
    
public:
    // DriverOsx - constructor
    // Parameters:
    //   aEnv:      OpenHome execution environment
    //   aPipeline: The pipeline to animate
    DriverOsx(Environment& aEnv, IPipeline& aPipeline);
    
    // DriverOsx - destructor
    ~DriverOsx();
    
    // isPlaying - inform callers whether then Driver is currently animating
    //             the pipeline
    // Return:
    //   true if the pipeline is being animated, false otherwise
    TBool isPlaying() { return iPlaying; }
    
    // Set the audio stream volume.
    // Parameters:
    //   volume:      volume - from 0 to 1.0
    void setVolume(Float32 volume);
    
    // Pause driver output
    void pause();
    
    // Resume driver output
    void resume();
    
    // Fill a host buffer with PCM pipeline data
    //
    // Called by the host audio thread when it needs an audio buffer filled.
    // This is also called on stream startup to prime buffers prior to
    // starting playback
    //
    // Params:
    //   inBuffer - the buffer to fill
    
    void fillBuffer(AudioQueueBufferRef inBuffer);
    
private: // from Thread
    // Run - the execution method for class's main thread
    void Run();
    
private:
    void ProcessAudio(MsgPlayable* aMsg);
    
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;

private: // from IPipelineAnimator
    TUint PipelineDriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo) override;
    
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
    
private:
    // A reference to the pipeline being animated
    IPipeline&      iPipeline;
    
    // The Os Context for the OpenHome enironment
    OsContext*      iOsCtx;
    
    // A flag to indcate when then main thread should quit
    TBool           iQuit;
    
    // The PcmHandler class used to queue and process the Pcm audio messages
    OsxPcmProcessor iPcmHandler;
    
    // A flag indicating whether we are currently animating the pipeline
    bool            iPlaying;
    
    // Define the relative audio level of the output stream. Defaults to 1.0f.
    Float32         iVolume;
    
    // describe the audio format of the active stream
    AudioStreamBasicDescription iAudioFormat;
    
    // Mutex to ensure serialised access to host buffers
    Mutex           iHostLock;
    
    // the host audio queue object being used for playback
    AudioQueueRef   iAudioQueue;
    
    // the audio queue buffers for the host playback audio queue
    AudioQueueBufferRef         iAudioQueueBuffers[kNumDataBuffers];
    
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_DRIVER_OSX
