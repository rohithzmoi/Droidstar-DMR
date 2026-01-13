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

Item {
    id: root

    property string title: ""
    // Optional status string shown in the header (e.g. "Connected")
    property string statusText: ""
    // If true, show status only when collapsed (recommended for compact headers).
    property bool statusWhenCollapsedOnly: true
    property bool expanded: true
    property alias contentItem: contentLoader.sourceComponent
    default property alias content: contentLoader.sourceComponent

    // Derive content height from the loaded item's implicit/explicit height.
    readonly property real contentHeight: contentLoader.item
                                        ? Math.max(contentLoader.item.implicitHeight || 0,
                                                   contentLoader.item.height || 0)
                                        : 0

    Layout.fillWidth: true
    implicitHeight: headerRow.height + (expanded ? root.contentHeight + 8 : 0)

    Tokens { id: t }

    Behavior on implicitHeight { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: t.surface2
        border.color: t.stroke
        border.width: 1
    }

    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 0

        // Header (tap to expand/collapse)
        Rectangle {
            id: headerRow
            width: parent.width
            height: 48
            radius: 14
            color: headerMouse.containsMouse ? Qt.rgba(1,1,1,0.04) : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Label {
                    text: root.title
                    font.bold: true
                    font.pixelSize: 15
                    Layout.fillWidth: true
                }

                Rectangle {
                    id: statusPill
                    visible: root.statusText !== "" && (!root.statusWhenCollapsedOnly || !root.expanded)
                    radius: 999
                    height: 26
                    color: Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.12)
                    border.color: Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.45)
                    border.width: 1
                    clip: true

                    // Responsive: cap width so it never crushes the title.
                    Layout.maximumWidth: Math.max(120, headerRow.width * 0.42)
                    Layout.preferredWidth: Math.min(statusTextLabel.implicitWidth + 18, statusPill.Layout.maximumWidth)

                    Label {
                        id: statusTextLabel
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        text: root.statusText
                        font.pixelSize: 12
                        color: t.text
                        opacity: 0.9
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Label {
                    text: root.expanded ? "\uf077" : "\uf078"
                    font.family: "FontAwesome"
                    font.pixelSize: 12
                    opacity: 0.7
                    Behavior on text { PropertyAnimation { duration: 120 } }
                }
            }

            MouseArea {
                id: headerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expanded = !root.expanded
            }
        }

        // Content area (clipped when collapsed)
        Item {
            width: parent.width
            height: root.expanded ? root.contentHeight : 0
            clip: true
            opacity: root.expanded ? 1 : 0

            Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 140 } }

            Loader {
                id: contentLoader
                width: parent.width - 24
                anchors.horizontalCenter: parent.horizontalCenter
                active: true
            }
        }
    }
}
