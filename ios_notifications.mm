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

#include "ios_notifications.h"
#include <QtGlobal>

#if defined(Q_OS_IOS) && !defined(Q_OS_IOS_APP_EXTENSION)

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <UserNotifications/UserNotifications.h>

static DSNotificationTapCallback g_tapCallback = nullptr;
static DSNotificationDeliveredCallback g_deliveredCallback = nullptr;
static DSSmsNotificationTapCallback g_smsTapCallback = nullptr;
static DSKeyboardHeightCallback g_kbHeightCallback = nullptr;
static bool g_chatKeyboardMode = false;
static __weak UIWindow *g_savedKbWindow = nil;
static UIGestureRecognizer *g_savedKbListener = nil;

static UIWindow *ds_activeWindow(void)
{
    if (@available(iOS 15.0, *)) {
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
            if (![scene isKindOfClass:[UIWindowScene class]]) continue;
            UIWindowScene *ws = (UIWindowScene *)scene;
            for (UIWindow *w in ws.windows) {
                if (w.isKeyWindow) return w;
            }
            if (ws.windows.count > 0) return ws.windows.firstObject;
        }
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [UIApplication sharedApplication].keyWindow;
#pragma clang diagnostic pop
}

static void ds_resetRootSublayerTransform(UIWindow *win)
{
    if (!win || !win.rootViewController || !win.rootViewController.view) return;
    CALayer *layer = win.rootViewController.view.layer;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [layer removeAnimationForKey:@"sublayerTransform"];
    if (!CATransform3DIsIdentity(layer.sublayerTransform)) {
        layer.sublayerTransform = CATransform3DIdentity;
    }
    [CATransaction commit];
}

static void ds_syncChatKeyboardModeOnMain(void)
{
    UIWindow *win = ds_activeWindow();
    Class kbClass = NSClassFromString(@"QIOSKeyboardListener");
    if (g_chatKeyboardMode) {
        if (win && kbClass) {
            NSArray<UIGestureRecognizer *> *recognizers = [win.gestureRecognizers copy];
            for (UIGestureRecognizer *gr in recognizers) {
                if ([gr isKindOfClass:kbClass]) {
                    g_savedKbListener = gr;
                    g_savedKbWindow = win;
                    [win removeGestureRecognizer:gr];
                }
            }
        }
        ds_resetRootSublayerTransform(win);
    } else {
        if (g_savedKbListener && !g_savedKbListener.view) {
            UIWindow *targetWin = g_savedKbWindow ?: win;
            if (targetWin) {
                [targetWin addGestureRecognizer:g_savedKbListener];
            }
        }
    }
}

@interface DSKeyboardObserver : NSObject
+ (instancetype)sharedObserver;
@end

@implementation DSKeyboardObserver

+ (instancetype)sharedObserver
{
    static DSKeyboardObserver *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[DSKeyboardObserver alloc] init];
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:instance
               selector:@selector(handleKeyboardNotification:)
                   name:UIKeyboardWillShowNotification
                 object:nil];
        [nc addObserver:instance
               selector:@selector(handleKeyboardNotification:)
                   name:UIKeyboardDidShowNotification
                 object:nil];
        [nc addObserver:instance
               selector:@selector(handleKeyboardNotification:)
                   name:UIKeyboardWillChangeFrameNotification
                 object:nil];
        [nc addObserver:instance
               selector:@selector(handleKeyboardNotification:)
                   name:UIKeyboardWillHideNotification
                 object:nil];
        [nc addObserver:instance
               selector:@selector(handleKeyboardNotification:)
                   name:UIKeyboardDidHideNotification
                 object:nil];
        [nc addObserver:instance
               selector:@selector(handleAppOrWindowActive:)
                   name:UIApplicationDidBecomeActiveNotification
                 object:nil];
        [nc addObserver:instance
               selector:@selector(handleAppOrWindowActive:)
                   name:UIWindowDidBecomeKeyNotification
                 object:nil];
    });
    return instance;
}

- (void)handleAppOrWindowActive:(NSNotification *)note
{
    (void)note;
    if (g_chatKeyboardMode) {
        ds_syncChatKeyboardModeOnMain();
        dispatch_async(dispatch_get_main_queue(), ^{
            if (g_chatKeyboardMode) {
                ds_syncChatKeyboardModeOnMain();
            }
        });
    }
}

