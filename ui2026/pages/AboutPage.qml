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

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: contentColumn.childrenRect.height + 40
        clip: true

        Column {
            id: contentColumn
            width: parent.width
            spacing: 14

            // ─────────────────────────────────────────────────────────
            // APP INFO
            // ─────────────────────────────────────────────────────────
            AppCard {
                width: parent.width
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    
                    // App Icon
                    Image {
                        source: "qrc:/DroidStar/images/droidstar.png"
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 80
                        Layout.alignment: Qt.AlignHCenter
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        asynchronous: true
                    }

                    Label {
                        text: qsTr("DroidStar")
                        font.pixelSize: 24
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        text: qsTr("iOS build - V0.44.17.0")
                        font.pixelSize: 14
                        opacity: 0.9
                    }

                    Item { Layout.preferredHeight: 4 }

                    Label {
                        text: qsTr("Architecture: %1").arg(page.droidstarRef ? page.droidstarRef.get_arch() : "")
                        font.pixelSize: 12
                        opacity: 0.85
                    }

                    Label {
                        text: qsTr("Build ABI: %1").arg(page.droidstarRef ? page.droidstarRef.get_build_abi() : "")
                        font.pixelSize: 12
                        opacity: 0.85
                    }

                    Label {
                        text: qsTr("Software version: %1").arg(page.droidstarRef ? page.droidstarRef.get_software_build() : "")
                        font.pixelSize: 12
                        opacity: 0.85
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // COPYRIGHT & LICENSE
            // ─────────────────────────────────────────────────────────
            AppCard {
                width: parent.width
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Label {
                        text: qsTr("Copyright & License")
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        opacity: 0.9
                        lineHeight: 1.4
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

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("View GPL License")
                        onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/")
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // DISCLAIMER
            // ─────────────────────────────────────────────────────────
            AppCard {
                width: parent.width
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Important Notice")
                        font.pixelSize: 15
                        font.bold: true
                        color: t.warning
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        opacity: 0.9
                        lineHeight: 1.4
                        text: qsTr("This customized iOS/Android version, built and distributed by VU3LVO, " +
                                   "is specifically designed for use by a select group and is not intended " +
                                   "for public use at the moment.")
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // DONATION BUTTONS (HIGHLIGHTED)
            // ─────────────────────────────────────────────────────────
            AppCard {
                width: parent.width
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Label {
                        text: qsTr("Support the Project")
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        opacity: 0.8
                        text: qsTr("If you find this app useful, please consider supporting the development.")
                    }

                    // Buy Me a Coffee - Highlighted
                    Button {
                        Layout.fillWidth: true
                        height: 52
                        text: qsTr("☕ Buy Me a Coffee")
                        font.bold: true
                        font.pixelSize: 15

                        background: Rectangle {
                            radius: 14
                            color: "#FF813F"
                            border.color: "#FFA366"
                            border.width: 2

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#FFA366" }
                                    GradientStop { position: 1.0; color: "#FF813F" }
                                }
                            }
                        }

                        contentItem: Label {
                            text: parent.text
                            font: parent.font
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: Qt.openUrlExternally("https://buymeacoffee.com/rohithz")
                    }

                    // PayPal - Highlighted
                    Button {
                        Layout.fillWidth: true
                        height: 52
                        text: qsTr("💳 PayPal Donation")
                        font.bold: true
                        font.pixelSize: 15

                        background: Rectangle {
                            radius: 14
                            color: "#0070BA"
                            border.color: "#009CDE"
                            border.width: 2

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#009CDE" }
                                    GradientStop { position: 1.0; color: "#0070BA" }
                                }
                            }
                        }

                        contentItem: Label {
                            text: parent.text
                            font: parent.font
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: Qt.openUrlExternally("https://www.paypal.com/ncp/payment/NU89529268M2W")
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // PROJECT LINKS
            // ─────────────────────────────────────────────────────────
            AppCard {
                width: parent.width
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Label {
                        text: qsTr("Project Links")
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Button {
                        Layout.fillWidth: true
                        text: qsTr("GitHub Project Page")
                        onClicked: Qt.openUrlExternally("https://github.com/nostar/DroidStar")
                    }
                }
            }

            Item { height: 40 }
        }
    }
}
