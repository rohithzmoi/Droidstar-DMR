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

// External C functions for calling process connect and clearing the audio buffer
extern "C" void callProcessConnect();
extern "C" void clearAudioBuffer();

@interface AudioSessionManager : NSObject {
    UIBackgroundTaskIdentifier _bgTask; // To manage background tasks
}
- (void)setupAVAudioSession;
- (void)setupBackgroundAudio;
- (void)startBackgroundTask;
- (void)stopBackgroundTask;
@end

@implementation AudioSessionManager

- (void)setupAVAudioSession {
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        NSLog(@"Setting up AVAudioSession...");

        // Use category options suitable for VoIP or low-latency audio
        BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                withOptions:AVAudioSessionCategoryOptionAllowBluetooth |
                                            AVAudioSessionCategoryOptionMixWithOthers |
                                            AVAudioSessionCategoryOptionDefaultToSpeaker |
                                            AVAudioSessionCategoryOptionAllowBluetoothA2DP
                                      error:&error];

        if (!success || error) {
            NSLog(@"Error setting AVAudioSession category: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession category set to PlayAndRecord with required options");

        // Set mode to VoiceChat or VoIP to reduce latency
        success = [session setMode:AVAudioSessionModeVoiceChat error:&error];
        if (!success || error) {
            NSLog(@"Error setting AVAudioSession mode: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession mode set to VoiceChat");

        // To Ensure audio is always routed to the speaker
        success = [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:&error];
        if (!success || error) {
            NSLog(@"Error overriding audio output to speaker: %@, code: %ld", error.localizedDescription, (long)error.code);
        } else {
            NSLog(@"Audio output overridden to speaker");
        }

        // Activate session
        success = [session setActive:YES error:&error];
        if (!success || error) {
            NSLog(@"Error activating AVAudioSession: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession activated successfully");

        // Handle audio interruptions
        [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionInterruptionNotification
                                                          object:session
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification *note) {
            NSUInteger interruptionType = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
            if (interruptionType == AVAudioSessionInterruptionTypeBegan) {
                NSLog(@"Audio session interruption began");
            } else if (interruptionType == AVAudioSessionInterruptionTypeEnded) {
                NSLog(@"Audio session interruption ended, attempting to reactivate...");
                [self setupAVAudioSession];
                NSError *activationError = nil;
                BOOL reactivationSuccess = [session setActive:YES error:&activationError];
                if (!reactivationSuccess) {
                    NSLog(@"Error re-activating AVAudioSession after interruption: %@, code: %ld", activationError.localizedDescription, (long)activationError.code);
                } else {
                    NSLog(@"Audio session successfully reactivated after interruption");
                    [session setCategory:AVAudioSessionCategoryPlayback
                             withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker
                                   error:nil];
                    [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
                    clearAudioBuffer();  // Clear the buffer to ensure current audio
                }
            }
        }];

        // Handle route changes (e.g., when Bluetooth or headphones are connected/disconnected)
        [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                          object:session
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification *note) {
            AVAudioSessionRouteChangeReason reason = (AVAudioSessionRouteChangeReason)[note.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
            NSLog(@"Audio route changed, reason: %lu", (unsigned long)reason);
            if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable ||
                reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable ||
                reason == AVAudioSessionRouteChangeReasonOverride) {
                NSLog(@"Audio route change detected, attempting to reactivate...");
                [self setupAVAudioSession];
                NSError *activationError = nil;
                BOOL reactivationSuccess = [session setActive:YES error:&activationError];
                if (!reactivationSuccess) {
                    NSLog(@"Error re-activating AVAudioSession after route change: %@, code: %ld", activationError.localizedDescription, (long)activationError.code);
                } else {
                    NSLog(@"Audio session successfully reactivated after route change");
                    [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
                }
            }
        }];

        // Start background task to keep the app alive longer - Fingers Crossed :D
        [self startBackgroundTask];
    }
    @catch (NSException *exception) {
        NSLog(@"Exception setting up AVAudioSession: %@", exception.reason);
    }
}

- (void)setupBackgroundAudio {
    @try {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;

            NSLog(@"Configuring AVAudioSession for background...");

            // Deactivate the session before setting the category
            [session setActive:NO error:nil];

            BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                    withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker |
                                                AVAudioSessionCategoryOptionAllowBluetooth |
                                                AVAudioSessionCategoryOptionAllowBluetoothA2DP |
                                                AVAudioSessionCategoryOptionMixWithOthers
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

            // Ensure the audio output is overridden to speake, else audio may at times play through earpiece or volume will be very less
            [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
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

// Expose the setup and management functions to C
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

// Handle app entering background
extern "C" void setupBackgroundAudio() {
    AudioSessionManager *audioManager = [[AudioSessionManager alloc] init];
    [audioManager setupBackgroundAudio];
}

// Handle app entering foreground
extern "C" void handleAppEnteringForeground() {
    AudioSessionManager *audioManager = [[AudioSessionManager alloc] init];
    [audioManager setupAVAudioSession];
}

// function to clear the audio buffer
extern "C" void clearAudioBuffer() {
    NSLog(@"Clearing audio buffer to ensure current audio playback");
}
