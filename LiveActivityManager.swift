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
// Note: SwiftUI and Widget struct moved to separate Widget Extension target
// For now, we only need ActivityKit for the LiveActivityManager

// IMPORTANT:
// We explicitly set the Objective-C runtime name so ObjC/ObjC++ can link against
// `_OBJC_CLASS_$_LiveActivityManager` (Qt/qmake links Swift into a C++ binary).
@available(iOS 16.1, *)
@objc(LiveActivityManager)
class LiveActivityManager: NSObject {
    // Singleton instance
    @objc static let shared = LiveActivityManager()
    
    private var activity: Activity<DroidStarActivityAttributes>?
    private let activityQueue = DispatchQueue(label: "com.droiddstar.liveactivity")
    
    private override init() {
        super.init()
    }
    
    // MARK: - Public API
    
    /// Check if Dynamic Island is available on this device
    @objc static var isDynamicIslandAvailable: Bool {
        if #available(iOS 16.1, *) {
            let enabled = ActivityAuthorizationInfo().areActivitiesEnabled
            if !enabled {
                print("[DroidStar][LiveActivity] areActivitiesEnabled == false")
            }
            return enabled
        }
        return false
    }
    
    /// Start or update a live activity with QSO details
    /// mode: "RX" or "TX"
    @objc(startOrUpdateLiveActivityWithMode:callsign:handle:country:tgid:)
    func startOrUpdateLiveActivity(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        if #available(iOS 16.1, *) {
            activityQueue.async { [weak self] in
                self?._startOrUpdateActivity(mode: mode, callsign: callsign, handle: handle, country: country, tgid: tgid)
            }
        }
    }
    
    /// End the current live activity
    @objc func endLiveActivity() {
        if #available(iOS 16.1, *) {
            activityQueue.async { [weak self] in
                self?._endActivity()
            }
        }
    }
    
    /// End ALL live activities (cleanup orphans from previous runs)
    @objc func endAllActivities() {
        if #available(iOS 16.1, *) {
            activityQueue.async { [weak self] in
                self?._endAllActivities()
            }
        }
    }
    
    /// Update QSO details in the live activity
    @objc(updateQsoDetailsWithMode:callsign:handle:country:tgid:)
    func updateQsoDetails(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        if #available(iOS 16.1, *) {
            activityQueue.async { [weak self] in
                self?._updateActivity(mode: mode, callsign: callsign, handle: handle, country: country, tgid: tgid)
            }
        }
    }

    // Backwards-compatible ObjC selectors (older code paths)
    @objc(startOrUpdateLiveActivityWithCallsign:handle:country:)
    func startOrUpdateLiveActivity(callsign: String, handle: String, country: String) {
        startOrUpdateLiveActivity(mode: "RX", callsign: callsign, handle: handle, country: country, tgid: "")
    }

    @objc(updateQsoDetailsWithCallsign:handle:country:)
    func updateQsoDetails(callsign: String, handle: String, country: String) {
        updateQsoDetails(mode: "RX", callsign: callsign, handle: handle, country: country, tgid: "")
    }
    
    // MARK: - Private Methods
    
    @available(iOS 16.1, *)
    private func _startOrUpdateActivity(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        let attributes = DroidStarActivityAttributes()
        let state = DroidStarActivityAttributes.ContentState(
            mode: mode,
            callsign: callsign,
            handle: handle,
            country: country,
            tgid: tgid,
            timestamp: Date()
        )
        
        if let activity = activity {
            // Update existing activity
            Task {
                await activity.update(using: state)
                print("[DroidStar][LiveActivity] Updating content for activity \(activity.id)")
            }
        } else {
            // End any stale/orphan activities from previous runs before starting a new one
            Task {
                for existingActivity in Activity<DroidStarActivityAttributes>.activities {
                    print("[DroidStar][LiveActivity] Ending stale activity: \(existingActivity.id)")
                    await existingActivity.end(dismissalPolicy: .immediate)
                }
            }
            
            // Start new activity
            do {
                print("[DroidStar][LiveActivity] Requesting Live Activity mode=\(mode) callsign=\(callsign) handle=\(handle) country=\(country) tgid=\(tgid)")
                let newActivity = try Activity.request(
                    attributes: attributes,
                    contentState: state,
                    pushType: nil
                )
                
                activity = newActivity
                print("[DroidStar][LiveActivity] Started Live Activity: \(newActivity.id)")
            } catch {
                print("[DroidStar][LiveActivity] Error starting Live Activity: \(error)")
            }
        }
    }
    
    @available(iOS 16.1, *)
    private func _updateActivity(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        guard activity != nil else { return }
        
        let state = DroidStarActivityAttributes.ContentState(
            mode: mode,
            callsign: callsign,
            handle: handle,
            country: country,
            tgid: tgid,
            timestamp: Date()
        )
        
        Task {
            await activity?.update(using: state)
        }
    }
    
    @available(iOS 16.1, *)
    private func _endActivity() {
        guard let activity = activity else { return }
        
        Task {
            await activity.end(dismissalPolicy: .default)
            self.activity = nil
            print("[DroidStar][LiveActivity] Ended Live Activity: \(activity.id)")
        }
    }
    
    @available(iOS 16.1, *)
    private func _endAllActivities() {
        Task {
            for existingActivity in Activity<DroidStarActivityAttributes>.activities {
                print("[DroidStar][LiveActivity] Ending activity: \(existingActivity.id)")
                await existingActivity.end(dismissalPolicy: .immediate)
            }
            self.activity = nil
            print("[DroidStar][LiveActivity] All activities ended")
        }
    }
}
