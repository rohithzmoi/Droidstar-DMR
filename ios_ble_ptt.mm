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

// CoreBluetooth central for Bluetooth LE PTT buttons.
//
// Design notes
//  - One serial queue owns every CoreBluetooth call and every piece of mutable state.
//  - Peripherals are retained in a dictionary: CoreBluetooth drops a connection the moment the
//    last strong reference to the CBPeripheral goes away.
//  - "Keep alive" peripherals always have a pending connectPeripheral: that is the documented way
//    to reconnect a sleeping button, which only advertises for a moment after it is pressed.
//  - Reconnects are never issued synchronously inside a disconnect/fail callback (dispatch_after).
//  - Shutdown is synchronous and zeroes the callbacks under the queue, so no callback can fire
//    into a destroyed C++ object.

#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>

#include "ios_ble_ptt.h"

#include <string>

static void *kDSBleQueueKey = &kDSBleQueueKey;

@interface DSBlePttCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
+ (instancetype)shared;
- (void)onQueue:(dispatch_block_t)block;
- (void)onQueueSync:(dispatch_block_t)block;
@end

@implementation DSBlePttCentral {
    dispatch_queue_t _queue;
    CBCentralManager *_central;
    DSBlePttCallbacks _callbacks;
    BOOL _hasCallbacks;
    NSMutableDictionary<NSUUID *, CBPeripheral *> *_peripherals;
    NSMutableSet<NSUUID *> *_keepAlive;
    NSMutableDictionary<NSUUID *, NSNumber *> *_failures;
    NSMutableDictionary<NSUUID *, NSNumber *> *_pendingDiscoveries;
    NSMutableDictionary<NSUUID *, NSDate *> *_lastAdvert;
    NSMutableSet<NSUUID *> *_reconnectScheduled;
    BOOL _scanning;
    std::atomic<int> _cachedState;
    std::atomic<bool> _hasCentral;
}

+ (instancetype)shared {
    static DSBlePttCentral *s = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [[DSBlePttCentral alloc] init]; });
    return s;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
        _queue = dispatch_queue_create("org.dudetronics.droidstar.bleptt", attr);
        dispatch_queue_set_specific(_queue, kDSBleQueueKey, kDSBleQueueKey, NULL);
        _peripherals = [NSMutableDictionary dictionary];
        _keepAlive = [NSMutableSet set];
        _failures = [NSMutableDictionary dictionary];
        _pendingDiscoveries = [NSMutableDictionary dictionary];
        _lastAdvert = [NSMutableDictionary dictionary];
        _reconnectScheduled = [NSMutableSet set];
        _scanning = NO;
        _hasCallbacks = NO;
        memset(&_callbacks, 0, sizeof(_callbacks));
        _cachedState.store(0);
        _hasCentral.store(false);
    }
    return self;
}

#pragma mark - queue helpers

- (void)onQueue:(dispatch_block_t)block {
    if (dispatch_get_specific(kDSBleQueueKey) == kDSBleQueueKey) {
        block();
    } else {
        dispatch_async(_queue, block);
    }
}

- (void)onQueueSync:(dispatch_block_t)block {
    if (dispatch_get_specific(kDSBleQueueKey) == kDSBleQueueKey) {
        block();
    } else {
        dispatch_sync(_queue, block);
    }
}

- (int)cachedState { return _cachedState.load(); }
- (bool)hasCentral { return _hasCentral.load(); }

#pragma mark - lifecycle

- (void)setCallbacks:(const DSBlePttCallbacks *)cbs {
    [self onQueue:^{
        if (cbs) {
            memcpy(&self->_callbacks, cbs, sizeof(self->_callbacks));
            self->_hasCallbacks = YES;
        } else {
            memset(&self->_callbacks, 0, sizeof(self->_callbacks));
            self->_hasCallbacks = NO;
        }
    }];
}

- (void)ensureCentral {
    [self onQueue:^{
        if (self->_central) return;
        NSDictionary *opts = @{
            CBCentralManagerOptionShowPowerAlertKey: @NO,
            CBCentralManagerOptionRestoreIdentifierKey: @"org.dudetronics.droidstar.bleptt"
        };
        self->_central = [[CBCentralManager alloc] initWithDelegate:self queue:self->_queue options:opts];
        self->_hasCentral.store(true);
        NSLog(@"[BlePtt] Central created");
    }];
}

