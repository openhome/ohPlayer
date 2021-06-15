//
//  ExampleMediaPlayer.h
//  Example media player class
//
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//
#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>

#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Av/FriendlyNameAdapter.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/ContainerFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/Av/Raop/Raop.h>
#include <OpenHome/Web/ConfigUi/FileResourceHandler.h>
#include <OpenHome/Av/Songcast/OhmTimestamp.h>
#include <OpenHome/Av/UpnpAv/FriendlyNameUpnpAv.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Av/RebootHandler.h>

#include "RamStore.h"
#include "Volume.h"
#include "ControlPointProxy.h"

namespace OpenHome {
    class PowerManager;
    class Shell;
    class ShellCommandDebug;
    namespace Net {
        class DviServerUpnp;
        class DvStack;
        class DvDevice;
        class CpStack;
    }
    namespace Media {
        class PipelineManager;
        class DriverSongcastSender;
        class AllocatorInfoLogger;
    }
    namespace Configuration {
        class ConfigPersistentStore;
        class ConfigManager;
    }
    namespace Web {
        class ConfigAppMediaPlayer;
    }
    namespace Av {
        class RamStore;
        namespace Example {

            class ExampleMediaPlayer :  private Net::IResourceManager
            {
            private:
                static const Brn   kIconOpenHomeFileName;
                static const TUint kMaxUiTabs       = 4;
                static const TUint kUiSendQueueSize = kMaxUiTabs * 200;
                static const TUint kShellPort       = 2323;

            public:
                ExampleMediaPlayer(Net::DvStack& aDvStack,
                                   Net::CpStack& aCpStack,
                                   const Brx& aUdn,
                                   const TChar* aRoom,
                                   const TChar* aProductName,
                                   const Brx& aUserAgent);
                virtual ~ExampleMediaPlayer();
                void AddAttribute(const TChar* aAttribute); // FIXME - only required by Songcasting driver
                virtual void Run(Net::CpStack& aCpStack);
                Media::PipelineManager& Pipeline();
                Net::DvDeviceStandard* Device();
                Net::DvDevice* UpnpAvDevice();
                VolumeControl& VolumeControl() {return iVolume;}
                void SetSongcastTimestampers(IOhmTimestamper& aTxTimestamper, IOhmTimestamper& aRxTimestamper);
                void SetSongcastTimestampMappers(IOhmTimestamper& aTxTsMapper, IOhmTimestamper& aRxTsMapper);

                // Pipeline status and control
                void                    StopPipeline();
                TBool                   CanPlay();
                void                    PlayPipeline();
                TBool                   CanPause();
                void                    PausePipeline();
                TBool                   CanHalt();
                void                    HaltPipeline();

                void                    SetHost(Media::DriverOsx *driver) { iDriver = driver; }

            private: // from Net::IResourceManager
                void WriteResource(const Brx& aUriTail,
                                   const TIpAddress& aInterface,
                                   std::vector<char*>& aLanguageList,
                                   Net::IResourceWriter& aResourceWriter) override;  

            private:
                void RegisterPlugins(Environment& aEnv);
                void AddConfigApp();
                void PresentationUrlChanged(const Brx& aUrl);
                TBool TryDisable(Net::DvDevice& aDevice);
                void Disabled();

            protected:
                MediaPlayer*                iMediaPlayer;
                Media::AllocatorInfoLogger* iInfoLogger;
                Net::DvDeviceStandard*  iDevice;
                Net::DvDevice*          iDeviceUpnpAv;
                Av::FriendlyNameAttributeUpdater* iFnUpdaterStandard;
                FriendlyNameManagerUpnpAv*        iFnManagerUpnpAv;
                Av::FriendlyNameAttributeUpdater* iFnUpdaterUpnpAv;
                RamStore*               iRamStore;
                Web::WebAppFramework*   iAppFramework;
                Media::DriverOsx*       iDriver;
                Configuration::ConfigPersistentStore* iConfigPersistentStore;
                RebootLogger            iRebootHandler;
            private:
                Semaphore iDisabled;
                Av::VolumeControl       iVolume;
                const Brx &             iUserAgent;
                ControlPointProxy *     iCpProxy;
                IOhmTimestamper*        iTxTimestamper;
                IOhmTimestamper*        iRxTimestamper;
                IOhmTimestamper*        iTxTsMapper;
                IOhmTimestamper*        iRxTsMapper;
                Web::FileResourceHandlerFactory iFileResourceHandlerFactory;
                Web::ConfigAppMediaPlayer* iConfigApp;
                Bws<Uri::kMaxUriBytes+1>   iPresentationUrl;
                Shell*                  iShell;
                ShellCommandDebug*      iShellDebug;

            };
        } // namespace Example
    } // namespace Av
} // namespace OpenHome

