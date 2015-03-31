#ifndef HEADER_PIPELINE_DRIVER_OSX
#define HEADER_PIPELINE_DRIVER_OSX

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioQueue.h>
#include <OpenHome/Types.h>
#include <OpenHome/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorPcmUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>

#include "OsxAudio.h"

namespace OpenHome {
    class Environment;
namespace Media {

class DriverOsx : public Thread, private IMsgProcessor, public IPullableClock, public IPipelineAnimator
{
    static const TUint kTimerFrequencyMs = 5;
    static const TInt64 kClockPullDefault = (1 << 29) * 100LL;
public:
    DriverOsx(Environment& aEnv, IPipeline& aPipeline);
    ~DriverOsx();
    
    TBool isPlaying() { return iPlaying; }
    
private: // from Thread
    void Run();
private:
    void ProcessAudio(MsgPlayable* aMsg);
    
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgSession* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private: // from IPullableClock
    void PullClock(TInt32 aValue);
private: // from IPipelineAnimator
    TUint PipelineDriverDelayJiffies(TUint aSampleRateFrom, TUint aSampleRateTo) override;
private:
    IPipeline& iPipeline;
    Semaphore iSem;
    OsContext* iOsCtx;
    TUint iJiffiesPerSample;
    TUint iPendingJiffies;
    TUint64 iLastTimeUs;
    TUint iNextTimerDuration;
    MsgPlayable* iPlayable;
    Mutex iPullLock;
    TInt64 iPullValue;
    TBool iQuit;
    
    /* Host audio manager */
    OsxAudio iOsxAudio;
    
    /* PCM processor */
    OsxPcmProcessor iPcmHandler;
    
    /* Indicate whether the driver is actively playing */
    bool iPlaying;
    
    /* Define the relative audio level of the output stream. Defaults to 1.0f. */
    Float32 iVolume;
    
    /* describe the audio format of the active stream */
    AudioStreamBasicDescription iAudioFormat;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_DRIVER_OSX
