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
    property string statusIcon: ""
    property color statusColor: t.accent
    property color statusIconColor: root.statusColor
    // If true, show status only when collapsed (recommended for compact headers).
    property bool statusWhenCollapsedOnly: true
    property bool expanded: true
    property int contentPadding: 12
    property alias contentItem: contentLoader.sourceComponent
    default property alias content: contentLoader.sourceComponent
    readonly property Item loadedItem: contentLoader.item

    // Derive content height from the loaded item's implicit/explicit height.
    readonly property real contentHeight: contentLoader.item
                                        ? Math.max(contentLoader.item.implicitHeight || 0,
                                                   contentLoader.item.height || 0)
                                        : 0

    function toggle() { root.expanded = !root.expanded }

    Layout.fillWidth: true
    implicitHeight: headerRow.height + body.height + 8

    Tokens { id: t }

    Rectangle {
        anchors.fill: parent
        radius: t.rMd
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
            color: headerMouse.pressed ? Qt.rgba(1, 1, 1, 0.07)
                 : (headerMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
            Behavior on color { ColorAnimation { duration: t.durFast } }

            Accessible.role: Accessible.Button
            Accessible.name: root.title
            Accessible.onPressAction: root.toggle()

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Label {
                    text: root.title
                    font.bold: true
                    font.pixelSize: 15
                    color: t.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                }

                Rectangle {
                    id: statusPill
                    visible: root.statusText !== "" && (!root.statusWhenCollapsedOnly || !root.expanded)
                    radius: height / 2
                    Layout.preferredHeight: 26
                    color: t.tint(root.statusColor, 0.12)
                    border.color: t.tint(root.statusColor, 0.45)
                    border.width: 1
                    clip: true

                    // Responsive: cap width so it never crushes the title.
                    Layout.maximumWidth: Math.max(110, headerRow.width * 0.5)
                    Layout.preferredWidth: Math.min(statusPillRow.implicitWidth + 18, statusPill.Layout.maximumWidth)

                    RowLayout {
                        id: statusPillRow
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        spacing: 5

                        Label {
                            visible: root.statusIcon !== ""
                            text: root.statusIcon
                            font.family: "FontAwesome"
                            font.pixelSize: 11
                            color: root.statusIconColor
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            id: statusTextLabel
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: root.statusText
                            font.pixelSize: 12
                            color: t.text
                            opacity: 0.92
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                Label {
                    text: "\uf078"
                    font.family: "FontAwesome"
                    font.pixelSize: 12
                    color: t.textMuted
                    rotation: root.expanded ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
                }
            }

            MouseArea {
                id: headerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggle()
            }
        }

        // Content area (clipped when collapsed)
        Item {
            id: body
            width: parent.width
            height: root.expanded ? root.contentHeight + root.contentPadding : 0
            clip: true
            opacity: root.expanded ? 1 : 0
            visible: height > 0

            Behavior on height { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: t.durFast } }

            Loader {
                id: contentLoader
                y: 2
                width: parent.width - root.contentPadding * 2
                anchors.horizontalCenter: parent.horizontalCenter
                active: true
            }
        }
    }
}
