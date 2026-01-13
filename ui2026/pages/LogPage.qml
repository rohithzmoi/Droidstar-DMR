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

Page {
    id: page
    title: qsTr("Log")
    padding: 0

    // Injected by App2026
    required property var logModel
    signal clearRequested()

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: page.title; Layout.fillWidth: true; leftPadding: 12 }
            Button { text: qsTr("Clear"); onClicked: page.clearRequested(); rightPadding: 12 }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ListView {
            id: lv
            width: parent.width
            model: page.logModel
            clip: true
            spacing: 6

            delegate: Rectangle {
                width: ListView.view.width
                radius: 12
                color: "#111827"
                border.color: "#233044"
                border.width: 1
                height: txt.implicitHeight + 16

                Label {
                    id: txt
                    anchors.fill: parent
                    anchors.margins: 8
                    text: model.t
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}

