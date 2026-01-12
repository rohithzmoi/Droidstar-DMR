import Foundation
import ActivityKit
import SwiftUI

@available(iOS 16.1, *)
@objc class LiveActivityManager: NSObject {
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
            return ActivityAuthorizationInfo().areActivitiesEnabled
        }
        return false
    }
    
    /// Start or update a live activity with QSO details
    @objc func startOrUpdateLiveActivity(callsign: String, handle: String, country: String) {
        if #available(iOS 16.1, *) {
            activityQueue.async { [weak self] in
                self?._startOrUpdateActivity(callsign: callsign, handle: handle, country: country)
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
    
    /// Update QSO details in the live activity
    @objc func updateQsoDetails(callsign: String, handle: String, country: String) {
        if #available(iOS 16.1, *) {
            activityQueue.async { [weak self] in
                self?._updateActivity(callsign: callsign, handle: handle, country: country)
            }
        }
    }
    
    // MARK: - Private Methods
    
    @available(iOS 16.1, *)
    private func _startOrUpdateActivity(callsign: String, handle: String, country: String) {
        let attributes = DroidStarActivityAttributes()
        let state = DroidStarActivityAttributes.ContentState(
            callsign: callsign,
            handle: handle,
            country: country,
            timestamp: Date()
        )
        
        if let activity = activity {
            // Update existing activity
            Task {
                await activity.update(using: state)
            }
        } else {
            // Start new activity
            do {
                let newActivity = try Activity.request(
                    attributes: attributes,
                    contentState: state,
                    pushType: nil
                )
                
                activity = newActivity
                print("Started Live Activity: \(newActivity.id)")
            } catch {
                print("Error starting Live Activity: \(error.localizedDescription)")
            }
        }
    }
    
    @available(iOS 16.1, *)
    private func _updateActivity(callsign: String, handle: String, country: String) {
        guard activity != nil else { return }
        
        let state = DroidStarActivityAttributes.ContentState(
            callsign: callsign,
            handle: handle,
            country: country,
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
            print("Ended Live Activity: \(activity.id)")
        }
    }
}

// MARK: - Activity Attributes

@available(iOS 16.1, *)
struct DroidStarActivityAttributes: ActivityAttributes {
    public typealias DroidStarActivityStatus = ContentState
    
    public struct ContentState: Codable, Hashable {
        var callsign: String
        var handle: String
        var country: String
        var timestamp: Date
    }
    
    // Additional attributes that won't change during the activity
    // Can be used for static data
}

// MARK: - Live Activity UI

@available(iOS 16.1, *)
struct DroidStarLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: DroidStarActivityAttributes.self) { context in
            // Lock screen/banner UI goes here
            VStack(alignment: .leading) {
                HStack {
                    VStack(alignment: .leading) {
                        Text("\(context.state.callsign)")
                            .font(.headline)
                        Text("\(context.state.handle)")
                            .font(.subheadline)
                        Text("\(context.state.country)")
                            .font(.caption)
                    }
                    Spacer()
                    Text(context.state.timestamp, style: .time)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .padding()
            .activityBackgroundTint(Color.black.opacity(0.8))
            .activitySystemActionForegroundColor(Color.white)
            
        } dynamicIsland: { context in
            // Dynamic Island UI goes here
            DynamicIsland {
                // Expanded UI when tapped or when in expanded state
                DynamicIslandExpandedRegion(.leading) {
                    Image(systemName: "radio")
                        .foregroundColor(.blue)
                }
                
                DynamicIslandExpandedRegion(.trailing) {
                    Text(context.state.timestamp, style: .time)
                        .foregroundColor(.secondary)
                }
                
                DynamicIslandExpandedRegion(.center) {
                    Text("\(context.state.callsign)")
                        .font(.headline)
                }
                
                DynamicIslandExpandedRegion(.bottom) {
                    HStack {
                        Text("\(context.state.handle)")
                        Spacer()
                        Text("\(context.state.country)")
                    }
                    .font(.subheadline)
                }
            } compactLeading: {
                // Compact leading view (minimal)
                Image(systemName: "radio")
                    .foregroundColor(.blue)
            } compactTrailing: {
                // Compact trailing view (minimal)
                Text(context.state.callsign.prefix(3))
                    .font(.caption)
            } minimal: {
                // Minimal view (when multiple activities are active)
                Image(systemName: "radio")
                    .foregroundColor(.blue)
            }
            .keylineTint(.blue)
        }
    }
}
