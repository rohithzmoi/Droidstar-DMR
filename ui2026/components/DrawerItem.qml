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

ItemDelegate {
    id: root
    
    // Use different property names to avoid conflicts with FINAL properties
    property alias iconText: iconItem.text
    property alias iconFont: iconItem.font.family

    width: ListView.view ? ListView.view.width : implicitWidth
    height: 48

    contentItem: Row {
        spacing: 12
        anchors.verticalCenter: parent.verticalCenter

        Text {
            id: iconItem
            font.pointSize: 16
            color: root.highlighted ? "white" : Material.foreground
            width: 24
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            text: root.text
            color: root.highlighted ? "white" : Material.foreground
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        radius: 12
        color: root.highlighted ? Qt.rgba(0.376, 0.647, 0.98, 0.18) : "transparent"
    }
}

