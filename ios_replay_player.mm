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

#include "ios_replay_player.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

@interface DSReplayPlayerHolder : NSObject <AVAudioPlayerDelegate>
@property (nonatomic, strong) AVAudioPlayer *player;
@property (nonatomic, assign) DSReplayFinishedCallback finishedCb;
@property (nonatomic, assign) DSReplayInterruptedCallback interruptedCb;
@property (nonatomic, assign) float volume;
@end

@implementation DSReplayPlayerHolder

- (instancetype)init
{
    self = [super init];
    if (self) {
        _volume = 1.0f;
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(onMediaServicesReset:)
                                                     name:AVAudioSessionMediaServicesWereResetNotification
                                                   object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(onInterruption:)
                                                     name:AVAudioSessionInterruptionNotification
                                                   object:nil];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)releasePlayer
{
    if (_player) {
        _player.delegate = nil;
        [_player stop];
        _player = nil;
    }
}

- (void)onMediaServicesReset:(NSNotification *)note
{
    (void)note;
    // The player object is invalid after a media-server reset: drop it and report an interruption
    // so the UI shows "paused"; the next tap on Replay loads the file again.
    const bool wasActive = (_player != nil);
    [self releasePlayer];
    if (wasActive && _interruptedCb) {
        _interruptedCb();
    }
}

- (void)onInterruption:(NSNotification *)note
{
    NSUInteger type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionInterruptionTypeBegan && _player && _player.isPlaying) {
        [_player pause];
        if (_interruptedCb) {
            _interruptedCb();
        }
    }
    // InterruptionTypeEnded: deliberately no auto-resume (live audio always wins).
}

- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)player successfully:(BOOL)flag
{
    (void)player;
    if (_finishedCb) {
        _finishedCb(flag ? true : false);
    }
}

- (void)audioPlayerDecodeErrorDidOccur:(AVAudioPlayer *)player error:(NSError *)error
{
    (void)player;
    NSLog(@"[QsoReplay] decode error: %@", error.localizedDescription);
    if (_finishedCb) {
        _finishedCb(false);
    }
}

@end

static DSReplayPlayerHolder *g_holder = nil;

static DSReplayPlayerHolder *holder(void)
{
    if (!g_holder) {
        g_holder = [[DSReplayPlayerHolder alloc] init];
    }
    return g_holder;
}

void ios_replay_player_init(DSReplayFinishedCallback finishedCb, DSReplayInterruptedCallback interruptedCb)
{
    holder().finishedCb = finishedCb;
    holder().interruptedCb = interruptedCb;
}

double ios_replay_player_load(const char *path)
{
    @autoreleasepool {
        DSReplayPlayerHolder *h = holder();
        [h releasePlayer];
        if (!path || !*path) {
            return -1.0;
        }
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        NSError *error = nil;
        AVAudioPlayer *p = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
        if (!p || error) {
            NSLog(@"[QsoReplay] cannot open clip: %@", error.localizedDescription);
            return -1.0;
        }
        p.delegate = h;
        p.volume = h.volume;
        p.numberOfLoops = 0;
        if (![p prepareToPlay]) {
            NSLog(@"[QsoReplay] prepareToPlay failed");
            return -1.0;
        }
        h.player = p;
        return p.duration;
    }
}

bool ios_replay_player_play(void)
{
    DSReplayPlayerHolder *h = holder();
    if (!h.player) {
        return false;
    }
    return [h.player play] ? true : false;
}

void ios_replay_player_pause(void)
{
    DSReplayPlayerHolder *h = holder();
    if (h.player) {
        [h.player pause];
    }
}

void ios_replay_player_stop(void)
{
    [holder() releasePlayer];
}

void ios_replay_player_seek(double seconds)
{
    DSReplayPlayerHolder *h = holder();
    if (h.player) {
        const double d = h.player.duration;
        double s = seconds;
        if (s < 0) s = 0;
        if (d > 0 && s > d) s = d;
        h.player.currentTime = s;
    }
}

double ios_replay_player_position(void)
{
    DSReplayPlayerHolder *h = holder();
    return h.player ? h.player.currentTime : 0.0;
}

bool ios_replay_player_is_playing(void)
{
    DSReplayPlayerHolder *h = holder();
    return (h.player && h.player.isPlaying) ? true : false;
}

void ios_replay_player_set_volume(float volume)
{
    DSReplayPlayerHolder *h = holder();
    float v = volume;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    h.volume = v;
    if (h.player) {
        h.player.volume = v;
    }
}
