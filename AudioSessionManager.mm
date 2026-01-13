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
#import <MediaPlayer/MediaPlayer.h>
#include "droidstar.h"

// Forward declarations for callbacks
extern "C" void callProcessConnect();
extern "C" void clearAudioBuffer();

// Callback function pointers for PTT control from remote commands
static void (*g_pttPressCallback)(void) = NULL;
static void (*g_pttReleaseCallback)(void) = NULL;

#pragma mark - AudioSessionManager Interface

@interface AudioSessionManager : NSObject {
    UIBackgroundTaskIdentifier _bgTask;
    BOOL _isAudioSessionActive;
    BOOL _isHandlingRouteChange;
    BOOL _isConnected;
    BOOL _isTransmitting;
    BOOL _isReceiving;
    
    // Silent audio player to keep audio session alive
    AVAudioPlayer *_silentPlayer;
    NSTimer *_keepAliveTimer;
    
    // Timer to refresh Now Playing periodically
    NSTimer *_nowPlayingTimer;
    NSTimeInterval _playbackStartTime;
    
    // Current state info for Now Playing
    NSString *_currentCallsign;
    NSString *_currentName;
    NSString *_currentCountry;
    NSString *_currentMode;
    NSString *_currentHost;
}

@property (nonatomic, assign) BOOL isConnected;
@property (nonatomic, assign) BOOL isTransmitting;
@property (nonatomic, assign) BOOL isReceiving;

+ (instancetype)sharedManager;

// Audio Session
- (void)setupAVAudioSession;
- (void)setupBackgroundAudio;
- (void)setPreferredInputDevice;

// Now Playing & Remote Commands
- (void)setupRemoteCommandCenter;
- (void)updateNowPlayingInfo;
- (void)clearNowPlayingInfo;

// State updates from app
- (void)setConnectionState:(BOOL)connected host:(NSString *)host mode:(NSString *)mode;
- (void)setCurrentRX:(NSString *)callsign name:(NSString *)name country:(NSString *)country;
- (void)setTransmitting:(BOOL)transmitting;
- (void)clearCurrentRX;

// Background task
- (void)startBackgroundTask;
- (void)stopBackgroundTask;

// Keep-alive audio
- (void)startKeepAliveAudio;
- (void)stopKeepAliveAudio;

// Now Playing refresh timer
- (void)startNowPlayingTimer;
- (void)stopNowPlayingTimer;

@end

#pragma mark - AudioSessionManager Implementation

@implementation AudioSessionManager

@synthesize isConnected = _isConnected;
@synthesize isTransmitting = _isTransmitting;
@synthesize isReceiving = _isReceiving;

+ (instancetype)sharedManager {
    static AudioSessionManager *shared = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        shared = [[AudioSessionManager alloc] init];
    });
    return shared;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _bgTask = UIBackgroundTaskInvalid;
        _isAudioSessionActive = NO;
        _isHandlingRouteChange = NO;
        _isConnected = NO;
        _isTransmitting = NO;
        _isReceiving = NO;
        
        _currentCallsign = @"";
        _currentName = @"";
        _currentCountry = @"";
        _currentMode = @"";
        _currentHost = @"";
        
        _nowPlayingTimer = nil;
        _playbackStartTime = 0;
        
        [self setupNotificationObservers];
        [self setupRemoteCommandCenter];
        [self createSilentAudioFile];
    }
    return self;
}

