#pragma once

#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/PipelineObserver.h>

#include "OptionalFeatures.h"

#include <string>

namespace OpenHome {

namespace Media {
    class DriverOsx;
}

namespace Net {
    class CpStack;
    class CpDeviceDv;
    class CpProxyAvOpenhomeOrgPlaylist1;
#ifdef ENABLE_RADIO
    class CpProxyAvOpenhomeOrgRadio1;
#endif // ENABLE_RADIO
    class CpProxyAvOpenhomeOrgReceiver1;
    class CpProxyAvOpenhomeOrgProduct2;
    class CpProxyUpnpOrgAVTransport1;
    class DvDevice;
}

namespace Av {

// Available sources
enum Sources {PLAYLIST, RADIO, RECEIVER, UPNPAV, UNKNOWN};

class ControlPointProxy
{
public:
    ControlPointProxy(Net::CpStack& aCpStack,
                      Net::DvDevice& aDevice,
                      Net::DvDevice& aUpnpDevice,
                      Media::PipelineManager& aPipeline,
                      Media::DriverOsx& aDriver);
    ~ControlPointProxy();

    void setActiveCp(Sources newSource);
    TBool canStop();
    TBool canPlay();
    TBool canPause();
    void  cpStop();
    void  cpPlay();
    void  cpPause();

private:
    class CPPlaylist
    {
        public:
            CPPlaylist(Net::CpDeviceDv &aCpPlayer, Media::DriverOsx& aDriver);
            ~CPPlaylist();

            void  setActive(TBool active);
            TBool canStop();
            TBool canPlay();
            TBool canPause();
            void  playlistStop();
            void  playlistPlay();
            void  playlistPause();
        private:
            void transportChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgPlaylist1 *iPlaylistProxy;
            Net::CpDeviceDv                    *iCpPlayer;
            Media::DriverOsx                   &iDriver;
            TBool                               iIsActive;

            Functor iTransportStateChanged;
    };

#ifdef ENABLE_RADIO
private:
    class CPRadio
    {
        public:
            CPRadio(Net::CpDeviceDv &aCpPlayer, Media::DriverOsx &aDriver);
            ~CPRadio();

            void  setActive(TBool active);
            TBool canStop();
            TBool canPlay();
            void  radioStop();
            void  radioPlay();
        private:
            void transportChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgRadio1 *iRadioProxy;
            Net::CpDeviceDv                 *iCpPlayer;
            Media::DriverOsx                &iDriver;
            TBool                            iIsActive;

            Functor iTransportStateChanged;
    };
#endif // ENABLE_RADIO

private:
    class CPReceiver
    {
        public:
            CPReceiver(Net::CpDeviceDv &aCpPlayer, Media::DriverOsx &aDriver);
            ~CPReceiver();

            void  setActive(TBool active);
            TBool canStop();
            TBool canPlay();
            void  receiverStop();
            void  receiverPlay();
        private:
            void transportChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgReceiver1 *iReceiverProxy;
            Net::CpDeviceDv                    *iCpPlayer;
            Media::DriverOsx                   &iDriver;
            TBool                               iIsActive;

            Functor iTransportStateChanged;
    };

private:
    class CPUpnpAv : private Media::IPipelineObserver
    {
        public:
            CPUpnpAv(Net::CpDeviceDv &aCpPlayer,
                     Media::PipelineManager &aPipeline,
                     Media::DriverOsx &aDriver);
            ~CPUpnpAv();

            void  setActive(TBool active);
            TBool canStop();
            TBool canPlay();
            TBool canPause();
            void  upnpAvStop();
            void  upnpAvPlay();
            void  upnpAvPause();
        private:
            void pipelineChangedEvent();
        private:
            Net::CpProxyUpnpOrgAVTransport1 *iUpnpAvProxy;
            Net::CpDeviceDv                 *iCpPlayer;
            Media::DriverOsx                &iDriver;
            TBool                            iIsActive;
            Media::PipelineManager&          iPipeline;
        private: // from Media::IPipelineObserver
            void NotifyPipelineState(Media::EPipelineState aState) override;
            void NotifyMode(const Brx& aMode,
                            const Media::ModeInfo& aInfo,
                            const Media::ModeTransportControls& aControls) override;
            void NotifyTrack(Media::Track& aTrack,
                             TBool aStartOfStream) override;
            void NotifyMetaText(const Brx& aText) override;
            void NotifyTime(TUint aSeconds) override;
            void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
            };

private:
    class CPProduct
    {
        public:
            CPProduct(Net::CpDeviceDv &aCpPlayer, ControlPointProxy &aCcp);
            ~CPProduct();

        private:
            TInt nthSubstrPos(TInt n, const std::string& s,
                              const std::string& p);
            Sources GetSourceAtIndex(std::string &sourceXml, TInt sourceIndex);
            void sourceIndexChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgProduct2 *iProductProxy;
            Net::CpDeviceDv                   *iCpPlayer;
            ControlPointProxy                 &iCcp;

            Functor iFuncSourceIndexChanged;
    };

private:
    Sources     iActiveSource;
    CPPlaylist *iCpPlaylist;
#ifdef ENABLE_RADIO
    CPRadio    *iCpRadio;
#endif // ENABLE_RADIO
    CPReceiver *iCpReceiver;
    CPUpnpAv   *iCpUpnpAv;
    CPProduct  *iCpProduct;

private:
    Net::CpDeviceDv         *iCpPlayer;
    Net::CpDeviceDv         *iCpUpnpAvPlayer;
};

} // namespace Av
} // namespace OpenHome
