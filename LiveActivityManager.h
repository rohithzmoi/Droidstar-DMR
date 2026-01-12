#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface LiveActivityManager : NSObject

+ (instancetype)shared;
+ (BOOL)isDynamicIslandAvailable;
- (void)startOrUpdateLiveActivityWithCallsign:(NSString *)callsign 
                                      handle:(NSString *)handle 
                                    country:(NSString *)country;
- (void)endLiveActivity;
- (void)updateQsoDetailsWithCallsign:(NSString *)callsign 
                             handle:(NSString *)handle 
                           country:(NSString *)country;

@end

NS_ASSUME_NONNULL_END
