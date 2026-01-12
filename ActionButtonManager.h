// ActionButtonManager.h
#import <Foundation/Foundation.h>
#import <QtQml/QQmlApplicationEngine>
#import <UIKit/UIKit.h>

@interface ActionButtonManager : NSObject

- (instancetype)initWithEngine:(QQmlApplicationEngine*)engine;
- (void)startListeningForActionButtonPress;
- (void)triggerTXButton;

@end

// Extern declaration of globalManager
extern ActionButtonManager *globalManager;
