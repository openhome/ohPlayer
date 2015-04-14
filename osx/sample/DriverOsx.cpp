#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Env.h>

#include <string>
#include <limits>

#include "DriverOsx.h"

using namespace OpenHome;
using namespace OpenHome::Media;


DriverOsx::DriverOsx(Environment& aEnv, IPipeline& aPipeline)
    : Thread("PipelineAnimator", kPrioritySystemHighest)
    , iPipeline(aPipeline)
    , iSem("DRVB", 0)
    , iOsCtx(aEnv.OsCtx())
    , iPlayable(NULL)
    , iPullLock("DBPL")
    , iPullValue(kClockPullDefault)
    , iQuit(false)
{
    iPipeline.SetAnimator(*this);
    Start();
}

DriverOsx::~DriverOsx()
{
    Join();
}


void DriverOsx::Run()
{
    // pull the first (assumed non-audio) msg here so that any delays populating the pipeline don't affect timing calculations below.
    Msg* msg = iPipeline.Pull();
    ASSERT(msg != NULL);
    (void)msg->Process(*this);

    TUint64 now = OsTimeInUs(iOsCtx);
    iLastTimeUs = now;
    iNextTimerDuration = kTimerFrequencyMs;
    iPendingJiffies = kTimerFrequencyMs * Jiffies::kPerMs;
    try {
        for (;;) {
            while (iPendingJiffies > 0) {
                if (iPlayable != NULL) {
                    ProcessAudio(iPlayable);
                }
                else {
                    Msg* msg = iPipeline.Pull();
                    msg = msg->Process(*this);
                    ASSERT(msg == NULL);
                }
            }
            if (iQuit) {
                break;
            }
            iLastTimeUs = now;
            if (iNextTimerDuration != 0) {
                try {
                    iSem.Wait(iNextTimerDuration);
                }
                catch (Timeout&) {}
            }
            iNextTimerDuration = kTimerFrequencyMs;
            now = OsTimeInUs(iOsCtx);
            const TUint diffMs = ((TUint)(now - iLastTimeUs + 500)) / 1000;
            if (diffMs > 100) { // assume delay caused by drop-out.  process regular amount of audio
                iPendingJiffies = kTimerFrequencyMs * Jiffies::kPerMs;
            }
            else {
                iPendingJiffies = diffMs * Jiffies::kPerMs;
                iPullLock.Wait();
                if (iPullValue != kClockPullDefault) {
                    TInt64 pending64 = iPullValue * iPendingJiffies;
                    pending64 /= kClockPullDefault;
                    //Log::Print("iPendingJiffies=%08x, pull=%08x\n", iPendingJiffies, pending64); // FIXME
                    //TInt pending = (TInt)iPendingJiffies + (TInt)pending64;
                    //Log::Print("Pulled clock, now want %u jiffies (%ums, %d%%) extra\n", (TUint)pending, pending/Jiffies::kPerMs, (pending-(TInt)iPendingJiffies)/iPendingJiffies); // FIXME
                    iPendingJiffies = (TUint)pending64;
                }
                iPullLock.Signal();
            }
        }
    }
    catch (ThreadKill&) {}

    // pull until the pipeline is emptied
    while (!iQuit) {
        Msg* msg = iPipeline.Pull();
        msg = msg->Process(*this);
        ASSERT(msg == NULL);
        if (iPlayable != NULL) {
            iPlayable->RemoveRef();
        }
    }
}

void DriverOsx::ProcessAudio(MsgPlayable* aMsg)
{
    iPlayable = NULL;
    
    const TUint numSamples = aMsg->Bytes() / ((iAudioFormat.mBitsPerChannel/8) * iAudioFormat.mChannelsPerFrame);
    TUint jiffies = numSamples * iJiffiesPerSample;
    if (jiffies > iPendingJiffies) {
        jiffies = iPendingJiffies;
        const TUint bytes = Jiffies::BytesFromJiffies(jiffies, iJiffiesPerSample, iAudioFormat.mChannelsPerFrame, (iAudioFormat.mBitsPerChannel/8));
        if (bytes == 0) {
            iPendingJiffies = 0;
            iPlayable = aMsg;
            return;
        }
        
        iPlayable = aMsg->Split(bytes);
        Log::Print("Looking for %d bytes\n", bytes);
        /* read the samples into our sample buffer */
        aMsg->Read(iPcmHandler);
        iOsxAudio.notifyAudioAvailable();

    }
    iPendingJiffies -= jiffies;

    
    aMsg->RemoveRef();
}

Msg* DriverOsx::ProcessMsg(MsgMode* aMsg)
{
    iPullLock.Wait();
    iPullValue = kClockPullDefault;
    iPullLock.Signal();
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgSession* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgTrack* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgHalt* aMsg)
{
    iPendingJiffies = 0;
    iNextTimerDuration = 0;
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& stream = aMsg->StreamInfo();
    
    iOsxAudio.stopQueue();
    
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
    
    iOsxAudio.initialise(&iPcmHandler, &iAudioFormat);
    
    iJiffiesPerSample = Jiffies::JiffiesPerSample(iAudioFormat.mSampleRate);
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgAudioPcm* aMsg)
{
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
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
    iPendingJiffies = 0;
    iNextTimerDuration = 0;
    aMsg->RemoveRef();
    return NULL;
}

void DriverOsx::PullClock(TInt32 aValue)
{
    AutoMutex _(iPullLock);
    iPullValue += aValue;
    Log::Print("DriverOsx::PullClock now at %u%%\n", iPullValue / (1<<29));
}

TUint DriverOsx::PipelineDriverDelayJiffies(TUint /*aSampleRateFrom*/, TUint /*aSampleRateTo*/)
{
    return 0;
}
