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
    title: qsTr("Log")
    padding: 0

    // Injected by App2026
    required property var logModel
    property string monoFont: ""
    signal clearRequested()

    // Follow new lines until the user scrolls up; resume when back at the bottom.
    property bool follow: true

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    property Component headerActions: Component {
        Row {
            HeaderAction {
                glyph: "\uf063"
                label: page.follow ? qsTr("Following") : qsTr("Follow")
                showLabel: page.width >= 560
                tone: page.follow ? t.accent : t.text
                onClicked: {
                    page.follow = !page.follow
                    if (page.follow) lv.positionViewAtEnd()
                }
            }
            HeaderAction {
                glyph: "\uf1f8"
                label: qsTr("Clear")
                showLabel: page.width >= 560
                tone: t.danger
                enabled: page.logModel.count > 0
                onClicked: page.clearRequested()
            }
        }
    }

    Connections {
        target: page.logModel
        function onCountChanged() { if (page.follow && page.visible) Qt.callLater(lv.positionViewAtEnd) }
    }
    onVisibleChanged: if (visible && follow) Qt.callLater(lv.positionViewAtEnd)

    ListView {
        id: lv
        anchors.fill: parent
        anchors.margins: 10
        model: page.logModel
        clip: true
        spacing: 0
        cacheBuffer: 1000
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        onMovementEnded: page.follow = atYEnd
        onFlickStarted: if (verticalVelocity < 0) page.follow = false

        Component.onCompleted: positionViewAtEnd()

        delegate: Rectangle {
            required property int index
            required property string t
            width: ListView.view.width - 8
            height: txt.implicitHeight + 10
            color: index % 2 === 0 ? "transparent" : Qt.rgba(1, 1, 1, 0.025)
            radius: 4

            Label {
                id: txt
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                text: parent.t
                wrapMode: Text.WrapAnywhere
                font.family: page.monoFont !== "" ? page.monoFont : Qt.application.font.family
                font.pixelSize: 12
                color: /error|fail|banned|invalid/i.test(parent.t) ? "#FCA5A5" : "#D1D5DB"
                textFormat: Text.PlainText
            }
        }

        Label {
            anchors.centerIn: parent
            visible: page.logModel.count === 0
            text: qsTr("No log messages yet")
            color: "#9CA3AF"
        }
    }

    // Jump-to-latest affordance when paused
    RoundButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        visible: !page.follow && page.logModel.count > 0
        text: "\uf063"
        font.family: "FontAwesome"
        Accessible.name: qsTr("Jump to latest")
        onClicked: {
            page.follow = true
            lv.positionViewAtEnd()
        }
    }
}
