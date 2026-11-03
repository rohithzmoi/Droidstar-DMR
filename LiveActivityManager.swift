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

// IMPORTANT:
// We explicitly set the Objective-C runtime name so ObjC/ObjC++ can link against
// `_OBJC_CLASS_$_LiveActivityManager` (Qt/qmake links Swift into a C++ binary).
@available(iOS 16.1, *)
@objc(LiveActivityManager)
class LiveActivityManager: NSObject {
    @objc static let shared = LiveActivityManager()

    private var activity: Activity<DroidStarActivityAttributes>?
    private let activityQueue = DispatchQueue(label: "com.droidstar.liveactivity", qos: .userInitiated)

    // Track last pushed state to avoid redundant ActivityKit IPC calls and preserve over timer
    private var lastMode: String = ""
    private var lastCallsign: String = ""
    private var lastHandle: String = ""
    private var lastCountry: String = ""
    private var lastTgid: String = ""
    private var currentOverStartDate: Date = Date()

    private override init() {
        super.init()
    }

    // MARK: - Public API

    /// Check if Live Activities / Dynamic Island are enabled on this device
    @objc static var isDynamicIslandAvailable: Bool {
        let enabled = ActivityAuthorizationInfo().areActivitiesEnabled
        if !enabled {
            print("[DroidStar][LiveActivity] areActivitiesEnabled == false")
        }
        return enabled
    }

    /// Start or update a live activity with QSO details
    /// mode: "RX", "TX", or "IDLE"
    @objc(startOrUpdateLiveActivityWithMode:callsign:handle:country:tgid:)
    func startOrUpdateLiveActivity(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        activityQueue.async { [weak self] in
            self?._startOrUpdateActivity(mode: mode, callsign: callsign, handle: handle, country: country, tgid: tgid)
        }
    }

    /// End the current live activity
    @objc func endLiveActivity() {
        activityQueue.async { [weak self] in
            self?._endActivity()
        }
    }

    /// End ALL live activities (cleanup orphans from previous runs)
    @objc func endAllActivities() {
        activityQueue.async { [weak self] in
            self?._endAllActivities()
        }
    }

    /// Update QSO details in the live activity
    @objc(updateQsoDetailsWithMode:callsign:handle:country:tgid:)
    func updateQsoDetails(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        startOrUpdateLiveActivity(mode: mode, callsign: callsign, handle: handle, country: country, tgid: tgid)
    }

    // Backwards-compatible ObjC selectors (older code paths)
    @objc(startOrUpdateLiveActivityWithCallsign:handle:country:)
    func startOrUpdateLiveActivity(callsign: String, handle: String, country: String) {
        startOrUpdateLiveActivity(mode: "RX", callsign: callsign, handle: handle, country: country, tgid: "")
    }

    @objc(updateQsoDetailsWithCallsign:handle:country:)
    func updateQsoDetails(callsign: String, handle: String, country: String) {
        startOrUpdateLiveActivity(mode: "RX", callsign: callsign, handle: handle, country: country, tgid: "")
    }

    // MARK: - Private Methods

    @available(iOS 16.1, *)
    private func _resolveActiveActivity() -> Activity<DroidStarActivityAttributes>? {
        if let current = activity {
            switch current.activityState {
            case .ended, .dismissed:
                self.activity = nil
            default:
                return current
            }
        }

        // Reuse an already-active activity from ActivityKit if one exists
        if let existing = Activity<DroidStarActivityAttributes>.activities.first(where: {
            $0.activityState != .ended && $0.activityState != .dismissed
        }) {
            self.activity = existing
            return existing
        }

        return nil
    }

    @available(iOS 16.1, *)
    private func _startOrUpdateActivity(mode: String, callsign: String, handle: String, country: String, tgid: String) {
        guard ActivityAuthorizationInfo().areActivitiesEnabled else {
            return
        }

        // Only reset the elapsed timer when the mode or active callsign changes
        let stateTransitioned = (mode != lastMode) || (callsign != lastCallsign)
        if stateTransitioned {
            currentOverStartDate = Date()
        }

        let unchanged = !stateTransitioned &&
                        handle == lastHandle &&
                        country == lastCountry &&
                        tgid == lastTgid

        let state = DroidStarActivityAttributes.ContentState(
            mode: mode,
            callsign: callsign,
            handle: handle,
            country: country,
            tgid: tgid,
            timestamp: currentOverStartDate
        )

        if let activeActivity = _resolveActiveActivity() {
            if unchanged {
                return
            }
            lastMode = mode
            lastCallsign = callsign
            lastHandle = handle
            lastCountry = country
            lastTgid = tgid

            Task {
                if #available(iOS 16.2, *) {
                    let content = ActivityContent(state: state, staleDate: nil)
                    await activeActivity.update(content)
                } else {
                    await activeActivity.update(using: state)
                }
            }
        } else {
            // Starting a new activity
            lastMode = mode
            lastCallsign = callsign
            lastHandle = handle
            lastCountry = country
            lastTgid = tgid

            do {
                let attributes = DroidStarActivityAttributes()
                let newActivity: Activity<DroidStarActivityAttributes>
                if #available(iOS 16.2, *) {
                    let content = ActivityContent(state: state, staleDate: nil)
                    newActivity = try Activity.request(
                        attributes: attributes,
                        content: content,
                        pushType: nil
                    )
                } else {
                    newActivity = try Activity.request(
                        attributes: attributes,
                        contentState: state,
                        pushType: nil
                    )
                }

                self.activity = newActivity
                let keepId = newActivity.id
                print("[DroidStar][LiveActivity] Started Live Activity: \(keepId) mode=\(mode) callsign=\(callsign)")

                // Clean up any other orphan activities without touching the newly created one
                Task {
                    for existing in Activity<DroidStarActivityAttributes>.activities where existing.id != keepId {
                        if #available(iOS 16.2, *) {
                            await existing.end(nil, dismissalPolicy: .immediate)
                        } else {
                            await existing.end(dismissalPolicy: .immediate)
                        }
                    }
                }
            } catch {
                print("[DroidStar][LiveActivity] Error starting Live Activity: \(error)")
            }
        }
    }

    @available(iOS 16.1, *)
    private func _endActivity() {
        lastMode = ""
        lastCallsign = ""
        lastHandle = ""
        lastCountry = ""
        lastTgid = ""

        let current = activity
        self.activity = nil

        Task {
            if let current = current {
                if #available(iOS 16.2, *) {
                    await current.end(nil, dismissalPolicy: .immediate)
                } else {
                    await current.end(dismissalPolicy: .immediate)
                }
            }
            for existing in Activity<DroidStarActivityAttributes>.activities {
                if #available(iOS 16.2, *) {
                    await existing.end(nil, dismissalPolicy: .immediate)
                } else {
                    await existing.end(dismissalPolicy: .immediate)
                }
            }
            print("[DroidStar][LiveActivity] Ended Live Activity")
        }
    }

    @available(iOS 16.1, *)
    private func _endAllActivities() {
        lastMode = ""
        lastCallsign = ""
        lastHandle = ""
        lastCountry = ""
        lastTgid = ""
        self.activity = nil

        Task {
            for existingActivity in Activity<DroidStarActivityAttributes>.activities {
                if #available(iOS 16.2, *) {
                    await existingActivity.end(nil, dismissalPolicy: .immediate)
                } else {
                    await existingActivity.end(dismissalPolicy: .immediate)
                }
            }
            print("[DroidStar][LiveActivity] All activities ended")
        }
    }
}
