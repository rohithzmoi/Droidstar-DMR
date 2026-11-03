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
import QtQuick.Templates as T

import "../theme"

T.TextField {
    id: control

    Tokens { id: t }

    // Placeholder length must not drive layout width (keeps grid columns even).
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, 96)
    implicitHeight: t.controlHeight

    leftPadding: 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter

    font.pixelSize: t.fsBody
    color: enabled ? t.text : t.textMuted
    selectionColor: t.tint(t.accent, 0.45)
    selectedTextColor: t.text
    placeholderTextColor: t.textMuted

    Text {
        id: placeholder
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        elide: Text.ElideRight
        visible: control.length === 0 && control.preeditText.length === 0
        renderType: control.renderType
    }

    background: Rectangle {
        radius: 10
        color: control.enabled ? t.surface : t.surface2
        border.color: control.activeFocus ? t.accent : (control.hovered ? t.strokeStrong : t.stroke)
        border.width: control.activeFocus ? 1.5 : 1
        Behavior on border.color { ColorAnimation { duration: t.durFast } }
    }
}
