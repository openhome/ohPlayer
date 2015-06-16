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
                iQuit = true;
            }
            
            if((msg != nil))
                (void)msg->Process(*this);
        }
    }
    catch (ThreadKill&) {}

    // pull until the pipeline is emptied
    while (!iQuit) {
        Msg* msg = iPipeline.Pull();
        msg = msg->Process(*this);
        ASSERT(msg == NULL);
    }
}

void DriverOsx::ProcessAudio(MsgPlayable* aMsg)
{
    /* process the PCM audio data - this may block */
    iPcmHandler.enqueue(aMsg);
}

Msg* DriverOsx::ProcessMsg(MsgMode* aMsg)
{
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
    aMsg->RemoveRef();
    return NULL;
}

void DriverOsx::setVolume(Float32 volume)
{
    iOsxAudio.setVolume(volume);
}


TUint DriverOsx::PipelineDriverDelayJiffies(TUint /*aSampleRateFrom*/, TUint /*aSampleRateTo*/)
{
    return 0;
}
