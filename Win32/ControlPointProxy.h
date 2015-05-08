#ifndef HEADER_PROCESSOR_PCM_FOR_WASAPI
#define HEADER_PROCESSOR_PCM_FOR_WASAPI

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/PipelineObserver.h>

namespace OpenHome {

namespace Net {
    class CpStack;
    class CpDeviceDv;
    class CpProxyAvOpenhomeOrgPlaylist1;
    class CpProxyAvOpenhomeOrgVolume1;
    class DvDevice;
}

namespace Av {

class ControlPointProxy
{
public:
    ControlPointProxy(Net::CpStack& aCpStack, Net::DvDevice& aDevice);
    ~ControlPointProxy();

    void ControlPointProxy::playlistStop();
    void ControlPointProxy::playlistPlay();
    void ControlPointProxy::playlistPause();

private:
    void ohNetGenericInitialEvent();
    void ohNetPlaylistIdChangedEvent();
    void ohNetVolumeInitialEvent();
    void ohNetVolumeChanged();
    void volumeChanged();
    void initialEventVolume();

private:
    Net::CpProxyAvOpenhomeOrgVolume1   *_volumeProxy;
    Net::CpProxyAvOpenhomeOrgPlaylist1 *_playlistProxy;
    Net::CpDeviceDv                    *_cpPlayer;

    Functor funcVolumeInitialEvent;
    Functor funcVolumeChanged;
    Functor funcGenericInitialEvent;
    Functor funcIdChanged;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_PROCESSOR_PCM_FOR_WASAPI
