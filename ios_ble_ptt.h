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

// C bridge to the CoreBluetooth central used for Bluetooth LE PTT buttons (iOS).
// All functions are safe to call from any thread; work is serialised on a private queue.
// Callbacks are invoked on that private queue: copy the data and hop to your own thread.

#ifndef IOS_BLE_PTT_H
#define IOS_BLE_PTT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CBManagerState values: 0 unknown, 1 resetting, 2 unsupported, 3 unauthorized, 4 poweredOff, 5 poweredOn
typedef struct {
    void (*state_changed)(int state);
    void (*scan_result)(const char *identifier, const char *name, int rssi,
                        const char *serviceUuidsCsv, const uint8_t *mfg, int mfgLen);
    void (*connected)(const char *identifier, const char *name);
    // notifyChannelsCsv: "SVC:CHR,SVC:CHR,..." for every notify/indicate characteristic
    void (*services_ready)(const char *identifier, const char *notifyChannelsCsv);
    void (*connect_failed)(const char *identifier, int errorCode);
    void (*disconnected)(const char *identifier, int errorCode);
    void (*notification)(const char *identifier, const char *serviceUuid, const char *charUuid,
                         const uint8_t *bytes, int len);
} DSBlePttCallbacks;

void ios_ble_ptt_init(const DSBlePttCallbacks *callbacks);   // stores callbacks; no central yet
void ios_ble_ptt_ensure_central(void);                        // creates the central (permission prompt)
void ios_ble_ptt_shutdown(void);                              // synchronous: no callbacks after return
int  ios_ble_ptt_state(void);
bool ios_ble_ptt_has_central(void);

void ios_ble_ptt_start_scan(void);
void ios_ble_ptt_stop_scan(void);

// keepAlive=true: the central keeps a pending connection and reconnects after every drop.
void ios_ble_ptt_connect(const char *identifier, bool keepAlive);
void ios_ble_ptt_set_keep_alive(const char *identifier, bool keepAlive);
void ios_ble_ptt_disconnect(const char *identifier);          // cancel, keep the reference
void ios_ble_ptt_forget(const char *identifier);              // cancel and drop the reference

void ios_ble_ptt_subscribe(const char *identifier, const char *serviceUuid, const char *charUuid);
void ios_ble_ptt_subscribe_all_candidates(const char *identifier);
void ios_ble_ptt_unsubscribe_all(const char *identifier);
void ios_ble_ptt_read(const char *identifier, const char *serviceUuid, const char *charUuid);

#ifdef __cplusplus
}
#endif

#endif // IOS_BLE_PTT_H
