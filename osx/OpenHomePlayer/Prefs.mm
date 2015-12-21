//
//  Prefs.m
//  sample
//
//  Created by Pete McLaughlin on 24/06/2015.
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//

#import "Prefs.h"

#define DBG(_x)
//#define DBG(_x)   NSLog _x

@implementation Prefs

+ (void)initialize
{
    // we have no default prefs to set up, but if we did we would
    // set them up here
}

- (void) load
{
    DBG((@"Loaded prefs: %@", [[NSUserDefaults standardUserDefaults] dictionaryRepresentation]));
}

- (void) save
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults synchronize];
    DBG((@"Saved prefs: %@", [defaults dictionaryRepresentation]));
}


- (void) setPref:(NSString *)key value:(NSString *)value
{
    [[NSUserDefaults standardUserDefaults] setValue:value forKey:key];
}

- (NSString *) getPref:(NSString *)key
{
    return [[NSUserDefaults standardUserDefaults] valueForKey:key];
}


@end

