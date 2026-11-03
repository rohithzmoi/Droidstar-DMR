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

// PTT / toggle-TX button. Rectangle + MouseArea gives reliable press/release on mobile.
Rectangle {
    id: root

    property var app: null
    property var appState: null
    property bool compact: false

    readonly property bool live: !!(appState && appState.txActive)
    readonly property bool armed: !!(appState && appState.txEnabled)
    readonly property int timeout: appState ? (parseInt(appState.txTimeout) || 0) : 0
    readonly property int seconds: appState ? appState.txSeconds : 0

    function _clock(s) {
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" : "") + m + ":" + (r < 10 ? "0" : "") + r
    }

    Tokens { id: t }

    implicitHeight: compact ? t.controlHeight : 72
    implicitWidth: compact ? 96 : 200
    radius: compact ? 12 : 16
    opacity: armed ? 1.0 : 0.45
    color: live ? t.txLive : (txMouse.pressed ? Qt.darker(t.txIdle, 1.15) : t.txIdle)
    border.color: live ? t.txLiveBorder : t.txIdleBorder
    border.width: live ? 2 : 1
    scale: txMouse.pressed ? 0.985 : 1
    Behavior on color { ColorAnimation { duration: t.durFast } }
    Behavior on scale { NumberAnimation { duration: t.durFast } }

    Accessible.role: Accessible.Button
    Accessible.name: live ? qsTr("Stop transmitting") : qsTr("Transmit")
    Accessible.onPressAction: if (root.app) root.app.txToggle()

    // On-air glow
    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: parent.radius + 4
        color: "transparent"
        border.color: t.txLiveBorder
        border.width: 2
        visible: root.live
        SequentialAnimation on opacity {
            running: root.live
            loops: Animation.Infinite
            NumberAnimation { from: 0.8; to: 0.1; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.1; to: 0.8; duration: 700; easing.type: Easing.InOutSine }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 16
        spacing: 2
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            text: root.live ? (root.compact ? root._clock(root.seconds) : qsTr("ON AIR  %1").arg(root._clock(root.seconds)))
                            : qsTr("TX")
            font.bold: true
            font.pixelSize: root.compact ? 15 : 20
            color: "white"
            elide: Text.ElideRight
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            visible: !root.compact && (!root.armed || (root.live && root.timeout > 0))
            text: !root.armed ? qsTr("Connect to transmit")
                              : qsTr("Time-out in %1 s").arg(Math.max(0, root.timeout - root.seconds))
            font.pixelSize: t.fsCaption
            color: "white"
            opacity: 0.8
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: txMouse
        anchors.fill: parent
        enabled: root.armed
        // Don't let a parent Flickable steal a held PTT (would drop TX mid-over).
        preventStealing: true
        onPressed: if (root.app) root.app.txPress()
        onReleased: if (root.app) root.app.txRelease()
        onCanceled: if (root.app) root.app.txRelease()
        onClicked: if (root.app && root.appState.toggleTx) root.app.txToggle()
    }
}
