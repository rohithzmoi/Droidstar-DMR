/*
    Copyright (C) 2025-2026 Rohith Namboothiri

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
import QtQuick.Templates as T
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

// tone: "primary" | "danger" | "success" | "warning" | "neutral"
T.Button {
    id: control

    property string tone: "neutral"
    property string glyph: ""
    property bool busy: false

    Tokens { id: t }

    readonly property color _fill: tone === "primary" ? t.accentStrong
                                 : tone === "danger" ? t.dangerStrong
                                 : tone === "success" ? "#059669"
                                 : tone === "warning" ? "#B45309"
                                 : t.surface
    readonly property bool _solid: tone !== "neutral"
    readonly property bool _hasIcon: control.glyph !== "" || control.busy
    readonly property real _iconWidth: control.busy ? 18 : (control.glyph !== "" ? Math.max(14, glyphLabel.implicitWidth) : 0)
    readonly property real _iconSpace: _hasIcon ? (_iconWidth + (control.text !== "" ? 8 : 0)) : 0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: t.controlHeight
    Layout.preferredWidth: implicitWidth
    Layout.minimumWidth: Layout.fillWidth ? Math.min(implicitWidth, 100) : implicitWidth
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: t.fsBody
    font.bold: _solid
    focusPolicy: Qt.TabFocus
    scale: down ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: t.durFast; easing.type: Easing.OutCubic } }

    Accessible.name: text

    contentItem: Item {
        implicitWidth: control._iconSpace + (control.text !== "" ? label.implicitWidth : 0)
        implicitHeight: Math.max(18, label.implicitHeight, glyphLabel.implicitHeight)
        clip: true

        Row {
            id: row
            anchors.centerIn: parent
            width: Math.min(parent.implicitWidth, parent.width > 0 ? parent.width : parent.implicitWidth)
            spacing: 8

            BusyIndicator {
                visible: control.busy
                running: control.busy
                width: 18; height: 18
                padding: 0
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: glyphLabel
                visible: control.glyph !== "" && !control.busy
                text: control.glyph
                font.family: "FontAwesome"
                font.pixelSize: control.font.pixelSize
                color: label.color
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: label
                visible: control.text !== ""
                text: control.text
                font: control.font
                color: control._solid ? "white" : t.text
                elide: Text.ElideRight
                width: row.parent.width > control._iconSpace
                       ? Math.min(implicitWidth, Math.max(0, row.parent.width - control._iconSpace))
                       : implicitWidth
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: 12
        opacity: control.enabled ? 1 : 0.45
        color: control.down ? Qt.darker(control._fill, control._solid ? 1.15 : 0.85) : control._fill
        border.color: control._solid ? Qt.lighter(control._fill, 1.25) : (control.visualFocus ? t.accent : t.stroke)
        border.width: 1
        Behavior on color { ColorAnimation { duration: t.durMed } }
    }
}
