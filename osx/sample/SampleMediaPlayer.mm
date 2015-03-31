//
//  AppDelegate.m
//  LitePipe Sample app delegate
//
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

#import "AppDelegate.h"
#import "SampleMediaPlayer.h"

#include <OpenHome/Net/Private/DviStack.h>

#import "BaseMediaPlayer.h"
#include "DriverOsx.h"

@interface SampleMediaPlayer ()

@end

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Sample;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

@implementation SampleMediaPlayer
{


    Library* lib;
    const TChar* cookie;
    NetworkAdapter* adapter;
    DvStack* dvStack;
    BaseMediaPlayerOptions options;
    BaseMediaPlayer* mp;
    DriverOsx* driver;
}

- (BOOL) setup {
    // Parse options.
    options.Parse(0, nil);

    // Create lib.
    lib = BaseMediaPlayerInit::CreateLibrary(options.Loopback().Value(), options.Adapter().Value());
    cookie = "BaseMediaPlayerMain";
    adapter = lib->CurrentSubnetAdapter(cookie);
    dvStack = lib->StartDv();
    
    // Seed random number generator.
    BaseMediaPlayerInit::SeedRandomNumberGenerator(dvStack->Env(), options.Room().Value(), adapter->Address(), dvStack->ServerUpnp());
    adapter->RemoveRef(cookie);
    
    // Set/construct UDN.
    Bwh udn;
    BaseMediaPlayerInit::AppendUniqueId(dvStack->Env(), options.Udn().Value(), Brn("OsxMediaPlayer"), udn);
    
    // Create TestMediaPlayer.
    driver = nil;
    mp = new BaseMediaPlayer(*dvStack, udn, options.Room().CString(), options.Name().CString(),
                             options.TuneIn().Value(), options.Tidal().Value(), options.Qobuz().Value(),
                             options.UserAgent().Value());
    
    if(mp != nil)
    {
        driver = new Media::DriverOsx(dvStack->Env(), mp->Pipeline());
        mp->SetPullableClock(*driver);
    }
    
    if (driver == nil)
    {
        delete mp;
        return NO;
    }
    
    mp->Run();
    
    return YES;
}


- (void) shutdown {
    delete driver;
    delete mp;
    delete lib;
}

- (BOOL) play {
    mp->PlayPipeline();
    return YES;
}

- (BOOL) pause {
    mp->PausePipeline();
    return YES;
}

- (BOOL) stop {
    mp->StopPipeline();
    return YES;
}


@end

