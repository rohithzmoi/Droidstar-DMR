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
import QtQuick.Controls

import "../theme"

// Toolbar action: icon always, text label only when there is room.
ToolButton {
    id: control

    property string glyph: ""
    property string label: ""
    property bool showLabel: false
    property color tone: t.text

    Tokens { id: t }

    implicitHeight: 44
    leftPadding: showLabel ? 10 : 8
    rightPadding: showLabel ? 12 : 8

    hoverEnabled: Qt.platform.os !== "ios" && Qt.platform.os !== "android"
    Accessible.name: label
    ToolTip.visible: hoverEnabled && hovered && !showLabel && label !== ""
    ToolTip.text: label
    ToolTip.delay: 500

    contentItem: Row {
        spacing: 6
        Label {
            text: control.glyph
            font.family: "FontAwesome"
            font.pixelSize: 16
            color: control.tone
            opacity: control.enabled ? 1 : 0.4
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            visible: control.showLabel && control.label !== ""
            text: control.label
            font.pixelSize: t.fsSmall
            color: control.tone
            opacity: control.enabled ? 1 : 0.4
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