- (void)setupNotificationObservers {
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    AVAudioSession *session = [AVAudioSession sharedInstance];
    
    // Audio session notifications
    [nc addObserver:self
           selector:@selector(handleAudioInterruption:)
               name:AVAudioSessionInterruptionNotification
             object:session];
    
    [nc addObserver:self
           selector:@selector(handleAudioRouteChange:)
               name:AVAudioSessionRouteChangeNotification
             object:session];
    
    [nc addObserver:self
           selector:@selector(handleMediaServicesWereReset:)
               name:AVAudioSessionMediaServicesWereResetNotification
             object:session];
    
    // App lifecycle notifications
    [nc addObserver:self
           selector:@selector(handleAppDidEnterBackground:)
               name:UIApplicationDidEnterBackgroundNotification
             object:nil];
    
    [nc addObserver:self
           selector:@selector(handleAppWillEnterForeground:)
               name:UIApplicationWillEnterForegroundNotification
             object:nil];
    
    [nc addObserver:self
           selector:@selector(handleAppDidBecomeActive:)
               name:UIApplicationDidBecomeActiveNotification
             object:nil];
    
    [nc addObserver:self
           selector:@selector(handleAppWillResignActive:)
               name:UIApplicationWillResignActiveNotification
             object:nil];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self stopNowPlayingTimer];
    [self stopKeepAliveAudio];
    [self stopBackgroundTask];
    [self clearNowPlayingInfo];
}

#pragma mark - Silent Audio for Keep-Alive

- (void)createSilentAudioFile {
    // Create a silent audio file in the temp directory
    // This will be used to keep the audio session active
    NSString *tempDir = NSTemporaryDirectory();
    NSString *silentPath = [tempDir stringByAppendingPathComponent:@"silence.wav"];
    
    // Check if file already exists
    if ([[NSFileManager defaultManager] fileExistsAtPath:silentPath]) {
        return;
    }
    
    // Create a minimal WAV file with silence (1 second of silence at 8kHz mono)
    // WAV header + 8000 samples of silence
    int sampleRate = 8000;
    int numSamples = sampleRate; // 1 second
    int bitsPerSample = 16;
    int numChannels = 1;
    int byteRate = sampleRate * numChannels * bitsPerSample / 8;
    int blockAlign = numChannels * bitsPerSample / 8;
    int dataSize = numSamples * blockAlign;
    int fileSize = 36 + dataSize;
    
    NSMutableData *wavData = [NSMutableData data];
    
    // RIFF header
    [wavData appendBytes:"RIFF" length:4];
    uint32_t fileSizeLE = CFSwapInt32HostToLittle(fileSize);
    [wavData appendBytes:&fileSizeLE length:4];
    [wavData appendBytes:"WAVE" length:4];
    
    // fmt chunk
    [wavData appendBytes:"fmt " length:4];
    uint32_t fmtSize = CFSwapInt32HostToLittle(16);
    [wavData appendBytes:&fmtSize length:4];
    uint16_t audioFormat = CFSwapInt16HostToLittle(1); // PCM
    [wavData appendBytes:&audioFormat length:2];
    uint16_t channels = CFSwapInt16HostToLittle(numChannels);
    [wavData appendBytes:&channels length:2];
    uint32_t sampleRateLE = CFSwapInt32HostToLittle(sampleRate);
    [wavData appendBytes:&sampleRateLE length:4];
    uint32_t byteRateLE = CFSwapInt32HostToLittle(byteRate);
    [wavData appendBytes:&byteRateLE length:4];
    uint16_t blockAlignLE = CFSwapInt16HostToLittle(blockAlign);
    [wavData appendBytes:&blockAlignLE length:2];
    uint16_t bitsPerSampleLE = CFSwapInt16HostToLittle(bitsPerSample);
    [wavData appendBytes:&bitsPerSampleLE length:2];
    
    // data chunk
    [wavData appendBytes:"data" length:4];
    uint32_t dataSizeLE = CFSwapInt32HostToLittle(dataSize);
    [wavData appendBytes:&dataSizeLE length:4];
    
    // Silent samples (zeros)
    void *silence = calloc(dataSize, 1);
    [wavData appendBytes:silence length:dataSize];
    free(silence);
    
    [wavData writeToFile:silentPath atomically:YES];
    NSLog(@"[AudioSessionManager] Created silent audio file at: %@", silentPath);
}

