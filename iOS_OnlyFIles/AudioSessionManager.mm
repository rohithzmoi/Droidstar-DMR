/*
    Copyright (C) 2024 Rohith Namboothiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#include "droidstar.h"
extern "C" void callProcessConnect();
extern "C" void clearAudioBuffer();

@interface AudioSessionManager : NSObject {
    UIBackgroundTaskIdentifier _bgTask; 
    BOOL isAudioSessionActive;         
    BOOL isHandlingRouteChange;       
}
- (void)setupAVAudioSession;
- (void)handleAudioInterruption:(NSNotification *)notification;
- (void)handleAudioRouteChange:(NSNotification *)notification;
- (void)setupBackgroundAudio;
- (void)startBackgroundTask;
- (void)stopBackgroundTask;
@end

@implementation AudioSessionManager

- (instancetype)init {
    self = [super init];
    if (self) {
        isAudioSessionActive = NO; 
        isHandlingRouteChange = NO; 
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(handleAudioInterruption:)
                                                     name:AVAudioSessionInterruptionNotification
                                                   object:[AVAudioSession sharedInstance]];
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(handleAudioRouteChange:)
                                                     name:AVAudioSessionRouteChangeNotification
                                                   object:[AVAudioSession sharedInstance]];
    }
    return self;
}

- (void)setupAVAudioSession {
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        NSLog(@"Setting up AVAudioSession...");

    
        BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                withOptions:(AVAudioSessionCategoryOptionAllowBluetooth |
                                             AVAudioSessionCategoryOptionMixWithOthers |
                                             AVAudioSessionCategoryOptionAllowBluetoothA2DP)
                                      error:&error];

        if (!success || error) {
            NSLog(@"Error setting AVAudioSession category: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession category set to PlayAndRecord with required options");

       
        success = [session setMode:AVAudioSessionModeDefault error:&error];
        if (!success || error) {
            NSLog(@"Error setting AVAudioSession mode: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession mode set to Default");
   
        [self configureAudioRoute:session];
        if (!isAudioSessionActive) {
            success = [session setActive:YES error:&error];
            if (!success || error) {
                NSLog(@"Error activating AVAudioSession: %@, code: %ld", error.localizedDescription, (long)error.code);
                return;
            }
            isAudioSessionActive = YES;
            NSLog(@"AVAudioSession activated successfully");
        }

        [self startBackgroundTask];
    }
    @catch (NSException *exception) {
        NSLog(@"Exception setting up AVAudioSession: %@", exception.reason);
    }
}

- (void)configureAudioRoute:(AVAudioSession *)session {
    NSError *error = nil;
    AVAudioSessionRouteDescription *currentRoute = session.currentRoute;

    BOOL hasExternalOutput = NO;
    for (AVAudioSessionPortDescription *output in currentRoute.outputs) {
        if ([output.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
            [output.portType isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [output.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [output.portType isEqualToString:AVAudioSessionPortHeadphones]) {
            hasExternalOutput = YES;
            break;
        }
    }

    if (!hasExternalOutput) {
        BOOL success = [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:&error];
        if (!success || error) {
            NSLog(@"Error overriding audio output to speaker: %@, code: %ld", error.localizedDescription, (long)error.code);
        } else {
            NSLog(@"Audio output overridden to speaker");
        }
    } else {
        NSLog(@"External output detected, no need to override to speaker");
    }
}

- (void)handleAudioInterruption:(NSNotification *)notification {
    NSUInteger interruptionType = [notification.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];

    if (interruptionType == AVAudioSessionInterruptionTypeBegan) {
        NSLog(@"Audio session interruption began");
        isAudioSessionActive = NO; 
    } else if (interruptionType == AVAudioSessionInterruptionTypeEnded) {
        NSLog(@"Audio session interruption ended, attempting to reactivate...");
        [self setupAVAudioSession];
    }
}

- (void)handleAudioRouteChange:(NSNotification *)notification {
    if (isHandlingRouteChange) {
        return; 
    }

    isHandlingRouteChange = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        AVAudioSessionRouteChangeReason reason = (AVAudioSessionRouteChangeReason)[notification.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];

        NSLog(@"Audio route changed, reason: %lu", (unsigned long)reason);

        if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable ||
            reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable) {
            NSLog(@"Audio route change detected, attempting to reactivate...");
            [self setupAVAudioSession];
        }

        isHandlingRouteChange = NO;
    });
}

- (void)setupBackgroundAudio {
    @try {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;

            NSLog(@"Configuring AVAudioSession for background...");

            [session setActive:NO error:nil];

            BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                    withOptions:(AVAudioSessionCategoryOptionDefaultToSpeaker |
                                                 AVAudioSessionCategoryOptionAllowBluetooth |
                                                 AVAudioSessionCategoryOptionAllowBluetoothA2DP |
                                                 AVAudioSessionCategoryOptionMixWithOthers)
                                          error:&error];

            if (!success || error) {
                NSLog(@"Error setting AVAudioSession category for background: %@, code: %ld", error.localizedDescription, (long)error.code);
            } else {
                NSLog(@"AVAudioSession category set successfully for background audio");

                success = [session setActive:YES error:&error];
                if (!success || error) {
                    NSLog(@"Error activating AVAudioSession in background: %@, code: %ld", error.localizedDescription, (long)error.code);
                } else {
                    NSLog(@"AVAudioSession activated successfully in background");
                }
            }
            [self configureAudioRoute:session];
        });
    }
    @catch (NSException *exception) {
        NSLog(@"Exception setting up AVAudioSession: %@", exception.reason);
    }
}

- (void)startBackgroundTask {
    _bgTask = [[UIApplication sharedApplication] beginBackgroundTaskWithExpirationHandler:^{
        [[UIApplication sharedApplication] endBackgroundTask:_bgTask];
        _bgTask = UIBackgroundTaskInvalid;
    }];
}

- (void)stopBackgroundTask {
    if (_bgTask != UIBackgroundTaskInvalid) {
        [[UIApplication sharedApplication] endBackgroundTask:_bgTask];
        _bgTask = UIBackgroundTaskInvalid;
    }
}

@end


extern "C" void setupAVAudioSession() {
    AudioSessionManager *audioManager = [[AudioSessionManager alloc] init];
    [audioManager setupAVAudioSession];
}

extern "C" void deactivateAVAudioSession() {
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        NSLog(@"Deactivating AVAudioSession...");

        BOOL success = [session setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:&error];
        if (!success || error) {
            NSLog(@"Error deactivating AVAudioSession: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }

        NSLog(@"AVAudioSession deactivated successfully");
    }
    @catch (NSException *exception) {
        NSLog(@"Exception deactivating AVAudioSession: %@", exception.reason);
    }
}


extern "C" void setupBackgroundAudio() {
    AudioSessionManager *audioManager = [[AudioSessionManager alloc] init];
    [audioManager setupBackgroundAudio];
}

extern "C" void handleAppEnteringForeground() {
    AudioSessionManager *audioManager = [[AudioSessionManager alloc] init];
    [audioManager setupAVAudioSession];
}


extern "C" void clearAudioBuffer() {
    NSLog(@"Clearing audio buffer to ensure current audio playback");
}
