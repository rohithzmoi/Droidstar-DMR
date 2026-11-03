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
import "../theme"

Page {
    id: page
    title: qsTr("Hosts")
    padding: 0

    property var app: null
    required property var droidstarRef
    property string hostsText: ""

    property string _savedText: ""
    readonly property bool dirty: editor.text !== _savedText

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    function save(silent) {
        if (!page.droidstarRef) return
        page.droidstarRef.update_custom_hosts(editor.text)
        page._savedText = editor.text
        if (!silent && page.app) page.app.notify(qsTr("Custom hosts saved."), "", null, "success")
    }

    onVisibleChanged: {
        if (!visible && page.dirty) {
            page.save(true)
        }
    }

    Component.onCompleted: {
        editor.text = page.hostsText
        page._savedText = page.hostsText
    }

    property Component headerActions: Component {
        Row {
            HeaderAction {
                glyph: "\uf0e2"
                label: qsTr("Revert")
                showLabel: page.width >= 560
                visible: page.dirty
                onClicked: editor.text = page._savedText
            }
            HeaderAction {
                glyph: "\uf0c7"
                label: page.dirty ? qsTr("Save") : qsTr("Saved")
                showLabel: true
                tone: page.dirty ? t.accent : t.textMuted
                enabled: page.dirty
                onClicked: page.save(false)
            }
        }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        width: Math.min(parent.width - 20, t.maxContentWidth)
        spacing: 12

        CollapsibleSection {
            title: qsTr("Custom host file format")
            expanded: page.height > 560

            content: Label {
                wrapMode: Text.WordWrap
                color: t.text
                opacity: 0.85
                font.pixelSize: t.fsSmall
                textFormat: Text.PlainText
                text: qsTr("<mode> <name> <host> <port> <username (optional)> <password (optional)>\n\n"
                           + "Example: REF REF123 192.168.1.1 20001\n"
                           + "Example: DMR MyNet 192.168.1.1 62030 passw0rd\n"
                           + "Example: IAX 12345 192.168.1.1 4569 iaxclient iaxpass\n"
                           + "Example: IAX 12345 wt\n"
                           + "Example: IAX 12345 wt 4571")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            radius: t.rMd
            color: t.surface2
            border.color: editor.activeFocus ? t.accent : t.stroke
            border.width: 1

            ScrollView {
                id: editorScroll
                anchors.fill: parent
                anchors.margins: 4
                clip: true

                TextArea {
                    id: editor
                    wrapMode: TextArea.WrapAnywhere
                    placeholderText: qsTr("One host per line…")
                    font.family: "monospace"
                    font.pixelSize: 13
                    color: t.text
                    selectByMouse: true
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    background: null
                    onEditingFinished: if (page.dirty) page.save(true)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: page.dirty ? qsTr("Unsaved changes") : qsTr("%1 lines").arg(editor.text === "" ? 0 : editor.text.split("\n").length)
            color: page.dirty ? t.warning : t.textMuted
            font.pixelSize: t.fsCaption
        }
    }
}
