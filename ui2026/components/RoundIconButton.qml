/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Controls

import "../theme"

// Circular glyph button. It never takes focus, so tapping it keeps the keyboard up.
Rectangle {
    id: root

    property string glyph: ""
    property color tone: t.text
    property string accessibleName: ""
    property bool filled: false
    property int glyphSize: 17
    // Optional count badge in the top-right corner (0 hides it)
    property int badge: 0
    signal clicked()

    Tokens { id: t }

    implicitWidth: 40
    implicitHeight: 40
    radius: width / 2
    color: filled ? (mouse.pressed ? Qt.darker(tone, 1.15) : tone)
                  : (mouse.pressed ? t.surface3 : "transparent")
    opacity: enabled ? 1 : 0.4
    scale: mouse.pressed ? 0.94 : 1
    Behavior on scale { NumberAnimation { duration: t.durFast } }

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: root.clicked()

    Label {
        anchors.centerIn: parent
        text: root.glyph
        font.family: "FontAwesome"
        font.pixelSize: root.glyphSize
        color: root.filled ? "white" : root.tone
    }

    Rectangle {
        visible: root.badge > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -2
        anchors.topMargin: -2
        width: Math.max(16, badgeLabel.implicitWidth + 8)
        height: 16
        radius: 8
        color: t.danger
        border.color: t.bg
        border.width: 1.5
        Label {
            id: badgeLabel
            anchors.centerIn: parent
            text: root.badge > 9 ? "9+" : root.badge
            color: "white"
            font.pixelSize: 9
            font.bold: true
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.margins: -4
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
