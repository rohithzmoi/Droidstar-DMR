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

#ifndef IOS_SPEECH_RECOGNIZER_H
#define IOS_SPEECH_RECOGNIZER_H

#include <cstddef>

typedef void (*DSSpeechPartialResultCallback)(int turnId, const char *text, bool isFinal);
typedef void (*DSSpeechStatusCallback)(bool authorized, bool available, const char *statusMsg);
// The turn cannot be recognized (no on-device model, Dictation disabled, recognizer error, ...)
typedef void (*DSSpeechErrorCallback)(int turnId, const char *reason);

void ios_speech_init(DSSpeechPartialResultCallback resultCb, DSSpeechStatusCallback statusCb, DSSpeechErrorCallback errorCb);
void ios_speech_request_permission(void);
bool ios_speech_is_authorized(void);
bool ios_speech_start_turn(int turnId, const char *langCode, const char *activeCallsign, const char *userCallsign);
void ios_speech_append_samples_16k(const float *samples, size_t count);
void ios_speech_end_turn(void);
void ios_speech_cancel_turn(void);

#endif // IOS_SPEECH_RECOGNIZER_H
