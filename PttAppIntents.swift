/*
    Copyright (C) 2026 Rohith Namboothiri

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

// App Intents: lets the iPhone Action Button, Apple Watch Ultra Action Button (via a shortcut),
// Siri, Shortcuts automations and the Back Tap gesture key DroidStar. The intents run inside the
// app process, so a press works while the app is in the background with the screen off.

import Foundation
import AppIntents

// Implemented in pttbuttonmanager.cpp. kind: 0 = toggle, 1 = stop transmitting.
@_silgen_name("ptt_intent_event")
func ptt_intent_event(_ kind: Int32)

@available(iOS 16.0, *)
struct DroidStarPttToggleIntent: AppIntent {
    static var title: LocalizedStringResource = "Push to Talk"
    static var description = IntentDescription("Starts or stops transmitting in DroidStar.")
    static var openAppWhenRun: Bool = false

    func perform() async throws -> some IntentResult {
        ptt_intent_event(0)
        return .result()
    }
}

@available(iOS 16.0, *)
struct DroidStarPttStopIntent: AppIntent {
    static var title: LocalizedStringResource = "Stop Transmitting"
    static var description = IntentDescription("Stops transmitting in DroidStar.")
    static var openAppWhenRun: Bool = false

    func perform() async throws -> some IntentResult {
        ptt_intent_event(1)
        return .result()
    }
}

@available(iOS 16.0, *)
struct DroidStarShortcuts: AppShortcutsProvider {
    static var appShortcuts: [AppShortcut] {
        AppShortcut(
            intent: DroidStarPttToggleIntent(),
            phrases: ["Push to talk in \(.applicationName)", "Toggle transmit in \(.applicationName)"],
            shortTitle: "Push to Talk",
            systemImageName: "mic.fill"
        )
        AppShortcut(
            intent: DroidStarPttStopIntent(),
            phrases: ["Stop transmitting in \(.applicationName)"],
            shortTitle: "Stop TX",
            systemImageName: "mic.slash.fill"
        )
    }
}
