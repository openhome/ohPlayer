//
//  Prefs.m
//  sample
//
//  Created by Pete McLaughlin on 24/06/2015.
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

#import "Prefs.h"


@implementation Prefs

NSMutableDictionary *defaultPrefs;

+ (void)initialize
{
    if (self == [Prefs class])
    {
        defaultPrefs = [[NSMutableDictionary alloc] init];
        [[NSUserDefaults standardUserDefaults] registerDefaults:defaultPrefs];
    }
}

- (void) load
{

}

- (void) save
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    
    [defaults synchronize];
}


- (void) setPref:(NSString *)key value:(NSString *)value
{
    if(defaultPrefs)
        defaultPrefs[key] = value;
}

- (NSString *) getPref:(NSString *)key
{
    NSString * result = nil;
    
    if(defaultPrefs)
        result = defaultPrefs[key];
    
    return result;
}


@end

