import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: aboutTab

    Rectangle {
        id: helpText
        anchors.fill: parent
        color: "#252424"

        Flickable {
            id: flickable
            anchors.fill: parent
            contentWidth: parent.width
            contentHeight: aboutText.height + buyMeCoffeeButton.height + 20 // Adjusted content height
            flickableDirection: Flickable.VerticalFlick
            clip: true

            Text {
                id: aboutText
                width: parent.width - 40
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.WordWrap
                color: "white"
                text: qsTr("\nDROID-Star git build " + droidstar.get_software_build() +
                           "\nPlatform:\t" + droidstar.get_platform() +
                           "\nArchitecture:\t" + droidstar.get_arch() +
                           "\nBuild ABI:\t" + droidstar.get_build_abi() +
                           "\n\nProject page: https://github.com/nostar/DroidStar" +
                           "\n\nOriginal Copyright (C) 2019-2021 Doug McLain AD8DP\n" +
                           "\n\nModification Copyright (C) 2024 Rohith Namboothiri VU3LVO\n" +
                           "\n\nThis customized iOS/Android version, built and distributed by VU3LVO, is specifically designed for use by a select group and is not intended for public use at the moment.")
            }

            // Buy Me a Coffee Button
            Rectangle {
                id: buyMeCoffeeButton
                width: 200
                height: 50
                color: "#FFDD00" // Color matching Buy Me a Coffee branding
                radius: 10
                border.color: "#333333"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: aboutText.bottom
                anchors.topMargin: 20 // Space between text and button

                Text {
                    id: buttonText
                    anchors.centerIn: parent
                    text: qsTr("Buy Me a Coffee")
                    color: "#333333"
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        Qt.openUrlExternally("https://buymeacoffee.com/rohithz")
                    }
                    onPressed: {
                        buyMeCoffeeButton.color = "#FFC700" // Slight color change on press
                    }
                    onReleased: {
                        buyMeCoffeeButton.color = "#FFDD00"
                    }
                }
            }
        }
    }
}
