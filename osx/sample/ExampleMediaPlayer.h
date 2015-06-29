//
//  ExampleMediaPlayer.h
//  LitePipe example media player class
//
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

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
#include <OpenHome/PowerManager.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Av/VolumeManager.h>

#include "RamStore.h"
#include "Volume.h"

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
        namespace Sample {
            
            class ExampleMediaPlayer : private Net::IResourceManager
            {
            private:
                static const Brn kSongcastSenderIconFileName;
            public:
                ExampleMediaPlayer(Net::DvStack& aDvStack, const Brx& aUdn, const TChar* aRoom, const TChar* aProductName,
                                const Brx& aTuneInPartnerId, const Brx& aTidalId, const Brx& aQobuzIdSecret, const Brx& aUserAgent);
                virtual ~ExampleMediaPlayer();
                void AddAttribute(const TChar* aAttribute); // FIXME - only required by Songcasting driver
                virtual void RunWithSemaphore(Net::CpStack& aCpStack);
                Media::PipelineManager& Pipeline();
                Net::DvDeviceStandard* Device();
                VolumeControl& VolumeControl() {return iVolume;}
            protected:
                virtual void RegisterPlugins(Environment& aEnv);
                void DoRegisterPlugins(Environment& aEnv, const Brx& aSupportedProtocols);
                
            private: // from Net::IResourceManager
                void WriteResource(const Brx& aUriTail, TIpAddress aInterface, std::vector<char*>& aLanguageList, Net::IResourceWriter& aResourceWriter) override;

            private:
                void PresentationUrlChanged(const Brx& aUrl);
                TBool TryDisable(Net::DvDevice& aDevice);
                void Disabled();
                
            protected:
                MediaPlayer* iMediaPlayer;
                Net::DvDeviceStandard* iDevice;
                Net::DvDevice* iDeviceUpnpAv;
                RamStore* iRamStore;
                Configuration::ConfigPersistentStore* iConfigPersistentStore;
                Semaphore iSemShutdown;
            private:
                Semaphore iDisabled;
                Av::VolumeControl iVolume;
                ObservableBrx iObservableFriendlyName;
            };
            
            
            // Not very nice, but only to allow reusable test functions.
            class ExampleMediaPlayerInit
            {
            public:
                static OpenHome::Net::Library* CreateLibrary(TBool aLoopback, TUint aAdapter);  // creates lib; client must start appropriate stacks
            };
            
        } // namespace Test
    } // namespace Av
} // namespace OpenHome

