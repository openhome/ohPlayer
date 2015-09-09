//
//  Prefs.m
//  sample
//
//  Created by Pete McLaughlin on 24/06/2015.
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//

#import "Prefs.h"


@implementation Prefs

+ (void)initialize
{
    // we have no default prefs to set up, but if we did we would
    // set them up here
}

- (void) load
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSLog(@"Loaded prefs: %@", [defaults dictionaryRepresentation]);
}

- (void) save
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults synchronize];
    NSLog(@"saved prefs: %@", [defaults dictionaryRepresentation]);
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

