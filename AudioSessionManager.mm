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
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <atomic>
#include "droidstar.h"
#include "AudioSessionManager.h"

// Forward declarations for callbacks
extern "C" void callProcessConnect();
extern "C" void clearAudioBuffer();

// Callback function pointers for PTT control from remote commands
static std::atomic<DSMediaButtonCallback> g_mediaButtonCallback{nullptr};
static std::atomic<bool> g_mediaTrackButtonsEnabled{false};

// Thread-safe state for cross-thread queries from AudioEngine (m_modethread)
static std::atomic<bool> g_appInBackground{false};
static std::atomic<uint32_t> g_audioRouteGeneration{1};
static std::atomic<uint32_t> g_audioIOInvalidation{0};
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
    NSTimeInterval _mediaCommandSuppressUntil;   // earbuds send play/pause on connect, wear and wake
    BOOL _isConfiguringSession;
    BOOL _speakerOverrideApplied;
    dispatch_queue_t _sessionQueue;
    DSAudioSessionPolicy _appliedPolicy;
    NSString *_lastRouteSignature;
    NSTimeInterval _lastPreferredInputAttemptTime;
    NSString *_lastPreferredInputUID;

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

#pragma mark - Session policy constants

// Single source of truth for the category options the whole app runs with. One PlayAndRecord
// session stays up across PTT so the capture source and playback sink are never torn down on key-up.
//  - AllowBluetooth (HFP) carries the earphone microphone. Once the HFP input is the preferred input,
//    iOS moves the output onto the same HFP link, so TX and RX both use the earphone.
//  - AllowBluetoothA2DP keeps RX on a Bluetooth device that only offers A2DP to this session, or whose
//    HFP link is still being brought up, instead of dropping to the handset speaker. When a device
//    offers both profiles iOS gives the HFP ports priority.
//  - DefaultToSpeaker only changes the no-accessory default from the earpiece to the loudspeaker.
static inline AVAudioSessionCategoryOptions ds_enforcedCategoryOptions(void) {
#if TARGET_OS_SIMULATOR
    return AVAudioSessionCategoryOptionDefaultToSpeaker;
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return (AVAudioSessionCategoryOptionDefaultToSpeaker |
            AVAudioSessionCategoryOptionAllowBluetooth |
            AVAudioSessionCategoryOptionAllowBluetoothA2DP);
#pragma clang diagnostic pop
#endif
}

static inline BOOL ds_isBluetoothInputPort(NSString *pt) {
    return [pt isEqualToString:AVAudioSessionPortBluetoothHFP] ||
           [pt isEqualToString:AVAudioSessionPortBluetoothLE];
}

static inline BOOL ds_isWiredInputPort(NSString *pt) {
    return [pt isEqualToString:AVAudioSessionPortHeadsetMic] ||
           [pt isEqualToString:AVAudioSessionPortUSBAudio] ||
           [pt isEqualToString:AVAudioSessionPortCarAudio] ||
           [pt isEqualToString:AVAudioSessionPortLineIn];
}

static inline BOOL ds_isExternalInputPort(NSString *pt) {
    return ds_isBluetoothInputPort(pt) || ds_isWiredInputPort(pt);
}

static inline BOOL ds_isExternalOutputPort(NSString *pt) {
    return [pt isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
           [pt isEqualToString:AVAudioSessionPortBluetoothHFP] ||
           [pt isEqualToString:AVAudioSessionPortBluetoothLE] ||
           [pt isEqualToString:AVAudioSessionPortHeadphones] ||
           [pt isEqualToString:AVAudioSessionPortCarAudio] ||
           [pt isEqualToString:AVAudioSessionPortUSBAudio] ||
           [pt isEqualToString:AVAudioSessionPortLineOut] ||
           [pt isEqualToString:AVAudioSessionPortAirPlay];
}

static void *kDSSessionQueueKey = &kDSSessionQueueKey;

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
    // QDarwinAudioSource::open() calls setCategory(PlayAndRecord, MixWithOthers) right before
    // AudioUnitInitialize. Either strips the Bluetooth options and throws the microphone off the
    // Bluetooth HFP link back onto the built-in mic. Every category request, ours included, lands on
    // the one option set the app runs with.
    (void)category;
    (void)options;
    const AVAudioSessionCategoryOptions enforcedOpts = ds_enforcedCategoryOptions();
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
        _mediaCommandSuppressUntil = 0;
        _isConfiguringSession = NO;
        _speakerOverrideApplied = NO;
        dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0);
        _sessionQueue = dispatch_queue_create("org.dudetronics.droidstar.audiosession", attr);
        dispatch_queue_set_specific(_sessionQueue, kDSSessionQueueKey, kDSSessionQueueKey, NULL);
        _lastRouteSignature = nil;
        _lastPreferredInputAttemptTime = 0;
        _lastPreferredInputUID = nil;
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
            // Becoming the Now Playing app makes some headsets send an AVRCP play right away.
            [self suppressMediaCommandsFor:3.0 reason:@"now playing started"];
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

