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

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface LiveActivityManager : NSObject

+ (instancetype)shared;
+ (BOOL)isDynamicIslandAvailable;
- (void)startOrUpdateLiveActivityWithMode:(NSString *)mode
                                 callsign:(NSString *)callsign
                                   handle:(NSString *)handle
                                  country:(NSString *)country
                                      tgid:(NSString *)tgid;
- (void)endLiveActivity;
- (void)updateQsoDetailsWithMode:(NSString *)mode
                        callsign:(NSString *)callsign
                          handle:(NSString *)handle
                         country:(NSString *)country
                             tgid:(NSString *)tgid;

@end

NS_ASSUME_NONNULL_END
