/*
    Copyright (C) 2024-2026 Rohith Namboothiri

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
#import <objc/runtime.h>
#include <atomic>
#include "droidstar.h"

// Forward declarations for callbacks
extern "C" void callProcessConnect();
extern "C" void clearAudioBuffer();

// Callback function pointers for PTT control from remote commands
static void (*g_pttPressCallback)(void) = NULL;
static void (*g_pttReleaseCallback)(void) = NULL;

// Thread-safe state for cross-thread queries from AudioEngine (m_modethread)
static std::atomic<bool> g_appInBackground{false};
static std::atomic<uint32_t> g_audioRouteGeneration{1};
static std::atomic<bool> g_appleVoiceIsolationEnabled{true};
static std::atomic<bool> g_iosExternalMicActive{false};
static std::atomic<double> g_activeSampleRate{48000.0};

typedef NS_ENUM(NSInteger, DSAudioSessionPolicy) {
    DSAudioSessionPolicyUnknown = 0,
    DSAudioSessionPolicyPlayback,
    DSAudioSessionPolicyDuplex
};

static inline void runOnMainThreadAsync(dispatch_block_t block) {
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_async(dispatch_get_main_queue(), block);
    }
}

#pragma mark - AudioSessionManager Interface

@interface AudioSessionManager : NSObject {
    UIBackgroundTaskIdentifier _bgTask;
    BOOL _isAudioSessionActive;
    BOOL _isConnected;
    BOOL _isTransmitting;
    BOOL _isCapturing;
    BOOL _isReceiving;

    // Silent audio player to keep audio session alive in background
    AVAudioPlayer *_silentPlayer;
    NSTimer *_keepAliveTimer;

    // Timer to refresh Now Playing periodically
    NSTimer *_nowPlayingTimer;
    NSTimeInterval _playbackStartTime;

    // Throttle: last time configureAudioRoute actually ran
    NSTimeInterval _lastRouteConfigTime;
    NSTimeInterval _lastSelfCategoryChangeTime;
    NSTimeInterval _lastRemotePttToggleTime;
    BOOL _isConfiguringSession;
    BOOL _speakerOverrideApplied;
    dispatch_queue_t _sessionQueue;
    DSAudioSessionPolicy _appliedPolicy;

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
- (void)prepareForCapture;
- (void)finishCapture;
- (BOOL)isFullDuplexMode;
- (BOOL)hasExternalAudioDevice:(AVAudioSession *)session;
- (DSAudioSessionPolicy)desiredSessionPolicy;
- (AVAudioSessionCategoryOptions)desiredDuplexCategoryOptions;
- (void)applySessionPolicy:(BOOL)force reason:(NSString *)reason;

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

// Deactivate session
- (void)deactivateSession;

// Keep-alive audio
- (void)startKeepAliveAudio;
- (void)stopKeepAliveAudio;

// Now Playing refresh timer
- (void)startNowPlayingTimer;
- (void)stopNowPlayingTimer;

@end

#pragma mark - Prevent Qt QDarwinAudioSource / QDarwinAudioSink Category Clobbering

@implementation AVAudioSession (DroidStarBluetoothGuard)

+ (void)load {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        Class cls = [AVAudioSession class];

        Method origWithOpts = class_getInstanceMethod(cls, @selector(setCategory:withOptions:error:));
        Method swizWithOpts = class_getInstanceMethod(cls, @selector(ds_setCategory:withOptions:error:));
        if (origWithOpts && swizWithOpts) {
            method_exchangeImplementations(origWithOpts, swizWithOpts);
        }

        Method origNoOpts = class_getInstanceMethod(cls, @selector(setCategory:error:));
        Method swizNoOpts = class_getInstanceMethod(cls, @selector(ds_setCategory:error:));
        if (origNoOpts && swizNoOpts) {
            method_exchangeImplementations(origNoOpts, swizNoOpts);
        }
    });
}

- (BOOL)ds_setCategory:(AVAudioSessionCategory)category
           withOptions:(AVAudioSessionCategoryOptions)options
                 error:(NSError * _Nullable __autoreleasing *)outError {
    // Qt 6.6.1's QDarwinAudioSink::open() calls setCategory(Ambient, 0) and
    (void)category;
    (void)options;
#if TARGET_OS_SIMULATOR
    AVAudioSessionCategoryOptions enforcedOpts = AVAudioSessionCategoryOptionDefaultToSpeaker;
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    AVAudioSessionCategoryOptions enforcedOpts = (AVAudioSessionCategoryOptionDefaultToSpeaker |
                                                  AVAudioSessionCategoryOptionAllowBluetooth);
#pragma clang diagnostic pop
#endif
    if ([self.category isEqualToString:AVAudioSessionCategoryPlayAndRecord] &&
        ((self.categoryOptions & enforcedOpts) == enforcedOpts)) {
        return YES;
    }
    return [self ds_setCategory:AVAudioSessionCategoryPlayAndRecord
                    withOptions:enforcedOpts
                          error:outError];
}

- (BOOL)ds_setCategory:(AVAudioSessionCategory)category
                 error:(NSError * _Nullable __autoreleasing *)outError {
    (void)category;
    return [self setCategory:AVAudioSessionCategoryPlayAndRecord
                 withOptions:0
                       error:outError];
}

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
        _isConnected = NO;
        _isTransmitting = NO;
        _isCapturing = NO;
        _isReceiving = NO;

        _currentCallsign = @"";
        _currentName = @"";
        _currentCountry = @"";
        _currentMode = @"";
        _currentHost = @"";

        _nowPlayingTimer = nil;
        _playbackStartTime = 0;
        _lastRouteConfigTime = 0;
        _lastSelfCategoryChangeTime = 0;
        _lastRemotePttToggleTime = 0;
        _isConfiguringSession = NO;
        _speakerOverrideApplied = NO;
        dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0);
        _sessionQueue = dispatch_queue_create("org.dudetronics.droidstar.audiosession", attr);
        _appliedPolicy = DSAudioSessionPolicyUnknown;

        if ([NSThread isMainThread]) {
            g_appInBackground.store([UIApplication sharedApplication].applicationState == UIApplicationStateBackground,
                                    std::memory_order_relaxed);
        }

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
    NSString *tempDir = NSTemporaryDirectory();
    NSString *silentPath = [tempDir stringByAppendingPathComponent:@"silence_48k.wav"];

    if ([[NSFileManager defaultManager] fileExistsAtPath:silentPath]) {
        return;
    }

    int sampleRate = 48000;
    int numSamples = sampleRate; // 1 second at native 48 kHz so AQME never down-switches mixer rate
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

    void *silence = calloc(dataSize, 1);
    if (silence) {
        [wavData appendBytes:silence length:dataSize];
        free(silence);
    }

    [wavData writeToFile:silentPath atomically:YES];
    NSLog(@"[AudioSessionManager] Created silent audio file at: %@", silentPath);
}

- (void)startKeepAliveAudio {
    runOnMainThreadAsync(^{
        if (self->_silentPlayer && self->_silentPlayer.isPlaying) {
            return;
        }

        NSString *tempDir = NSTemporaryDirectory();
        NSString *silentPath = [tempDir stringByAppendingPathComponent:@"silence_48k.wav"];
        if (![[NSFileManager defaultManager] fileExistsAtPath:silentPath]) {
            [self createSilentAudioFile];
        }
        NSURL *silentURL = [NSURL fileURLWithPath:silentPath];

        NSError *error = nil;
        self->_silentPlayer = [[AVAudioPlayer alloc] initWithContentsOfURL:silentURL error:&error];

        if (error || !self->_silentPlayer) {
            NSLog(@"[AudioSessionManager] Error creating silent player: %@", error.localizedDescription);
            return;
        }

        self->_silentPlayer.numberOfLoops = -1;
        self->_silentPlayer.volume = 0.0001;

        if ([self->_silentPlayer play]) {
            NSLog(@"[AudioSessionManager] Keep-alive audio started");
            [[UIApplication sharedApplication] beginReceivingRemoteControlEvents];
        } else {
            NSLog(@"[AudioSessionManager] Failed to start keep-alive audio");
        }
    });
}

- (void)stopKeepAliveAudio {
    runOnMainThreadAsync(^{
        if (self->_silentPlayer) {
            [self->_silentPlayer stop];
            self->_silentPlayer = nil;
            NSLog(@"[AudioSessionManager] Keep-alive audio stopped");
        }

        if (self->_keepAliveTimer) {
            [self->_keepAliveTimer invalidate];
            self->_keepAliveTimer = nil;
        }

        [[UIApplication sharedApplication] endReceivingRemoteControlEvents];
    });
}

#pragma mark - Audio Session Setup & Bluetooth Routing

- (BOOL)isFullDuplexMode {
    return [_currentMode isEqualToString:@"IAX"];
}

- (BOOL)hasExternalAudioDevice:(AVAudioSession *)session {
    if (!session) {
        session = [AVAudioSession sharedInstance];
    }

    // Check available inputs for Bluetooth HFP/BLE or wired headset mic
    NSArray<AVAudioSessionPortDescription *> *inputs = session.availableInputs;
    for (AVAudioSessionPortDescription *input in inputs) {
        NSString *pt = input.portType;
        if ([pt isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [pt isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [pt isEqualToString:AVAudioSessionPortHeadsetMic] ||
            [pt isEqualToString:AVAudioSessionPortCarAudio] ||
            [pt isEqualToString:AVAudioSessionPortUSBAudio]) {
            return YES;
        }
    }

    // Also check current route outputs & inputs (e.g. A2DP headphones, wired headphones)
    AVAudioSessionRouteDescription *route = session.currentRoute;
    for (AVAudioSessionPortDescription *output in route.outputs) {
        NSString *pt = output.portType;
        if ([pt isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
            [pt isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [pt isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [pt isEqualToString:AVAudioSessionPortHeadphones] ||
            [pt isEqualToString:AVAudioSessionPortCarAudio] ||
            [pt isEqualToString:AVAudioSessionPortUSBAudio] ||
            [pt isEqualToString:AVAudioSessionPortAirPlay]) {
            return YES;
        }
    }
    for (AVAudioSessionPortDescription *input in route.inputs) {
        NSString *pt = input.portType;
        if ([pt isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [pt isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [pt isEqualToString:AVAudioSessionPortHeadsetMic] ||
            [pt isEqualToString:AVAudioSessionPortCarAudio] ||
            [pt isEqualToString:AVAudioSessionPortUSBAudio]) {
            return YES;
        }
    }

    return NO;
}

- (DSAudioSessionPolicy)desiredSessionPolicy {
    // Always keep AVAudioSession in Duplex (PlayAndRecord + DefaultToSpeaker +
    return DSAudioSessionPolicyDuplex;
}

- (AVAudioSessionCategoryOptions)desiredDuplexCategoryOptions {
#if TARGET_OS_SIMULATOR
    return AVAudioSessionCategoryOptionDefaultToSpeaker;
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // Omit AllowBluetoothA2DP so iOS routes both input and output via Bluetooth HFP
    return (AVAudioSessionCategoryOptionDefaultToSpeaker |
            AVAudioSessionCategoryOptionAllowBluetooth);
#pragma clang diagnostic pop
#endif
}

- (BOOL)isCategoryClobbered:(AVAudioSession *)session {
    if (![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
        return YES;
    }
#if !TARGET_OS_SIMULATOR
    AVAudioSessionCategoryOptions desiredOpts = [self desiredDuplexCategoryOptions];
    if ((session.categoryOptions & desiredOpts) != desiredOpts) {
        return YES;
    }
#endif
    return NO;
}

- (void)applySessionPolicy:(BOOL)force reason:(NSString *)reason {
    dispatch_async(_sessionQueue, ^{
        if (self->_isConfiguringSession) {
            return;
        }
        self->_isConfiguringSession = YES;
        @try {
            DSAudioSessionPolicy policy = [self desiredSessionPolicy];
            AVAudioSession *session = [AVAudioSession sharedInstance];
            AVAudioSessionCategoryOptions desiredOpts = [self desiredDuplexCategoryOptions];
            const BOOL categoryClobbered = [self isCategoryClobbered:session];

            if (!force && self->_isAudioSessionActive && policy == self->_appliedPolicy && !categoryClobbered) {
                [self configureAudioRouteInternal:session];
                self->_isConfiguringSession = NO;
                return;
            }

            NSError *error = nil;
            BOOL success = NO;
            NSString *category = AVAudioSessionCategoryPlayAndRecord;
            AVAudioSessionCategoryOptions options = desiredOpts;
            const double prevSampleRate = session.sampleRate;

            self->_lastSelfCategoryChangeTime = [NSProcessInfo processInfo].systemUptime;
            success = [session setCategory:category withOptions:options error:&error];
            if (!success || error) {
                NSLog(@"[AudioSessionManager] Error setting category: %@", error.localizedDescription);
                self->_isConfiguringSession = NO;
                return;
            }

            if (![session.mode isEqualToString:AVAudioSessionModeDefault]) {
                error = nil;
                [session setMode:AVAudioSessionModeDefault error:&error];
            }

            if (![self hasExternalAudioDevice:session]) {
                error = nil;
                [session setPreferredSampleRate:48000.0 error:&error];
            }
            error = nil;
            [session setPreferredIOBufferDuration:0.02 error:&error];

            if (!self->_isAudioSessionActive) {
                error = nil;
                success = [session setActive:YES error:&error];
                if (!success || error) {
                    NSLog(@"[AudioSessionManager] Error activating session: %@", error.localizedDescription);
                    self->_isConfiguringSession = NO;
                    return;
                }
                self->_isAudioSessionActive = YES;
            }
            self->_appliedPolicy = policy;

            [self setPreferredInputDeviceInternal];
            self->_lastRouteConfigTime = 0;
            [self configureAudioRouteInternal:session];

            const double newSampleRate = session.sampleRate;
            if (newSampleRate >= 8000.0) {
                g_activeSampleRate.store(newSampleRate, std::memory_order_relaxed);
            }
            const BOOL isHardwareRouteChange = [reason isEqualToString:@"hardware-route-change"] ||
                                               [reason isEqualToString:@"interruption"] ||
                                               [reason isEqualToString:@"connection"] ||
                                               [reason isEqualToString:@"setup"];
            if (isHardwareRouteChange ||
                (prevSampleRate > 0.0 && newSampleRate > 0.0 && fabs(newSampleRate - prevSampleRate) > 100.0)) {
                g_audioRouteGeneration.fetch_add(1, std::memory_order_relaxed);
            }
        } @finally {
            self->_isConfiguringSession = NO;
        }
    });
}

- (void)setupAVAudioSession {
    [self applySessionPolicy:YES reason:@"setup"];
    [self startBackgroundTask];
    if (_isConnected) {
        [self startKeepAliveAudio];
    }
}

- (void)setupBackgroundAudio {
    [self applySessionPolicy:YES reason:@"background"];
    [self startBackgroundTask];
    if (_isConnected) {
        [self startKeepAliveAudio];
    }
}

- (void)deactivateSession {
    dispatch_async(_sessionQueue, ^{
        @try {
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;
            BOOL success = [session setActive:NO
                                  withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                        error:&error];
            if (success && !error) {
                self->_isAudioSessionActive = NO;
                self->_appliedPolicy = DSAudioSessionPolicyUnknown;
            }
        } @catch (NSException *exception) {
            NSLog(@"[AudioSessionManager] Exception deactivating: %@", exception.reason);
        }
    });
}

- (void)configureAudioRoute:(AVAudioSession *)session {
    (void)session;
    dispatch_async(_sessionQueue, ^{
        if (self->_isConfiguringSession) {
            return;
        }
        self->_isConfiguringSession = YES;
        @try {
            [self configureAudioRouteInternal:[AVAudioSession sharedInstance]];
        } @finally {
            self->_isConfiguringSession = NO;
        }
    });
}

- (void)configureAudioRouteInternal:(AVAudioSession *)session {
    session = [AVAudioSession sharedInstance];
    AVAudioSessionCategoryOptions desiredOpts = [self desiredDuplexCategoryOptions];

    if ([self isCategoryClobbered:session]) {
        NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
        if ((now - _lastSelfCategoryChangeTime) >= 0.35) {
            _lastSelfCategoryChangeTime = now;
            NSError *catErr = nil;
            BOOL catOk = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                  withOptions:desiredOpts
                                        error:&catErr];
            if (catOk && !catErr) {
                if (!_isAudioSessionActive) {
                    [session setActive:YES error:nil];
                    _isAudioSessionActive = YES;
                }
                _appliedPolicy = DSAudioSessionPolicyDuplex;
            }
        }
    }

    // Always ensure preferredInput is bound to Bluetooth HFP/BLE or Headset mic if available
    [self setPreferredInputDeviceInternal];

#if TARGET_OS_SIMULATOR
    return;
#else
    AVAudioSessionRouteDescription *currentRoute = session.currentRoute;

    BOOL hasExternalOutput = NO;
    BOOL alreadyOnSpeaker = NO;
    BOOL onReceiverEarpiece = NO;
    for (AVAudioSessionPortDescription *output in currentRoute.outputs) {
        if ([output.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
            [output.portType isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [output.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [output.portType isEqualToString:AVAudioSessionPortHeadphones] ||
            [output.portType isEqualToString:AVAudioSessionPortCarAudio] ||
            [output.portType isEqualToString:AVAudioSessionPortUSBAudio] ||
            [output.portType isEqualToString:AVAudioSessionPortAirPlay]) {
            hasExternalOutput = YES;
            break;
        }
        if ([output.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker]) {
            alreadyOnSpeaker = YES;
        }
        if ([output.portType isEqualToString:AVAudioSessionPortBuiltInReceiver]) {
            onReceiverEarpiece = YES;
        }
    }

    for (AVAudioSessionPortDescription *input in currentRoute.inputs) {
        if ([input.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [input.portType isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [input.portType isEqualToString:AVAudioSessionPortHeadsetMic]) {
            hasExternalOutput = YES;
            break;
        }
    }

    if (hasExternalOutput) {
        NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
        if ((now - _lastRouteConfigTime) >= 0.50) {
            _lastRouteConfigTime = now;
            _lastSelfCategoryChangeTime = now;
            [session overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
        }
        return;
    }

    // Do NOT call overrideOutputAudioPort:Speaker if DefaultToSpeaker already placed output
    if (alreadyOnSpeaker && !onReceiverEarpiece) {
        return;
    }

    NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
    if ((now - _lastRouteConfigTime) < 0.50) {
        return;
    }
    _lastRouteConfigTime = now;
    _lastSelfCategoryChangeTime = now;
    [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
#endif
}

- (void)setPreferredInputDevice {
    dispatch_async(_sessionQueue, ^{
        [self setPreferredInputDeviceInternal];
    });
}

- (void)prepareForCapture {
    _isCapturing = YES;
    _isTransmitting = YES;
#if !TARGET_OS_SIMULATOR
    AVAudioSession *session = [AVAudioSession sharedInstance];
    AVAudioSessionCategoryOptions desiredOpts = [self desiredDuplexCategoryOptions];
    if (![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord] ||
        ((session.categoryOptions & desiredOpts) != desiredOpts)) {
        _lastSelfCategoryChangeTime = [NSProcessInfo processInfo].systemUptime;
        [session setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:desiredOpts error:nil];
    }
    [self setPreferredInputDeviceInternal];
#else
    g_iosExternalMicActive.store(false, std::memory_order_relaxed);
    AudioEngine::setExternalMicActive(false);
#endif
}

- (void)finishCapture {
    _isCapturing = NO;
    _isTransmitting = NO;
}

- (void)setPreferredInputDeviceInternal {
    AVAudioSession *session = [AVAudioSession sharedInstance];
#if TARGET_OS_SIMULATOR
    g_iosExternalMicActive.store(false, std::memory_order_relaxed);
    AudioEngine::setExternalMicActive(false);
    if (session.sampleRate >= 8000.0) {
        g_activeSampleRate.store(session.sampleRate, std::memory_order_relaxed);
    }
    return;
#else
    NSError *error = nil;
    NSArray<AVAudioSessionPortDescription *> *availableInputs = session.availableInputs;
    AVAudioSessionPortDescription *currentInput = session.currentRoute.inputs.firstObject;

    // 1. Prioritize Bluetooth HFP / BLE microphone
    for (AVAudioSessionPortDescription *input in availableInputs) {
        if ([input.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [input.portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
            g_iosExternalMicActive.store(true, std::memory_order_relaxed);
            AudioEngine::setExternalMicActive(true);
            if (![session.preferredInput.UID isEqualToString:input.UID] ||
                ![currentInput.UID isEqualToString:input.UID]) {
                const double prevRate = session.sampleRate;
                [session overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
                BOOL ok = [session setPreferredInput:input error:&error];
                if (ok) {
                    const double newRate = session.sampleRate;
                    if (newRate >= 8000.0) {
                        g_activeSampleRate.store(newRate, std::memory_order_relaxed);
                    }
                    NSLog(@"[AudioSessionManager] Routed microphone to Bluetooth (%@) SR=%.0f Hz",
                          input.portName, newRate);
                    if (fabs(newRate - prevRate) > 100.0 ||
                        ![currentInput.UID isEqualToString:input.UID]) {
                        g_audioRouteGeneration.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            } else if (session.sampleRate >= 8000.0) {
                g_activeSampleRate.store(session.sampleRate, std::memory_order_relaxed);
            }
            return;
        }
    }

    // 2. Wired Headset / USB Audio / CarAudio / LineIn microphone
    for (AVAudioSessionPortDescription *input in availableInputs) {
        if ([input.portType isEqualToString:AVAudioSessionPortHeadsetMic] ||
            [input.portType isEqualToString:AVAudioSessionPortUSBAudio] ||
            [input.portType isEqualToString:AVAudioSessionPortCarAudio] ||
            [input.portType isEqualToString:AVAudioSessionPortLineIn]) {
            g_iosExternalMicActive.store(true, std::memory_order_relaxed);
            AudioEngine::setExternalMicActive(true);
            if (![session.preferredInput.UID isEqualToString:input.UID] ||
                ![currentInput.UID isEqualToString:input.UID]) {
                const double prevRate = session.sampleRate;
                [session overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
                BOOL ok = [session setPreferredInput:input error:&error];
                if (ok) {
                    const double newRate = session.sampleRate;
                    if (newRate >= 8000.0) {
                        g_activeSampleRate.store(newRate, std::memory_order_relaxed);
                    }
                    NSLog(@"[AudioSessionManager] Routed microphone to Headset/USB (%@) SR=%.0f Hz",
                          input.portName, newRate);
                    if (fabs(newRate - prevRate) > 100.0 ||
                        ![currentInput.UID isEqualToString:input.UID]) {
                        g_audioRouteGeneration.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            } else if (session.sampleRate >= 8000.0) {
                g_activeSampleRate.store(session.sampleRate, std::memory_order_relaxed);
            }
            return;
        }
    }

    // 3. Built-in microphone when no external mic is connected
    if (session.preferredInput != nil) {
        [session setPreferredInput:nil error:nil];
    }
    for (AVAudioSessionPortDescription *input in availableInputs) {
        if ([input.portType isEqualToString:AVAudioSessionPortBuiltInMic]) {
            for (AVAudioSessionDataSourceDescription *ds in input.dataSources) {
                if ([ds.orientation isEqualToString:AVAudioSessionOrientationBottom]) {
                    if (![input.preferredDataSource.dataSourceID isEqualToNumber:ds.dataSourceID]) {
                        [input setPreferredDataSource:ds error:nil];
                    }
                    break;
                }
            }
            break;
        }
    }
    bool routeHasExternalMic = false;
    for (AVAudioSessionPortDescription *inPort in session.currentRoute.inputs) {
        NSString *pt = inPort.portType;
        if ([pt isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [pt isEqualToString:AVAudioSessionPortBluetoothLE] ||
            [pt isEqualToString:AVAudioSessionPortHeadsetMic] ||
            [pt isEqualToString:AVAudioSessionPortUSBAudio] ||
            [pt isEqualToString:AVAudioSessionPortCarAudio] ||
            [pt isEqualToString:AVAudioSessionPortLineIn]) {
            routeHasExternalMic = true;
            break;
        }
    }
    g_iosExternalMicActive.store(routeHasExternalMic, std::memory_order_relaxed);
    AudioEngine::setExternalMicActive(routeHasExternalMic);
    if (session.sampleRate >= 8000.0) {
        g_activeSampleRate.store(session.sampleRate, std::memory_order_relaxed);
    }
#endif
}

#pragma mark - Remote Command Center (Control Center / Lock Screen / Bluetooth Earbuds)

- (void)handleRemotePttToggle {
    NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
    // Debounce 250ms in case Bluetooth earbuds send both play/pause and togglePlayPause
    if ((now - _lastRemotePttToggleTime) < 0.25) {
        return;
    }
    _lastRemotePttToggleTime = now;

    if (g_pttPressCallback && g_pttReleaseCallback) {
        if (self->_isTransmitting) {
            g_pttReleaseCallback();
        } else {
            g_pttPressCallback();
        }
    }
}

- (void)setupRemoteCommandCenter {
    MPRemoteCommandCenter *commandCenter = [MPRemoteCommandCenter sharedCommandCenter];

    // Play command (many Bluetooth earbuds send Play on stem/button press)
    commandCenter.playCommand.enabled = YES;
    [commandCenter.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote play command received - PTT toggle");
        [self handleRemotePttToggle];
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // Pause command (many Bluetooth earbuds send Pause on stem/button press)
    commandCenter.pauseCommand.enabled = YES;
    [commandCenter.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote pause command received - PTT toggle");
        [self handleRemotePttToggle];
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // Toggle play/pause (headphone / AirPods button single press)
    commandCenter.togglePlayPauseCommand.enabled = YES;
    [commandCenter.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote toggle play/pause received - PTT toggle");
        [self handleRemotePttToggle];
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // Stop command
    commandCenter.stopCommand.enabled = YES;
    [commandCenter.stopCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote stop command received");
        if (self->_isTransmitting && g_pttReleaseCallback) {
            g_pttReleaseCallback();
        }
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

    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateNowPlayingInfo];
        });
        return;
    }

    NSMutableDictionary *nowPlayingInfo = [NSMutableDictionary dictionary];

    NSString *title;
    if (_isTransmitting) {
        title = @"📡 Transmitting";
    } else if (_isReceiving && _currentCallsign.length > 0) {
        title = [NSString stringWithFormat:@"📻 %@", _currentCallsign];
    } else {
        title = @"📻 Listening...";
    }
    nowPlayingInfo[MPMediaItemPropertyTitle] = title;

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

    NSString *album = @"DroidStar";
    if (_currentHost.length > 0) {
        album = [NSString stringWithFormat:@"%@ - %@", _currentMode, _currentHost];
    }
    nowPlayingInfo[MPMediaItemPropertyAlbumTitle] = album;

    NSTimeInterval elapsed = 0;
    if (_playbackStartTime > 0) {
        elapsed = [[NSDate date] timeIntervalSince1970] - _playbackStartTime;
    }

    nowPlayingInfo[MPNowPlayingInfoPropertyPlaybackRate] = @(1.0);
    nowPlayingInfo[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(elapsed);
    nowPlayingInfo[MPNowPlayingInfoPropertyIsLiveStream] = @(YES);

    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nowPlayingInfo;
}

- (void)clearNowPlayingInfo {
    runOnMainThreadAsync(^{
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    });
}

#pragma mark - State Updates from App

- (void)setConnectionState:(BOOL)connected host:(NSString *)host mode:(NSString *)mode {
    _isConnected = connected;
    _isTransmitting = NO;
    _isCapturing = NO;
    _currentHost = [host copy] ?: @"";
    _currentMode = [mode copy] ?: @"";

    NSLog(@"[AudioSessionManager] Connection state: %@ (%@ - %@)",
          connected ? @"Connected" : @"Disconnected", mode, host);

    if (connected) {
        _playbackStartTime = [[NSDate date] timeIntervalSince1970];
        [self applySessionPolicy:YES reason:@"connection"];
        [self startKeepAliveAudio];
        [self startNowPlayingTimer];
        [self updateNowPlayingInfo];
    } else {
        _playbackStartTime = 0;
        [self stopNowPlayingTimer];
        [self stopKeepAliveAudio];
        [self clearCurrentRX];
        [self clearNowPlayingInfo];
    }
}

- (void)setCurrentRX:(NSString *)callsign name:(NSString *)name country:(NSString *)country {
    _currentCallsign = [callsign copy] ?: @"";
    _currentName = [name copy] ?: @"";
    _currentCountry = [country copy] ?: @"";
    _isReceiving = (_currentCallsign.length > 0);

    [self updateNowPlayingInfo];
}

- (void)setTransmitting:(BOOL)transmitting {
    if (_isTransmitting == transmitting) {
        [self updateNowPlayingInfo];
        return;
    }
    _isTransmitting = transmitting;
    if (transmitting || !_isCapturing) {
        [self applySessionPolicy:NO reason:@"tx-state"];
    }
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
    runOnMainThreadAsync(^{
        [self stopNowPlayingTimer];

        self->_nowPlayingTimer = [NSTimer timerWithTimeInterval:15.0
                                                         target:self
                                                       selector:@selector(nowPlayingTimerFired)
                                                       userInfo:nil
                                                        repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:self->_nowPlayingTimer forMode:NSRunLoopCommonModes];
    });
}

- (void)stopNowPlayingTimer {
    if (_nowPlayingTimer) {
        [_nowPlayingTimer invalidate];
        _nowPlayingTimer = nil;
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
    runOnMainThreadAsync(^{
        [self stopBackgroundTask];

        self->_bgTask = [[UIApplication sharedApplication] beginBackgroundTaskWithName:@"DroidStarAudio"
                                                                      expirationHandler:^{
            runOnMainThreadAsync(^{
                NSLog(@"[AudioSessionManager] Background task expiring - renewing");
                if (self->_bgTask != UIBackgroundTaskInvalid) {
                    [[UIApplication sharedApplication] endBackgroundTask:self->_bgTask];
                    self->_bgTask = UIBackgroundTaskInvalid;
                }
                if (self->_isConnected) {
                    [self startBackgroundTask];
                }
            });
        }];
    });
}

- (void)stopBackgroundTask {
    runOnMainThreadAsync(^{
        if (self->_bgTask != UIBackgroundTaskInvalid) {
            [[UIApplication sharedApplication] endBackgroundTask:self->_bgTask];
            self->_bgTask = UIBackgroundTaskInvalid;
        }
    });
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

        // setActive: is a synchronous XPC call into mediaserverd (tens to hundreds of ms after
        dispatch_async(_sessionQueue, ^{
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;
            BOOL success = [session setActive:YES error:&error];

            if (success) {
                self->_isAudioSessionActive = YES;
                [self applySessionPolicy:YES reason:@"interruption"];
                if (self->_isConnected) {
                    [self startKeepAliveAudio];
                }
            } else {
                NSLog(@"[AudioSessionManager] Reactivation after interruption failed: %@", error.localizedDescription);
                [self setupAVAudioSession];
            }
        });
    }
}

- (void)handleAudioRouteChange:(NSNotification *)notification {
    AVAudioSessionRouteChangeReason reason = (AVAudioSessionRouteChangeReason)
        [notification.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];

    switch (reason) {
        case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
        case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
        case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
            _lastRouteConfigTime = 0;
            [self applySessionPolicy:YES reason:@"hardware-route-change"];
            break;

        case AVAudioSessionRouteChangeReasonCategoryChange:
        case AVAudioSessionRouteChangeReasonOverride: {
            NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
            if (_isConfiguringSession || (now - _lastSelfCategoryChangeTime) < 0.35) {
                break;
            }
            AVAudioSession *session = [AVAudioSession sharedInstance];
            if ([self isCategoryClobbered:session]) {
                _lastRouteConfigTime = 0;
                [self configureAudioRoute:session];
            }
            break;
        }

        default:
            break;
    }
}

- (void)handleMediaServicesWereReset:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] Media services reset - reinitializing");
    _isAudioSessionActive = NO;
    _appliedPolicy = DSAudioSessionPolicyUnknown;
    [self stopKeepAliveAudio];
    [self setupRemoteCommandCenter];
    [self setupAVAudioSession];
}

- (void)handleAppDidEnterBackground:(NSNotification *)notification {
    g_appInBackground.store(true, std::memory_order_relaxed);
    NSLog(@"[AudioSessionManager] App entered background");
    [self setupBackgroundAudio];
    [self updateNowPlayingInfo];
}

- (void)handleAppWillEnterForeground:(NSNotification *)notification {
    g_appInBackground.store(false, std::memory_order_relaxed);
    NSLog(@"[AudioSessionManager] App entering foreground");
    if (_isConnected) {
        [self applySessionPolicy:YES reason:@"foreground"];
    }
}

- (void)handleAppDidBecomeActive:(NSNotification *)notification {
    g_appInBackground.store(false, std::memory_order_relaxed);
    NSLog(@"[AudioSessionManager] App became active");
    if (_isConnected) {
        [self updateNowPlayingInfo];
    }
}

- (void)handleAppWillResignActive:(NSNotification *)notification {
    NSLog(@"[AudioSessionManager] App will resign active");
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
    [[AudioSessionManager sharedManager] deactivateSession];
}

extern "C" void setupBackgroundAudio() {
    [[AudioSessionManager sharedManager] setupBackgroundAudio];
}

extern "C" void handleAppEnteringForeground() {
    AudioSessionManager *mgr = [AudioSessionManager sharedManager];
    [mgr configureAudioRoute:[AVAudioSession sharedInstance]];
}

extern "C" void clearAudioBuffer() {
    NSLog(@"[AudioSessionManager] Clear audio buffer requested");
}

extern "C" void setPreferredInputDevice() {
    [[AudioSessionManager sharedManager] setPreferredInputDevice];
}

extern "C" void prepareAudioForCapture() {
    [[AudioSessionManager sharedManager] prepareForCapture];
}

extern "C" void finishAudioCapture() {
    [[AudioSessionManager sharedManager] finishCapture];
}

extern "C" bool isAppInBackground() {
    return g_appInBackground.load(std::memory_order_relaxed);
}

extern "C" double getActiveAudioSampleRate() {
    double cached = g_activeSampleRate.load(std::memory_order_relaxed);
    if (cached >= 8000.0) {
        return cached;
    }
    double sr = [AVAudioSession sharedInstance].sampleRate;
    if (sr >= 8000.0) {
        g_activeSampleRate.store(sr, std::memory_order_relaxed);
        return sr;
    }
    return 48000.0;
}

extern "C" uint32_t getAudioRouteGeneration() {
    return g_audioRouteGeneration.load(std::memory_order_relaxed);
}

extern "C" bool isIOSExternalMicActive() {
    return g_iosExternalMicActive.load(std::memory_order_relaxed);
}

extern "C" void setAppleVoiceIsolationEnabled(bool enabled) {
    g_appleVoiceIsolationEnabled.store(enabled, std::memory_order_relaxed);
    runOnMainThreadAsync(^{
        [[AudioSessionManager sharedManager] setPreferredInputDevice];
    });
}

extern "C" bool isAppleVoiceIsolationEnabled() {
    return g_appleVoiceIsolationEnabled.load(std::memory_order_relaxed);
}

extern "C" void deactivateBackgroundAudio() {
    [[AudioSessionManager sharedManager] stopKeepAliveAudio];
    deactivateAVAudioSession();
}

extern "C" void renewBackgroundTask() {
    [[AudioSessionManager sharedManager] startBackgroundTask];
}

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

extern "C" void ensureSpeakerRoute() {
    [[AudioSessionManager sharedManager] configureAudioRoute:[AVAudioSession sharedInstance]];
}

extern "C" void setPTTCallbacks(void (*pressCallback)(void), void (*releaseCallback)(void)) {
    g_pttPressCallback = pressCallback;
    g_pttReleaseCallback = releaseCallback;
    NSLog(@"[AudioSessionManager] PTT callbacks registered");
}

extern "C" bool getSafeAreaInsets(double *top, double *left, double *bottom, double *right) {
    UIWindow *window = nil;
    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        for (UIWindow *w in ((UIWindowScene *)scene).windows) {
            if (w.isKeyWindow) { window = w; break; }
        }
        if (!window) window = ((UIWindowScene *)scene).windows.firstObject;
        if (window) break;
    }
    if (!window) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        window = [UIApplication sharedApplication].keyWindow;
        if (!window) window = [UIApplication sharedApplication].windows.firstObject;
#pragma clang diagnostic pop
    }
    if (!window) return false;

    UIEdgeInsets insets = window.safeAreaInsets;
    if (top) *top = insets.top;
    if (left) *left = insets.left;
    if (bottom) *bottom = insets.bottom;
    if (right) *right = insets.right;
    return true;
}

extern "C" void getIOSBundleVersion(char *shortVerBuf, int shortVerLen, char *buildVerBuf, int buildVerLen) {
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *shortVer = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"";
    NSString *buildVer = [bundle objectForInfoDictionaryKey:@"CFBundleVersion"] ?: @"";
    if (shortVerBuf && shortVerLen > 0) {
        const char *s = [shortVer UTF8String];
        strncpy(shortVerBuf, s ? s : "", static_cast<size_t>(shortVerLen - 1));
        shortVerBuf[shortVerLen - 1] = '\0';
    }
    if (buildVerBuf && buildVerLen > 0) {
        const char *b = [buildVer UTF8String];
        strncpy(buildVerBuf, b ? b : "", static_cast<size_t>(buildVerLen - 1));
        buildVerBuf[buildVerLen - 1] = '\0';
    }
}
