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

#ifdef Q_OS_IOS
#import <UIKit/UIKit.h>

// Function to share the file on iOS
void shareFileOnIOS(const QString &filePath) {
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:filePath.toUtf8().constData()];
        NSURL *fileURL = [NSURL fileURLWithPath:path];

        if (!fileURL) {
            qWarning("Invalid file URL for sharing.");
            return;
        }

        UIViewController *rootViewController = [UIApplication sharedApplication].keyWindow.rootViewController;

        // Use UIActivityViewController to share the file
        NSArray *activityItems = @[fileURL];
        UIActivityViewController *activityVC = [[UIActivityViewController alloc] initWithActivityItems:activityItems applicationActivities:nil];

        // Present the share sheet
        [rootViewController presentViewController:activityVC animated:YES completion:nil];
    }
}
#endif