- (void)startKeepAliveAudio {
    if (_silentPlayer && _silentPlayer.isPlaying) {
        return; // Already playing
    }
    
    NSString *tempDir = NSTemporaryDirectory();
    NSString *silentPath = [tempDir stringByAppendingPathComponent:@"silence.wav"];
    NSURL *silentURL = [NSURL fileURLWithPath:silentPath];
    
    NSError *error = nil;
    _silentPlayer = [[AVAudioPlayer alloc] initWithContentsOfURL:silentURL error:&error];
    
    if (error) {
        NSLog(@"[AudioSessionManager] Error creating silent player: %@", error.localizedDescription);
        return;
    }
    
    _silentPlayer.numberOfLoops = -1; // Loop indefinitely
    // Use tiny volume instead of 0 - iOS may not register 0 volume as "playing"
    _silentPlayer.volume = 0.001;
    
    if ([_silentPlayer play]) {
        NSLog(@"[AudioSessionManager] Keep-alive audio started");
        // Become the "Now Playing" app
        [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
    } else {
        NSLog(@"[AudioSessionManager] Failed to start keep-alive audio");
    }
}

- (void)stopKeepAliveAudio {
    if (_silentPlayer) {
        [_silentPlayer stop];
        _silentPlayer = nil;
        NSLog(@"[AudioSessionManager] Keep-alive audio stopped");
    }
    
    if (_keepAliveTimer) {
        [_keepAliveTimer invalidate];
        _keepAliveTimer = nil;
    }
    
    // Stop being the "Now Playing" app
    [[UIApplication sharedApplication] endReceivingRemoteControlEvents];
}

#pragma mark - Audio Session Setup

- (void)setupAVAudioSession {
    NSLog(@"[AudioSessionManager] Setting up audio session...");
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        // Use PlayAndRecord for VoIP-style audio
        BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                withOptions:(AVAudioSessionCategoryOptionDefaultToSpeaker |
                                             AVAudioSessionCategoryOptionAllowBluetooth |
                                             AVAudioSessionCategoryOptionAllowBluetoothA2DP)
                                      error:&error];

        if (!success || error) {
            NSLog(@"[AudioSessionManager] Error setting category: %@", error.localizedDescription);
            return;
        }

        // VoiceChat mode for echo cancellation and voice processing
        success = [session setMode:AVAudioSessionModeVoiceChat error:&error];
        if (!success || error) {
            NSLog(@"[AudioSessionManager] Error setting mode: %@", error.localizedDescription);
        }

        // Set preferences
        error = nil;
        [session setPreferredSampleRate:48000.0 error:&error];
        error = nil;
        [session setPreferredIOBufferDuration:0.02 error:&error]; // ~20ms buffer

        [self configureAudioRoute:session];
        [self setPreferredInputDevice];

        // Activate session
        error = nil;
        success = [session setActive:YES error:&error];
        if (!success || error) {
            NSLog(@"[AudioSessionManager] Error activating session: %@", error.localizedDescription);
            return;
        }
        
        _isAudioSessionActive = YES;
        NSLog(@"[AudioSessionManager] Audio session activated successfully");
        
        [self startBackgroundTask];
        
        // Start keep-alive if connected
        if (_isConnected) {
            [self startKeepAliveAudio];
        }
    }
    @catch (NSException *exception) {
        NSLog(@"[AudioSessionManager] Exception: %@", exception.reason);
    }
}

- (void)setupBackgroundAudio {
    NSLog(@"[AudioSessionManager] Setting up background audio...");
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        // Configure for background playback
        BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                withOptions:(AVAudioSessionCategoryOptionDefaultToSpeaker |
                                             AVAudioSessionCategoryOptionAllowBluetooth |
                                             AVAudioSessionCategoryOptionAllowBluetoothA2DP)
                                      error:&error];

        if (!success || error) {
            NSLog(@"[AudioSessionManager] Error setting background category: %@", error.localizedDescription);
        }
        
        error = nil;
        [session setMode:AVAudioSessionModeVoiceChat error:&error];
        
        // Activate session
        error = nil;
        success = [session setActive:YES error:&error];
        if (!success || error) {
            NSLog(@"[AudioSessionManager] Error activating background session: %@", error.localizedDescription);
        } else {
            _isAudioSessionActive = YES;
            NSLog(@"[AudioSessionManager] Background audio session activated");
        }
        
        [self configureAudioRoute:session];
        [self startBackgroundTask];
        
        // CRITICAL: Start keep-alive audio when entering background
        if (_isConnected) {
            [self startKeepAliveAudio];
        }
    }
    @catch (NSException *exception) {
        NSLog(@"[AudioSessionManager] Exception in background setup: %@", exception.reason);
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
            NSLog(@"[AudioSessionManager] Error overriding to speaker: %@", error.localizedDescription);
        } else {
            NSLog(@"[AudioSessionManager] Audio output set to speaker");
        }
    } else {
        NSLog(@"[AudioSessionManager] External audio output detected");
    }
}