- (void)handleKeyboardNotification:(NSNotification *)note
{
    if (g_chatKeyboardMode) {
        ds_syncChatKeyboardModeOnMain();
        dispatch_async(dispatch_get_main_queue(), ^{
            if (g_chatKeyboardMode) {
                ds_syncChatKeyboardModeOnMain();
            }
        });
    }
    if (!g_kbHeightCallback) return;
    NSString *name = note.name ?: @"";
    if ([name isEqualToString:UIKeyboardWillHideNotification] ||
        [name isEqualToString:UIKeyboardDidHideNotification]) {
        g_kbHeightCallback(0.0);
        return;
    }
    NSDictionary *info = note.userInfo;
    NSValue *endVal = info[UIKeyboardFrameEndUserInfoKey];
    if (!endVal) return;
    CGRect kbScreen = [endVal CGRectValue];
    UIWindow *win = ds_activeWindow();
    CGRect refBounds = win ? win.bounds : [UIScreen mainScreen].bounds;
    CGRect kbLocal = (win && win.screen)
        ? [win convertRect:kbScreen fromCoordinateSpace:win.screen.coordinateSpace]
        : kbScreen;
    CGFloat overlap = CGRectGetMaxY(refBounds) - CGRectGetMinY(kbLocal);
    if (overlap < 0.0 || CGRectIsEmpty(kbScreen)) {
        overlap = 0.0;
    }
    g_kbHeightCallback(static_cast<double>(overlap));
}

@end

@interface DSNotificationCenterDelegate : NSObject <UNUserNotificationCenterDelegate>
+ (instancetype)sharedDelegate;
@end

@implementation DSNotificationCenterDelegate

+ (instancetype)sharedDelegate {
    static DSNotificationCenterDelegate *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[DSNotificationCenterDelegate alloc] init];
    });
    return instance;
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler {
    (void)center;
    NSDictionary *userInfo = notification.request.content.userInfo;
    NSString *netId = [userInfo objectForKey:@"netId"];
    if (netId && g_deliveredCallback) {
        g_deliveredCallback([netId UTF8String]);
    }
    if (@available(iOS 14.0, *)) {
        completionHandler(UNNotificationPresentationOptionBanner |
                          UNNotificationPresentationOptionList |
                          UNNotificationPresentationOptionSound);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        completionHandler(UNNotificationPresentationOptionAlert |
                          UNNotificationPresentationOptionSound);
#pragma clang diagnostic pop
    }
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
didReceiveNotificationResponse:(UNNotificationResponse *)response
         withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;
    NSDictionary *userInfo = response.notification.request.content.userInfo;
    NSString *netId = [userInfo objectForKey:@"netId"];
    NSString *smsThreadId = [userInfo objectForKey:@"smsThreadId"];
    if (smsThreadId && g_smsTapCallback) {
        g_smsTapCallback([smsThreadId UTF8String]);
    }
    if (netId && g_tapCallback) {
        g_tapCallback([netId UTF8String]);
    }
    completionHandler();
}

@end

void ios_notifications_init(DSNotificationTapCallback tapCb,
                            DSNotificationDeliveredCallback deliveredCb)
{
    g_tapCallback = tapCb;
    g_deliveredCallback = deliveredCb;

    dispatch_async(dispatch_get_main_queue(), ^{
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
        center.delegate = [DSNotificationCenterDelegate sharedDelegate];
    });
}

void ios_notifications_request_permission(DSNotificationPermissionCallback cb)
{
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    UNAuthorizationOptions opts = (UNAuthorizationOptionAlert |
                                   UNAuthorizationOptionSound |
                                   UNAuthorizationOptionBadge);
    [center requestAuthorizationWithOptions:opts
                          completionHandler:^(BOOL granted, NSError * _Nullable error) {
        if (error) {
            NSLog(@"[DroidStar][Notifications] Permission error: %@", error.localizedDescription);
        }
        if (cb) {
            cb(granted ? true : false);
        }
    }];
}

void ios_notifications_check_permission(DSNotificationPermissionCallback cb)
{
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings * _Nonnull settings) {
        const bool authorized = (settings.authorizationStatus == UNAuthorizationStatusAuthorized ||
                                 settings.authorizationStatus == UNAuthorizationStatusProvisional);
        if (cb) {
            cb(authorized);
        }
    }];
}

