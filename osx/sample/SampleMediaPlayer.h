//
//  SampleMediaPlayer.h
//  LitePipe Sample media player class
//
//  Copyright (c) 2015 Linn Products Limited. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface SampleMediaPlayer : NSObject 


- (BOOL) setup;
- (void) shutdown;
- (BOOL) play;
- (BOOL) pause;
- (BOOL) stop;

@end

