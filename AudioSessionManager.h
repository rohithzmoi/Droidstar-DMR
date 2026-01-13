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

#ifndef AUDIOSESSIONMANAGER_H
#define AUDIOSESSIONMANAGER_H

#include <stdbool.h>

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
bool isAppInBackground(void);

// Now Playing / Lock Screen / Control Center state updates
void setAudioConnectionState(bool connected, const char *host, const char *mode);
void setAudioRXState(const char *callsign, const char *name, const char *country);
void setAudioTXState(bool transmitting);
void clearAudioRXState(void);
void updateNowPlayingInfo(void);

// Remote command callbacks (for PTT from headphones/Control Center)
typedef void (*PTTCallback)(void);
void setPTTCallbacks(PTTCallback pressCallback, PTTCallback releaseCallback);

#ifdef __cplusplus
}
#endif

#endif // AUDIOSESSIONMANAGER_H
