#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Private/Printer.h>

#include "Volume.h"

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// RebootLogger

void RebootLogger::Reboot(const Brx& aReason)
{
    Log::Print("\n\n\nRebootLogger::Reboot. Reason:\n%.*s\n\n\n",
               PBUF(aReason));
}

TUint VolumeProfile::VolumeMax() const
{
    return kVolumeMax;
}

TUint VolumeProfile::VolumeDefault() const
{
    return kVolumeDefault;
}

TUint VolumeProfile::VolumeUnity() const
{
    return kVolumeUnity;
}

TUint VolumeProfile::VolumeDefaultLimit() const
{
    return kVolumeDefaultLimit;
}

TUint VolumeProfile::VolumeStep() const
{
    return kVolumeStep;
}

TUint VolumeProfile::VolumeMilliDbPerStep() const
{
    return kVolumeMilliDbPerStep;
}

TUint VolumeProfile::BalanceMax() const
{
    return kBalanceMax;
}

TUint VolumeProfile::FadeMax() const
{
    return kFadeMax;
}

TBool VolumeProfile::AlwaysOn() const
{
    return kAlwaysOn;
}

TUint VolumeProfile::OffsetMax() const
{
    return kOffsetMax;
}

TUint VolumeProfile::ThreadPriority() const
{
    return kThreadPriority;
}

IVolumeProfile::StartupVolume VolumeProfile::StartupVolumeConfig() const
{
    return StartupVolume::LastUsed;
}

void VolumeControl::SetHost(Media::DriverOsx *driver)
{
    // Set the audio driver for use in volume calls.
    hostDriver = driver;
    SetVolume(iVolume);
}

void VolumeControl::SetVolume(TUint aVolume)
{
    const TUint MILLI_DB_PER_STEP  = 1024;

    aVolume /= MILLI_DB_PER_STEP;

    // Set the audio session volume.
    if(aVolume != iVolume)
    {
        iVolume = aVolume;
        if(hostDriver != NULL)
            hostDriver->setVolume(float(aVolume)/100.0f);
    }
}

void VolumeControl::SetBalance(TInt /*aBalance*/)
{
    // Not Implemented
}

void VolumeControl::SetFade(TInt /*aFade*/)
{
    // Not Implemented
}