- (void)shutdown {
    [self onQueueSync:^{
        if (self->_central) {
            if (self->_scanning) {
                [self->_central stopScan];
                self->_scanning = NO;
            }
            for (CBPeripheral *p in self->_peripherals.allValues) {
                p.delegate = nil;
                if (p.state != CBPeripheralStateDisconnected) {
                    [self->_central cancelPeripheralConnection:p];
                }
            }
            self->_central.delegate = nil;
            self->_central = nil;
            self->_hasCentral.store(false);
        }
        [self->_peripherals removeAllObjects];
        [self->_keepAlive removeAllObjects];
        [self->_failures removeAllObjects];
        [self->_pendingDiscoveries removeAllObjects];
        [self->_lastAdvert removeAllObjects];
        [self->_reconnectScheduled removeAllObjects];
        memset(&self->_callbacks, 0, sizeof(self->_callbacks));
        self->_hasCallbacks = NO;
        NSLog(@"[BlePtt] Shut down");
    }];
}

#pragma mark - helpers (queue only)

static NSUUID *ds_uuidFromC(const char *identifier) {
    if (!identifier) return nil;
    NSString *s = [NSString stringWithUTF8String:identifier];
    if (!s.length) return nil;
    return [[NSUUID alloc] initWithUUIDString:s];
}

static NSString *ds_normUuid(CBUUID *u) {
    // CBUUID prints 16-bit UUIDs as 4 hex digits and 128-bit ones with dashes; upper-case both.
    return [u.UUIDString uppercaseString];
}

static BOOL ds_isStandardService(CBUUID *u) {
    NSString *s = ds_normUuid(u);
    if (s.length != 4) return NO;
    // GAP, GATT, Device Information, Battery, HID, Current Time, Link Loss, Tx Power, Immediate Alert
    static NSSet<NSString *> *std = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        std = [NSSet setWithArray:@[@"1800", @"1801", @"180A", @"180F", @"1812", @"1805", @"1803", @"1804", @"1802"]];
    });
    return [std containsObject:s];
}

- (CBPeripheral *)peripheralFor:(NSUUID *)uuid retrieve:(BOOL)retrieve {
    if (!uuid) return nil;
    CBPeripheral *p = _peripherals[uuid];
    if (p || !retrieve || !_central) return p;
    if (_central.state != CBManagerStatePoweredOn) return nil;
    NSArray<CBPeripheral *> *found = [_central retrievePeripheralsWithIdentifiers:@[uuid]];
    p = found.firstObject;
    if (p) {
        p.delegate = self;
        _peripherals[uuid] = p;
    }
    return p;
}

- (void)issueConnect:(CBPeripheral *)p {
    if (!p || !_central || _central.state != CBManagerStatePoweredOn) return;
    if (p.state == CBPeripheralStateDisconnected) {
        NSLog(@"[BlePtt] connect %@ (%@)", p.identifier.UUIDString, p.name ?: @"-");
        [_central connectPeripheral:p options:nil];
    } else if (p.state == CBPeripheralStateConnected) {
        // Already up: resync the caller by re-running discovery.
        [self startDiscovery:p];
    }
}

- (void)scheduleReconnect:(NSUUID *)uuid {
    if (!uuid || ![_keepAlive containsObject:uuid]) return;
    if ([_reconnectScheduled containsObject:uuid]) return;
    const int failures = _failures[uuid].intValue;
    double delay = 0.3 * (1 << MIN(failures, 5));
    if (delay > 10.0) delay = 10.0;
    [_reconnectScheduled addObject:uuid];
    __weak DSBlePttCentral *weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), _queue, ^{
        DSBlePttCentral *strongSelf = weakSelf;
        if (!strongSelf) return;
        [strongSelf->_reconnectScheduled removeObject:uuid];
        if (![strongSelf->_keepAlive containsObject:uuid]) return;
        CBPeripheral *p = [strongSelf peripheralFor:uuid retrieve:YES];
        [strongSelf issueConnect:p];
    });
}

- (void)startDiscovery:(CBPeripheral *)p {
    p.delegate = self;
    _pendingDiscoveries[p.identifier] = @(-1); // services pending
    [p discoverServices:nil];
}

