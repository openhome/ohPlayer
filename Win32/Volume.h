#pragma once

#include <OpenHome/Media/VolumeManager.h>
#include <OpenHome/Media/MuteManager.h>

namespace OpenHome {
namespace Media {

class VolumeControl : public IVolume
{
public: // from IVolume
    void SetVolume(TUint aVolume) override;
};

class MuteControl : public IMute
{
public: // from IMute
    void Mute() override;
    void Unmute() override;
};

} // namespace Media
} // namespace OpenHome
