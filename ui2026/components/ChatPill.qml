/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Controls

import "../theme"

Rectangle {
    id: root

    property string text: ""
    property color tone: t.accent

    Tokens { id: t }

    implicitWidth: pillText.implicitWidth + 12
    implicitHeight: 20
    radius: 6
    color: t.tint(tone, 0.13)
    border.color: t.tint(tone, 0.45)
    border.width: 1

    Label {
        id: pillText
        anchors.centerIn: parent
        text: root.text
        color: root.tone
        font.pixelSize: 10
        font.bold: true
    }
}
