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

// BrandMeister / TGIF login check result. Bottom sheet on phones, centered card on wide windows.
// statuses: { BM: "", TGIF: "" } with "", "checking", "ok", "default", "rejected", "unreachable".
Item {
    id: root

    property var statuses: ({})
    property var networks: []           // networks shown, e.g. ["BM", "TGIF"]
    property bool showSettingsButton: false

    signal retryRequested(var networks)
    signal settingsRequested()

    readonly property bool opened: sheet.opened

    Tokens { id: t }

    function _st(n) { return (root.statuses && root.statuses[n]) ? root.statuses[n] : "" }
    function _any(s) {
        for (var i = 0; i < root.networks.length; i++) if (_st(root.networks[i]) === s) return true
        return false
    }
    readonly property bool anyChecking: _any("checking")
    readonly property bool anyRejected: _any("rejected")
    readonly property bool anyUnreachable: _any("unreachable")
    readonly property bool bmRejected: networks.indexOf("BM") >= 0 && _st("BM") === "rejected"
    readonly property bool tgifRejected: networks.indexOf("TGIF") >= 0 && _st("TGIF") === "rejected"

    readonly property string kind: anyChecking ? "checking" : anyRejected ? "error" : anyUnreachable ? "warning" : "success"
    readonly property color toneColor: kind === "error" ? t.danger : kind === "warning" ? t.warning
                                     : kind === "checking" ? t.accent : t.success
    readonly property string title: kind === "checking" ? qsTr("Checking login…")
                                  : kind === "error" ? qsTr("Login failed")
                                  : kind === "warning" ? qsTr("Couldn't check login")
                                  : qsTr("Login verified")

    function show(nets) {
        root.networks = nets || []
        sheet.open()
    }
    function close() { sheet.close() }

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
        readonly property real safeLeft: (win && win.safeArea) ? win.safeArea.left : 0
        readonly property real safeRight: (win && win.safeArea) ? win.safeArea.right : 0
        readonly property real bottomPad: 14 + (wideLayout ? 0 : safeBottom)
        readonly property real sideMargin: 16 + (wideLayout ? 0 : Math.max(safeLeft, safeRight))
        readonly property real chromeHeight: (wideLayout ? 16 : 22) + bottomPad + 10 + 12
        readonly property real maxHeight: wideLayout ? oh - safeTop - safeBottom - 48 : oh - safeTop - 24

        width: wideLayout ? Math.min(440, ow - 48) : ow
        height: Math.min(maxHeight, header.implicitHeight + body.implicitHeight + (actions.visible ? actions.implicitHeight : -12) + chromeHeight)

        Overlay.modal: Rectangle { color: "#99000000" }
        x: (ow - width) / 2
        y: wideLayout ? safeTop + Math.max(24, (oh - safeTop - safeBottom - height) / 2) : oh - height

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
        onClosed: slide.y = 0

        contentItem: Item {
            transform: Translate { id: slide }
            NumberAnimation { id: snapBack; target: slide; property: "y"; to: 0; duration: 160; easing.type: Easing.OutCubic }

            Rectangle {
                anchors.fill: parent
                anchors.bottomMargin: sheet.wideLayout ? 0 : -18
                color: t.surface
                radius: 18
                border.color: t.stroke
                border.width: sheet.wideLayout ? 1 : 0
            }

            // Drag handle (phones)
            Item {
                id: handle
                visible: !sheet.wideLayout
                width: parent.width
                height: visible ? 22 : 0
                Rectangle { anchors.centerIn: parent; width: 40; height: 5; radius: 2.5; color: t.textMuted; opacity: 0.5 }
                DragHandler {
                    target: null
                    xAxis.enabled: false
                    onTranslationChanged: slide.y = Math.max(0, translation.y)
                    onActiveChanged: {
                        if (active) return
                        if (slide.y > 80) sheet.close()
                        else snapBack.start()
                    }
                }
            }

            RowLayout {
                id: header
                anchors.top: handle.bottom
                anchors.topMargin: sheet.wideLayout ? 16 : 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: sheet.sideMargin
                anchors.rightMargin: sheet.sideMargin
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 16
                    color: t.tint(root.toneColor, 0.16)
                    BusyIndicator {
                        anchors.centerIn: parent
                        visible: root.kind === "checking"
                        running: visible
                        width: 20; height: 20; padding: 0
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: root.kind !== "checking"
                        text: root.kind === "error" ? "" : root.kind === "warning" ? "" : ""
                        font.family: "FontAwesome"
                        font.pixelSize: 15
                        color: root.toneColor
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.title
                    font.pixelSize: t.fsTitle
                    font.bold: true
                    color: t.text
                    elide: Text.ElideRight
                }
                ToolButton {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    text: ""
                    font.family: "FontAwesome"
                    font.pixelSize: 14
                    opacity: 0.7
                    Accessible.name: qsTr("Close")
                    onClicked: sheet.close()
                }
            }

            Flickable {
                id: scroller
                anchors.top: header.bottom
                anchors.bottom: actions.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 10
                anchors.bottomMargin: 12
                anchors.leftMargin: sheet.sideMargin
                anchors.rightMargin: sheet.sideMargin
                contentWidth: width
                contentHeight: body.implicitHeight
                clip: contentHeight > height
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator {}

                ColumnLayout {
                    id: body
                    width: scroller.width
                    spacing: 12

                    // One row per network
                    Repeater {
                        model: root.networks
                        delegate: Rectangle {
                            id: netRow
                            required property string modelData
                            readonly property string st: root._st(modelData)
                            readonly property color stColor: st === "rejected" ? t.danger
                                                           : st === "unreachable" ? t.warning
                                                           : (st === "ok" || st === "default") ? t.success : t.textMuted
                            Layout.fillWidth: true
                            implicitHeight: 44
                            radius: 12
                            color: t.surface2
                            border.color: t.stroke
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8
                                Label {
                                    Layout.fillWidth: true
                                    text: netRow.modelData === "TGIF" ? "TGIF" : "BrandMeister"
                                    font.pixelSize: t.fsBody
                                    font.bold: true
                                    color: t.text
                                    elide: Text.ElideRight
                                }
                                BusyIndicator {
                                    visible: netRow.st === "checking"
                                    running: visible
                                    Layout.preferredWidth: 16; Layout.preferredHeight: 16; padding: 0
                                }
                                Label {
                                    visible: netRow.st !== "checking"
                                    text: netRow.st === "rejected" ? "" : netRow.st === "unreachable" ? "" : ""
                                    font.family: "FontAwesome"
                                    font.pixelSize: 12
                                    color: netRow.stColor
                                }
                                Label {
                                    text: netRow.st === "checking" ? qsTr("Checking")
                                        : netRow.st === "rejected" ? qsTr("Rejected")
                                        : netRow.st === "unreachable" ? qsTr("No reply")
                                        : netRow.st === "default" ? qsTr("OK, security off")
                                        : qsTr("Verified")
                                    font.pixelSize: t.fsSmall + 1
                                    color: netRow.stColor
                                }
                            }
                        }
                    }

                    // Tips when a password was rejected
                    Repeater {
                        model: [
                            qsTr("Use the <b>Hotspot password</b>, not your account login password."),
                            root.bmRejected && root.tgifRejected ? qsTr("Set it in BM SelfCare and copy it from TGIF User Security.")
                                : root.tgifRejected ? qsTr("Copy it from TGIF User Security.")
                                : qsTr("Set it in BM SelfCare under Hotspot Security."),
                            qsTr("A simple 6 to 8 character password with no special characters works best.")
                        ]
                        // Flat in the body layout: a nested layout of wrapped text causes a polish loop
                        delegate: Label {
                            required property string modelData
                            visible: root.anyRejected
                            Layout.fillWidth: true
                            Layout.topMargin: -4
                            leftPadding: 13
                            text: modelData
                            textFormat: Text.StyledText
                            wrapMode: Text.WordWrap
                            font.pixelSize: t.fsSmall + 1
                            color: t.text
                            lineHeight: 1.15
                            Rectangle {
                                y: 7
                                width: 5; height: 5
                                radius: 2.5
                                color: t.textMuted
                            }
                        }
                    }

                Label {
                    Layout.fillWidth: true
                    visible: root.anyUnreachable && !root.anyRejected
                    text: qsTr("No reply from the server. Check your internet and retry.")
                    wrapMode: Text.WordWrap
                    font.pixelSize: t.fsSmall + 1
                    color: t.textMuted
                    }

                    // Links to the password pages
                    Flow {
                        Layout.fillWidth: true
                        visible: root.anyRejected
                        spacing: 8
                        AppButton {
                            visible: root.bmRejected
                            text: qsTr("BM SelfCare")
                            glyph: ""
                            implicitHeight: 38
                            font.pixelSize: t.fsSmall + 1
                            onClicked: Qt.openUrlExternally("https://brandmeister.network/?page=selfcare")
                        }
                        AppButton {
                            visible: root.tgifRejected
                            text: qsTr("TGIF User Security")
                            glyph: ""
                            implicitHeight: 38
                            font.pixelSize: t.fsSmall + 1
                            onClicked: Qt.openUrlExternally("https://tgif.network/")
                        }
                    }
                }
            }

            // Pinned so actions stay reachable on short landscape screens
            RowLayout {
                id: actions
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: sheet.sideMargin
                anchors.rightMargin: sheet.sideMargin
                anchors.bottomMargin: sheet.bottomPad
                spacing: 8
                AppButton {
                    Layout.fillWidth: true
                    visible: root.showSettingsButton && root.anyRejected
                    text: qsTr("Settings")
                    glyph: ""
                    onClicked: { sheet.close(); root.settingsRequested() }
                }
                AppButton {
                    Layout.fillWidth: true
                    visible: root.kind === "error" || root.kind === "warning" || root.kind === "checking"
                    enabled: !root.anyChecking
                    busy: root.anyChecking
                    tone: "primary"
                    text: root.anyChecking ? qsTr("Checking…") : qsTr("Retry")
                    glyph: ""
                    onClicked: {
                        var redo = []
                        for (var i = 0; i < root.networks.length; i++) {
                            var s = root._st(root.networks[i])
                            if (s === "rejected" || s === "unreachable") redo.push(root.networks[i])
                        }
                        root.retryRequested(redo)
                    }
                }
                AppButton {
                    Layout.fillWidth: true
                    visible: root.kind === "success"
                    tone: "primary"
                    text: qsTr("Done")
                    onClicked: sheet.close()
                }
            }
        }
    }
}
