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

#import "ActionButtonManager.h"
#import <Foundation/Foundation.h>
#import <QtQml/QQmlApplicationEngine>
#import <UIKit/UIKit.h>

ActionButtonManager *globalManager = nil; // Define the globalManager

// Extern function to initialize the action button detection
extern "C" void initializeActionButtonDetection(QQmlApplicationEngine *engine) {
    if (!globalManager) {
        globalManager = [[ActionButtonManager alloc] initWithEngine:engine];
        [globalManager startListeningForActionButtonPress];
    }
}

// Extern function to trigger the action button
extern "C" void triggerActionButton() {
    if (globalManager) {
        [globalManager triggerTXButton];
    }
}

@implementation ActionButtonManager {
    QQmlApplicationEngine *_engine;  // Store the QML engine reference
}

- (instancetype)initWithEngine:(QQmlApplicationEngine*)engine {
    self = [super init];
    if (self) {
        _engine = engine;
    }
    return self;
}

- (void)startListeningForActionButtonPress {
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(handleActionButtonPress:)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
}

- (void)handleActionButtonPress:(NSNotification *)notification {
    [self triggerTXButton];
}

- (void)triggerTXButton {
    if (_engine) {
        QObject *rootObject = _engine->rootObjects().first();
        if (rootObject) {
            // Navigate the hierarchy to find _buttonTX
            QObject *swipeView = rootObject->findChild<QObject*>("swiper");
            if (swipeView) {
                QObject *mainTab = swipeView->findChild<QObject*>("mainTab");
                if (mainTab) {
                    QObject *txButton = mainTab->findChild<QObject*>("_buttonTX");
                    if (txButton) {
                        QMetaObject::invokeMethod(txButton, "clicked");
                    } else {
                        qDebug() << "TX button not found in MainTab.qml.";
                    }
                } else {
                    qDebug() << "MainTab.qml not found in SwipeView.";
                }
            } else {
                qDebug() << "SwipeView not found in main.qml.";
            }
        } else {
            qDebug() << "Root object not found in QML.";
        }
    } else {
        qDebug() << "QML engine is null.";
    }
}

@end
