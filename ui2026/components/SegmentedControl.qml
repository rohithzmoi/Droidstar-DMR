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

// options: [{ label: "...", value: <any>, disabled: false, disabledReason: "..." }, ...]
Rectangle {
    id: root

    property var options: []
    property var currentValue
    signal selected(var value, int index)
    signal disabledSelected(var value, string reason, int index)

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: t.controlHeight
    implicitWidth: 120
    radius: 12
    color: t.surface
    border.color: t.stroke
    border.width: 1

    Tokens { id: t }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 3

        Repeater {
            model: root.options
            delegate: Rectangle {
                id: segDelegate
                required property var modelData
                required property int index
                readonly property bool isDisabled: !!(modelData && modelData.disabled)
                readonly property bool isSelected: !isDisabled && (root.currentValue === modelData.value)
                readonly property real iconSpace: isDisabled ? (lockIcon.implicitWidth + 4) : 0
                readonly property real availTextW: Math.max(0, segDelegate.width - iconSpace - 8)

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                radius: 9
                clip: true
                opacity: isDisabled ? 0.48 : 1.0
                color: isSelected ? t.tint(t.accent, 0.24) : (segMouse.pressed ? t.surface3 : "transparent")
                border.color: isSelected ? t.accent : "transparent"
                border.width: 1
                Behavior on color { ColorAnimation { duration: t.durFast } }

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: isSelected
                Accessible.onPressAction: {
                    if (isDisabled) {
                        root.disabledSelected(modelData.value, modelData.disabledReason || "", index)
                    } else {
                        root.selected(modelData.value, index)
                    }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 4

                    Label {
                        id: lockIcon
                        visible: segDelegate.isDisabled
                        text: "\uf023"
                        font.family: "FontAwesome"
                        font.pixelSize: t.fsSmall - 2
                        color: t.warning
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label {
                        width: segDelegate.availTextW > 0
                               ? Math.min(implicitWidth, segDelegate.availTextW)
                               : implicitWidth
                        text: modelData.label
                        font.pixelSize: t.fsSmall
                        font.bold: isSelected
                        color: isSelected ? t.text : t.textMuted
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (isDisabled) {
                            root.disabledSelected(modelData.value, modelData.disabledReason || "", index)
                        } else {
                            root.selected(modelData.value, index)
                        }
                    }
                }
            }
        }
    }
}
