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
    id: logTab
    padding: 0

    // Bounded log storage for iOS performance.
    readonly property int maxLines: 1200

    function appendLog(s) {
        if (s === undefined || s === null) return;
        var str = "" + s;
        // Split on newlines, append each non-empty line.
        var parts = str.split(/\r?\n/);
        for (var i = 0; i < parts.length; i++) {
            var line = parts[i];
            if (line.length === 0) continue;
            logModel.append({ "t": line });
        }
        // Trim old lines
        while (logModel.count > maxLines) {
            logModel.remove(0, logModel.count - maxLines);
        }
        // Scroll to bottom on new logs
        if (logModel.count > 0) {
            logList.positionViewAtEnd();
        }
    }

    function clearLog() {
        logModel.clear();
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: qsTr("Log")
                Layout.fillWidth: true
                leftPadding: 12
            }
            Button {
                text: qsTr("Clear")
                onClicked: logTab.clearLog()
                rightPadding: 12
            }
        }
    }

    ListModel { id: logModel }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ListView {
            id: logList
            width: parent.width
            model: logModel
            clip: true
            spacing: 4

            delegate: Label {
                text: model.t
                wrapMode: Text.WordWrap
                width: ListView.view.width
                padding: 8
            }
        }
    }
}
