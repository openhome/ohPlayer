#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Tests/VolumeUtils.h>
#include <OpenHome/Private/Printer.h>

#include "AudioDriver.h"
#include "MemoryCheck.h"
#include "Volume.h"

using namespace OpenHome;
using namespace OpenHome::Media;

void VolumeControl::SetVolume(TUint aVolume)
{
    Log::Print("Volume: %u\n", aVolume);

    // Set the audio session volume.
    AudioDriver::SetVolume(float(aVolume)/100.0f);
}

void MuteControl::Mute()
{
    Log::Print("Volume: muted\n");

    // Set the audio session volume.
    AudioDriver::SetMute(true);
}

void MuteControl::Unmute()
{
    Log::Print("Volume: unmuted\n");

    // Set the audio session volume.
    AudioDriver::SetMute(false);
}
