// Media Player Control IF.

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/OsTypes.h>
#include <string>
#include <vector>

typedef struct {
    std::string     *menuString;  // Human readable string identifying
                                  // network adapter and IP address.
    TIpAddress       subnet;      // Subnet address
    OpenHome::TBool  isCurrent;   // Is this the current active subnet
} SubnetRecord;

typedef struct {
    static const TIpAddress NO_SUBNET = 0xFFFFFFFF;

    HWND       hwnd;                     // Main window handle
    TIpAddress subnet;                   // Requested subnet
} InitArgs;
                                         // Initialise the media player thread.
DWORD WINAPI InitAndRunMediaPlayer( LPVOID lpParam );
void ExitMediaPlayer();                  // Terminate the media player thread.
void PipeLinePlay();                     // Pipeline - Play
void PipeLinePause();                    // Pipeline - Pause
void PipeLineStop();                     // Pipeline - Stop

// Get a list of available subnets
std::vector<SubnetRecord*> * GetSubnets();

// Free up a list of available subnets.
void FreeSubnets(std::vector<SubnetRecord*> *subnetVector);

bool CheckForAppUpdates();
