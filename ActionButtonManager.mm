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
