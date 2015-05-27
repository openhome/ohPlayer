#pragma once

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
    Net::CpProxyAvOpenhomeOrgVolume1   *iVolumeProxy;
    Net::CpProxyAvOpenhomeOrgPlaylist1 *iPlaylistProxy;
    Net::CpDeviceDv                    *iCpPlayer;

    Functor iFuncVolumeInitialEvent;
    Functor iFuncVolumeChanged;
    Functor iFuncGenericInitialEvent;
    Functor iFuncIdChanged;
};

} // namespace Av
} // namespace OpenHome