void ios_notifications_replace_scheduled_alerts(const std::vector<DSScheduledAlert> &alerts)
{
    std::vector<DSScheduledAlert> copyAlerts = alerts;

    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    [center getPendingNotificationRequestsWithCompletionHandler:^(NSArray<UNNotificationRequest *> * _Nonnull requests) {
        NSMutableArray<NSString *> *toRemove = [NSMutableArray array];
        for (UNNotificationRequest *req in requests) {
            if ([req.identifier hasPrefix:@"droidstar.net."]) {
                [toRemove addObject:req.identifier];
            }
        }
        if (toRemove.count > 0) {
            [center removePendingNotificationRequestsWithIdentifiers:toRemove];
        }

        NSCalendar *cal = [NSCalendar currentCalendar];
        NSCalendarUnit units = (NSCalendarUnitYear |
                                NSCalendarUnitMonth |
                                NSCalendarUnitDay |
                                NSCalendarUnitHour |
                                NSCalendarUnitMinute |
                                NSCalendarUnitSecond);

        const size_t maxAlerts = std::min<size_t>(copyAlerts.size(), 60U);
        const qint64 nowSecs = [[NSDate date] timeIntervalSince1970];

        for (size_t i = 0; i < maxAlerts; ++i) {
            const DSScheduledAlert &item = copyAlerts[i];
            if (item.fireTimestampSecs <= nowSecs + 1) {
                continue;
            }

            UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
            content.title = item.title.toNSString();
            if (!item.subtitle.isEmpty()) {
                content.subtitle = item.subtitle.toNSString();
            }
            content.body = item.body.toNSString();
            content.sound = [UNNotificationSound defaultSound];
            content.userInfo = @{ @"netId": item.netId.toNSString() };

            NSDate *fireDate = [NSDate dateWithTimeIntervalSince1970:static_cast<NSTimeInterval>(item.fireTimestampSecs)];
            NSDateComponents *comps = [cal components:units fromDate:fireDate];
            UNCalendarNotificationTrigger *trigger = [UNCalendarNotificationTrigger triggerWithDateMatchingComponents:comps
                                                                                                              repeats:NO];

            NSString *identifier = item.identifier.toNSString();
            UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:identifier
                                                                                  content:content
                                                                                  trigger:trigger];
            [center addNotificationRequest:request withCompletionHandler:^(NSError * _Nullable error) {
                if (error) {
                    NSLog(@"[DroidStar][Notifications] Failed to schedule %@: %@", identifier, error.localizedDescription);
                }
            }];
        }
    }];
}

void ios_notifications_set_sms_tap_callback(DSSmsNotificationTapCallback tapCb)
{
    g_smsTapCallback = tapCb;
}

void ios_notifications_post_sms_alert(const QString &threadId,
                                      const QString &title,
                                      const QString &body)
{
    if (threadId.isEmpty()) {
        return;
    }
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = title.toNSString();
    content.body = body.toNSString();
    content.sound = [UNNotificationSound defaultSound];
    content.userInfo = @{ @"smsThreadId": threadId.toNSString() };

    NSString *safeThread = [threadId.toNSString() stringByReplacingOccurrencesOfString:@":" withString:@"-"];
    NSString *identifier = [NSString stringWithFormat:@"droidstar.sms.%@.%lld",
                            safeThread,
                            (long long)([[NSDate date] timeIntervalSince1970] * 1000.0)];
    UNNotificationRequest *request =
        [UNNotificationRequest requestWithIdentifier:identifier content:content trigger:nil];
    [[UNUserNotificationCenter currentNotificationCenter]
        addNotificationRequest:request
        withCompletionHandler:^(NSError * _Nullable error) {
            if (error) {
                NSLog(@"[DroidStar][Notifications] Failed to post SMS alert: %@",
                      error.localizedDescription);
            }
        }];
}

void ios_keyboard_observer_init(DSKeyboardHeightCallback cb)
{
    g_kbHeightCallback = cb;
    dispatch_async(dispatch_get_main_queue(), ^{
        [DSKeyboardObserver sharedObserver];
    });
}

void ios_set_chat_keyboard_mode(bool active)
{
    g_chatKeyboardMode = active;
    if ([NSThread isMainThread]) {
        ds_syncChatKeyboardModeOnMain();
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            ds_syncChatKeyboardModeOnMain();
        });
    }
}

#else

void ios_notifications_init(DSNotificationTapCallback, DSNotificationDeliveredCallback) {}
void ios_notifications_request_permission(DSNotificationPermissionCallback cb) { if (cb) cb(true); }
void ios_notifications_check_permission(DSNotificationPermissionCallback cb) { if (cb) cb(true); }
void ios_notifications_replace_scheduled_alerts(const std::vector<DSScheduledAlert> &) {}
void ios_notifications_set_sms_tap_callback(DSSmsNotificationTapCallback) {}
void ios_notifications_post_sms_alert(const QString &, const QString &, const QString &) {}
void ios_keyboard_observer_init(DSKeyboardHeightCallback) {}
void ios_set_chat_keyboard_mode(bool) {}

#endif
