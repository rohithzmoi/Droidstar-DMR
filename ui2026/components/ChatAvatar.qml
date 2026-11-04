/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

// Round glyph avatar used for conversations and stations.
Rectangle {
    id: root

    property string glyph: "\uf007"
    property color tone: t.accent
    property int size: 40

    Tokens { id: t }

    Layout.preferredWidth: size
    Layout.preferredHeight: size
    implicitWidth: size
    implicitHeight: size
    radius: size / 2
    color: t.tint(tone, 0.16)
    border.color: t.tint(tone, 0.4)
    border.width: 1

    Label {
        anchors.centerIn: parent
        text: root.glyph
        font.family: "FontAwesome"
        font.pixelSize: Math.round(root.size * 0.4)
        color: root.tone
    }
}
