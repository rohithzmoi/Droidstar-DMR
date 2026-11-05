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

#ifndef IOS_REPLAY_PLAYER_H
#define IOS_REPLAY_PLAYER_H

// AVAudioPlayer bridge for QSO Replay playback. Plays through the app's existing PlayAndRecord
// audio session (AudioSessionManager owns the category and routing). Callbacks may arrive on any
// thread; QsoReplayPlayer trampolines them onto the main thread.

typedef void (*DSReplayFinishedCallback)(bool completedNormally);
// iOS paused playback on our behalf (phone call, another app's audio, media-server reset).
// Playback is never resumed automatically; the user taps play again.
typedef void (*DSReplayInterruptedCallback)(void);

void ios_replay_player_init(DSReplayFinishedCallback finishedCb, DSReplayInterruptedCallback interruptedCb);
// Loads the file. Returns the duration in seconds, or a negative value on failure.
double ios_replay_player_load(const char *path);
bool ios_replay_player_play(void);
void ios_replay_player_pause(void);
// Stops and releases the player.
void ios_replay_player_stop(void);
void ios_replay_player_seek(double seconds);
double ios_replay_player_position(void);
bool ios_replay_player_is_playing(void);
void ios_replay_player_set_volume(float volume);

#endif // IOS_REPLAY_PLAYER_H
