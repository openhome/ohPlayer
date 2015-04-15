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
    , iOsCtx(aEnv.OsCtx())
    , iQuit(false)
{
    iPipeline.SetAnimator(*this);
    Start();
}

DriverOsx::~DriverOsx()
{
    iOsxAudio.Quit();
    Join();
}

void DriverOsx::Run()
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
                Log::Print("Failed with exception %s\n", ex.Message());
                iQuit = true;
            }
            
            if((msg != nil))
                (void)msg->Process(*this);
        }
    }
    catch (ThreadKill&) {}

    Log::Print("EXIT Driver loop - iQuit = %s\n", iQuit ? "true" : "false");

    // pull until the pipeline is emptied
    while (!iQuit) {
        Msg* msg = iPipeline.Pull();
        msg = msg->Process(*this);
        ASSERT(msg == NULL);
    }
    Log::Print("EXIT DriverOsx Thread\n");
}

void DriverOsx::ProcessAudio(MsgPlayable* aMsg)
{
    /* process the message - this may block */
    iPcmHandler.enqueue(aMsg);
}

Msg* DriverOsx::ProcessMsg(MsgMode* aMsg)
{
    Log::Print("DriverOsx::Process MsgMode\n");
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgSession* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgSession\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgTrack* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgTrack\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgDelay* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgDelay\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgEncodedStream\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgAudioEncoded\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgMetaText\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgHalt* aMsg)
{
    Log::Print("DriverOsx::Process MsgHalt\n");
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgFlush* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgFlush\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgWait* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgWait\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& stream = aMsg->StreamInfo();
    
    Log::Print("DriverOsx::Process MsgDecodedStream\n");
    iPlaying = false;
    iVolume = 1.0;
    
    Log::Print("DriverOSX::Process Decoded Stream\n");
    iAudioFormat.mFormatID         = kAudioFormatLinearPCM;
    iAudioFormat.mSampleRate       = stream.SampleRate();
    iAudioFormat.mChannelsPerFrame = stream.NumChannels();
    iAudioFormat.mBitsPerChannel   = stream.BitDepth();
    iAudioFormat.mFramesPerPacket  = 1;  // uncompressed audio
    iAudioFormat.mBytesPerFrame    = iAudioFormat.mChannelsPerFrame * iAudioFormat.mBitsPerChannel/8;
    iAudioFormat.mBytesPerPacket   = iAudioFormat.mBytesPerFrame * iAudioFormat.mFramesPerPacket;
    iAudioFormat.mFormatFlags      = kLinearPCMFormatFlagIsBigEndian | kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    
    iOsxAudio.initialise(&iPcmHandler, &iAudioFormat);
    
    aMsg->RemoveRef();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgAudioPcm* aMsg)
{
    Log::Print("DriverOsx::Process MsgAudioPcm\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgSilence* /*aMsg*/)
{
    Log::Print("DriverOsx::Process MsgSilence\n");
    ASSERTS();
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgPlayable* aMsg)
{
    Log::Print("DriverOsx::Process MsgPlayable\n");
    ProcessAudio(aMsg);
    return NULL;
}

Msg* DriverOsx::ProcessMsg(MsgQuit* aMsg)
{
    Log::Print("DriverOsx::Process MsgQuit\n");
    iQuit = true;
    aMsg->RemoveRef();
    return NULL;
}


TUint DriverOsx::PipelineDriverDelayJiffies(TUint /*aSampleRateFrom*/, TUint /*aSampleRateTo*/)
{
    return 0;
}
