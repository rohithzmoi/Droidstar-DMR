/*
    Copyright (C) 2026 Rohith Namboothiri

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

// Compact QSO Replay player: play/pause, scrubber, position and close. Collapses to nothing when
// no clip is loaded. Space toggles playback and Esc closes it when the bar has keyboard focus
// (iPad hardware keyboards); both are harmless elsewhere.
Rectangle {
    id: root

    property var player: null

    readonly property bool active: !!(player && player.active)
    readonly property bool playing: !!(player && player.playing)
    readonly property int durationMs: player ? (player.durationMs || 0) : 0
    readonly property int positionMs: player ? (player.positionMs || 0) : 0
    readonly property string label: player ? (player.currentLabel || qsTr("Replay")) : ""
    readonly property string reasonText: {
        if (!player || player.playing) return ""
        if (player.pauseReason === "live") return qsTr("Paused for live RX")
        if (player.pauseReason === "tx") return qsTr("Paused for TX")
        if (durationMs > 0 && positionMs >= durationMs) return qsTr("Finished")
        return qsTr("Paused")
    }

    Tokens { id: t }

    visible: active
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: active ? barLayout.implicitHeight + 16 : 0
    radius: 12
    color: t.surface2
    border.color: t.tint(t.accent, 0.45)
    border.width: 1
    activeFocusOnTab: true

    Keys.onPressed: function(ev) {
        if (!root.player) return
        if (ev.key === Qt.Key_Space) { root.player.toggle(); ev.accepted = true }
        else if (ev.key === Qt.Key_Escape) { root.player.stop(); ev.accepted = true }
    }

    function fmt(ms) {
        var s = Math.max(0, Math.round(ms / 1000))
        var m = Math.floor(s / 60)
        var r = s % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    RowLayout {
        id: barLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 10

        RoundIconButton {
            implicitWidth: 40
            implicitHeight: 40
            glyph: root.playing ? "" : ""
            glyphSize: 15
            tone: t.accentStrong
            filled: true
            accessibleName: root.playing ? qsTr("Pause replay") : qsTr("Play replay")
            onClicked: if (root.player) root.player.toggle()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.label
                    font.pixelSize: t.fsBody
                    font.bold: true
                    color: t.text
                    elide: Text.ElideRight
                }
                Label {
                    visible: text !== ""
                    text: root.reasonText
                    font.pixelSize: t.fsCaption
                    color: root.player && (root.player.pauseReason === "live" || root.player.pauseReason === "tx")
                           ? t.warning : t.textMuted
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: root.fmt(root.positionMs)
                    font.pixelSize: t.fsCaption
                    color: t.textMuted
                }
                Slider {
                    id: scrub
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    implicitHeight: 24
                    from: 0
                    to: Math.max(1, root.durationMs)
                    stepSize: 100
                    Accessible.name: qsTr("Replay position")
                    onPressedChanged: {
                        if (!pressed && root.player) root.player.seek(Math.round(value))
                    }
                }
                Binding {
                    target: scrub
                    property: "value"
                    value: root.positionMs
                    when: !scrub.pressed
                }
                Label {
                    text: root.fmt(root.durationMs)
                    font.pixelSize: t.fsCaption
                    color: t.textMuted
                }
            }
        }

        ToolButton {
            text: ""
            font.family: "FontAwesome"
            font.pixelSize: 14
            Accessible.name: qsTr("Close replay")
            onClicked: if (root.player) root.player.stop()
        }
    }
}