- (void)setPreferredInputDevice {
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    NSArray<AVAudioSessionPortDescription *> *availableInputs = session.availableInputs;

    for (AVAudioSessionPortDescription *input in availableInputs) {
        if ([input.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [input.portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
            BOOL success = [session setPreferredInput:input error:&error];
            if (success) {
                NSLog(@"[AudioSessionManager] Preferred input: Bluetooth");
            }
            return;
        } else if ([input.portType isEqualToString:AVAudioSessionPortHeadsetMic]) {
            BOOL success = [session setPreferredInput:input error:&error];
            if (success) {
                NSLog(@"[AudioSessionManager] Preferred input: Headset Mic");
            }
            return;
        }
    }
    NSLog(@"[AudioSessionManager] Using default input device");
}

#pragma mark - Remote Command Center (Control Center / Lock Screen / Headphones)

- (void)setupRemoteCommandCenter {
    MPRemoteCommandCenter *commandCenter = [MPRemoteCommandCenter sharedCommandCenter];
    
    // Play command - can be used to start receiving or toggle PTT
    commandCenter.playCommand.enabled = YES;
    [commandCenter.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
        NSLog(@"[AudioSessionManager] Remote play command received");
        // For DroidStar, this could toggle TX or just acknowledge
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    
    // Pause command
    commandCenter.pauseCommand.enabled = YES;
    [commandCenter.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
        NSLog(@"[AudioSessionManager] Remote pause command received");
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    
    // Toggle play/pause (headphone button single press)
    commandCenter.togglePlayPauseCommand.enabled = YES;
    [commandCenter.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
        NSLog(@"[AudioSessionManager] Remote toggle play/pause received - PTT toggle");
        // Toggle PTT
        if (g_pttPressCallback && g_pttReleaseCallback) {
            if (self->_isTransmitting) {
                g_pttReleaseCallback();
            } else {
                g_pttPressCallback();
            }
        }
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    
    // Stop command - could be used to disconnect
    commandCenter.stopCommand.enabled = YES;
    [commandCenter.stopCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
        NSLog(@"[AudioSessionManager] Remote stop command received");
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    
    // Disable skip commands (not applicable for radio)
    commandCenter.nextTrackCommand.enabled = NO;
    commandCenter.previousTrackCommand.enabled = NO;
    commandCenter.skipForwardCommand.enabled = NO;
    commandCenter.skipBackwardCommand.enabled = NO;
    commandCenter.seekForwardCommand.enabled = NO;
    commandCenter.seekBackwardCommand.enabled = NO;
    
    NSLog(@"[AudioSessionManager] Remote command center configured");
}

#pragma mark - Now Playing Info (Lock Screen / Control Center display)

- (void)updateNowPlayingInfo {
    if (!_isConnected) {
        [self clearNowPlayingInfo];
        return;
    }
    
    // Must run on main thread
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateNowPlayingInfo];
        });
        return;
    }
    
    NSMutableDictionary *nowPlayingInfo = [NSMutableDictionary dictionary];
    
    // Title: Show current activity
    NSString *title;
    if (_isTransmitting) {
        title = @"📡 Transmitting";
    } else if (_isReceiving && _currentCallsign.length > 0) {
        title = [NSString stringWithFormat:@"📻 %@", _currentCallsign];
    } else {
        title = @"📻 Listening...";
    }
    nowPlayingInfo[MPMediaItemPropertyTitle] = title;
    
    // Artist: Show name and country for RX, or "TX" for transmit
    NSString *artist;
    if (_isTransmitting) {
        artist = @"Push-to-Talk Active";
    } else if (_currentName.length > 0 || _currentCountry.length > 0) {
        NSMutableArray *parts = [NSMutableArray array];
        if (_currentName.length > 0) [parts addObject:_currentName];
        if (_currentCountry.length > 0) [parts addObject:_currentCountry];
        artist = [parts componentsJoinedByString:@" • "];
    } else {
        artist = @"Awaiting Signal";
    }
    nowPlayingInfo[MPMediaItemPropertyArtist] = artist;
    
    // Album: Show connection info
    NSString *album = @"DroidStar";
    if (_currentHost.length > 0) {
        album = [NSString stringWithFormat:@"%@ - %@", _currentMode, _currentHost];
    }
    nowPlayingInfo[MPMediaItemPropertyAlbumTitle] = album;
    
    // Calculate elapsed time since connection (forces UI refresh)
    NSTimeInterval elapsed = 0;
    if (_playbackStartTime > 0) {
        elapsed = [[NSDate date] timeIntervalSince1970] - _playbackStartTime;
    }
    
    // Playback state - changing elapsed time forces Control Center refresh
    nowPlayingInfo[MPNowPlayingInfoPropertyPlaybackRate] = @(1.0);
    nowPlayingInfo[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(elapsed);
    
    // Set "Live" indicator (shows LIVE badge on Control Center)
    nowPlayingInfo[MPNowPlayingInfoPropertyIsLiveStream] = @(YES);
    
    // Update the info center
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nowPlayingInfo;
}

- (void)clearNowPlayingInfo {
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    NSLog(@"[AudioSessionManager] Now Playing info cleared");
}

#pragma mark - State Updates from App

- (void)setConnectionState:(BOOL)connected host:(NSString *)host mode:(NSString *)mode {
    _isConnected = connected;
    _currentHost = host ?: @"";
    _currentMode = mode ?: @"";
    
    NSLog(@"[AudioSessionManager] Connection state: %@ (%@ - %@)",
          connected ? @"Connected" : @"Disconnected", mode, host);
    
    if (connected) {
        // Record playback start time for elapsed time calculation
        _playbackStartTime = [[NSDate date] timeIntervalSince1970];
        
        // Start keep-alive FIRST so we become the "now playing" app
        [self startKeepAliveAudio];
        
        // Start timer to periodically refresh Now Playing (every 2 seconds)
        [self startNowPlayingTimer];
        
        // Small delay to let audio system register playback, then update Now Playing
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self updateNowPlayingInfo];
        });
    } else {
        _playbackStartTime = 0;
        [self stopNowPlayingTimer];
        [self stopKeepAliveAudio];
        [self clearCurrentRX];
        [self clearNowPlayingInfo];
        [[UIApplication sharedApplication] endReceivingRemoteControlEvents];
    }
}

