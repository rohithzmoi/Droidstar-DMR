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

import "../components"

Page {
    id: page
    title: qsTr("Hosts")
    padding: 12

    required property var droidstarRef
    property string hostsText: ""

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label { text: page.title; Layout.fillWidth: true; leftPadding: 12 }
            Button {
                text: qsTr("Save")
                onClicked: page.droidstarRef.update_custom_hosts(editor.text)
                rightPadding: 12
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 12

            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    Label { text: qsTr("Custom host file format"); font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.85
                        text: qsTr("<mode> <name> <host> <port> <username (optional)> <password (optional)>\n"
                                   + "Example: REF REF123 192.168.1.1 20001\n"
                                   + "Example: DMR MyNet 192.168.1.1 62030 passw0rd\n"
                                   + "Example: IAX 12345 192.168.1.1 4569 iaxclient iaxpass")
                    }
                }
            }

            AppCard {
                Layout.fillWidth: true
                TextArea {
                    id: editor
                    width: parent.width
                    wrapMode: TextArea.WordWrap
                    placeholderText: qsTr("Paste or edit custom hosts here…")
                    text: page.hostsText
                    onEditingFinished: page.droidstarRef.update_custom_hosts(text)
                }
            }
        }
    }
}

