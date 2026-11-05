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
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

// Add / re-learn a hardware PTT button: scan -> connect -> learn -> result.
Item {
    id: root

    property var manager: null
    property var app: null
    property string mode: "ble"          // "ble" | "relearn" | "key"
    property bool showUnnamed: false

    readonly property string learnState: manager ? manager.learnState : "idle"
    readonly property var learnResult: manager ? manager.learnResult : ({})
    readonly property bool scanning: !!manager && manager.scanning
    readonly property string btState: manager ? manager.bluetoothState : "unsupported"

    // scan | connecting | learning | done | failed | key | bluetooth
    readonly property string step: {
        if (mode === "key") return learnState === "done" ? "done" : (learnState === "failed" ? "failed" : "key")
        if (learnState === "connecting" || learnState === "discovering") return "connecting"
        if (learnState === "learning") return "learning"
        if (learnState === "done") return "done"
        if (learnState === "failed") return "failed"
        if (btState !== "poweredOn" && btState !== "unknown") return "bluetooth"
        return "scan"
    }

    readonly property string stepTitle: step === "scan" ? qsTr("Add Bluetooth button")
                                      : step === "connecting" ? qsTr("Connecting…")
                                      : step === "learning" ? qsTr("Learning the button")
                                      : step === "done" ? qsTr("Button ready")
                                      : step === "failed" ? qsTr("Could not add button")
                                      : step === "key" ? qsTr("Add keyboard-type button")
                                      : qsTr("Bluetooth unavailable")

    Tokens { id: t }

    function openForScan() {
        root.mode = "ble"
        sheet.open()
        if (root.manager) root.manager.startScan()
    }
    function openForRelearn() {
        root.mode = "relearn"
        sheet.open()
    }
    function openForKey() {
        root.mode = "key"
        sheet.open()
        if (root.manager) root.manager.startKeyLearning()
    }
    function finishAndClose() {
        if (root.manager) root.manager.finishAdd()
        sheet.close()
    }
    function retry() {
        if (!root.manager) return
        if (root.mode === "key") root.manager.startKeyLearning()
        else { root.manager.finishAdd(); root.manager.startScan() }
    }

    readonly property var visibleResults: {
        var src = root.manager ? root.manager.scanResults : []
        if (!src) return []
        if (root.showUnnamed) return src
        var out = []
        for (var i = 0; i < src.length; i++) if (src[i].named || src[i].recognised) out.push(src[i])
        return out
    }

    Popup {
        id: sheet
        parent: Overlay.overlay
        modal: true
        dim: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        readonly property var win: root.Window.window
        readonly property real ow: parent ? parent.width : 400
        readonly property real oh: parent ? parent.height : 700
        readonly property bool wideLayout: ow >= 600
        readonly property real safeTop: (win && win.safeArea) ? win.safeArea.top : 0
        readonly property real safeBottom: (win && win.safeArea) ? win.safeArea.bottom : 0
        readonly property real radiusPad: 18

        width: wideLayout ? Math.min(520, ow - 48) : ow
        readonly property real centeredRoom: oh - safeTop - safeBottom - 48
        height: wideLayout ? Math.min(560, centeredRoom) : Math.min(oh * 0.78, oh - safeTop - 24)

        Overlay.modal: Rectangle { color: "#99000000" }
        x: (ow - width) / 2
        y: wideLayout ? safeTop + 24 + Math.max(0, (centeredRoom - height) / 2) : oh - height

        background: Item {}

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { target: slide; property: "y"; from: sheet.wideLayout ? 18 : sheet.height; to: 0; duration: 220; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { target: slide; property: "y"; to: sheet.wideLayout ? 12 : sheet.height; duration: 160; easing.type: Easing.InCubic }
                NumberAnimation { property: "opacity"; to: 0; duration: 140 }
            }
        }

        onClosed: {
            slide.y = 0
            if (root.manager) {
                root.manager.stopScan()
                root.manager.cancelKeyLearning()
                // "done" is committed already; anything else is abandoned.
                if (root.manager.learnState !== "done") root.manager.cancelAdd()
                else root.manager.finishAdd()
            }
        }

        contentItem: Item {
            transform: Translate { id: slide }

            NumberAnimation { id: snapBack; target: slide; property: "y"; to: 0; duration: 160; easing.type: Easing.OutCubic }

            Rectangle {
                anchors.fill: parent
                anchors.bottomMargin: sheet.wideLayout ? 0 : -sheet.radiusPad
                color: t.surface
                radius: 18
                border.color: t.stroke
                border.width: sheet.wideLayout ? 1 : 0
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 12 + (sheet.wideLayout ? 0 : sheet.safeBottom)
                spacing: 10

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    visible: !sheet.wideLayout
                    Rectangle { anchors.centerIn: parent; width: 40; height: 5; radius: 2.5; color: t.textMuted; opacity: 0.5 }
                    DragHandler {
                        target: null
                        xAxis.enabled: false
                        onTranslationChanged: slide.y = Math.max(0, translation.y)
                        onActiveChanged: {
                            if (active) return
                            if (slide.y > 90) sheet.close()
                            else snapBack.start()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: root.stepTitle
                        font.pixelSize: t.fsTitle; font.bold: true; color: t.text
                        elide: Text.ElideRight
                    }
                    BusyIndicator {
                        visible: root.step === "connecting" || (root.step === "scan" && root.scanning)
                        running: visible
                        width: 22; height: 22
                        padding: 0
                    }
                    ToolButton {
                        text: ""
                        font.family: "FontAwesome"
                        font.pixelSize: 14
                        Accessible.name: qsTr("Close")
                        onClicked: sheet.close()
                    }
                }

                // ---------------- SCAN ----------------
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.step === "scan"
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall + 1
                        color: t.textMuted
                        text: qsTr("Press the button once, then pick it from the list.")
                    }

                    ListView {
                        id: scanList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.visibleResults
                        boundsBehavior: Flickable.StopAtBounds
                        spacing: 6
                        ScrollBar.vertical: ScrollBar { policy: scanList.contentHeight > scanList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                        delegate: Rectangle {
                            id: scanRow
                            required property var modelData
                            width: ListView.view.width
                            implicitHeight: Math.max(t.controlHeight + 10, scanCol.implicitHeight + 16)
                            radius: 12
                            color: scanMouse.pressed ? t.surface3 : t.surface2
                            border.color: modelData.recognised ? t.tint(t.success, 0.6) : t.stroke
                            border.width: 1

                            readonly property int bars: modelData.bars

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                ColumnLayout {
                                    id: scanCol
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.pixelSize: t.fsBody
                                        font.bold: true
                                        color: t.text
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        visible: text !== ""
                                        text: modelData.recognised ? modelData.signatureName
                                              : (modelData.saved ? qsTr("Already added. Tap to re-learn") : "")
                                        font.pixelSize: t.fsCaption
                                        color: modelData.recognised ? t.success : t.textMuted
                                        elide: Text.ElideRight
                                    }
                                }
                                Row {
                                    spacing: 2
                                    Repeater {
                                        model: 4
                                        delegate: Rectangle {
                                            required property int index
                                            width: 4
                                            height: 6 + index * 3
                                            anchors.bottom: parent.bottom
                                            radius: 1
                                            color: index < scanRow.bars ? t.accent : t.stroke
                                        }
                                    }
                                }
                                Label {
                                    text: ""
                                    font.family: "FontAwesome"
                                    font.pixelSize: 12
                                    color: t.textMuted
                                }
                            }

                            MouseArea {
                                id: scanMouse
                                anchors.fill: parent
                                onClicked: {
                                    if (!root.manager) return
                                    root.mode = modelData.saved ? "relearn" : "ble"
                                    root.manager.addScannedDevice(modelData.id)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            visible: root.visibleResults.length === 0
                            text: root.scanning ? qsTr("Searching. Press the button to wake it.")
                                                : qsTr("Nothing found. Press the button and scan again.")
                            font.pixelSize: 13
                            color: t.textMuted
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        CheckBox {
                            text: qsTr("Show unnamed devices")
                            font.pixelSize: t.fsSmall
                            checked: root.showUnnamed
                            onToggled: root.showUnnamed = checked
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: root.scanning ? qsTr("Stop") : qsTr("Scan again")
                            glyph: root.scanning ? "" : ""
                            onClicked: {
                                if (!root.manager) return
                                if (root.scanning) root.manager.stopScan()
                                else root.manager.startScan()
                            }
                        }
                    }
                }

                // ---------------- CONNECTING / LEARNING ----------------
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.step === "connecting" || root.step === "learning"
                    spacing: 12

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.step === "learning" ? "" : ""
                        font.family: "FontAwesome"
                        font.pixelSize: 40
                        color: t.accent
                        SequentialAnimation on opacity {
                            running: root.step === "learning"
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.35; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsBody
                        color: t.text
                        text: root.step === "learning"
                              ? qsTr("Hold the button for one second, release, then do it again.")
                              : qsTr("Connecting. If the button is asleep, press it once.")
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall
                        color: t.textMuted
                        text: root.manager ? root.manager.learnProgress : ""
                    }

                    Item { Layout.fillHeight: true }

                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Cancel")
                        onClicked: sheet.close()
                    }
                }

                // ---------------- KEY LEARNING ----------------
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.step === "key"
                    spacing: 12

                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: ""
                        font.family: "FontAwesome"
                        font.pixelSize: 40
                        color: t.accent
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsBody
                        color: t.text
                        text: qsTr("Press and release the button to use for PTT.")
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall
                        color: t.textMuted
                        text: root.manager ? root.manager.learnProgress : ""
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsCaption
                        color: t.warning
                        text: qsTr("Bluetooth or wired keyboard-type buttons. Works only while DroidStar is on screen. Volume buttons cannot be used. Wired earphone buttons use the Headset button switch instead.")
                    }
                    Item { Layout.fillHeight: true }
                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Cancel")
                        onClicked: sheet.close()
                    }
                }

                // ---------------- DONE ----------------
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.step === "done"
                    spacing: 12

                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: ""
                        font.family: "FontAwesome"
                        font.pixelSize: 40
                        color: t.success
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsBody
                        font.bold: true
                        color: t.text
                        text: root.learnResult && root.learnResult.name ? root.learnResult.name : qsTr("PTT button")
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsSmall + 1
                        color: t.textMuted
                        text: {
                            var r = root.learnResult || {}
                            var s = (r.capability === "hold")
                                    ? qsTr("Reports press and release. Hold or toggle both work.")
                                    : qsTr("Sends one click per press. It toggles TX on and off.")
                            if (r.tentative) s += " " + qsTr("Only one press was seen. Re-learn if it misbehaves.")
                            
                            return s
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: root.mode !== "key"
                        font.pixelSize: t.fsCaption
                        color: t.textMuted
                        text: qsTr("Works with the screen off while connected.")
                    }
                    Item { Layout.fillHeight: true }
                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Done")
                        tone: "primary"
                        onClicked: root.finishAndClose()
                    }
                }

                // ---------------- FAILED / BLUETOOTH ----------------
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.step === "failed" || root.step === "bluetooth"
                    spacing: 12

                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: ""
                        font.family: "FontAwesome"
                        font.pixelSize: 40
                        color: t.warning
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsBody
                        color: t.text
                        text: root.step === "failed"
                              ? ((root.learnResult && root.learnResult.reason) ? root.learnResult.reason : qsTr("Something went wrong."))
                              : root.btState === "unauthorized"
                                ? qsTr("Allow Bluetooth for DroidStar in Settings, then try again.")
                                : root.btState === "poweredOff"
                                  ? qsTr("Turn Bluetooth on, then try again.")
                                  : qsTr("Bluetooth LE is not available on this device.")
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 10
                        AppButton {
                            visible: root.step === "bluetooth" && root.btState === "unauthorized"
                            text: qsTr("Open Settings")
                            tone: "primary"
                            onClicked: if (root.manager) root.manager.openBluetoothSettings()
                        }
                        AppButton {
                            visible: root.step === "failed"
                            text: qsTr("Try again")
                            tone: "primary"
                            onClicked: root.retry()
                        }
                        AppButton {
                            text: qsTr("Close")
                            onClicked: sheet.close()
                        }
                    }
                }
            }
        }
    }
}
