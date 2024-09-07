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



extern "C" void AudioEngine_stop_playback();
extern "C" void AudioEngine_start_playback();

static UIBackgroundTaskIdentifier bgTask = UIBackgroundTaskInvalid;


extern "C" void setupAVAudioSession() {
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        NSLog(@"Setting up AVAudioSession...");
        [[NSNotificationCenter defaultCenter] removeObserver:session];
        BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                withOptions:AVAudioSessionCategoryOptionAllowBluetooth |
                                            AVAudioSessionCategoryOptionMixWithOthers |
                                            AVAudioSessionCategoryOptionDefaultToSpeaker
                                      error:&error];

        if (!success || error) {
            NSLog(@"Error setting AVAudioSession category: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession category set to PlayAndRecord with DefaultToSpeaker option");

        
        success = [session setActive:YES error:&error];
        if (!success || error) {
            NSLog(@"Error activating AVAudioSession: %@, code: %ld", error.localizedDescription, (long)error.code);
            return;
        }
        NSLog(@"AVAudioSession activated successfully");

        
        [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionInterruptionNotification
                                                          object:session
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification *note) {
            AVAudioSessionInterruptionType interruptionType = (AVAudioSessionInterruptionType)[note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
            if (interruptionType == AVAudioSessionInterruptionTypeBegan) {
                NSLog(@"Audio session interruption began");
               
            } else if (interruptionType == AVAudioSessionInterruptionTypeEnded) {
                NSLog(@"Audio session interruption ended, attempting to reactivate...");
                NSError *activationError = nil;
                BOOL reactivationSuccess = [session setActive:YES error:&activationError];
                if (!reactivationSuccess) {
                    NSLog(@"Error re-activating AVAudioSession after interruption: %@, code: %ld", activationError.localizedDescription, (long)activationError.code);
                } else {
                    NSLog(@"Audio session successfully reactivated after interruption");
                    
                }
            }
        }];

        
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
                // Call C++ function to stop playback
                AudioEngine_stop_playback();
                NSError *activationError = nil;
                BOOL reactivationSuccess = [session setActive:YES error:&activationError];
                AudioEngine_start_playback();
                if (!reactivationSuccess) {
                    NSLog(@"Error re-activating AVAudioSession after route change: %@, code: %ld", activationError.localizedDescription, (long)activationError.code);
                } else {
                    NSLog(@"Audio session successfully reactivated after route change");
                    
                    // Call C++ function to start playback
                                        //AudioEngine_start_playback();
                }
            }
        }];
    }
    @catch (NSException *exception) {
        NSLog(@"Exception setting up AVAudioSession: %@", exception.reason);
    }
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

      
        [[NSNotificationCenter defaultCenter] removeObserver:session];
    }
    @catch (NSException *exception) {
        NSLog(@"Exception deactivating AVAudioSession: %@", exception.reason);
    }
}


extern "C" void setupBackgroundAudio() {
    @try {
        if (bgTask != UIBackgroundTaskInvalid) {
            [[UIApplication sharedApplication] endBackgroundTask:bgTask];
            bgTask = UIBackgroundTaskInvalid;
        }

       
        bgTask = [[UIApplication sharedApplication] beginBackgroundTaskWithExpirationHandler:^{
            NSLog(@"Background task expired. Cleaning up...");
            [[UIApplication sharedApplication] endBackgroundTask:bgTask];
            bgTask = UIBackgroundTaskInvalid;
        }];


        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;

            NSLog(@"Configuring AVAudioSession for background...");


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


            if (bgTask != UIBackgroundTaskInvalid) {
                [[UIApplication sharedApplication] endBackgroundTask:bgTask];
                bgTask = UIBackgroundTaskInvalid;
            }
        });
    }
    @catch (NSException *exception) {
        NSLog(@"Exception setting up AVAudioSession: %@", exception.reason);
    }
}