- (void)emitServicesReady:(CBPeripheral *)p {
    NSMutableArray<NSString *> *channels = [NSMutableArray array];
    for (CBService *svc in p.services ?: @[]) {
        for (CBCharacteristic *c in svc.characteristics ?: @[]) {
            if (c.properties & (CBCharacteristicPropertyNotify | CBCharacteristicPropertyIndicate)) {
                [channels addObject:[NSString stringWithFormat:@"%@:%@", ds_normUuid(svc.UUID), ds_normUuid(c.UUID)]];
            }
        }
    }
    [_pendingDiscoveries removeObjectForKey:p.identifier];
    if (_hasCallbacks && _callbacks.services_ready) {
        _callbacks.services_ready(p.identifier.UUIDString.UTF8String, [channels componentsJoinedByString:@","].UTF8String);
    }
}

- (CBCharacteristic *)findChar:(CBPeripheral *)p service:(NSString *)svcUuid chr:(NSString *)chrUuid {
    NSString *s = [svcUuid uppercaseString];
    NSString *c = [chrUuid uppercaseString];
    for (CBService *svc in p.services ?: @[]) {
        if (![ds_normUuid(svc.UUID) isEqualToString:s]) continue;
        for (CBCharacteristic *ch in svc.characteristics ?: @[]) {
            if ([ds_normUuid(ch.UUID) isEqualToString:c]) return ch;
        }
    }
    return nil;
}

#pragma mark - public operations (hop to queue)

- (void)startScan {
    [self onQueue:^{
        if (!self->_central || self->_central.state != CBManagerStatePoweredOn) return;
        if (self->_scanning) return;
        [self->_lastAdvert removeAllObjects];
        [self->_central scanForPeripheralsWithServices:nil
                                               options:@{CBCentralManagerScanOptionAllowDuplicatesKey: @YES}];
        self->_scanning = YES;
        NSLog(@"[BlePtt] Scan started");
    }];
}

- (void)stopScan {
    [self onQueue:^{
        if (self->_central && self->_scanning) {
            [self->_central stopScan];
            NSLog(@"[BlePtt] Scan stopped");
        }
        self->_scanning = NO;
        [self->_lastAdvert removeAllObjects];
        // Drop references to peripherals we only saw advertising: keep-alive devices, anything
        // connected or connecting, and anything with discovery in flight stay retained.
        NSMutableArray<NSUUID *> *drop = [NSMutableArray array];
        for (NSUUID *uuid in self->_peripherals) {
            CBPeripheral *p = self->_peripherals[uuid];
            if ([self->_keepAlive containsObject:uuid]) continue;
            if (p.state != CBPeripheralStateDisconnected) continue;
            if (self->_pendingDiscoveries[uuid]) continue;
            [drop addObject:uuid];
        }
        for (NSUUID *uuid in drop) {
            self->_peripherals[uuid].delegate = nil;
            [self->_peripherals removeObjectForKey:uuid];
        }
    }];
}

- (void)connect:(NSUUID *)uuid keepAlive:(BOOL)keepAlive {
    if (!uuid) return;
    [self onQueue:^{
        if (keepAlive) [self->_keepAlive addObject:uuid];
        CBPeripheral *p = [self peripheralFor:uuid retrieve:YES];
        if (!p) {
            // Not retrievable yet (Bluetooth off or unknown id): the keep-alive set makes us retry on PoweredOn.
            if (!keepAlive && self->_hasCallbacks && self->_callbacks.connect_failed) {
                self->_callbacks.connect_failed(uuid.UUIDString.UTF8String, -1);
            }
            return;
        }
        [self issueConnect:p];
    }];
}

- (void)setKeepAlive:(NSUUID *)uuid on:(BOOL)on {
    if (!uuid) return;
    [self onQueue:^{
        if (on) {
            [self->_keepAlive addObject:uuid];
            CBPeripheral *p = [self peripheralFor:uuid retrieve:YES];
            if (p && p.state == CBPeripheralStateDisconnected) [self issueConnect:p];
        } else {
            [self->_keepAlive removeObject:uuid];
        }
    }];
}

- (void)disconnect:(NSUUID *)uuid {
    if (!uuid) return;
    [self onQueue:^{
        [self->_keepAlive removeObject:uuid];
        CBPeripheral *p = self->_peripherals[uuid];
        if (p && self->_central && p.state != CBPeripheralStateDisconnected) {
            [self->_central cancelPeripheralConnection:p];
        }
    }];
}

