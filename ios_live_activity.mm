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

#include "ios_live_activity.h"

#include <QtGlobal>

#if defined(Q_OS_IOS)

#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

static inline Class liveActivityManagerClass(void)
{
    // Swift class runtime name can be "<Module>.<Class>" OR explicit @objc name.
    Class cls = NSClassFromString(@"LiveActivityManager"); // @objc(LiveActivityManager)
    if (!cls) cls = NSClassFromString(@"DroidStar.LiveActivityManager");
    return cls;
}

bool ios_live_activity_is_available(void)
{
    if (@available(iOS 16.1, *)) {
        Class cls = liveActivityManagerClass();
        if (!cls) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager class not found. "
                  @"Make sure LiveActivityManager.swift is part of the MAIN app target.");
            return false;
        }

        SEL sel = NSSelectorFromString(@"isDynamicIslandAvailable");
        if (![cls respondsToSelector:sel]) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager missing selector isDynamicIslandAvailable");
            return false;
        }

        typedef BOOL (*MsgSendBool)(id, SEL);
        BOOL enabled = ((MsgSendBool)objc_msgSend)((id)cls, sel);
        if (!enabled) {
            NSLog(@"[DroidStar][LiveActivity] ActivityAuthorizationInfo().areActivitiesEnabled == false "
                  @"(Settings → DroidStar → Live Activities might be OFF).");
        }
        return enabled;
    }
    return false;
}

static inline NSString *ns(const char *s)
{
    if (!s) return @"";
    return [NSString stringWithUTF8String:s] ?: @"";
}

void ios_live_activity_start_or_update(const char *mode,
                                       const char *callsign,
                                       const char *handle,
                                       const char *country,
                                       const char *tgid)
{
    if (@available(iOS 16.1, *)) {
        Class cls = liveActivityManagerClass();
        if (!cls) {
            NSLog(@"[DroidStar][LiveActivity] start/update called but LiveActivityManager class not found");
            return;
        }

        // Get singleton: +shared
        SEL sharedSel = NSSelectorFromString(@"shared");
        if (![cls respondsToSelector:sharedSel]) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager missing selector +shared");
            return;
        }
        typedef id (*MsgSendId)(id, SEL);
        id mgr = ((MsgSendId)objc_msgSend)((id)cls, sharedSel);
        if (!mgr) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager.shared returned nil");
            return;
        }

        SEL updSel = NSSelectorFromString(@"startOrUpdateLiveActivityWithMode:callsign:handle:country:tgid:");
        if (![mgr respondsToSelector:updSel]) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager missing selector startOrUpdateLiveActivityWithMode:callsign:handle:country:tgid:");
            return;
        }
        typedef void (*MsgSendUpdate)(id, SEL, NSString*, NSString*, NSString*, NSString*, NSString*);
        ((MsgSendUpdate)objc_msgSend)(mgr,
                                      updSel,
                                      ns(mode),
                                      ns(callsign),
                                      ns(handle),
                                      ns(country),
                                      ns(tgid));
    }
}

void ios_live_activity_end(void)
{
    if (@available(iOS 16.1, *)) {
        Class cls = liveActivityManagerClass();
        if (!cls) {
            NSLog(@"[DroidStar][LiveActivity] end called but LiveActivityManager class not found");
            return;
        }

        SEL sharedSel = NSSelectorFromString(@"shared");
        if (![cls respondsToSelector:sharedSel]) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager missing selector +shared");
            return;
        }
        typedef id (*MsgSendId)(id, SEL);
        id mgr = ((MsgSendId)objc_msgSend)((id)cls, sharedSel);
        if (!mgr) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager.shared returned nil");
            return;
        }

        SEL endSel = NSSelectorFromString(@"endLiveActivity");
        if (![mgr respondsToSelector:endSel]) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager missing selector endLiveActivity");
            return;
        }
        typedef void (*MsgSendVoid)(id, SEL);
        ((MsgSendVoid)objc_msgSend)(mgr, endSel);
    }
}

void ios_live_activity_end_all(void)
{
    if (@available(iOS 16.1, *)) {
        Class cls = liveActivityManagerClass();
        if (!cls) {
            NSLog(@"[DroidStar][LiveActivity] endAll called but LiveActivityManager class not found");
            return;
        }

        SEL sharedSel = NSSelectorFromString(@"shared");
        if (![cls respondsToSelector:sharedSel]) return;
        typedef id (*MsgSendId)(id, SEL);
        id mgr = ((MsgSendId)objc_msgSend)((id)cls, sharedSel);
        if (!mgr) return;

        SEL endAllSel = NSSelectorFromString(@"endAllActivities");
        if (![mgr respondsToSelector:endAllSel]) {
            NSLog(@"[DroidStar][LiveActivity] LiveActivityManager missing selector endAllActivities");
            return;
        }
        typedef void (*MsgSendVoid)(id, SEL);
        ((MsgSendVoid)objc_msgSend)(mgr, endAllSel);
    }
}

#else

bool ios_live_activity_is_available(void) { return false; }
void ios_live_activity_start_or_update(const char *, const char *, const char *, const char *, const char *) {}
void ios_live_activity_end(void) {}
void ios_live_activity_end_all(void) {}

#endif

