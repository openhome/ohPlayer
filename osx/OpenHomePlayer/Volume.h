#pragma once

#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Av/RebootHandler.h>
#include <OpenHome/Media/MuteManager.h>

#include "DriverOsx.h"

namespace OpenHome {
namespace Av {

class RebootLogger : public IRebootHandler
{
public: // from IRebootHandler
    void Reboot(const Brx& aReason) override;
};

class VolumeProfile : public IVolumeProfile
{
    static const TUint kVolumeMax = 100;
    static const TUint kVolumeDefault = 45;
    static const TUint kVolumeUnity = 80;
    static const TUint kVolumeDefaultLimit = 85;
    static const TUint kVolumeStep = 1;
    static const TUint kVolumeMilliDbPerStep = 1024;
    static const TUint kBalanceMax = 12;
    static const TUint kFadeMax = 10;
    static const TBool kAlwaysOn = false;
    static const TUint kOffsetMax = 4 * 1024;
    static const TUint kThreadPriority = OpenHome::kPriorityHigh;
private: // from IVolumeProfile
    TUint VolumeMax() const override;
    TUint VolumeDefault() const override;
    TUint VolumeUnity() const override;
    TUint VolumeDefaultLimit() const override;
    TUint VolumeStep() const override;
    TUint VolumeMilliDbPerStep() const override;
    TUint BalanceMax() const override;
    TUint FadeMax() const override;
    TBool AlwaysOn() const override;
    TUint OffsetMax() const override;
    TUint ThreadPriority() const override;
    StartupVolume StartupVolumeConfig() const override;
};

class VolumeControl : public IVolume, public IBalance, public IFade
{
public:
    VolumeControl() { iVolume = 0.0; hostDriver = nil; }

    /* Set the host driver which will handle audio change requests */
    void SetHost(Media::DriverOsx *driver);

private: // from IVolume
    void SetVolume(TUint aVolume) override;
private: // from IBalance
    void SetBalance(TInt aBalance) override;
private: // from IFade
    void SetFade(TInt aFade) override;

private:
    Media::DriverOsx* hostDriver;
    float iVolume;
};

} // namespace Av
} // namespace OpenHome