- (void)forget:(NSUUID *)uuid {
    if (!uuid) return;
    [self onQueue:^{
        [self->_keepAlive removeObject:uuid];
        [self->_failures removeObjectForKey:uuid];
        [self->_pendingDiscoveries removeObjectForKey:uuid];
        CBPeripheral *p = self->_peripherals[uuid];
        if (p) {
            p.delegate = nil;
            if (self->_central && p.state != CBPeripheralStateDisconnected) {
                [self->_central cancelPeripheralConnection:p];
            }
            [self->_peripherals removeObjectForKey:uuid];
        }
    }];
}

- (void)subscribe:(NSUUID *)uuid service:(NSString *)svc chr:(NSString *)chr {
    if (!uuid || !svc || !chr) return;
    [self onQueue:^{
        CBPeripheral *p = self->_peripherals[uuid];
        if (!p || p.state != CBPeripheralStateConnected) return;
        CBCharacteristic *c = [self findChar:p service:svc chr:chr];
        if (!c) { NSLog(@"[BlePtt] subscribe: %@:%@ not found on %@", svc, chr, uuid.UUIDString); return; }
        if (!c.isNotifying) [p setNotifyValue:YES forCharacteristic:c];
    }];
}

- (void)subscribeAllCandidates:(NSUUID *)uuid {
    if (!uuid) return;
    [self onQueue:^{
        CBPeripheral *p = self->_peripherals[uuid];
        if (!p || p.state != CBPeripheralStateConnected) return;
        int n = 0;
        for (CBService *svc in p.services ?: @[]) {
            if (ds_isStandardService(svc.UUID)) continue;
            for (CBCharacteristic *c in svc.characteristics ?: @[]) {
                if (c.properties & (CBCharacteristicPropertyNotify | CBCharacteristicPropertyIndicate)) {
                    if (!c.isNotifying) [p setNotifyValue:YES forCharacteristic:c];
                    ++n;
                }
            }
        }
        NSLog(@"[BlePtt] learn: subscribed to %d candidate characteristics on %@", n, uuid.UUIDString);
    }];
}

- (void)unsubscribeAll:(NSUUID *)uuid {
    if (!uuid) return;
    [self onQueue:^{
        CBPeripheral *p = self->_peripherals[uuid];
        if (!p || p.state != CBPeripheralStateConnected) return;
        for (CBService *svc in p.services ?: @[]) {
            for (CBCharacteristic *c in svc.characteristics ?: @[]) {
                if (c.isNotifying) [p setNotifyValue:NO forCharacteristic:c];
            }
        }
    }];
}

- (void)read:(NSUUID *)uuid service:(NSString *)svc chr:(NSString *)chr {
    if (!uuid || !svc || !chr) return;
    [self onQueue:^{
        CBPeripheral *p = self->_peripherals[uuid];
        if (!p || p.state != CBPeripheralStateConnected) return;
        CBCharacteristic *c = [self findChar:p service:svc chr:chr];
        if (c && (c.properties & CBCharacteristicPropertyRead)) [p readValueForCharacteristic:c];
    }];
}

#pragma mark - CBCentralManagerDelegate (on _queue)

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    const int st = (int)central.state;
    _cachedState.store(st);
    NSLog(@"[BlePtt] State %d", st);
    if (st != CBManagerStatePoweredOn) {
        _scanning = NO;
        [_reconnectScheduled removeAllObjects];
    }
    if (_hasCallbacks && _callbacks.state_changed) _callbacks.state_changed(st);
    if (st == CBManagerStatePoweredOn) {
        for (NSUUID *uuid in [_keepAlive copy]) {
            CBPeripheral *p = [self peripheralFor:uuid retrieve:YES];
            [self issueConnect:p];
        }
    }
}

