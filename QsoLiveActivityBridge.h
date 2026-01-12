/*
    Copyright (C) 2025 Rohith Namboothiri

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

#ifndef QSOLIVEACTIVITYBRIDGE_H
#define QSOLIVEACTIVITYBRIDGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque pointer type for the bridge instance
typedef void* QsoLiveActivityBridgeRef;

// Function declarations
QsoLiveActivityBridgeRef QsoLiveActivityBridge_create(void);
void QsoLiveActivityBridge_release(QsoLiveActivityBridgeRef bridge);

// Activity management
void QsoLiveActivityBridge_startOrUpdateLiveActivity(QsoLiveActivityBridgeRef bridge, 
                                                    const char* callsign, 
                                                    const char* handle, 
                                                    const char* country);

void QsoLiveActivityBridge_endLiveActivity(QsoLiveActivityBridgeRef bridge);

// Dynamic Island availability
bool QsoLiveActivityBridge_isDynamicIslandAvailable(void);

// Update QSO details
void QsoLiveActivityBridge_updateQsoDetails(QsoLiveActivityBridgeRef bridge,
                                          const char* callsign,
                                          const char* handle,
                                          const char* country);

#ifdef __cplusplus
}
#endif

#endif // QSOLIVEACTIVITYBRIDGE_H
