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

// Labelled text input with optimistic local updates and a debounced commit.
// `edited` fires on every keystroke (update UI state); `committed` fires after a pause,
// on Enter, or on focus loss (push to backend). iOS can skip editingFinished when the
// app is backgrounded, hence the debounce.
ColumnLayout {
    id: root

    property string label: ""
    property string value: ""
    property alias placeholderText: field.placeholderText
    property int inputMethodHints: Qt.ImhNone
    property alias validator: field.validator
    property alias maximumLength: field.maximumLength
    property alias field: field
    property bool uppercase: false
    property bool password: false
    property int commitDelay: password ? 1200 : 600

    readonly property string text: field.text
    readonly property bool editing: field.activeFocus

    signal edited(string text)
    signal committed(string text)

    spacing: 4
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.preferredWidth: 100

    Tokens { id: t }

    property string _lastCommitted: ""
    property bool _revealed: false

    function _sanitizePasswordText(s) {
        if (!s) return ""
        // Normalize iOS smart punctuation back to standard ASCII symbols used in passwords
        return s.replace(/[\u2018\u2019]/g, "'")
                .replace(/[\u201C\u201D]/g, "\"")
                .replace(/\u2014/g, "--")
                .replace(/\u2013/g, "-")
    }

    function _normalized() {
        var v = field.text
        if (root.password) {
            return _sanitizePasswordText(v)
        }
        return root.uppercase ? v.toUpperCase() : v
    }

    function commitNow() {
        commitTimer.stop()
        // If iOS IME still has uncommitted preedit text while focused, don't commit partial text mid-composition
        if (field.activeFocus && field.preeditText && field.preeditText.length > 0) {
            return
        }
        var v = _normalized()
        if (field.text !== v) {
            var pos = field.cursorPosition
            field.text = v
            field.cursorPosition = Math.min(pos, v.length)
        }
        if (v === root._lastCommitted) return
        root._lastCommitted = v
        root.committed(v)
    }

    Component.onCompleted: {
        field.text = root.value
        root._lastCommitted = root.value
    }

    // External updates win only while the user isn't typing.
    onValueChanged: {
        if (field.activeFocus) return
        if (field.text !== root.value) field.text = root.value
        root._lastCommitted = root.value
    }

    Timer {
        id: commitTimer
        interval: root.commitDelay
        repeat: false
        onTriggered: root.commitNow()
    }

    Label {
        visible: root.label !== ""
        text: root.label
        font.pixelSize: t.fsCaption
        color: t.textMuted
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    AppTextField {
        id: field
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        font.capitalization: root.uppercase ? Font.AllUppercase : Font.MixedCase
        echoMode: (root.password && !root._revealed) ? TextInput.Password : TextInput.Normal
        passwordMaskDelay: 0
        rightPadding: root.password ? revealButton.width + 6 : 12
        inputMethodHints: root.password
                          ? (Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData | Qt.ImhNoEditMenu)
                          : (root.uppercase ? (Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText) : root.inputMethodHints)
        Accessible.name: root.label

        onTextEdited: {
            var norm = root._normalized()
            if (root.password && field.text !== norm) {
                var cpos = field.cursorPosition
                field.text = norm
                field.cursorPosition = Math.min(cpos, norm.length)
            }
            root.edited(norm)
            commitTimer.restart()
        }
        onAccepted: {
            Qt.inputMethod.commit()
            root.commitNow()
        }
        onEditingFinished: {
            Qt.inputMethod.commit()
            root.commitNow()
        }
        onActiveFocusChanged: {
            if (activeFocus) return
            Qt.inputMethod.commit()
            root.commitNow()
            // Pick up anything the backend changed while we were editing.
            if (field.text !== root.value && root.value === root._lastCommitted) field.text = root.value
        }

        ToolButton {
            id: revealButton
            visible: root.password
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: parent.height
            text: root._revealed ? "\uf070" : "\uf06e"
            font.family: "FontAwesome"
            font.pixelSize: 14
            opacity: 0.7
            focusPolicy: Qt.NoFocus
            Accessible.name: root._revealed ? qsTr("Hide password") : qsTr("Show password")
            onClicked: root._revealed = !root._revealed
        }
    }
}
