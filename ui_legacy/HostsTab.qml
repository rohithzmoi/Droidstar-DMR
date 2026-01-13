/*
	Copyright (C) 2019-2021 Doug McLain

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
    id: hostsTab
    padding: 12

    property alias hostsTextEdit: hostsTxtEdit

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: qsTr("Hosts")
                Layout.fillWidth: true
                leftPadding: 12
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Custom hostfile format:\n" +
                           "<mode> <name> <host> <port> <username (optional)> <password (optional)>\n" +
                           "Example: REF REF123 192.168.1.1 20001\n" +
                           "Example: DMR MyNet 192.168.1.1 62030 passw0rd\n" +
                           "Example: IAX 12345 192.168.1.1 4569 iaxclient iaxpass")
            }

            TextArea {
                id: hostsTxtEdit
                Layout.fillWidth: true
                Layout.minimumHeight: 420
                wrapMode: TextArea.WordWrap
                placeholderText: qsTr("Paste or edit your custom hosts here…")
                onEditingFinished: droidstar.update_custom_hosts(hostsTxtEdit.text)
            }
        }
    }
}