- (void)setCurrentRX:(NSString *)callsign name:(NSString *)name country:(NSString *)country {
    _currentCallsign = callsign ?: @"";
    _currentName = name ?: @"";
    _currentCountry = country ?: @"";
    _isReceiving = (callsign.length > 0);
    
    [self updateNowPlayingInfo];
}

- (void)setTransmitting:(BOOL)transmitting {
    _isTransmitting = transmitting;
    [self updateNowPlayingInfo];
}

- (void)clearCurrentRX {
    _currentCallsign = @"";
    _currentName = @"";
    _currentCountry = @"";
    _isReceiving = NO;
    
    [self updateNowPlayingInfo];
}

#pragma mark - Now Playing Refresh Timer

- (void)startNowPlayingTimer {
    [self stopNowPlayingTimer];
    
    // Update Now Playing every 2 seconds to keep Control Center/Lock Screen fresh
    _nowPlayingTimer = [NSTimer scheduledTimerWithTimeInterval:2.0
                                                        target:self
                                                      selector:@selector(nowPlayingTimerFired)
                                                      userInfo:nil
                                                       repeats:YES];
    
    // Allow timer to fire even when scrolling
    [[NSRunLoop mainRunLoop] addTimer:_nowPlayingTimer forMode:NSRunLoopCommonModes];
    
    NSLog(@"[AudioSessionManager] Now Playing timer started");
}

