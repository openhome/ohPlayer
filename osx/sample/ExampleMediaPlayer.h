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
#include <OpenHome/Av/Utils/DriverSongcastSender.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/Av/Raop/Raop.h>
#include <OpenHome/Av/Songcast/OhmTimestamp.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Av/VolumeManager.h>

#include "RamStore.h"
#include "Volume.h"
#include "ControlPointProxy.h"

namespace OpenHome {
    class PowerManager;
    namespace Net {
        class DviServerUpnp;
        class DvStack;
        class DvDevice;
        class Shell;
        class ShellCommandDebug;
    }
    namespace Media {
        class PipelineManager;
        class DriverSongcastSender;
        class IPullableClock;
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
                static const Brn kSongcastSenderIconFileName;
                static const TUint kMaxUiTabs = 4;
                static const TUint kUiSendQueueSize = 32;
                static const TUint kShellPort       = 2323;

            public:
                ExampleMediaPlayer(Net::DvStack& aDvStack, const Brx& aUdn, const TChar* aRoom, const TChar* aProductName, const Brx& aUserAgent);
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
                void WriteResource(const Brx& aUriTail, TIpAddress aInterface, std::vector<char*>& aLanguageList, Net::IResourceWriter& aResourceWriter) override;

            private:
                void RegisterPlugins(Environment& aEnv);
                void AddConfigApp();
                void PresentationUrlChanged(const Brx& aUrl);
                TBool TryDisable(Net::DvDevice& aDevice);
                void Disabled();
                
            protected:
                MediaPlayer*            iMediaPlayer;
                Net::DvDeviceStandard*  iDevice;
                Net::DvDevice*          iDeviceUpnpAv;
                RamStore*               iRamStore;
                Web::WebAppFramework*   iAppFramework;
                Media::DriverOsx*              iDriver;
                Configuration::ConfigPersistentStore* iConfigPersistentStore;
            private:
                Semaphore iDisabled;
                Av::VolumeControl       iVolume;
                ObservableBrx           iObservableFriendlyName;
                const Brx &             iUserAgent;
                ControlPointProxy *     iCpProxy;
                IOhmTimestamper*        iTxTimestamper;
                IOhmTimestamper*        iRxTimestamper;
                IOhmTimestamper*        iTxTsMapper;
                IOhmTimestamper*        iRxTsMapper;
                Web::ConfigAppMediaPlayer* iConfigApp;
                Net::Shell*             iShell;
                Net::ShellCommandDebug* iShellDebug;

            };
        } // namespace Example
    } // namespace Av
} // namespace OpenHome

