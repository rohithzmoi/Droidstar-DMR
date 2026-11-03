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
#import <UserNotifications/UserNotifications.h>

static DSNotificationTapCallback g_tapCallback = nullptr;
static DSNotificationDeliveredCallback g_deliveredCallback = nullptr;

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

#else

void ios_notifications_init(DSNotificationTapCallback, DSNotificationDeliveredCallback) {}
void ios_notifications_request_permission(DSNotificationPermissionCallback cb) { if (cb) cb(true); }
void ios_notifications_check_permission(DSNotificationPermissionCallback cb) { if (cb) cb(true); }
void ios_notifications_replace_scheduled_alerts(const std::vector<DSScheduledAlert> &) {}

#endif
