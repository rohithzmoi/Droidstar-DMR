import QtQuick
import QtQuick.Controls.Material

QtObject {
    // Centralized design tokens (keep lightweight; expand as screens migrate).
    readonly property color bg: "#121212"
    readonly property color surface: "#1E1E1E"
    readonly property color surface2: "#252525"
    readonly property color text: "#FFFFFF"
    readonly property color textSecondary: "#BDBDBD"
    readonly property color accent: "steelblue"
    readonly property color divider: "#2E2E2E"

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20

    readonly property int radius1: 10
    readonly property int radius2: 16
}

