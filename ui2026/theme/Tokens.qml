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

import QtQuick
import QtQuick.Controls.Material

QtObject {
    // 2026 dark theme tokens (Material-friendly, iOS readable)
    readonly property color bg: "#0B0F14"
    readonly property color surface: "#111827"
    readonly property color surface2: "#0F172A"
    readonly property color surface3: "#1A2334"
    readonly property color stroke: "#233044"
    readonly property color strokeStrong: "#334155"
    readonly property color text: "#E5E7EB"
    readonly property color textMuted: "#9CA3AF"
    readonly property color accent: "#60A5FA"     // blue-400
    readonly property color accentStrong: "#2563EB" // blue-600 (white text passes contrast)
    readonly property color success: "#34D399"    // emerald-400
    readonly property color danger: "#F87171"     // red-400
    readonly property color dangerStrong: "#DC2626"
    readonly property color warning: "#FBBF24"    // amber-400

    readonly property color txIdle: "#1D4ED8"
    readonly property color txIdleBorder: "#3B82F6"
    readonly property color txLive: "#B91C1C"
    readonly property color txLiveBorder: "#EF4444"

    readonly property int rSm: 12
    readonly property int rMd: 16
    readonly property int rLg: 22

    readonly property int s1: 4
    readonly property int s2: 8
    readonly property int s3: 12
    readonly property int s4: 16
    readonly property int s5: 20
    readonly property int s6: 24

    // Minimum comfortable touch target (Apple HIG / Material)
    readonly property int controlHeight: 44
    // Forms read poorly when stretched across a tablet/desktop window
    readonly property int maxContentWidth: 860
    // Width at which the navigation drawer docks as a permanent sidebar
    readonly property int dockedNavBreakpoint: 900

    readonly property int fsCaption: 11
    readonly property int fsSmall: 12
    readonly property int fsBody: 14
    readonly property int fsTitle: 16

    readonly property int durFast: 110
    readonly property int durMed: 180

    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