#pragma mark - Session capabilities

- (BOOL)isFullDuplexMode {
    return [_currentMode isEqualToString:@"IAX"];
}

- (BOOL)hasExternalAudioDevice:(AVAudioSession *)session {
    if (!session) {
        session = [AVAudioSession sharedInstance];
    }
    for (AVAudioSessionPortDescription *input in session.availableInputs) {
        if (ds_isExternalInputPort(input.portType)) {
            return YES;
        }
    }
    AVAudioSessionRouteDescription *route = session.currentRoute;
    for (AVAudioSessionPortDescription *output in route.outputs) {
        if (ds_isExternalOutputPort(output.portType)) {
            return YES;
        }
    }
    for (AVAudioSessionPortDescription *input in route.inputs) {
        if (ds_isExternalInputPort(input.portType)) {
            return YES;
        }
    }
    return NO;
}

- (DSAudioSessionPolicy)desiredSessionPolicy {
    // One Duplex session (PlayAndRecord) for the life of the app: no category flips on PTT, so no
    // route switches, mic array overrides or category races between threads.
    return DSAudioSessionPolicyDuplex;
}

- (AVAudioSessionCategoryOptions)desiredDuplexCategoryOptions {
    return ds_enforcedCategoryOptions();
}

- (BOOL)isCategoryClobbered:(AVAudioSession *)session {
    if (![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord]) {
        return YES;
    }
#if !TARGET_OS_SIMULATOR
    const AVAudioSessionCategoryOptions desiredOpts = ds_enforcedCategoryOptions();
    if ((session.categoryOptions & desiredOpts) != desiredOpts) {
        return YES;
    }
#endif
    if (![session.mode isEqualToString:AVAudioSessionModeDefault]) {
        return YES;
    }
    return NO;
}

#pragma mark - Session queue helpers

// Everything that mutates AVAudioSession runs on one serial queue, in order. The sync variant is
// for PTT, where the caller is about to open the capture source and must see the final route.
- (void)onSessionQueueSync:(dispatch_block_t)block {
    if (dispatch_get_specific(kDSSessionQueueKey) == kDSSessionQueueKey) {
        block();
    } else {
        dispatch_sync(_sessionQueue, block);
    }
}

- (void)onSessionQueueAsync:(dispatch_block_t)block {
    if (dispatch_get_specific(kDSSessionQueueKey) == kDSSessionQueueKey) {
        block();
    } else {
        dispatch_async(_sessionQueue, block);
    }
}

#pragma mark - Route state published to AudioEngine

- (NSString *)routeSignature:(AVAudioSession *)session {
    NSMutableString *sig = [NSMutableString string];
    AVAudioSessionRouteDescription *route = session.currentRoute;
    for (AVAudioSessionPortDescription *input in route.inputs) {
        [sig appendFormat:@"i:%@;", input.UID ?: input.portType];
    }
    for (AVAudioSessionPortDescription *output in route.outputs) {
        [sig appendFormat:@"o:%@;", output.UID ?: output.portType];
    }
    [sig appendFormat:@"sr:%.0f", session.sampleRate];
    return sig;
}