- (void)stopNowPlayingTimer {
    if (_nowPlayingTimer) {
        [_nowPlayingTimer invalidate];
        _nowPlayingTimer = nil;
        NSLog(@"[AudioSessionManager] Now Playing timer stopped");
    }
}

- (void)nowPlayingTimerFired {
    if (_isConnected) {
        [self updateNowPlayingInfo];
    } else {
        [self stopNowPlayingTimer];
    }
}

#pragma mark - Background Task Management

- (void)startBackgroundTask {
    void (^startTask)(void) = ^{
        [self stopBackgroundTask];
        
        self->_bgTask = [[UIApplication sharedApplication] beginBackgroundTaskWithName:@"DroidStarAudio"
                                                                      expirationHandler:^{
            NSLog(@"[AudioSessionManager] Background task expiring - renewing");
            [[UIApplication sharedApplication] endBackgroundTask:self->_bgTask];
            self->_bgTask = UIBackgroundTaskInvalid;
            
            // Try to renew if still connected
            if (self->_isConnected) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self startBackgroundTask];
                });
            }
        }];
        
        if (self->_bgTask != UIBackgroundTaskInvalid) {
            NSTimeInterval remaining = [UIApplication sharedApplication].backgroundTimeRemaining;
            NSLog(@"[AudioSessionManager] Background task started (ID: %lu, remaining: %.1fs)",
                  (unsigned long)self->_bgTask, remaining);
        } else {
            NSLog(@"[AudioSessionManager] Failed to start background task");
        }
    };
    
    if ([NSThread isMainThread]) {
        startTask();
    } else {
        dispatch_sync(dispatch_get_main_queue(), startTask);
    }
}

- (void)stopBackgroundTask {
    if (_bgTask != UIBackgroundTaskInvalid) {
        [[UIApplication sharedApplication] endBackgroundTask:_bgTask];
        _bgTask = UIBackgroundTaskInvalid;
        NSLog(@"[AudioSessionManager] Background task ended");
    }
}

#pragma mark - Notification Handlers

- (void)handleAudioInterruption:(NSNotification *)notification {
    NSUInteger interruptionType = [notification.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];

    if (interruptionType == AVAudioSessionInterruptionTypeBegan) {
        NSLog(@"[AudioSessionManager] Audio interruption began");
        _isAudioSessionActive = NO;
        [self stopKeepAliveAudio];
    } else if (interruptionType == AVAudioSessionInterruptionTypeEnded) {
        NSLog(@"[AudioSessionManager] Audio interruption ended");
        
        NSUInteger options = [notification.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
        if (options & AVAudioSessionInterruptionOptionShouldResume) {
            NSLog(@"[AudioSessionManager] System indicates we should resume");
        }
        
        // Reactivate session
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        BOOL success = [session setActive:YES error:&error];
        
        if (success) {
            _isAudioSessionActive = YES;
            NSLog(@"[AudioSessionManager] Audio session reactivated after interruption");
            
            if (_isConnected) {
                [self startKeepAliveAudio];
            }
        } else {
            NSLog(@"[AudioSessionManager] Failed to reactivate: %@", error.localizedDescription);
            [self setupAVAudioSession];
        }
    }
}

- (void)handleAudioRouteChange:(NSNotification *)notification {
    if (_isHandlingRouteChange) return;
    
    _isHandlingRouteChange = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        AVAudioSessionRouteChangeReason reason = (AVAudioSessionRouteChangeReason)
            [notification.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
        
        NSLog(@"[AudioSessionManager] Audio route changed, reason: %lu", (unsigned long)reason);
        
        if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable ||
            reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable) {
            [self setupAVAudioSession];
        }
        
        self->_isHandlingRouteChange = NO;
    });
}

