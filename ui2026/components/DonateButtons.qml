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

ColumnLayout {
    id: root

    property int buttonHeight: 48
    property int fontSize: 14

    spacing: 10
    Layout.fillWidth: true

    Repeater {
        model: [
            { label: qsTr("☕ Buy Me a Coffee"), url: "https://buymeacoffee.com/rohithz", top: "#FFA366", bottom: "#FF813F" },
            { label: qsTr("💳 PayPal Donation"), url: "https://www.paypal.com/ncp/payment/NU89529268M2W", top: "#009CDE", bottom: "#0070BA" }
        ]

        delegate: Button {
            id: donate
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredHeight: root.buttonHeight
            text: modelData.label
            font.bold: true
            font.pixelSize: root.fontSize
            scale: down ? 0.98 : 1
            Behavior on scale { NumberAnimation { duration: 110 } }

            background: Rectangle {
                radius: 14
                border.color: donate.modelData.top
                border.width: 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: donate.modelData.top }
                    GradientStop { position: 1.0; color: donate.modelData.bottom }
                }
                opacity: donate.down ? 0.85 : 1
            }

            contentItem: Label {
                text: donate.text
                font: donate.font
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            onClicked: Qt.openUrlExternally(modelData.url)
        }
    }
}
