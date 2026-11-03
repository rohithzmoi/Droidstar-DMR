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
import QtQuick.Layouts

import "../theme"

Rectangle {
    id: root

    property string text: ""
    property string glyph: ""
    property bool checked: false
    property color activeColor: t.accent
    property string holdDescription: ""

    signal clicked()
    signal pressAndHold()

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: 40
    implicitWidth: chipRow.implicitWidth + 24
    radius: 10
    opacity: enabled ? 1 : 0.45
    color: root.checked ? t.tint(root.activeColor, 0.22) : (chipMouse.pressed ? t.surface3 : t.surface)
    border.color: root.checked ? root.activeColor : t.stroke
    border.width: 1
    scale: chipMouse.pressed ? 0.97 : 1
    Behavior on color { ColorAnimation { duration: t.durFast } }
    Behavior on scale { NumberAnimation { duration: t.durFast; easing.type: Easing.OutCubic } }

    Accessible.role: Accessible.CheckBox
    Accessible.name: root.text
    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.onPressAction: root.clicked()
    Accessible.onToggleAction: root.clicked()

    Tokens { id: t }

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: 6
        Label {
            visible: root.glyph !== ""
            text: root.glyph
            font.family: "FontAwesome"
            font.pixelSize: 12
            color: root.checked ? root.activeColor : t.textMuted
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: root.text
            font.pixelSize: t.fsSmall
            font.bold: root.checked
            color: root.checked ? t.text : t.textMuted
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: chipMouse
        anchors.fill: parent
        hoverEnabled: Qt.platform.os !== "ios" && Qt.platform.os !== "android"
        pressAndHoldInterval: 380
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        onPressAndHold: {
            root.pressAndHold()
            if (root.holdDescription !== "") {
                holdTip.show(root.holdDescription, 4500)
            }
        }
    }

    ToolTip {
        id: holdTip
        visible: chipMouse.hoverEnabled && root.holdDescription !== "" && chipMouse.containsMouse && !chipMouse.pressed
        delay: 450
        timeout: 4500
        text: root.holdDescription
    }
}