// The engine (mode thread) never talks to AVAudioSession directly. It reads three things from here:
// the hardware sample rate, whether an external microphone is in the route, and a route generation
// counter. The counter only moves when the real route (ports or rate) changed, and that is what makes
// AudioEngine rebuild its QAudioSource / QAudioSink on the new hardware instead of on every policy pass.
- (void)publishRouteState:(AVAudioSession *)session reason:(NSString *)reason {
    const double rate = session.sampleRate;
    if (rate >= 8000.0) {
        g_activeSampleRate.store(rate, std::memory_order_relaxed);
    }

    BOOL externalMic = NO;
    for (AVAudioSessionPortDescription *input in session.currentRoute.inputs) {
        if (ds_isExternalInputPort(input.portType)) {
            externalMic = YES;
            break;
        }
    }
    g_iosExternalMicActive.store(externalMic, std::memory_order_relaxed);
    AudioEngine::setExternalMicActive(externalMic);

    NSString *sig = [self routeSignature:session];
    if (_lastRouteSignature == nil || ![sig isEqualToString:_lastRouteSignature]) {
        _lastRouteSignature = sig;
        g_audioRouteGeneration.fetch_add(1, std::memory_order_relaxed);
        NSLog(@"[AudioSessionManager] Route changed (%@): %@", reason, sig);
    }
}

#pragma mark - Audio Session Setup & Bluetooth Routing

- (AVAudioSessionPortDescription *)preferredExternalInput:(AVAudioSession *)session {
    NSArray<AVAudioSessionPortDescription *> *inputs = session.availableInputs;
    for (AVAudioSessionPortDescription *input in inputs) {
        if (ds_isBluetoothInputPort(input.portType)) {
            return input;
        }
    }
    for (AVAudioSessionPortDescription *input in inputs) {
        if (ds_isWiredInputPort(input.portType)) {
            return input;
        }
    }
    return nil;
}

// Idempotent. Runs on the session queue.
- (void)applyPolicyOnQueue:(BOOL)force reason:(NSString *)reason {
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    const AVAudioSessionCategoryOptions opts = ds_enforcedCategoryOptions();
    const BOOL clobbered = [self isCategoryClobbered:session];
    const BOOL reconfigure = force || clobbered || !_isAudioSessionActive ||
                             (_appliedPolicy != DSAudioSessionPolicyDuplex);

    if (reconfigure) {
        _lastSelfCategoryChangeTime = [NSProcessInfo processInfo].systemUptime;
        if (![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord] ||
            ((session.categoryOptions & opts) != opts)) {
            if (![session setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:opts error:&error]) {
                NSLog(@"[AudioSessionManager] setCategory failed (%@): %@", reason, error.localizedDescription);
            }
        }
        if (![session.mode isEqualToString:AVAudioSessionModeDefault]) {
            error = nil;
            [session setMode:AVAudioSessionModeDefault error:&error];
        }
        if ([self preferredExternalInput:session] == nil) {
            // Handset hardware: ask for 48 kHz so the mixer never down-switches. Bluetooth / wired
            // devices dictate their own rate and must not be fought.
            if (fabs(session.preferredSampleRate - 48000.0) > 1.0) {
                [session setPreferredSampleRate:48000.0 error:nil];
            }
        }
        if (fabs(session.preferredIOBufferDuration - 0.02) > 0.0005) {
            [session setPreferredIOBufferDuration:0.02 error:nil];
        }
    }

    // Activation is never gated on our own flag alone: interruptions and media-server resets drop
    // it without telling us in time, and setActive:YES on an already active session is a no-op.
    if (reconfigure || !_isAudioSessionActive) {
        error = nil;
        if ([session setActive:YES error:&error]) {
            _isAudioSessionActive = YES;
            _appliedPolicy = DSAudioSessionPolicyDuplex;
        } else {
            _isAudioSessionActive = NO;
            NSLog(@"[AudioSessionManager] setActive failed (%@): %@", reason, error.localizedDescription);
        }
    }

    [self applyPreferredInputOnQueue:session];
    [self applyOutputOverrideOnQueue:session];
    [self publishRouteState:session reason:reason];
}

