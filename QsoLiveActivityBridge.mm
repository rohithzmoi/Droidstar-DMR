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
