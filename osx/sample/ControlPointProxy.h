#pragma once

#include <OpenHome/Media/PipelineObserver.h>

namespace OpenHome {

namespace Net {
    class CpStack;
    class CpDeviceDv;
    class CpProxyAvOpenhomeOrgPlaylist1;
    class DvDevice;
}

namespace Av {

class ControlPointProxy
{
public:
    ControlPointProxy(Net::CpStack& aCpStack, Net::DvDevice& aDevice);
    ~ControlPointProxy();

    void playlistStop();
    void playlistPlay();
    void playlistPause();

private:
    void ohNetGenericInitialEvent();
    void ohNetPlaylistIdChangedEvent();

private:
    Net::CpProxyAvOpenhomeOrgPlaylist1 *iPlaylistProxy;
    Net::CpDeviceDv                    *iCpPlayer;

    Functor iFuncGenericInitialEvent;
    Functor iFuncIdChanged;
};

} // namespace Av
} // namespace OpenHome
