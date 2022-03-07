#pragma once

#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/PipelineObserver.h>

#include "OptionalFeatures.h"

#include <string>

namespace OpenHome {

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
                      Media::PipelineManager& aPipeline);
    ~ControlPointProxy();

    void setActiveCp(Sources newSource);
    void cpStop();
    void cpPlay();
    void cpPause();

private:
    class CPPlaylist
    {
        public:
            CPPlaylist(Net::CpDeviceDv &aCpPlayer);
            ~CPPlaylist();

            void  setActive(TBool active);
            TBool canStop(std::string &state);
            TBool canPlay(std::string &state);
            TBool canPause(std::string &state);
            void  playlistStop();
            void  playlistPlay();
            void  playlistPause();
        private:
            void transportChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgPlaylist1 *iPlaylistProxy;
            Net::CpDeviceDv                    *iCpPlayer;
            TBool                               iIsActive;

            Functor iTransportStateChanged;
    };

#ifdef ENABLE_RADIO
private:
    class CPRadio
    {
        public:
            CPRadio(Net::CpDeviceDv &aCpPlayer);
            ~CPRadio();

            void  setActive(TBool active);
            TBool canStop(std::string &state);
            TBool canPlay(std::string &state);
            void  radioStop();
            void  radioPlay();
        private:
            void transportChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgRadio1 *iRadioProxy;
            Net::CpDeviceDv                 *iCpPlayer;
            TBool                            iIsActive;

            Functor iTransportStateChanged;
    };
#endif // ENABLE_RADIO

private:
    class CPReceiver
    {
        public:
            CPReceiver(Net::CpDeviceDv &aCpPlayer);
            ~CPReceiver();

            void  setActive(TBool active);
            TBool canStop(std::string &state);
            TBool canPlay(std::string &state);
            void  receiverStop();
            void  receiverPlay();
        private:
            void transportChangedEvent();
        private:
            Net::CpProxyAvOpenhomeOrgReceiver1 *iReceiverProxy;
            Net::CpDeviceDv                    *iCpPlayer;
            TBool                               iIsActive;

            Functor iTransportStateChanged;
    };

private:
    class CPUpnpAv : private Media::IPipelineObserver
    {
        public:
            CPUpnpAv(Net::CpDeviceDv &aCpPlayer,
                     Media::PipelineManager& aPipeline);
            ~CPUpnpAv();

            void  setActive(TBool active);
            TBool canStop(std::string &state);
            TBool canPlay(std::string &state);
            TBool canPause(std::string &state);
            void  upnpAvStop();
            void  upnpAvPlay();
            void  upnpAvPause();
        private:
            void pipelineChangedEvent();
        private:
            Net::CpProxyUpnpOrgAVTransport1 *iUpnpAvProxy;
            Net::CpDeviceDv                 *iCpPlayer;
            TBool                            iIsActive;
            Media::PipelineManager&          iPipeline;
        private: // from Media::IPipelineObserver
            void NotifyPipelineState(Media::EPipelineState aState) override;
            void NotifyMode(const Brx& aMode,
                            const Media::ModeInfo& aInfo,
							const Media::ModeTransportControls& aTransportControls) override;
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