- (void)centralManager:(CBCentralManager *)central willRestoreState:(NSDictionary<NSString *, id> *)dict {
    NSArray<CBPeripheral *> *restored = dict[CBCentralManagerRestoredStatePeripheralsKey];
    for (CBPeripheral *p in restored ?: @[]) {
        p.delegate = self;
        _peripherals[p.identifier] = p;
    }
    NSLog(@"[BlePtt] Restored %lu peripheral(s)", (unsigned long)restored.count);
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
     advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                  RSSI:(NSNumber *)RSSI {
    if (!_scanning) return;
    NSUUID *uuid = peripheral.identifier;
    NSDate *now = [NSDate date];
    NSDate *last = _lastAdvert[uuid];
    if (last && [now timeIntervalSinceDate:last] < 1.0) return;
    _lastAdvert[uuid] = now;

    // Keep a strong reference so a later connect does not race the framework's cache.
    if (!_peripherals[uuid]) {
        peripheral.delegate = self;
        _peripherals[uuid] = peripheral;
    }

    NSString *name = peripheral.name;
    NSString *local = advertisementData[CBAdvertisementDataLocalNameKey];
    if ([local isKindOfClass:[NSString class]] && local.length) name = local;
    NSMutableArray<NSString *> *svcs = [NSMutableArray array];
    NSArray *adv = advertisementData[CBAdvertisementDataServiceUUIDsKey];
    if ([adv isKindOfClass:[NSArray class]]) {
        for (id u in adv) if ([u isKindOfClass:[CBUUID class]]) [svcs addObject:ds_normUuid((CBUUID *)u)];
    }
    NSData *mfg = advertisementData[CBAdvertisementDataManufacturerDataKey];
    if (![mfg isKindOfClass:[NSData class]]) mfg = nil;

    if (_hasCallbacks && _callbacks.scan_result) {
        _callbacks.scan_result(uuid.UUIDString.UTF8String,
                               (name ?: @"").UTF8String,
                               RSSI.intValue,
                               [svcs componentsJoinedByString:@","].UTF8String,
                               (const uint8_t *)mfg.bytes,
                               (int)mfg.length);
    }
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
    NSUUID *uuid = peripheral.identifier;
    [_failures removeObjectForKey:uuid];
    NSLog(@"[BlePtt] Connected %@ (%@)", uuid.UUIDString, peripheral.name ?: @"-");
    if (_hasCallbacks && _callbacks.connected) {
        _callbacks.connected(uuid.UUIDString.UTF8String, (peripheral.name ?: @"").UTF8String);
    }
    [self startDiscovery:peripheral];
}

- (void)centralManager:(CBCentralManager *)central didFailToConnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error {
    NSUUID *uuid = peripheral.identifier;
    _failures[uuid] = @(_failures[uuid].intValue + 1);
    NSLog(@"[BlePtt] Connect failed %@: %@", uuid.UUIDString, error.localizedDescription ?: @"-");
    if (_hasCallbacks && _callbacks.connect_failed) {
        _callbacks.connect_failed(uuid.UUIDString.UTF8String, (int)error.code);
    }
    [self scheduleReconnect:uuid];
}

- (void)centralManager:(CBCentralManager *)central didDisconnectPeripheral:(CBPeripheral *)peripheral error:(NSError *)error {
    NSUUID *uuid = peripheral.identifier;
    [_pendingDiscoveries removeObjectForKey:uuid];
    NSLog(@"[BlePtt] Disconnected %@: %@", uuid.UUIDString, error.localizedDescription ?: @"clean");
    if (_hasCallbacks && _callbacks.disconnected) {
        _callbacks.disconnected(uuid.UUIDString.UTF8String, error ? (int)error.code : 0);
    }
    if (error) _failures[uuid] = @(_failures[uuid].intValue + 1);
    else [_failures removeObjectForKey:uuid];
    [self scheduleReconnect:uuid];
}

#pragma mark - CBPeripheralDelegate (on _queue)

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    if (error) {
        NSLog(@"[BlePtt] discoverServices error: %@", error.localizedDescription);
        [self emitServicesReady:peripheral];
        return;
    }
    NSArray<CBService *> *services = peripheral.services ?: @[];
    if (services.count == 0) {
        [self emitServicesReady:peripheral];
        return;
    }
    _pendingDiscoveries[peripheral.identifier] = @(services.count);
    for (CBService *svc in services) {
        [peripheral discoverCharacteristics:nil forService:svc];
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverCharacteristicsForService:(CBService *)service error:(NSError *)error {
    if (error) NSLog(@"[BlePtt] discoverCharacteristics error (%@): %@", ds_normUuid(service.UUID), error.localizedDescription);
    NSNumber *left = _pendingDiscoveries[peripheral.identifier];
    if (!left) return; // discovery was cancelled/forgotten
    const int remaining = left.intValue - 1;
    if (remaining <= 0) {
        [self emitServicesReady:peripheral];
    } else {
        _pendingDiscoveries[peripheral.identifier] = @(remaining);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)characteristic error:(NSError *)error {
    if (error) {
        NSLog(@"[BlePtt] notify state error on %@: %@", ds_normUuid(characteristic.UUID), error.localizedDescription);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic error:(NSError *)error {
    if (error || !characteristic.value) return;
    NSData *v = characteristic.value;
    if (_hasCallbacks && _callbacks.notification) {
        _callbacks.notification(peripheral.identifier.UUIDString.UTF8String,
                                ds_normUuid(characteristic.service.UUID).UTF8String,
                                ds_normUuid(characteristic.UUID).UTF8String,
                                (const uint8_t *)v.bytes, (int)v.length);
    }
}

- (void)peripheral:(CBPeripheral *)peripheral didModifyServices:(NSArray<CBService *> *)invalidatedServices {
    // GATT table changed (firmware update / reset): rediscover so subscriptions can be re-applied.
    if (peripheral.state == CBPeripheralStateConnected) [self startDiscovery:peripheral];
}

@end

#pragma mark - C interface

extern "C" void ios_ble_ptt_init(const DSBlePttCallbacks *callbacks) {
    [[DSBlePttCentral shared] setCallbacks:callbacks];
}

extern "C" void ios_ble_ptt_ensure_central(void) {
#if TARGET_OS_SIMULATOR
    // The simulator has no Bluetooth radio; creating the central only yields "unsupported".
#endif
    [[DSBlePttCentral shared] ensureCentral];
}

extern "C" void ios_ble_ptt_shutdown(void) {
    [[DSBlePttCentral shared] shutdown];
}

extern "C" int ios_ble_ptt_state(void) {
    return [[DSBlePttCentral shared] cachedState];
}

extern "C" bool ios_ble_ptt_has_central(void) {
    return [[DSBlePttCentral shared] hasCentral];
}

extern "C" void ios_ble_ptt_start_scan(void) { [[DSBlePttCentral shared] startScan]; }
extern "C" void ios_ble_ptt_stop_scan(void) { [[DSBlePttCentral shared] stopScan]; }

extern "C" void ios_ble_ptt_connect(const char *identifier, bool keepAlive) {
    [[DSBlePttCentral shared] connect:ds_uuidFromC(identifier) keepAlive:keepAlive];
}

extern "C" void ios_ble_ptt_set_keep_alive(const char *identifier, bool keepAlive) {
    [[DSBlePttCentral shared] setKeepAlive:ds_uuidFromC(identifier) on:keepAlive];
}

extern "C" void ios_ble_ptt_disconnect(const char *identifier) {
    [[DSBlePttCentral shared] disconnect:ds_uuidFromC(identifier)];
}

extern "C" void ios_ble_ptt_forget(const char *identifier) {
    [[DSBlePttCentral shared] forget:ds_uuidFromC(identifier)];
}

extern "C" void ios_ble_ptt_subscribe(const char *identifier, const char *serviceUuid, const char *charUuid) {
    if (!serviceUuid || !charUuid) return;
    [[DSBlePttCentral shared] subscribe:ds_uuidFromC(identifier)
                                service:[NSString stringWithUTF8String:serviceUuid]
                                    chr:[NSString stringWithUTF8String:charUuid]];
}

extern "C" void ios_ble_ptt_subscribe_all_candidates(const char *identifier) {
    [[DSBlePttCentral shared] subscribeAllCandidates:ds_uuidFromC(identifier)];
}

extern "C" void ios_ble_ptt_unsubscribe_all(const char *identifier) {
    [[DSBlePttCentral shared] unsubscribeAll:ds_uuidFromC(identifier)];
}

extern "C" void ios_ble_ptt_read(const char *identifier, const char *serviceUuid, const char *charUuid) {
    if (!serviceUuid || !charUuid) return;
    [[DSBlePttCentral shared] read:ds_uuidFromC(identifier)
                           service:[NSString stringWithUTF8String:serviceUuid]
                               chr:[NSString stringWithUTF8String:charUuid]];
}
