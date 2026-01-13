import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Pane {
    id: card

    // A simple reusable “card” surface for the new UI.
    Material.elevation: 1
    padding: 12

    background: Rectangle {
        color: Material.background
        radius: 12
        border.color: "#2E2E2E"
        border.width: 1
    }
}

