/*
    Copyright (C) 2026 Rohith Namboothiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "ios_speech_recognizer.h"
#include <QtGlobal>

#if defined(Q_OS_IOS) && !defined(Q_OS_IOS_APP_EXTENSION)

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>

static DSSpeechPartialResultCallback g_resultCallback = nullptr;
static DSSpeechStatusCallback g_statusCallback = nullptr;
static DSSpeechErrorCallback g_errorCallback = nullptr;

// Apple caps a single buffer request at about one minute of audio
static const size_t kSegmentRotateSamples = 16000 * 48;
static const int kMaxSilentSegmentRestarts = 3;

static BOOL DSIsNoSpeechError(NSError *error)
{
    return [error.domain isEqualToString:@"kAFAssistantErrorDomain"] && error.code == 1110;
}

static NSString *DSFriendlySpeechError(NSError *error)
{
    if ([error.domain isEqualToString:@"kLSRErrorDomain"]) {
        if (error.code == 201) {
            return @"Dictation is off in iOS Settings";
        }
        if (error.code == 102) {
            return @"on-device speech model not downloaded";
        }
    }
    NSString *desc = error.localizedDescription;
    if (desc.length > 0) {
        return desc;
    }
    return [NSString stringWithFormat:@"%@ error %ld", error.domain ?: @"Speech", (long)error.code];
}

@interface DSSpeechController : NSObject {
    dispatch_queue_t _queue;
    NSOperationQueue *_callbackQueue;
    SFSpeechRecognizer *_recognizer;
    SFSpeechAudioBufferRecognitionRequest *_request;
    SFSpeechRecognitionTask *_task;
    AVAudioFormat *_format16k;
    int _activeTurnId;
    uint64_t _segmentGeneration;
    BOOL _turnEnded;
    BOOL _allowServerFallback;
    int _silentRestarts;
    size_t _samplesInCurrentSegment;
    NSString *_committedPrefix;
    NSString *_lastSegmentText;
    NSString *_lastPublishedCombined;
    NSString *_currentLangCode;
    NSString *_currentCallsign;
    NSString *_currentUserCallsign;
    NSString *_lastStartError;
}

+ (instancetype)shared;
- (void)requestPermission;
- (BOOL)isAuthorized;
- (BOOL)startTurn:(int)turnId lang:(NSString *)lang callsign:(NSString *)callsign userCall:(NSString *)userCall;
- (void)appendSamples16k:(const float *)samples count:(size_t)count;
- (void)endTurn;
- (void)cancelTurn;

@end

@implementation DSSpeechController

+ (instancetype)shared {
    static DSSpeechController *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[DSSpeechController alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _queue = dispatch_queue_create("org.dudetronics.droidstar.speech", DISPATCH_QUEUE_SERIAL);
        _callbackQueue = [[NSOperationQueue alloc] init];
        _callbackQueue.maxConcurrentOperationCount = 1;
        _callbackQueue.underlyingQueue = _queue;
        _format16k = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                      sampleRate:16000.0
                                                        channels:1
                                                     interleaved:NO];
        _activeTurnId = 0;
        _segmentGeneration = 0;
        _turnEnded = NO;
        _allowServerFallback = NO;
        _silentRestarts = 0;
        _samplesInCurrentSegment = 0;
        _committedPrefix = @"";
        _lastSegmentText = @"";
        _lastPublishedCombined = @"";
        _currentLangCode = @"en";
        _currentCallsign = @"";
        _currentUserCallsign = @"";
        _lastStartError = @"";
    }
    return self;
}

- (BOOL)isAuthorized {
    return ([SFSpeechRecognizer authorizationStatus] == SFSpeechRecognizerAuthorizationStatusAuthorized);
}

- (void)requestPermission {
    const SFSpeechRecognizerAuthorizationStatus cur = [SFSpeechRecognizer authorizationStatus];
    if (cur == SFSpeechRecognizerAuthorizationStatusAuthorized) {
        if (g_statusCallback) {
            g_statusCallback(true, true, "Apple Neural Ready");
        }
        return;
    }
    if (cur == SFSpeechRecognizerAuthorizationStatusDenied || cur == SFSpeechRecognizerAuthorizationStatusRestricted) {
        if (g_statusCallback) {
            g_statusCallback(false, false, "Speech recognition access denied");
        }
        return;
    }

    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        const bool granted = (status == SFSpeechRecognizerAuthorizationStatusAuthorized);
        if (g_statusCallback) {
            g_statusCallback(granted, granted, granted ? "Apple Neural Ready" : "Speech recognition access denied");
        }
    }];
}

+ (NSString *)localeIdentifierForLang:(NSString *)lang {
    if (!lang || lang.length == 0 || [lang isEqualToString:@"auto"]) {
        static NSString *cachedAutoLocale = nil;
        static dispatch_once_t autoOnce;
        dispatch_once(&autoOnce, ^{
            NSString *sysLocale = [[[NSLocale currentLocale] localeIdentifier] stringByReplacingOccurrencesOfString:@"_" withString:@"-"];
            if (sysLocale.length > 0) {
                NSLocale *loc = [NSLocale localeWithLocaleIdentifier:sysLocale];
                if ([[SFSpeechRecognizer supportedLocales] containsObject:loc]) {
                    cachedAutoLocale = sysLocale;
                } else {
                    NSString *langPrefix = [[sysLocale componentsSeparatedByString:@"-"] firstObject];
                    if (langPrefix.length > 0 && ![langPrefix isEqualToString:@"auto"]) {
                        cachedAutoLocale = [self localeIdentifierForLang:langPrefix];
                    }
                }
            }
            if (!cachedAutoLocale || cachedAutoLocale.length == 0) {
                cachedAutoLocale = @"en-US";
            }
        });
        return cachedAutoLocale;
    }
    if ([lang isEqualToString:@"en"]) {
        NSString *sysLocale = [[[NSLocale currentLocale] localeIdentifier] stringByReplacingOccurrencesOfString:@"_" withString:@"-"];
        if ([sysLocale hasPrefix:@"en-"]) {
            return sysLocale;
        }
        return @"en-US";
    }
    static NSDictionary<NSString *, NSString *> *map = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        map = @{
            @"es": @"es-ES",
            @"de": @"de-DE",
            @"fr": @"fr-FR",
            @"ja": @"ja-JP",
            @"it": @"it-IT",
            @"pt": @"pt-BR",
            @"ru": @"ru-RU",
            @"zh": @"zh-CN",
            @"hi": @"hi-IN",
            @"ar": @"ar-SA",
            @"nl": @"nl-NL",
            @"pl": @"pl-PL",
            @"ko": @"ko-KR",
            @"tr": @"tr-TR"
        };
    });
    return map[lang] ?: @"en-US";
}

+ (NSArray<NSString *> *)hamContextualStringsWithCallsign:(NSString *)callsign userCall:(NSString *)userCall {
    NSMutableArray<NSString *> *phrases = [NSMutableArray arrayWithObjects:
        // NATO & Multilingual Phonetics
        @"Alpha", @"Alfa", @"Bravo", @"Charlie", @"Delta", @"Echo", @"Eco", @"Foxtrot", @"Golf", @"Hotel",
        @"India", @"Juliett", @"Juliet", @"Julieta", @"Giulietta", @"Kilo", @"Lima", @"Mike",
        @"November", @"Noviembre", @"Novembre", @"Oscar", @"Papa", @"Quebec", @"Romeo", @"Sierra",
        @"Tango", @"Uniform", @"Uniforme", @"Victor", @"Whiskey", @"Whisky", @"X-ray", @"Yankee", @"Zulu",
        @"Number Zero", @"Number One", @"Number Two", @"Number Three", @"Number Four",
        @"Number Five", @"Number Six", @"Number Seven", @"Number Eight", @"Number Nine",
        @"Niner", @"Stroke Portable", @"Stroke Mobile", @"Stroke QRP",
        // Q-Codes & Prosigns
        @"CQ", @"CQ DX", @"QRZ", @"QSL", @"QSO", @"QTH", @"QRM", @"QRN", @"QRP", @"QRO", @"QSY", @"QSB",
        @"QRT", @"QRV", @"QRX", @"QRL", @"QRS", @"QRQ", @"QST", @"QSP", @"QSA", @"QTR",
        @"73", @"88", @"59", @"59 plus", @"OM", @"YL", @"XYL",
        // Ham Radio Brands & Popular Transceivers
        @"Baofeng", @"UV-5R", @"UV-82", @"BF-F8HP",
        @"TIDRADIO", @"TID Radio", @"TD-H3", @"TD-H8",
        @"Quansheng", @"UV-K5", @"UV-K6", @"egzumer",
        @"AnyTone", @"D878UV", @"D868UV", @"D578UV", @"878", @"578",
        @"Yaesu", @"FT-70D", @"FT-60R", @"FT-65R", @"FT-5DR", @"FT-891", @"FT-991A", @"FT-710", @"FTDX10", @"FTM-400", @"FTM-300", @"FTM-500",
        @"Icom", @"IC-7300", @"IC-705", @"IC-9700", @"IC-7610", @"ID-51", @"ID-52", @"ID-5100",
        @"Kenwood", @"TH-D74", @"TH-D75", @"TS-590", @"TS-890", @"TM-V71", @"TM-D710",
        @"Radioddity", @"GD-77", @"GD-88", @"OpenGD77",
        @"Tytera", @"TYT", @"MD-380", @"MD-UV380", @"MD-9600",
        @"Retevis", @"RT3S", @"Ailunce", @"HD1", @"HD2",
        @"Alinco", @"Wouxun", @"BTECH", @"Elecraft", @"KX2", @"KX3", @"K3", @"K4",
        @"FlexRadio", @"Xiegu", @"G90", @"X6100", @"Motorola", @"MOTOTRBO", @"Hytera", @"BridgeCom",
        @"Nagoya", @"NA-771", @"ABBREE", @"Signal Stick", @"Diamond", @"Comet", @"MFJ", @"Daiwa",
        @"Buddipole", @"Buddistick", @"Wolf River Coil", @"Chameleon", @"Bioenno", @"LiFePO4", @"Powerpole",
        // Antennas, Feedlines & Test Gear
        @"Yagi", @"dipole", @"end-fed", @"EFHW", @"J-pole", @"Slim Jim", @"Hexbeam", @"Moxon",
        @"mag loop", @"magnetic loop", @"inverted V", @"discone", @"collinear", @"ground plane", @"rubber duck",
        @"balun", @"unun", @"choke", @"SWR", @"VSWR", @"NanoVNA", @"tinySA",
        @"coax", @"LMR-400", @"RG-58", @"RG-8X", @"RG-213", @"PL-259", @"SO-239", @"SMA", @"BNC",
        // Digital Voice, Hotspots, Networks & Modes
        @"DroidStar", @"BrandMeister", @"TGIF", @"FreeDMR", @"Pi-Star", @"WPSD",
        @"MMDVM", @"JumboSpot", @"ZUMspot", @"openSPOT", @"SkyBridge", @"ClearNode", @"SHARI",
        @"DVSwitch", @"AllStar", @"EchoLink", @"WIRES-X", @"Peanut",
        @"DMR", @"D-STAR", @"YSF", @"System Fusion", @"C4FM", @"NXDN", @"P25", @"M17",
        @"FT8", @"FT4", @"JS8Call", @"WSJT-X", @"WSPR", @"APRS", @"Winlink", @"SSTV", @"RTTY", @"PSK31",
        @"CW", @"SSB", @"LSB", @"USB", @"FM", @"AM", @"VHF", @"UHF", @"HF",
        @"Talkgroup", @"timeslot", @"color code", @"codeplug", @"CHIRP", @"repeater", @"local repeater", @"linked repeater", @"simplex", @"duplex",
        @"transceiver", @"handheld", @"HT", @"base station", @"mobile rig", @"AMBE", @"vocoder",
        @"CTCSS", @"DCS", @"DTMF", @"PTT", @"AGC", @"squelch",
        @"Net Control", @"Check-in", @"Over", @"Copy", @"Roger",
        @"POTA", @"Parks on the Air", @"SOTA", @"Summits on the Air", @"IOTA", @"Field Day", @"LoTW", @"eQSL", @"Hamvention",
        nil];
    NSMutableArray<NSString *> *priority = [NSMutableArray array];
    for (NSString *call in @[callsign ?: @"", userCall ?: @""]) {
        if (call.length == 0 || [priority containsObject:call]) {
            continue;
        }
        [priority addObject:call];
        NSString *spelled = [DSSpeechController natoSpellingForCallsign:call];
        if (spelled.length > 0) {
            [priority addObject:spelled];
        }
    }
    [phrases insertObjects:priority atIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, priority.count)]];
    return phrases;
}

+ (NSString *)natoSpellingForCallsign:(NSString *)call {
    static NSArray<NSString *> *letters = nil;
    static NSArray<NSString *> *digits = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        letters = @[@"Alpha", @"Bravo", @"Charlie", @"Delta", @"Echo", @"Foxtrot", @"Golf", @"Hotel", @"India",
                    @"Juliett", @"Kilo", @"Lima", @"Mike", @"November", @"Oscar", @"Papa", @"Quebec", @"Romeo",
                    @"Sierra", @"Tango", @"Uniform", @"Victor", @"Whiskey", @"X-ray", @"Yankee", @"Zulu"];
        digits = @[@"Zero", @"One", @"Two", @"Three", @"Four", @"Five", @"Six", @"Seven", @"Eight", @"Nine"];
    });
    NSString *upper = call.uppercaseString;
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    for (NSUInteger i = 0; i < upper.length; ++i) {
        const unichar c = [upper characterAtIndex:i];
        if (c >= 'A' && c <= 'Z') {
            [parts addObject:letters[c - 'A']];
        } else if (c >= '0' && c <= '9') {
            [parts addObject:digits[c - '0']];
        }
    }
    return parts.count >= 3 ? [parts componentsJoinedByString:@" "] : @"";
}

- (NSString *)_combinedTextLocked {
    if (_committedPrefix.length == 0) {
        return _lastSegmentText ?: @"";
    }
    if (_lastSegmentText.length == 0) {
        return _committedPrefix;
    }
    return [NSString stringWithFormat:@"%@ %@", _committedPrefix, _lastSegmentText];
}

- (void)_commitSegmentTextLocked {
    if (_lastSegmentText.length > 0) {
        _committedPrefix = (_committedPrefix.length > 0)
                           ? [NSString stringWithFormat:@"%@ %@", _committedPrefix, _lastSegmentText]
                           : [_lastSegmentText copy];
    }
    _lastSegmentText = @"";
}

- (void)_stopTaskLocked {
    _segmentGeneration++;
    if (_task) {
        [_task cancel];
        _task = nil;
    }
    if (_request) {
        [_request endAudio];
        _request = nil;
    }
}

- (void)_resetTurnLocked {
    [self _stopTaskLocked];
    _activeTurnId = 0;
    _turnEnded = NO;
    _allowServerFallback = NO;
    _silentRestarts = 0;
    _samplesInCurrentSegment = 0;
    _committedPrefix = @"";
    _lastSegmentText = @"";
    _lastPublishedCombined = @"";
}

// Must run on _queue
- (BOOL)_beginRecognitionSegmentLocked {
    [self _stopTaskLocked];

    NSString *localeId = [DSSpeechController localeIdentifierForLang:_currentLangCode];
    if (!_recognizer || ![_recognizer.locale.localeIdentifier isEqualToString:localeId]) {
        _recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:localeId]];
        if (!_recognizer) {
            _recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:@"en-US"]];
        }
        _recognizer.queue = _callbackQueue;
    }

    if (!_recognizer) {
        _lastStartError = [NSString stringWithFormat:@"%@ is not supported", localeId];
        return NO;
    }
    if (!_recognizer.isAvailable) {
        _lastStartError = @"speech recognizer is unavailable";
        return NO;
    }

    _request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    _request.shouldReportPartialResults = YES;
    _request.taskHint = SFSpeechRecognitionTaskHintDictation;
    _request.contextualStrings = [DSSpeechController hamContextualStringsWithCallsign:_currentCallsign
                                                                             userCall:_currentUserCallsign];
#if !TARGET_OS_SIMULATOR
    if (@available(iOS 13.0, *)) {
        if (!_allowServerFallback && _recognizer.supportsOnDeviceRecognition) {
            _request.requiresOnDeviceRecognition = YES;
        }
    }
#endif
    if (@available(iOS 16.0, *)) {
        _request.addsPunctuation = YES;
    }

    _samplesInCurrentSegment = 0;
    _lastSegmentText = @"";

    const int turnId = _activeTurnId;
    const uint64_t generation = _segmentGeneration;
    __weak DSSpeechController *weakSelf = self;
    _task = [_recognizer recognitionTaskWithRequest:_request
                                      resultHandler:^(SFSpeechRecognitionResult * _Nullable result, NSError * _Nullable error) {
        DSSpeechController *strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        NSString *segText = result ? (result.bestTranscription.formattedString ?: @"") : nil;
        const BOOL finished = (result && result.isFinal) || error != nil;
        dispatch_async(strongSelf->_queue, ^{
            [strongSelf _handleSegmentText:segText finished:finished error:error turn:turnId generation:generation];
        });
    }];

    if (!_task) {
        _lastStartError = @"could not start a recognition task";
    }
    return (_task != nil);
}

// Runs on _queue
- (void)_handleSegmentText:(NSString *)segText finished:(BOOL)finished error:(NSError *)error turn:(int)turnId generation:(uint64_t)generation {
    if (turnId != _activeTurnId || generation != _segmentGeneration) {
        return;
    }
    if (segText.length > 0) {
        // SFSpeechRecognizer sometimes resets formattedString to a fresh clause after a mid-over pause without setting isFinal.
        // Detect when a multi-word segment suddenly shrinks to a new disjoint word and commit the previous clause first.
        if (_lastSegmentText.length >= 12 && segText.length * 2 < _lastSegmentText.length) {
            NSString *prevFirst = [[[_lastSegmentText componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] firstObject] lowercaseString] ?: @"";
            NSString *curFirst  = [[[segText componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] firstObject] lowercaseString] ?: @"";
            if (prevFirst.length > 0 && curFirst.length > 0 &&
                ![curFirst hasPrefix:prevFirst] && ![prevFirst hasPrefix:curFirst]) {
                [self _commitSegmentTextLocked];
            }
        }
        _lastSegmentText = segText;
        _silentRestarts = 0;
    }
    NSString *combined = [self _combinedTextLocked];

    if (!finished) {
        if (combined.length > 0 && g_resultCallback && ![combined isEqualToString:_lastPublishedCombined]) {
            _lastPublishedCombined = [combined copy];
            g_resultCallback(turnId, [combined UTF8String], false);
        }
        return;
    }

    if (error) {
        NSLog(@"[DSSpeech] turn %d recognition error %@ %ld: %@", turnId, error.domain, (long)error.code, error.localizedDescription);
    }

    if (_turnEnded) {
        if (g_resultCallback) {
            g_resultCallback(turnId, [combined UTF8String], true);
        }
        [self _resetTurnLocked];
        return;
    }

    // An error before any word was recognized: if on-device failed (e.g. offline pack not downloaded for this locale),
    // retry without requiresOnDeviceRecognition before reporting failure.
    if (error && combined.length == 0 && !DSIsNoSpeechError(error)) {
        if (@available(iOS 13.0, *)) {
            if (_request && _request.requiresOnDeviceRecognition && !_allowServerFallback && _silentRestarts == 0) {
                _silentRestarts++;
                _allowServerFallback = YES;
                if ([self _beginRecognitionSegmentLocked]) {
                    return;
                }
            }
        }
        NSString *reason = DSFriendlySpeechError(error);
        [self _resetTurnLocked];
        if (g_errorCallback) {
            g_errorCallback(turnId, [reason UTF8String]);
        }
        return;
    }

    // The recognizer closed this segment while the over is still on air: continue in a fresh segment
    [self _commitSegmentTextLocked];
    if (combined.length > 0 && g_resultCallback && ![combined isEqualToString:_lastPublishedCombined]) {
        _lastPublishedCombined = [combined copy];
        g_resultCallback(turnId, [combined UTF8String], false);
    }
    if (segText.length == 0 && ++_silentRestarts > kMaxSilentSegmentRestarts) {
        [self _stopTaskLocked];
        return;
    }
    [self _beginRecognitionSegmentLocked];
}

- (BOOL)startTurn:(int)turnId lang:(NSString *)lang callsign:(NSString *)callsign userCall:(NSString *)userCall {
    if (![self isAuthorized]) {
        return NO;
    }

    __block BOOL started = NO;
    __block NSString *failure = nil;
    dispatch_sync(_queue, ^{
        [self _resetTurnLocked];
        self->_activeTurnId = turnId;
        self->_currentLangCode = [lang copy] ?: @"en";
        self->_currentCallsign = [callsign copy] ?: @"";
        self->_currentUserCallsign = [userCall copy] ?: @"";
        self->_lastStartError = @"";
        started = [self _beginRecognitionSegmentLocked];
        if (!started) {
            failure = [self->_lastStartError copy];
            [self _resetTurnLocked];
        }
    });
    if (!started) {
        NSLog(@"[DSSpeech] turn %d not started: %@", turnId, failure);
        if (g_errorCallback) {
            g_errorCallback(turnId, [(failure.length > 0 ? failure : @"speech recognizer unavailable") UTF8String]);
        }
    }
    return started;
}

- (void)appendSamples16k:(const float *)samples count:(size_t)count {
    if (!samples || count == 0 || !_format16k) {
        return;
    }

    AVAudioPCMBuffer *pcmBuf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_format16k
                                                             frameCapacity:(AVAudioFrameCount)count];
    if (!pcmBuf || !pcmBuf.floatChannelData || !pcmBuf.floatChannelData[0]) {
        return;
    }
    pcmBuf.frameLength = (AVAudioFrameCount)count;
    memcpy(pcmBuf.floatChannelData[0], samples, count * sizeof(float));

    dispatch_async(_queue, ^{
        if (!self->_request || self->_activeTurnId <= 0 || self->_turnEnded) {
            return;
        }
        [self->_request appendAudioPCMBuffer:pcmBuf];
        self->_samplesInCurrentSegment += count;

        if (self->_samplesInCurrentSegment >= kSegmentRotateSamples) {
            [self _commitSegmentTextLocked];
            [self _beginRecognitionSegmentLocked];
        }
    });
}

- (void)endTurn {
    dispatch_async(_queue, ^{
        if (self->_activeTurnId <= 0 || self->_turnEnded) {
            return;
        }
        self->_turnEnded = YES;
        if (self->_request) {
            [self->_request endAudio];
            return;
        }
        // No live recognition task (it stopped after repeated silent segments): report what we have
        const int turnId = self->_activeTurnId;
        NSString *combined = [self _combinedTextLocked];
        if (g_resultCallback) {
            g_resultCallback(turnId, [combined UTF8String], true);
        }
        [self _resetTurnLocked];
    });
}

- (void)cancelTurn {
    dispatch_async(_queue, ^{
        [self _resetTurnLocked];
    });
}

@end

void ios_speech_init(DSSpeechPartialResultCallback resultCb, DSSpeechStatusCallback statusCb, DSSpeechErrorCallback errorCb)
{
    g_resultCallback = resultCb;
    g_statusCallback = statusCb;
    g_errorCallback = errorCb;
    [DSSpeechController shared];
}

void ios_speech_request_permission(void)
{
    [[DSSpeechController shared] requestPermission];
}

bool ios_speech_is_authorized(void)
{
    return [[DSSpeechController shared] isAuthorized] ? true : false;
}

bool ios_speech_start_turn(int turnId, const char *langCode, const char *activeCallsign, const char *userCallsign)
{
    NSString *lang = langCode ? [NSString stringWithUTF8String:langCode] : @"en";
    NSString *call = activeCallsign ? [NSString stringWithUTF8String:activeCallsign] : @"";
    NSString *uCall = userCallsign ? [NSString stringWithUTF8String:userCallsign] : @"";
    return [[DSSpeechController shared] startTurn:turnId lang:lang callsign:call userCall:uCall] ? true : false;
}

void ios_speech_append_samples_16k(const float *samples, size_t count)
{
    [[DSSpeechController shared] appendSamples16k:samples count:count];
}

void ios_speech_end_turn(void)
{
    [[DSSpeechController shared] endTurn];
}

void ios_speech_cancel_turn(void)
{
    [[DSSpeechController shared] cancelTurn];
}

#else

void ios_speech_init(DSSpeechPartialResultCallback, DSSpeechStatusCallback, DSSpeechErrorCallback) {}
void ios_speech_request_permission(void) {}
bool ios_speech_is_authorized(void) { return false; }
bool ios_speech_start_turn(int, const char *, const char *, const char *) { return false; }
void ios_speech_append_samples_16k(const float *, size_t) {}
void ios_speech_end_turn(void) {}
void ios_speech_cancel_turn(void) {}

#endif
