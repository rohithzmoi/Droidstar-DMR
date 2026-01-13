import QtQuick
import QtQuick.Controls

TabButton {
    id: root

    // Provided by the parent (FontLoader name).
    property string iconFontFamily: ""
    property string iconText: ""
    property string labelText: ""

    padding: 6

    contentItem: Column {
        spacing: 2
        anchors.centerIn: parent

        Text {
            text: root.iconText
            font.family: root.iconFontFamily
            font.pointSize: 18
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        Text {
            text: root.labelText
            font.pointSize: 10
            color: "white"
            opacity: root.checked ? 1.0 : 0.75
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }
    }
}

