/*
	Copyright (C) 2019-2021 Doug McLain
    Modified Copyright (C) 2024 Rohith Namboothiri

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

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Page {
    id: aboutTab
    padding: 12

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: qsTr("About")
                Layout.fillWidth: true
                leftPadding: 12
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("DroidStar iOS build - V0.44.17.0 " +
                           "\nArchitecture:\t" + droidstar.get_arch() +
                           "\nBuild ABI:\t" + droidstar.get_build_abi() +
                           "\n\nProject page: https://github.com/nostar/DroidStar" +
                           "\n\nOriginal Copyright (C) 2019-2021 Doug McLain AD8DP\n" +
                           "\n\nModification Copyright (C) 2024 Rohith Namboothiri VU3LVO\n" +
                           "\n\nThis customized iOS/Android version, built and distributed by VU3LVO, is specifically designed for use by a select group and is not intended for public use at the moment.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Buy Me a Coffee")
                    onClicked: Qt.openUrlExternally("https://buymeacoffee.com/rohithz")
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("PayPal")
                    onClicked: Qt.openUrlExternally("https://www.paypal.com/ncp/payment/NU89529268M2W")
                }
            }
        }
    }
}
