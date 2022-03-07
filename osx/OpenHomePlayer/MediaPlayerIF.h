//
//  MediaPlayerIF
//  OSX media player interface class
//
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//

#include <OpenHome/Types.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Core/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Printer.h>

#include "DriverOsx.h"
#include "ExampleMediaPlayer.h"

#include <string>
#include <vector>

namespace OpenHome {
namespace Av {
namespace Example {

class MediaPlayerIF
{
public:
    typedef struct
    {
        std::string     *menuString;  // Human readable string identifying
        // network adapter and IP address.
        TIpAddress       subnet;      // Subnet address
        OpenHome::TBool  isCurrent;   // Is this the current active subnet
    } SubnetRecord;

    typedef struct InitArgs
    {
        static constexpr TIpAddress NO_SUBNET =
        {
            kFamilyV4, // iFamily
            0xFFFFFF,  // iV4
            { 255 }    // iV6
        };
        
        static constexpr TIpAddress NO_SUBNET_V6 =
        {
            kFamilyV6, // iFamily
            0xFFFFFF,  // iV4
            { 255 }    // iV6
        };
        
        TIpAddress subnet;                   // Requested subnet
    } InitArgs;


public:
    MediaPlayerIF(TIpAddress subnet);
    ~MediaPlayerIF();

    TBool setup(TIpAddress subnet);
    void shutdown();

    void NotifySuspended();     // suspend the player (and network activity)
    void NotifyResumed();       // resume the player (and network activity)

    void PlayPipeLine();                     // Pipeline - Play
    void PausePipeLine();                    // Pipeline - Pause
    void StopPipeLine();                     // Pipeline - Stop

    TChar * checkForUpdate(TUint major, TUint minor, const TChar *currentVersion);

    Example::ExampleMediaPlayer* mediaPlayer() { return iExampleMediaPlayer; }

    // Get a list of available subnets
    std::vector<SubnetRecord*> * GetSubnets();

    // Free up a list of available subnets.
    void FreeSubnets(std::vector<SubnetRecord*> *subnetVector);


private:

    OpenHome::Net::Library* CreateLibrary(TIpAddress preferredSubnet);

    void volumeChanged();

    TBool iMute;
    TUint iVolume;
    TUint iVolumeLimit;
    TUint iPlaylistTrackId;
    TBool iLive;

    Net::Library* iLib;
    Media::PriorityArbitratorDriver* iArbDriver;
    Media::PriorityArbitratorPipeline* iArbPipeline;
    const TChar* iCookie;
    NetworkAdapter* iAdapter;
    Net::DvStack* iDvStack;
    Net::CpStack* iCpStack;
    Net::CpDeviceDv* iCpPlayerVol;
    Net::CpDeviceDv* iCpPlayerPlaylist;
    Example::ExampleMediaPlayer* iExampleMediaPlayer;
    Media::DriverOsx* iDriver;

};

}  // namespace Example
}  // namespace Av
}  // namespace OpenHome
