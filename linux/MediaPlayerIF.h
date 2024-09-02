#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/OsTypes.h>
#include <string>
#include <vector>

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
        kFamilyV4,  // iFamily
        0xFFFFFF,   // iV4
        { 255 }     // iV6
    };

    static constexpr TIpAddress NO_SUBNET_V6 =
    {
        kFamilyV6,  // iFamily
        0xFFFFF,    // iV4
        { 255 },    // iV6
    };

    TIpAddress      subnet;              // Requested subnet
    OpenHome::TBool restarted;           // Has the MediaPlayer been restarted.
} InitArgs;

void InitAndRunMediaPlayer(gpointer args);
void ExitMediaPlayer();                  // Terminate the media player thread
void PipeLinePlay();                     // Pipeline - Play
void PipeLinePause();                    // Pipeline - Pause
void PipeLineStop();                     // Pipeline - Stop

// Get a list of available subnets
std::vector<SubnetRecord*> * GetSubnets();

// Free up a list of available subnets.
void FreeSubnets(std::vector<SubnetRecord*> *subnetVector);
