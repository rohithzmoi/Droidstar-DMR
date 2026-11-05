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

// Lightweight snackbar. kind: "info" | "success" | "warning" | "error"
Item {
    id: root

    property real bottomOffset: 0

    property string _message: ""
    property string _actionText: ""
    property var _action: null
    property string _kind: "info"
    property bool _shown: false

    Tokens { id: t }

    function show(message, actionText, action, kind) {
        root._message = message || ""
        root._actionText = actionText || ""
        root._action = action || null
        root._kind = kind || "info"
        root._shown = true
        hideTimer.interval = root._actionText !== "" ? 4500 : 2600
        hideTimer.restart()
    }

    function hide() { root._shown = false }

    readonly property color _tone: _kind === "success" ? t.success
                                 : _kind === "warning" ? t.warning
                                 : _kind === "error" ? t.danger
                                 : t.accent

    Timer { id: hideTimer; onTriggered: root.hide() }

    TextMetrics {
        id: messageMetrics
        font.pixelSize: t.fsSmall + 1
        text: root._message
    }

    Rectangle {
        id: box
        anchors.horizontalCenter: parent.horizontalCenter
        y: root._shown ? root.height - height - 16 - root.bottomOffset : root.height + 8
        // Sized from the unwrapped message so the wrapped row can't feed back into the width
        width: Math.min(root.width - 32, 560,
                        Math.max(220, messageMetrics.advanceWidth + 14 + 8 + 10 + 10
                                      + (actionButton.visible ? actionButton.implicitWidth + 10 : 0) + 8))
        height: Math.max(48, messageLabel.implicitHeight + 20,
                         actionButton.visible ? actionButton.implicitHeight + 4 : 0)
        radius: 14
        color: t.surface3
        border.color: t.tint(root._tone, 0.6)
        border.width: 1
        opacity: root._shown ? 1 : 0
        visible: opacity > 0
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 160 } }

        Accessible.role: Accessible.AlertMessage
        Accessible.name: root._message

        MouseArea { anchors.fill: parent; onClicked: root.hide() }

        // Plain anchors: a wrapping label inside a RowLayout re-triggers the layout pass
        Rectangle {
            id: toneDot
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root._tone
        }
        Label {
            id: messageLabel
            anchors.left: toneDot.right
            anchors.leftMargin: 10
            anchors.right: actionButton.visible ? actionButton.left : parent.right
            anchors.rightMargin: actionButton.visible ? 6 : 14
            anchors.verticalCenter: parent.verticalCenter
            text: root._message
            color: t.text
            font.pixelSize: t.fsSmall + 1
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Button {
            id: actionButton
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            visible: root._actionText !== ""
            text: root._actionText
            flat: true
            font.bold: true
            onClicked: {
                var fn = root._action
                root.hide()
                if (fn) fn()
            }
        }
    }
}
