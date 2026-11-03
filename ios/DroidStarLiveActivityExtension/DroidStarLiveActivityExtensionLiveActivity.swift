/*
    Copyright (C) 2025-2026 Rohith Namboothiri

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

import ActivityKit
import WidgetKit
import SwiftUI

@available(iOS 16.1, *)
private enum DSRadioState {
    case tx
    case rx
    case idle

    init(state: DroidStarActivityAttributes.ContentState) {
        let m = state.mode.uppercased()
        if m == "TX" {
            self = .tx
        } else if m == "IDLE" || m == "STBY" || state.callsign.isEmpty || state.callsign == "Awaiting" {
            self = .idle
        } else {
            self = .rx
        }
    }

    var accentColor: Color {
        switch self {
        case .tx:
            return Color(red: 1.0, green: 0.27, blue: 0.27)
        case .rx:
            return Color(red: 0.16, green: 0.88, blue: 0.52)
        case .idle:
            return Color(red: 0.24, green: 0.72, blue: 0.98)
        }
    }

    var badgeText: String {
        switch self {
        case .tx: return "TX"
        case .rx: return "RX"
        case .idle: return "STBY"
        }
    }

    var statusTitle: String {
        switch self {
        case .tx: return "ON AIR • TRANSMITTING"
        case .rx: return "RECEIVING SIGNAL"
        case .idle: return "CONNECTED • STANDBY"
        }
    }

    var symbolName: String {
        switch self {
        case .tx: return "antenna.radiowaves.left.and.right"
        case .rx: return "waveform"
        case .idle: return "dot.radiowaves.left.and.right"
        }
    }
}

@available(iOS 16.1, *)
private struct SignalBarsView: View {
    let color: Color
    let active: Bool

    var body: some View {
        HStack(alignment: .bottom, spacing: 2.5) {
            RoundedRectangle(cornerRadius: 1.5)
                .fill(color.opacity(active ? 0.95 : 0.35))
                .frame(width: 3, height: 6)
            RoundedRectangle(cornerRadius: 1.5)
                .fill(color.opacity(active ? 0.95 : 0.30))
                .frame(width: 3, height: 10)
            RoundedRectangle(cornerRadius: 1.5)
                .fill(color.opacity(active ? 0.95 : 0.25))
                .frame(width: 3, height: 14)
            RoundedRectangle(cornerRadius: 1.5)
                .fill(color.opacity(active ? 0.85 : 0.20))
                .frame(width: 3, height: 18)
        }
    }
}

@available(iOS 16.1, *)
private struct DroidStarLockScreenBannerView: View {
    let state: DroidStarActivityAttributes.ContentState

    private var radioState: DSRadioState {
        DSRadioState(state: state)
    }

    private var primaryTitle: String {
        switch radioState {
        case .tx:
            return state.callsign.isEmpty ? "ON AIR" : state.callsign
        case .rx:
            return state.callsign
        case .idle:
            if !state.callsign.isEmpty && state.callsign != "Awaiting" {
                return state.callsign
            }
            return "DroidStar"
        }
    }

    private var subtitleLine: String {
        switch radioState {
        case .tx:
            var parts: [String] = []
            if !state.handle.isEmpty { parts.append(state.handle) }
            if !state.country.isEmpty { parts.append(state.country) }
            return parts.isEmpty ? "Push-to-Talk Active" : parts.joined(separator: " • ")
        case .rx:
            var parts: [String] = []
            if !state.handle.isEmpty { parts.append(state.handle) }
            if !state.country.isEmpty { parts.append(state.country) }
            return parts.isEmpty ? "Incoming Digital Voice" : parts.joined(separator: " • ")
        case .idle:
            var parts: [String] = []
            if !state.handle.isEmpty && state.handle != "RX..." { parts.append(state.handle) }
            if !state.country.isEmpty { parts.append(state.country) }
            return parts.isEmpty ? "Monitoring channel…" : parts.joined(separator: " • ")
        }
    }

    private var formattedTg: String {
        let tg = state.tgid.trimmingCharacters(in: .whitespacesAndNewlines)
        if tg.isEmpty { return "" }
        if tg.uppercased().hasPrefix("TG") { return tg }
        if Int(tg) != nil { return "TG \(tg)" }
        return tg
    }

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                Circle()
                    .fill(radioState.accentColor.opacity(0.18))
                    .frame(width: 46, height: 46)
                Circle()
                    .strokeBorder(radioState.accentColor.opacity(0.65), lineWidth: 1.5)
                    .frame(width: 46, height: 46)
                Image(systemName: radioState.symbolName)
                    .font(.system(size: 20, weight: .bold))
                    .foregroundColor(radioState.accentColor)
            }

            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 6) {
                    Text(radioState.statusTitle)
                        .font(.system(size: 10, weight: .heavy, design: .rounded))
                        .foregroundColor(radioState.accentColor)
                        .lineLimit(1)
                }

                Text(primaryTitle)
                    .font(.system(size: 21, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
                    .lineLimit(1)

                Text(subtitleLine)
                    .font(.system(size: 13, weight: .medium))
                    .foregroundColor(Color.white.opacity(0.78))
                    .lineLimit(1)
            }

            Spacer(minLength: 4)

            VStack(alignment: .trailing, spacing: 6) {
                if !formattedTg.isEmpty {
                    Text(formattedTg)
                        .font(.system(size: 12, weight: .bold, design: .monospaced))
                        .foregroundColor(radioState.accentColor)
                        .padding(.horizontal, 9)
                        .padding(.vertical, 4)
                        .background(
                            Capsule()
                                .fill(radioState.accentColor.opacity(0.16))
                        )
                        .overlay(
                            Capsule()
                                .strokeBorder(radioState.accentColor.opacity(0.45), lineWidth: 1)
                        )
                }

                HStack(spacing: 5) {
                    SignalBarsView(color: radioState.accentColor, active: radioState != .idle)
                    Text(state.timestamp, style: .timer)
                        .font(.system(size: 13, weight: .semibold, design: .monospaced))
                        .multilineTextAlignment(.trailing)
                        .foregroundColor(Color.white.opacity(0.85))
                        .frame(minWidth: 38, alignment: .trailing)
                }
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 14)
        .background(Color(red: 0.06, green: 0.08, blue: 0.12))
    }
}

@available(iOS 16.1, *)
struct DroidStarLiveActivityExtensionLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: DroidStarActivityAttributes.self) { context in
            DroidStarLockScreenBannerView(state: context.state)
                .activityBackgroundTint(Color(red: 0.06, green: 0.08, blue: 0.12))
                .activitySystemActionForegroundColor(.white)
        } dynamicIsland: { context in
            let radioState = DSRadioState(state: context.state)
            let tgRaw = context.state.tgid.trimmingCharacters(in: .whitespacesAndNewlines)
            let tgText = tgRaw.isEmpty ? "" : (Int(tgRaw) != nil ? "TG \(tgRaw)" : tgRaw)

            return DynamicIsland {
                DynamicIslandExpandedRegion(.leading) {
                    HStack(spacing: 6) {
                        HStack(spacing: 4) {
                            Image(systemName: radioState.symbolName)
                                .font(.system(size: 12, weight: .bold))
                            Text(radioState.badgeText)
                                .font(.system(size: 12, weight: .heavy, design: .rounded))
                        }
                        .foregroundColor(.black)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(
                            Capsule().fill(radioState.accentColor)
                        )

                        if !tgText.isEmpty {
                            Text(tgText)
                                .font(.system(size: 12, weight: .bold, design: .monospaced))
                                .foregroundColor(radioState.accentColor)
                                .lineLimit(1)
                        }
                    }
                    .padding(.leading, 4)
                }

                DynamicIslandExpandedRegion(.trailing) {
                    HStack(spacing: 6) {
                        SignalBarsView(color: radioState.accentColor, active: radioState != .idle)
                        Text(context.state.timestamp, style: .timer)
                            .font(.system(size: 13, weight: .bold, design: .monospaced))
                            .multilineTextAlignment(.trailing)
                            .foregroundColor(radioState.accentColor)
                            .frame(minWidth: 42, alignment: .trailing)
                    }
                    .padding(.trailing, 4)
                }

                DynamicIslandExpandedRegion(.bottom) {
                    HStack(alignment: .center, spacing: 12) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(
                                radioState == .idle
                                ? (context.state.callsign.isEmpty || context.state.callsign == "Awaiting" ? "DroidStar Standby" : "\(context.state.callsign) • Standby")
                                : (context.state.callsign.isEmpty ? "ON AIR" : context.state.callsign)
                            )
                            .font(.system(size: 20, weight: .bold, design: .monospaced))
                            .foregroundColor(.white)
                            .lineLimit(1)

                            let detail: String = {
                                var parts: [String] = []
                                if !context.state.handle.isEmpty && context.state.handle != "RX..." {
                                    parts.append(context.state.handle)
                                }
                                if !context.state.country.isEmpty {
                                    parts.append(context.state.country)
                                }
                                if parts.isEmpty {
                                    return radioState == .tx ? "Transmitting Voice" : (radioState == .rx ? "Receiving Voice" : "Listening for activity")
                                }
                                return parts.joined(separator: " • ")
                            }()

                            Text(detail)
                                .font(.system(size: 13, weight: .medium))
                                .foregroundColor(Color.white.opacity(0.78))
                                .lineLimit(1)
                        }

                        Spacer(minLength: 4)

                        if !context.state.country.isEmpty && radioState == .rx {
                            Text(context.state.country)
                                .font(.system(size: 11, weight: .semibold))
                                .foregroundColor(.white.opacity(0.9))
                                .padding(.horizontal, 8)
                                .padding(.vertical, 4)
                                .background(
                                    Capsule()
                                        .fill(Color.white.opacity(0.12))
                                )
                                .lineLimit(1)
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(
                        RoundedRectangle(cornerRadius: 14, style: .continuous)
                            .fill(Color.white.opacity(0.07))
                    )
                }
            } compactLeading: {
                HStack(spacing: 4) {
                    Image(systemName: radioState.symbolName)
                        .font(.system(size: 12, weight: .bold))
                        .foregroundColor(radioState.accentColor)
                    Text(radioState.badgeText)
                        .font(.system(size: 12, weight: .heavy, design: .rounded))
                        .foregroundColor(radioState.accentColor)
                }
                .padding(.leading, 2)
            } compactTrailing: {
                Group {
                    switch radioState {
                    case .tx:
                        Text(context.state.callsign.isEmpty ? "ON AIR" : context.state.callsign)
                            .font(.system(size: 13, weight: .bold, design: .monospaced))
                            .foregroundColor(radioState.accentColor)
                            .lineLimit(1)
                    case .rx:
                        HStack(spacing: 4) {
                            Text(context.state.callsign)
                                .font(.system(size: 13, weight: .bold, design: .monospaced))
                                .foregroundColor(.white)
                                .lineLimit(1)
                            if !context.state.handle.isEmpty {
                                Text(context.state.handle)
                                    .font(.system(size: 11, weight: .medium))
                                    .foregroundColor(radioState.accentColor)
                                    .lineLimit(1)
                            }
                        }
                    case .idle:
                        Text(!tgText.isEmpty ? tgText : (!context.state.callsign.isEmpty && context.state.callsign != "Awaiting" ? context.state.callsign : "Standby"))
                            .font(.system(size: 12, weight: .semibold, design: .monospaced))
                            .foregroundColor(Color.white.opacity(0.8))
                            .lineLimit(1)
                    }
                }
                .padding(.trailing, 2)
            } minimal: {
                Image(systemName: radioState.symbolName)
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(radioState.accentColor)
            }
            .keylineTint(radioState.accentColor)
        }
    }
}
