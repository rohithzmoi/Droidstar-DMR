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

// QsoLiveActivityBridge.mm

#include "QsoLiveActivityBridge.h"
#import "DroidStar-Swift.h"  // Ensure this is the correct import for the generated Swift header

// Define the LiveActivityBridge Objective-C interface
@interface LiveActivityBridge : NSObject

+ (instancetype)sharedInstance;
- (void)startOrUpdateLiveActivityWithCallsign:(NSString *)callsign handle:(NSString *)handle;
- (void)endLiveActivity;

@end

@implementation LiveActivityBridge

+ (instancetype)sharedInstance {
    static LiveActivityBridge *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[LiveActivityBridge alloc] init];
    });
    return sharedInstance;
}

// Start or update a live activity with the given callsign and handle
- (void)startOrUpdateLiveActivityWithCallsign:(NSString *)callsign handle:(NSString *)handle {
    // Use the shared instance of the Swift class
    [[LiveActivityManager shared] startOrUpdateLiveActivityWithCallsign:callsign handle:handle];
}

// End the live activity
- (void)endLiveActivity {
    // Use the shared instance of the Swift class
    [[LiveActivityManager shared] endLiveActivity];
}

@end

// Implementation of QsoLiveActivityBridge
QsoLiveActivityBridge::QsoLiveActivityBridge(QObject *parent) : QObject(parent) {}

// Exposed method to start or update the live activity from QML
void QsoLiveActivityBridge::startOrUpdateLiveActivity(const QString &callsign, const QString &handle) {
    NSString *nsCallsign = callsign.toNSString();
    NSString *nsHandle = handle.toNSString();
    [[LiveActivityBridge sharedInstance] startOrUpdateLiveActivityWithCallsign:nsCallsign handle:nsHandle];
}

// Exposed method to end the live activity from QML
void QsoLiveActivityBridge::endLiveActivity() {
    [[LiveActivityBridge sharedInstance] endLiveActivity];
}
