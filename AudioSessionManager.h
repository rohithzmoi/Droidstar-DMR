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

#ifndef AUDIOSESSIONMANAGER_H
#define AUDIOSESSIONMANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio session management
void setupAVAudioSession(void);
void deactivateAVAudioSession(void);
void setupBackgroundAudio(void);
void deactivateBackgroundAudio(void);
void renewBackgroundTask(void);
void setPreferredInputDevice(void);
void prepareAudioForCapture(void);
void finishAudioCapture(void);
bool isAppInBackground(void);

// Query active iOS AVAudioSession hardware sample rate and route generation counter
double getActiveAudioSampleRate(void);
uint32_t getAudioRouteGeneration(void);
bool isIOSExternalMicActive(void);

// Apple hardware microphone directional beamforming / voice isolation control
void setAppleVoiceIsolationEnabled(bool enabled);
bool isAppleVoiceIsolationEnabled(void);

// Now Playing / Lock Screen / Control Center state updates
void setAudioConnectionState(bool connected, const char *host, const char *mode);
void setAudioRXState(const char *callsign, const char *name, const char *country);
void setAudioTXState(bool transmitting);
void clearAudioRXState(void);
void updateNowPlayingInfo(void);

// Re-apply speaker override if needed (call after playback starts)
void ensureSpeakerRoute(void);

// Remote command callbacks (for PTT from headphones/Control Center)
typedef void (*PTTCallback)(void);
void setPTTCallbacks(PTTCallback pressCallback, PTTCallback releaseCallback);

// Safe-area insets (points) of the key window. Must be called on the main thread.
bool getSafeAreaInsets(double *top, double *left, double *bottom, double *right);

// Read CFBundleShortVersionString and CFBundleVersion from Info.plist at runtime
void getIOSBundleVersion(char *shortVerBuf, int shortVerLen, char *buildVerBuf, int buildVerLen);

#ifdef __cplusplus
}
#endif

#endif // AUDIOSESSIONMANAGER_H