- (void)handleMediaServicesWereReset:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] Media services reset - reinitializing");
    _isAudioSessionActive = NO;
    [self stopKeepAliveAudio];
    [self setupRemoteCommandCenter];
    [self setupAVAudioSession];
}

- (void)handleAppDidEnterBackground:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] App entered background");
    [self setupBackgroundAudio];
    [self updateNowPlayingInfo];
}

- (void)handleAppWillEnterForeground:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] App entering foreground");
    [self setupAVAudioSession];
}

- (void)handleAppDidBecomeActive:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] App became active");
    [self updateNowPlayingInfo];
}

- (void)handleAppWillResignActive:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] App will resign active");
    // Ensure we're ready for background
    if (_isConnected) {
        [self startKeepAliveAudio];
        [self updateNowPlayingInfo];
    }
}

@end

#pragma mark - C Interface

extern "C" void setupAVAudioSession() {
    [[AudioSessionManager sharedManager] setupAVAudioSession];
}

extern "C" void deactivateAVAudioSession() {
    @try {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        
        NSLog(@"[AudioSessionManager] Deactivating audio session...");
        
        BOOL success = [session setActive:NO
                              withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                    error:&error];
        if (!success || error) {
            NSLog(@"[AudioSessionManager] Error deactivating: %@", error.localizedDescription);
        } else {
            NSLog(@"[AudioSessionManager] Audio session deactivated");
        }
    }
    @catch (NSException *exception) {
        NSLog(@"[AudioSessionManager] Exception deactivating: %@", exception.reason);
    }
}

extern "C" void setupBackgroundAudio() {
    [[AudioSessionManager sharedManager] setupBackgroundAudio];
}

extern "C" void handleAppEnteringForeground() {
    [[AudioSessionManager sharedManager] setupAVAudioSession];
}

extern "C" void clearAudioBuffer() {
    NSLog(@"[AudioSessionManager] Clear audio buffer requested");
}

extern "C" void setPreferredInputDevice() {
    [[AudioSessionManager sharedManager] setPreferredInputDevice];
}

extern "C" bool isAppInBackground() {
    UIApplicationState state = [UIApplication sharedApplication].applicationState;
    return (state == UIApplicationStateBackground);
}

extern "C" void deactivateBackgroundAudio() {
    [[AudioSessionManager sharedManager] stopKeepAliveAudio];
    deactivateAVAudioSession();
}

extern "C" void renewBackgroundTask() {
    [[AudioSessionManager sharedManager] startBackgroundTask];
}

// New functions for state management

extern "C" void setAudioConnectionState(bool connected, const char *host, const char *mode) {
    NSString *hostStr = host ? [NSString stringWithUTF8String:host] : @"";
    NSString *modeStr = mode ? [NSString stringWithUTF8String:mode] : @"";
    [[AudioSessionManager sharedManager] setConnectionState:connected host:hostStr mode:modeStr];
}

extern "C" void setAudioRXState(const char *callsign, const char *name, const char *country) {
    NSString *callsignStr = callsign ? [NSString stringWithUTF8String:callsign] : @"";
    NSString *nameStr = name ? [NSString stringWithUTF8String:name] : @"";
    NSString *countryStr = country ? [NSString stringWithUTF8String:country] : @"";
    [[AudioSessionManager sharedManager] setCurrentRX:callsignStr name:nameStr country:countryStr];
}

extern "C" void setAudioTXState(bool transmitting) {
    [[AudioSessionManager sharedManager] setTransmitting:transmitting];
}

extern "C" void clearAudioRXState() {
    [[AudioSessionManager sharedManager] clearCurrentRX];
}

extern "C" void updateNowPlayingInfo() {
    [[AudioSessionManager sharedManager] updateNowPlayingInfo];
}

extern "C" void setPTTCallbacks(void (*pressCallback)(void), void (*releaseCallback)(void)) {
    g_pttPressCallback = pressCallback;
    g_pttReleaseCallback = releaseCallback;
    NSLog(@"[AudioSessionManager] PTT callbacks registered");
}
