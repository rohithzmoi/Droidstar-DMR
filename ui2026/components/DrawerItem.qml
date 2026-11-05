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
import QtQuick.Controls.Material
import QtQuick.Layouts

import "../theme"

ItemDelegate {
    id: root

    // Use different property names to avoid conflicts with FINAL properties
    property string iconText: ""
    property string iconFont: "FontAwesome"
    property string badgeText: ""
    property bool compact: false

    Tokens { id: t }

    implicitHeight: compact ? 42 : 48
    leftPadding: 14
    rightPadding: 12

    contentItem: RowLayout {
        spacing: 10

        Text {
            id: iconItem
            text: root.iconText
            font.family: root.iconFont
            font.pixelSize: 17
            color: root.highlighted ? t.accent : t.textMuted
            Layout.preferredWidth: 22
            Layout.minimumWidth: 22
            Layout.maximumWidth: 22
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Label {
            id: titleLabel
            text: root.text
            font.pixelSize: 15
            font.bold: root.highlighted
            color: root.highlighted ? t.text : Material.foreground
            elide: Text.ElideRight
            // Shrinks below its natural width before the badge does; no dependency on the row width
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: implicitWidth
            Layout.maximumWidth: Math.ceil(implicitWidth) + 1
            Layout.alignment: Qt.AlignVCenter
        }

        // Distinctive pill badge right next to the menu item title (e.g. "NEW" next to "Nets")
        Rectangle {
            id: badge
            visible: root.badgeText !== ""
            implicitHeight: 19
            implicitWidth: badgeRow.implicitWidth + 12
            Layout.preferredHeight: 19
            Layout.preferredWidth: implicitWidth
            Layout.minimumWidth: implicitWidth
            Layout.maximumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            radius: 6
            border.color: t.tint(t.success, 0.75)
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: t.tint(t.success, 0.28) }
                GradientStop { position: 1.0; color: t.tint(t.accent, 0.22) }
            }

            Row {
                id: badgeRow
                anchors.centerIn: parent
                spacing: 4

                Rectangle {
                    width: 5
                    height: 5
                    radius: 2.5
                    color: t.success
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    id: badgeLabel
                    text: root.badgeText
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 0.5
                    color: t.success
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
    }

    background: Rectangle {
        radius: 12
        color: root.highlighted ? t.tint(t.accent, 0.16) : (root.down ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
        Behavior on color { ColorAnimation { duration: t.durFast } }

        Rectangle {
            visible: root.highlighted
            width: 3
            height: parent.height * 0.5
            radius: 1.5
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: t.accent
        }
    }
}
