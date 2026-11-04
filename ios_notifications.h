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

#ifndef IOS_NOTIFICATIONS_H
#define IOS_NOTIFICATIONS_H

#include <QString>
#include <vector>
#include <cstdint>

struct DSScheduledAlert {
    QString identifier;
    QString netId;
    QString title;
    QString subtitle;
    QString body;
    qint64 fireTimestampSecs { 0 };
};

typedef void (*DSNotificationPermissionCallback)(bool granted);
typedef void (*DSNotificationTapCallback)(const char *netId);
typedef void (*DSNotificationDeliveredCallback)(const char *netId);
typedef void (*DSSmsNotificationTapCallback)(const char *threadId);
typedef void (*DSKeyboardHeightCallback)(double heightPts);

void ios_notifications_init(DSNotificationTapCallback tapCb,
                            DSNotificationDeliveredCallback deliveredCb);
void ios_notifications_request_permission(DSNotificationPermissionCallback cb);
void ios_notifications_check_permission(DSNotificationPermissionCallback cb);
void ios_notifications_replace_scheduled_alerts(const std::vector<DSScheduledAlert> &alerts);
void ios_notifications_set_sms_tap_callback(DSSmsNotificationTapCallback tapCb);
void ios_notifications_post_sms_alert(const QString &threadId,
                                      const QString &title,
                                      const QString &body);
void ios_keyboard_observer_init(DSKeyboardHeightCallback cb);
void ios_set_chat_keyboard_mode(bool active);

#endif // IOS_NOTIFICATIONS_H
