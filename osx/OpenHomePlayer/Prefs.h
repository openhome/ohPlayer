//
//  Prefs.h
//  sample
//
//  Created by Pete McLaughlin on 24/06/2015.
//  Copyright (c) 2015 OpenHome Limited. All rights reserved.
//

#ifndef sample_Prefs_h
#define sample_Prefs_h

#import <Foundation/Foundation.h>

@interface Prefs : NSObject

- (void) load;
- (void) save;

- (void) setPref:(NSString *)key value:(NSString *)value;
- (NSString *) getPref:(NSString *)key;

@end

#endif
