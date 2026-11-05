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

// Whole-row tappable switch tile.
Rectangle {
    id: root

    property string text: ""
    property string description: ""
    property bool checked: false
    property color activeColor: t.accent

    signal toggled(bool checked)

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredWidth: 100
    implicitHeight: Math.max(t.controlHeight + 6, textCol.implicitHeight + 18)
    implicitWidth: textCol.implicitWidth + sw.implicitWidth + 36
    radius: 12
    opacity: enabled ? 1 : 0.5
    color: root.checked ? t.tint(root.activeColor, 0.14) : (rowMouse.pressed ? t.surface3 : t.surface)
    border.color: root.checked ? t.tint(root.activeColor, 0.65) : t.stroke
    border.width: 1
    Behavior on color { ColorAnimation { duration: t.durFast } }
    Behavior on border.color { ColorAnimation { duration: t.durFast } }

    Accessible.role: Accessible.CheckBox
    Accessible.name: root.text
    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.onToggleAction: root.toggled(!root.checked)
    Accessible.onPressAction: root.toggled(!root.checked)

    Tokens { id: t }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 6
        spacing: 8

        ColumnLayout {
            id: textCol
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: root.text
                font.pixelSize: t.fsBody
                color: t.text
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: root.description !== ""
                text: root.description
                font.pixelSize: t.fsCaption
                color: t.textMuted
                wrapMode: Text.WordWrap
            }
        }

        Switch {
            id: sw
            checked: root.checked
            focusPolicy: Qt.TabFocus
            onToggled: {
                root.toggled(checked)
                // A click breaks the binding; restore it so later state changes (e.g. auto IPv6 off) still show.
                checked = Qt.binding(function() { return root.checked })
            }
        }
    }
}
