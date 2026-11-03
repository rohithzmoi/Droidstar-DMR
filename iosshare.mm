/*
    Copyright (C) 2024 Rohith Namboothiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
*/
#include <QtCore/qglobal.h>
#include <QString>

#if defined(Q_OS_IOS) && !defined(Q_OS_IOS_APP_EXTENSION)
#import <UIKit/UIKit.h>

static UIViewController *_rootViewController(void) {
    // Modern scene-based lookup (iOS 15+); avoids deprecated keyWindow.
    if (@available(iOS 15.0, *)) {
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
            if (![scene isKindOfClass:[UIWindowScene class]]) continue;
            UIWindowScene *ws = (UIWindowScene *)scene;
            for (UIWindow *w in ws.windows) {
                if (w.isKeyWindow) return w.rootViewController;
            }
        }
    }
    // Fallback for iOS 13-14
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [UIApplication sharedApplication].keyWindow.rootViewController;
#pragma clang diagnostic pop
}

void shareFileOnIOS(const QString &filePath) {
    NSString *path = [NSString stringWithUTF8String:filePath.toUtf8().constData()];
    if (!path || path.length == 0) {
        qWarning("Invalid file path for sharing.");
        return;
    }

    void (^presentShareSheet)(void) = ^{
        @autoreleasepool {
            NSURL *fileURL = [NSURL fileURLWithPath:path];
            if (!fileURL) {
                qWarning("Invalid file URL for sharing.");
                return;
            }

            UIViewController *topVC = _rootViewController();
            while (topVC.presentedViewController && !topVC.presentedViewController.isBeingDismissed) {
                topVC = topVC.presentedViewController;
            }
            if (!topVC || !topVC.view) {
                qWarning("Could not find active view controller for sharing.");
                return;
            }

            NSArray *activityItems = @[fileURL];
            UIActivityViewController *activityVC = [[UIActivityViewController alloc] initWithActivityItems:activityItems applicationActivities:nil];

            // Required on iPad to prevent UIPopoverPresentationController NSGenericException crash
            UIPopoverPresentationController *popover = activityVC.popoverPresentationController;
            if (popover) {
                popover.sourceView = topVC.view;
                CGRect bounds = topVC.view.bounds;
                popover.sourceRect = CGRectMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds), 1.0, 1.0);
                popover.permittedArrowDirections = 0;
            }

            [topVC presentViewController:activityVC animated:YES completion:nil];
        }
    };

    if ([NSThread isMainThread]) {
        presentShareSheet();
    } else {
        dispatch_async(dispatch_get_main_queue(), presentShareSheet);
    }
}
#endif
