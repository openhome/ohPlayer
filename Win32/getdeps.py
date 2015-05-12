################################################################################
#
# Copy the required libraries an include files from the OpenHome repositories
# into our source tree.
#
# This script *must* be run from root folder of the Win32 application.
#
################################################################################
import os
import shutil

TEST_APP='./'
OPENHOME='../../'

# Dependency List
#
# Src files are assumed to reside under OPENHOME, dst files are assumed to reside
# under TEST_APP
depList = [
    # Libraries
    [ 'LitePipe/build/CodecAac.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAacBase.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAdts.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAifc.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAiff.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAiffBase.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAlac.lib', 'Libs' ],
    [ 'LitePipe/build/CodecAlacBase.lib', 'Libs' ],
    [ 'LitePipe/build/CodecFlac.lib', 'Libs' ],
    [ 'LitePipe/build/CodecPcm.lib', 'Libs' ],
    [ 'LitePipe/build/CodecVorbis.lib', 'Libs' ],
    [ 'LitePipe/build/CodecWav.lib', 'Libs' ],
    [ 'LitePipe/build/SourcePlaylist.lib', 'Libs' ],
    [ 'LitePipe/build/SourceSongCast.lib', 'Libs' ],
    [ 'LitePipe/build/SourceUpnpAv.lib', 'Libs' ],
    [ 'LitePipe/dependencies/Windows-x86/openssl/lib/lib.pdb', 'Libs' ],
    [ 'LitePipe/build/libOgg.lib', 'Libs' ],
    [ 'LitePipe/dependencies/Windows-x86/openssl/lib/libeay32.lib', 'Libs' ],
    [ 'LitePipe/build/ohMediaPlayer.lib', 'Libs' ],
    [ 'LitePipe/build/ohMediaPlayerTestUtils.lib', 'Libs' ],
    [ 'LitePipe/build/ohPipeline.lib', 'Libs' ],
    [ 'LitePipe/dependencies/Windows-x86/openssl/lib/ssleay32.lib', 'Libs' ],
    [ 'OhNet/Build/Obj/Windows/Debug/ohNetCore.lib', 'Libs' ],
    [ 'OhNetMon/build/ohNetmon.lib', 'Libs' ],

    # OhNet Header Files
    [ 'OhNet/OpenHome/Ascii.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Buffer.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Buffer.inl', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Debug.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Defines.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Env.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Exception.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Fifo.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Functor.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/FunctorMsg.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/FunctorNetworkAdapter.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Http.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Network.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/OhNetTypes.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/OsTypes.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Parser.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Printer.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Queue.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Standard.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Stream.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Thread.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Timer.h', 'Include/OpenHome/Private' ],
    [ 'OhNet/OpenHome/Types.h', 'Include/OpenHome' ],
    [ 'OhNet/OpenHome/Uri.h', 'Include/OpenHome/Private' ],

    [ 'OhNet/OpenHome/Net/Discovery.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Error.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/FunctorAsync.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/OhNet.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/Service.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Ssdp.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Subscription.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/OpenHome/Net/Bindings/C/OhNet.h', 'Include/OpenHome/Net/C' ],
    [ 'OhNet/OpenHome/Net/Bindings/C/ControlPoint/Async.h', 'Include/OpenHome/Net/C' ],

    [ 'OhNet/OpenHome/Net/ControlPoint/AsyncPrivate.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/CpDevice.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/CpDeviceDv.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/CpiDevice.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/CpiService.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/CpiStack.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/CpProxy.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/FunctorCpDevice.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/ControlPoint/FunctorCpiDevice.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/OpenHome/Net/Device/DvDevice.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/Device/DviDevice.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/DvInvocationResponse.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/Device/DviPropertyUpdateCollection.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/DviServer.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/DviService.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/DviStack.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/DviSubscription.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/DvResourceWriter.h', 'Include/OpenHome/Net/Core' ],
    [ 'OhNet/OpenHome/Net/Device/FunctorDviInvocation.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/OpenHome/Net/Device/Bonjour/Bonjour.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/OpenHome/Net/Device/Lpec/DviServerLpec.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/OpenHome/Net/Device/Upnp/DviProtocolUpnp.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/Upnp/DviServerUpnp.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/Upnp/DviServerWebSocket.h', 'Include/OpenHome/Net/Private' ],
    [ 'OhNet/OpenHome/Net/Device/Upnp/DviSsdpNotifier.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/OpenHome/Net/Shell/Shell.h', 'Include/OpenHome/Net/Private' ],

    [ 'OhNet/Os/Os.h', 'Include/OpenHome' ],
    [ 'OhNet/Os/OsWrapper.h', 'Include/OpenHome' ],
    [ 'OhNet/Os/OsWrapper.inl', 'Include/OpenHome' ],

    # LitePipe Header Files
    [ 'LitePipe/OpenHome/PowerManager.h', 'Include/OpenHome' ],

    [ 'LitePipe/OpenHome/Av/Debug.h', 'Include/OpenHome/Av' ],
    [ 'LitePipe/OpenHome/Av/MediaPlayer.h', 'Include/OpenHome/Av' ],
    [ 'LitePipe/OpenHome/Av/KvpStore.h', 'Include/OpenHome/Av' ],
    [ 'LitePipe/OpenHome/Av/Source.h', 'Include/OpenHome/Av' ],
    [ 'LitePipe/OpenHome/Av/SourceFactory.h', 'Include/OpenHome/Av' ],

    [ 'LitePipe/OpenHome/Av/Tests/RamStore.h', 'Include/OpenHome/Av/Tests' ],

    [ 'LitePipe/OpenHome/Av/Songcast/Ohm.h', 'Include/OpenHome/Av/Songcast' ],
    [ 'LitePipe/OpenHome/Av/Songcast/OhmMsg.h', 'Include/OpenHome/Av/Songcast' ],
    [ 'LitePipe/OpenHome/Av/Songcast/OhmSender.h', 'Include/OpenHome/Av/Songcast' ],
    [ 'LitePipe/OpenHome/Av/Songcast/OhmSenderDriver.h', 'Include/OpenHome/Av/Songcast' ],
    [ 'LitePipe/OpenHome/Av/Songcast/OhmSocket.h', 'Include/OpenHome/Av/Songcast' ],
    [ 'LitePipe/OpenHome/Av/Songcast/ZoneHandler.h', 'Include/OpenHome/Av/Songcast' ],

    [ 'LitePipe/OpenHome/Av/Utils/DriverSongcastSender.h', 'Include/OpenHome/Av/Utils' ],
    [ 'LitePipe/OpenHome/Av/Utils/IconDriverSongcastSender.h', 'Include/OpenHome/Av/Utils' ],

    [ 'LitePipe/OpenHome/Av/UpnpAv/UpnpAv.h', 'Include/OpenHome/Av/UpnpAv' ],

    [ 'LitePipe/OpenHome/Configuration/ConfigManager.h', 'Include/OpenHome/Configuration' ],
    [ 'LitePipe/OpenHome/Configuration/BufferPtrCmp.h', 'Include/OpenHome/Configuration' ],
    [ 'LitePipe/OpenHome/Configuration/IStore.h', 'Include/OpenHome/Configuration' ],

    [ 'LitePipe/OpenHome/Configuration/Tests/ConfigRamStore.h', 'Include/OpenHome/Configuration/Tests' ],

    [ 'LitePipe/OpenHome/Media/ClockPuller.h', 'Include/OpenHome/Media' ],
    [ 'LitePipe/OpenHome/Media/Debug.h', 'Include/OpenHome/Media' ],
    [ 'LitePipe/OpenHome/Media/InfoProvider.h', 'Include/OpenHome/Media' ],
    [ 'LitePipe/OpenHome/Media/MuteManager.h', 'Include/OpenHome/Media' ],
    [ 'LitePipe/OpenHome/Media/PipelineManager.h', 'Include/OpenHome/Media' ],
    [ 'LitePipe/OpenHome/Media/PipelineObserver.h', 'Include/OpenHome/Media' ],
    [ 'LitePipe/OpenHome/Media/VolumeManager.h', 'Include/OpenHome/Media' ],

    [ 'LitePipe/OpenHome/Media/Codec/CodecController.h', 'Include/OpenHome/Media/Codec' ],
    [ 'LitePipe/OpenHome/Media/Codec/CodecFactory.h', 'Include/OpenHome/Media/Codec' ],
    [ 'LitePipe/OpenHome/Media/Codec/Container.h', 'Include/OpenHome/Media/Codec' ],

    [ 'LitePipe/OpenHome/Media/Pipeline/AudioReservoir.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/DecodedAudioAggregator.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/DecodedAudioReservoir.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/EncodedAudioReservoir.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Logger.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Gorger.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Msg.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/PreDriver.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Pipeline.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Protocol/ProtocolFactory.h', 'Include/OpenHome/Media/Protocol' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Pruner.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Ramper.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/RampValidator.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Rewinder.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Reporter.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Router.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/SampleRateValidator.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Seeker.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Skipper.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/SpotifyReporter.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/StarvationMonitor.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Stopper.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/TimestampInspector.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/TrackInspector.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/VariableDelay.h', 'Include/OpenHome/Media/Pipeline' ],
    [ 'LitePipe/OpenHome/Media/Pipeline/Waiter.h', 'Include/OpenHome/Media/Pipeline' ],

    [ 'LitePipe/OpenHome/Media/Tests/VolumeUtils.h', 'Include/OpenHome/Media/Tests' ],

    [ 'LitePipe/OpenHome/Media/Utils/ProcessorPcmUtils.h', 'Include/OpenHome/Media/Utils' ],

    # Files Generated During the LitePipe Build Process
    [ 'LitePipe/build/Generated/CpAvOpenhomeOrgVolume1.h', 'Include' ],
    [ 'LitePipe/build/Generated/CpAvOpenhomeOrgPlaylist1.h', 'Include' ],
    [ 'LitePipe/build/Generated/CpAvOpenhomeOrgVolume1.cpp', 'Generated' ]
]

# Process the dependencies list copying src files to their destination locations.
# Destination directories are created as required.
for le in depList:
    src = OPENHOME + le[0]
    dst = TEST_APP + le[1]

    if not os.path.exists(dst):
        os.makedirs(dst)

    shutil.copy(src, dst)
