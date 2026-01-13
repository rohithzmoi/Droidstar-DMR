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

import Foundation
import ActivityKit

// Shared between the main app target and the Widget Extension target.
// Add this file to BOTH targets in Xcode.

@available(iOS 16.1, *)
public struct DroidStarActivityAttributes: ActivityAttributes {
    public typealias DroidStarActivityStatus = ContentState

    public struct ContentState: Codable, Hashable {
        public var mode: String            // "RX" or "TX"
        public var callsign: String
        public var handle: String
        public var country: String
        public var tgid: String
        public var timestamp: Date

        public init(mode: String, callsign: String, handle: String, country: String, tgid: String, timestamp: Date) {
            self.mode = mode
            self.callsign = callsign
            self.handle = handle
            self.country = country
            self.tgid = tgid
            self.timestamp = timestamp
        }
    }

    public init() {}
}

