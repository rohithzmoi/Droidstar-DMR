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
    title: qsTr("About")
    padding: 0

    required property var droidstarRef
    property Component headerActions: null

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + 40
        boundsBehavior: Flickable.DragOverBounds
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: contentColumn
            x: Math.max(10, (flick.width - width) / 2)
            y: 12
            width: Math.min(flick.width - 20, 640)
            spacing: 12

            // App info
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Image {
                        id: aboutWhiteLogo
                        readonly property real targetW: Math.min(Math.max(180, parent.width - 24), 260)
                        source: "qrc:/DroidStar/images/droidstar_logo_white.png"
                        sourceSize.width: 684
                        sourceSize.height: 228
                        Layout.preferredWidth: targetW
                        Layout.preferredHeight: Math.round(targetW * 341 / 1024)
                        Layout.topMargin: 4
                        Layout.alignment: Qt.AlignHCenter
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        asynchronous: true
                        onStatusChanged: {
                            if (status === Image.Error) source = Qt.resolvedUrl("../../images/droidstar_logo_white.png")
                        }
                    }

                    Label {
                        readonly property string buildVer: page.droidstarRef ? page.droidstarRef.get_software_build() : ""
                        text: buildVer !== "" ? qsTr("iOS build - V%1").arg(buildVer) : qsTr("iOS build")
                        font.pixelSize: t.fsBody
                        color: t.text
                        opacity: 0.9
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 4; Layout.preferredHeight: 1; color: t.stroke }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 4

                        Label { text: qsTr("Architecture"); color: t.textMuted; font.pixelSize: t.fsSmall }
                        Label { Layout.fillWidth: true; text: page.droidstarRef ? page.droidstarRef.get_arch() : ""; color: t.text; font.pixelSize: t.fsSmall; elide: Text.ElideRight }
                        Label { text: qsTr("Build ABI"); color: t.textMuted; font.pixelSize: t.fsSmall }
                        Label { Layout.fillWidth: true; text: page.droidstarRef ? page.droidstarRef.get_build_abi() : ""; color: t.text; font.pixelSize: t.fsSmall; elide: Text.ElideRight }
                        Label { text: qsTr("Software"); color: t.textMuted; font.pixelSize: t.fsSmall }
                        Label { Layout.fillWidth: true; text: page.droidstarRef ? page.droidstarRef.get_software_build() : ""; color: t.text; font.pixelSize: t.fsSmall; elide: Text.ElideRight }
                    }
                }
            }

            // Copyright & license
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Label { text: qsTr("Copyright & License"); font.pixelSize: 15; font.bold: true; color: t.text }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall
                        color: t.text
                        opacity: 0.9
                        lineHeight: 1.3
                        text: qsTr("Original Copyright (C) 2019-2021 Doug McLain AD8DP\n\n" +
                                   "Modification Copyright (C) 2024 Rohith Namboothiri VU3LVO\n\n" +
                                   "This program is free software: you can redistribute it and/or modify " +
                                   "it under the terms of the GNU General Public License as published by " +
                                   "the Free Software Foundation, either version 3 of the License, or " +
                                   "(at your option) any later version.\n\n" +
                                   "This program is distributed in the hope that it will be useful, " +
                                   "but WITHOUT ANY WARRANTY; without even the implied warranty of " +
                                   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the " +
                                   "GNU General Public License for more details.\n\n" +
                                   "You should have received a copy of the GNU General Public License " +
                                   "along with this program. If not, see https://www.gnu.org/licenses/.")
                    }

                    AppButton {
                        Layout.fillWidth: true
                        text: qsTr("View GPL License")
                        glyph: "\uf08e"
                        onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/")
                    }
                }
            }

            // Disclaimer
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label { text: qsTr("Important Notice"); font.pixelSize: 15; font.bold: true; color: t.warning }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall
                        color: t.text
                        opacity: 0.9
                        lineHeight: 1.3
                        text: qsTr("This customized iOS/Android version, built and distributed by VU3LVO, " +
                                   "is specifically designed for use by a select group and is not intended " +
                                   "for public use at the moment.")
                    }
                }
            }

            // Support
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Label { text: qsTr("Support the Project"); font.pixelSize: 15; font.bold: true; color: t.text }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall
                        color: t.textMuted
                        text: qsTr("If you find this app useful, please consider supporting the development.")
                    }

                    DonateButtons {
                        Layout.fillWidth: true
                        buttonHeight: 52
                        fontSize: 15
                    }
                }
            }

            // Links
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Label { text: qsTr("Project Links"); font.pixelSize: 15; font.bold: true; color: t.text }

                    AppButton {
                        Layout.fillWidth: true
                        text: qsTr("Visit Official Website")
                        glyph: "\uf0ac"
                        onClicked: Qt.openUrlExternally("https://vu3lvo.com/droidstar")
                    }

                    AppButton {
                        Layout.fillWidth: true
                        text: qsTr("GitHub Project Page")
                        glyph: "\uf09b"
                        onClicked: Qt.openUrlExternally("https://github.com/rohithzmoi/Droidstar-DMR")
                    }
                }
            }

            Item { Layout.preferredHeight: 20 }
        }
    }
}