// Bluetooth HFP / LE first, then wired / USB / car / line-in, then the handset's bottom microphone.
// Selecting an HFP input is what makes iOS move the output onto the same HFP link.
- (void)applyPreferredInputOnQueue:(AVAudioSession *)session {
#if TARGET_OS_SIMULATOR
    (void)session;
    return;
#else
    AVAudioSessionPortDescription *target = [self preferredExternalInput:session];
    AVAudioSessionPortDescription *currentInput = session.currentRoute.inputs.firstObject;

    if (target != nil) {
        const BOOL preferredOk = [session.preferredInput.UID isEqualToString:target.UID];
        const BOOL routedOk = [currentInput.UID isEqualToString:target.UID];
        if (preferredOk && routedOk) {
            return;
        }
        // Re-asserting the same port more often than once a second can only make iOS flap the route.
        const NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
        if (preferredOk && [_lastPreferredInputUID isEqualToString:target.UID] &&
            (now - _lastPreferredInputAttemptTime) < 1.0) {
            return;
        }
        // A loudspeaker override pins the output and stops iOS from bringing the HFP link up for
        // the input, so lift it before asking for the Bluetooth microphone.
        if (_speakerOverrideApplied) {
            [session overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
            _speakerOverrideApplied = NO;
        }
        _lastSelfCategoryChangeTime = now;
        _lastPreferredInputAttemptTime = now;
        _lastPreferredInputUID = [target.UID copy];
        NSError *error = nil;
        if ([session setPreferredInput:target error:&error]) {
            NSLog(@"[AudioSessionManager] Preferred input -> %@ (%@)", target.portName, target.portType);
        } else {
            NSLog(@"[AudioSessionManager] setPreferredInput(%@) failed: %@", target.portName, error.localizedDescription);
        }
        return;
    }

    // No external microphone. Release a stale preference (iOS clears it when the device disappears,
    // but not when the user turns the headset off in Control Center) and use the bottom mic.
    if (session.preferredInput != nil) {
        [session setPreferredInput:nil error:nil];
    }
    _lastPreferredInputUID = nil;
    for (AVAudioSessionPortDescription *input in session.availableInputs) {
        if (![input.portType isEqualToString:AVAudioSessionPortBuiltInMic]) {
            continue;
        }
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
#endif
}

// DefaultToSpeaker already keeps handset playback on the loudspeaker. The override is only needed
// when the category was clobbered and the output landed on the earpiece, and it must be lifted the
// moment any external device is present, otherwise it pins the audio to the handset.
- (void)applyOutputOverrideOnQueue:(AVAudioSession *)session {
#if TARGET_OS_SIMULATOR
    (void)session;
    return;
#else
    AVAudioSessionRouteDescription *route = session.currentRoute;
    BOOL external = NO;
    BOOL onReceiver = NO;
    for (AVAudioSessionPortDescription *output in route.outputs) {
        if (ds_isExternalOutputPort(output.portType)) {
            external = YES;
        } else if ([output.portType isEqualToString:AVAudioSessionPortBuiltInReceiver]) {
            onReceiver = YES;
        }
    }
    for (AVAudioSessionPortDescription *input in route.inputs) {
        if (ds_isExternalInputPort(input.portType)) {
            external = YES;
        }
    }
    if (!external && [self preferredExternalInput:session] != nil) {
        external = YES;   // an external input is being brought up: never pin the speaker over it
    }

    if (external) {
        if (_speakerOverrideApplied) {
            _lastSelfCategoryChangeTime = [NSProcessInfo processInfo].systemUptime;
            [session overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
            _speakerOverrideApplied = NO;
        }
        return;
    }

    if (onReceiver) {
        const NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
        if ((now - _lastRouteConfigTime) >= 0.50) {
            _lastRouteConfigTime = now;
            _lastSelfCategoryChangeTime = now;
            if ([session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil]) {
                _speakerOverrideApplied = YES;
            }
        }
    }
#endif
}

- (void)applySessionPolicy:(BOOL)force reason:(NSString *)reason {
    [self onSessionQueueAsync:^{
        [self applyPolicyOnQueue:force reason:reason];
    }];
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
    [self onSessionQueueAsync:^{
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
    }];
}

- (void)configureAudioRoute:(AVAudioSession *)session {
    (void)session;
    [self onSessionQueueAsync:^{
        [self applyPolicyOnQueue:NO reason:@"route-check"];
    }];
}

- (void)configureAudioRouteInternal:(AVAudioSession *)session {
    session = [AVAudioSession sharedInstance];
    [self applyPreferredInputOnQueue:session];
    [self applyOutputOverrideOnQueue:session];
    [self publishRouteState:session reason:@"route"];
}

- (void)setPreferredInputDevice {
    [self onSessionQueueAsync:^{
        [self applyPolicyOnQueue:NO reason:@"preferred-input"];
    }];
}

- (void)prepareForCapture {
    _isCapturing = YES;
    _isTransmitting = YES;
    // Called from AudioEngine::start_capture() on the mode thread right before QAudioSource::start().
    // Run the policy synchronously so the capture source opens on the final route and reads the
    // matching route generation and sample rate, instead of racing a change still queued here.
    [self onSessionQueueSync:^{
        [self applyPolicyOnQueue:NO reason:@"ptt"];
    }];
}

- (void)finishCapture {
    _isCapturing = NO;
    _isTransmitting = NO;
}

- (void)setPreferredInputDeviceInternal {
    [self applyPreferredInputOnQueue:[AVAudioSession sharedInstance]];
}

#pragma mark - Remote Command Center (Control Center / Lock Screen / Bluetooth Earbuds)

// Headsets emit AVRCP play/pause on their own when they connect, wake from sleep, become the
// Now Playing route, or detect being worn or removed. iOS delivers those exactly like a button
// press, so media commands are ignored for a short settling window after such events. Our own
// policy changes (category/override) are not covered, so a real press right after keying TX works.
- (void)suppressMediaCommandsFor:(NSTimeInterval)seconds reason:(NSString *)reason {
    NSTimeInterval until = [NSProcessInfo processInfo].systemUptime + seconds;
    if (until > _mediaCommandSuppressUntil) {
        _mediaCommandSuppressUntil = until;
    }
    NSLog(@"[AudioSessionManager] Media button ignored for %.1fs (%@)", seconds, reason);
}

- (void)handleRemotePttToggle {
    NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
    if (now < _mediaCommandSuppressUntil) {
        NSLog(@"[AudioSessionManager] Media command ignored: route settling (%.1fs left)",
              _mediaCommandSuppressUntil - now);
        return;
    }
    // Bluetooth earbuds can deliver play/pause and togglePlayPause for one physical press.
    if ((now - _lastRemotePttToggleTime) < 0.05) {
        return;
    }
    _lastRemotePttToggleTime = now;

    DSMediaButtonCallback cb = g_mediaButtonCallback.load();
    if (cb) {
        cb(0);
    }
}

- (void)setupRemoteCommandCenter {
    MPRemoteCommandCenter *commandCenter = [MPRemoteCommandCenter sharedCommandCenter];
    // Re-run after a media-services reset: drop the previous handlers or every press fires N times.
    [commandCenter.playCommand removeTarget:nil];
    [commandCenter.pauseCommand removeTarget:nil];
    [commandCenter.togglePlayPauseCommand removeTarget:nil];
    [commandCenter.stopCommand removeTarget:nil];
    [commandCenter.nextTrackCommand removeTarget:nil];
    [commandCenter.previousTrackCommand removeTarget:nil];

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

    // Stop command: always a release/safety stop
    commandCenter.stopCommand.enabled = YES;
    [commandCenter.stopCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote stop command received");
        DSMediaButtonCallback cb = g_mediaButtonCallback.load();
        if (cb) {
            cb(1);
        }
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    // Next/previous track: optional press sources (steering wheel buttons, headset skip keys)
    const BOOL track = g_mediaTrackButtonsEnabled.load();
    commandCenter.nextTrackCommand.enabled = track;
    [commandCenter.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote next track received - PTT press");
        [self handleRemotePttToggle];
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    commandCenter.previousTrackCommand.enabled = track;
    [commandCenter.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        NSLog(@"[AudioSessionManager] Remote previous track received - PTT press");
        [self handleRemotePttToggle];
        return MPRemoteCommandHandlerStatusSuccess;
    }];
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
    // Called for every RX packet (50 Hz): writing MPNowPlayingInfoCenter each time is an XPC on the
    // main thread. Only push when something actually changed.
    NSString *cs = callsign ?: @"";
    NSString *nm = name ?: @"";
    NSString *co = country ?: @"";
    if ([_currentCallsign isEqualToString:cs] && [_currentName isEqualToString:nm] && [_currentCountry isEqualToString:co]) {
        return;
    }
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
        // The system stops our I/O units: make the engine rebuild its sink/source on next use
        // instead of stopping units that will never call back again.
        g_audioIOInvalidation.fetch_add(1, std::memory_order_relaxed);
        g_audioRouteGeneration.fetch_add(1, std::memory_order_relaxed);
        [self stopKeepAliveAudio];
    } else if (interruptionType == AVAudioSessionInterruptionTypeEnded) {
        NSLog(@"[AudioSessionManager] Audio interruption ended");

        // setActive: is a synchronous XPC call into mediaserverd (tens to hundreds of ms after a
        // phone call / Siri). Notifications arrive on the main thread, so do the reactivation on the
        // session queue like every other activate/deactivate in this file, then resume the policy
        // and keep-alive from there.
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
    AVAudioSessionRouteDescription *previous = notification.userInfo[AVAudioSessionRouteChangePreviousRouteKey];
    NSLog(@"[AudioSessionManager] Route change reason=%lu previous=%@ now=%@",
          (unsigned long)reason,
          previous.outputs.firstObject.portType ?: @"-",
          [AVAudioSession sharedInstance].currentRoute.outputs.firstObject.portType ?: @"-");

    if (reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable ||
        reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable ||
        reason == AVAudioSessionRouteChangeReasonWakeFromSleep) {
        [self suppressMediaCommandsFor:2.5 reason:@"hardware route change"];
    }

    switch (reason) {
        case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
        case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
        case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
        case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory:
        case AVAudioSessionRouteChangeReasonWakeFromSleep:
        case AVAudioSessionRouteChangeReasonUnknown:
            // Hardware came or went (earphones connected / removed, HFP link up): re-run the whole
            // policy so the preferred input follows the hardware and the engine learns the new rate.
            _lastRouteConfigTime = 0;
            [self applySessionPolicy:YES reason:@"hardware-route-change"];
            break;

        case AVAudioSessionRouteChangeReasonCategoryChange:
        case AVAudioSessionRouteChangeReasonOverride: {
            // Our own category / override / preferred-input calls land here as well. Only repair the
            // session when somebody else changed it, but always publish the resulting route so the
            // engine follows a Bluetooth link that came up asynchronously after setPreferredInput.
            const NSTimeInterval now = [NSProcessInfo processInfo].systemUptime;
            const BOOL selfInduced = (now - _lastSelfCategoryChangeTime) < 0.35;
            [self onSessionQueueAsync:^{
                AVAudioSession *session = [AVAudioSession sharedInstance];
                if (!selfInduced && [self isCategoryClobbered:session]) {
                    self->_lastRouteConfigTime = 0;
                    [self applyPolicyOnQueue:YES reason:@"category-clobbered"];
                } else {
                    [self applyPreferredInputOnQueue:session];
                    [self applyOutputOverrideOnQueue:session];
                    [self publishRouteState:session reason:@"route-update"];
                }
            }];
            break;
        }

        default:
            [self onSessionQueueAsync:^{
                [self publishRouteState:[AVAudioSession sharedInstance] reason:@"route-update"];
            }];
            break;
    }
}

- (void)handleMediaServicesWereReset:(NSNotification *)notification {
    [self suppressMediaCommandsFor:3.0 reason:@"media services reset"];
    NSLog(@"[AudioSessionManager] Media services reset - reinitializing");
    _isAudioSessionActive = NO;
    _appliedPolicy = DSAudioSessionPolicyUnknown;
    g_audioIOInvalidation.fetch_add(1, std::memory_order_relaxed);   // every audio unit is dead now
    g_audioRouteGeneration.fetch_add(1, std::memory_order_relaxed);
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

extern "C" void setMediaButtonCallback(DSMediaButtonCallback callback) {
    g_mediaButtonCallback.store(callback);
    NSLog(@"[AudioSessionManager] Media button callback %@", callback ? @"registered" : @"cleared");
}

extern "C" void setMediaTrackButtonsEnabled(bool enabled) {
    g_mediaTrackButtonsEnabled.store(enabled);
    runOnMainThreadAsync(^{
        MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
        cc.nextTrackCommand.enabled = enabled;
        cc.previousTrackCommand.enabled = enabled;
    });
}

extern "C" bool ios_metal_supports_family7(void) {
    // Called from the transcription worker and the GUI thread: compute once, thread-safely.
    static bool value = false;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        value = dev && [dev respondsToSelector:@selector(supportsFamily:)] && [dev supportsFamily:MTLGPUFamilyApple7];
    });
    return value;
}

extern "C" uint32_t getAudioIOInvalidationGeneration(void) {
    return g_audioIOInvalidation.load(std::memory_order_relaxed);
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
