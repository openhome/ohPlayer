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

typedef struct
{
    static const TIpAddress NO_SUBNET = 0xFFFFFFFF;

    //HWND       hwnd;                     // Main window handle
    TIpAddress subnet;                   // Requested subnet
} InitArgs;

void InitAndRunMediaPlayer(gpointer args);

// Get a list of available subnets
std::vector<SubnetRecord*> * GetSubnets();

// Free up a list of available subnets.
void FreeSubnets(std::vector<SubnetRecord*> *subnetVector);
