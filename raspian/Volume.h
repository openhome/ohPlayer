#pragma once

#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/MuteManager.h>

#include <alsa/asoundlib.h>

namespace OpenHome {
namespace Av {

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
private: // from IVolumeProfile
    TUint VolumeMax() const override;
    TUint VolumeDefault() const override;
    TUint VolumeUnity() const override;
    TUint VolumeDefaultLimit() const override;
    TUint VolumeStep() const override;
    TUint VolumeMilliDbPerStep() const override;
    TUint BalanceMax() const override;
    TUint FadeMax() const override;
};

class VolumeControl : public IVolume, public IBalance, public IFade
{
public:
    VolumeControl();
    ~VolumeControl();
private:
    snd_mixer_t          *iHandle;    // ALSA mixer handle.
    snd_mixer_elem_t     *iElem;      // PCM mixer element
private: // from IVolume
    void SetVolume(TUint aVolume) override;
private: // from IBalance
    void SetBalance(TInt aBalance) override;
private: // from IFade
    void SetFade(TInt aFade) override;
};

} // namespace Av
} // namespace OpenHome
