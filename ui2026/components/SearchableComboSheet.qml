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
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

Item {
    id: root
    Layout.fillWidth: true
    implicitHeight: t.controlHeight
    implicitWidth: 120

    property var model: []
    // Bind this from the owner; the component never assigns it, so owner bindings stay intact.
    property string currentText: ""
    property string displayText: ""
    property string placeholderText: qsTr("Select...")
    property string sheetTitle: ""
    property bool searchable: (model && model.length > 8)
    property var itemIconFn: null
    property color itemIconColor: t.accent

    signal activated(int index, string text)

    readonly property bool isMobile: Qt.platform.os === "ios" || Qt.platform.os === "android"

    Tokens { id: t }

    Accessible.role: Accessible.ComboBox
    Accessible.name: sheetTitle !== "" ? sheetTitle : placeholderText
    Accessible.description: currentText
    activeFocusOnTab: true
    Keys.onSpacePressed: root.open()
    Keys.onReturnPressed: root.open()

    property var _filteredItems: []

    function _runFilter() {
        var src = root.model
        if (!src || src.length === 0) {
            root._filteredItems = []
            return
        }
        var query = searchField.text.trim().toLowerCase()
        var result = []
        for (var i = 0; i < src.length; i++) {
            var item = "" + src[i]
            if (query === "" || item.toLowerCase().indexOf(query) !== -1)
                result.push({ label: item, idx: i })
        }
        root._filteredItems = result
    }

    function open() {
        if (!root.enabled) return
        searchField.text = ""
        _runFilter()
        sheet.open()
    }

    function _choose(entry) {
        if (!entry) return
        root.activated(entry.idx, entry.label)
        sheet.close()
    }

    // Large host lists: debounce typing; small lists filter immediately.
    Timer {
        id: filterTimer
        interval: (root.model && root.model.length > 1500) ? 60 : 0
        repeat: false
        onTriggered: root._runFilter()
    }

    Rectangle {
        id: triggerButton
        anchors.fill: parent
        radius: 10
        color: triggerMouse.pressed ? t.surface3 : t.surface2
        border.color: (sheet.opened || root.activeFocus) ? t.accent : t.stroke
        border.width: 1
        opacity: root.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: t.durFast } }
        Behavior on border.color { ColorAnimation { duration: t.durFast } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 6

            Label {
                readonly property string glyph: (typeof root.itemIconFn === "function" && root.currentText !== "")
                                                ? (root.itemIconFn(root.currentText) || "")
                                                : ""
                visible: glyph !== ""
                text: glyph
                font.family: "FontAwesome"
                font.pixelSize: 11
                color: root.itemIconColor
                verticalAlignment: Text.AlignVCenter
            }

            Label {
                Layout.fillWidth: true
                text: root.displayText !== "" ? root.displayText
                      : (root.currentText !== "" ? root.currentText : root.placeholderText)
                font.pixelSize: t.fsBody
                color: (root.currentText !== "" || root.displayText !== "") ? t.text : t.textMuted
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Label {
                text: "\uf078"
                font.family: "FontAwesome"
                font.pixelSize: 11
                color: t.textMuted
                rotation: sheet.opened ? 180 : 0
                Behavior on rotation { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
            }
        }

        MouseArea {
            id: triggerMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.open()
        }
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
        readonly property real kb: Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height : 0

        // Size to content (stable while filtering), capped to the available space.
        readonly property real wantedHeight: (wideLayout ? 0 : 28)
                                             + (root.sheetTitle !== "" ? 54 : 0)
                                             + (root.searchable ? t.controlHeight + 36 : 0)
                                             + Math.max(2, root.model ? root.model.length : 0) * t.controlHeight
                                             + 28 + (wideLayout || kb > 0 ? 0 : safeBottom)

        width: wideLayout ? Math.min(520, ow - 48) : ow
        height: Math.max(200, Math.min(wantedHeight, wideLayout ? 560 : oh * 0.72, oh - kb - safeTop - 24))

        Overlay.modal: Rectangle {
            color: "#99000000"
        }
        x: (ow - width) / 2
        y: wideLayout ? Math.max(safeTop + 24, (oh - kb - height) / 2) : oh - kb - height

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

        onOpened: {
            var cur = -1
            for (var i = 0; i < root._filteredItems.length; i++) {
                if (root._filteredItems[i].label === root.currentText) { cur = i; break }
            }
            if (cur >= 0) resultsList.positionViewAtIndex(cur, ListView.Center)
            // Popping the keyboard on phones hides most of the list; only autofocus on desktop.
            if (!root.isMobile && root.searchable) searchField.forceActiveFocus()
        }
        onClosed: {
            slide.y = 0
            searchField.focus = false
            root.forceActiveFocus()
        }

        contentItem: Item {
            id: panel
            transform: Translate { id: slide }

            NumberAnimation { id: snapBack; target: slide; property: "y"; to: 0; duration: 160; easing.type: Easing.OutCubic }

            Rectangle {
                anchors.fill: parent
                // Extend under the home indicator on phones
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
                anchors.bottomMargin: 12 + (sheet.wideLayout || sheet.kb > 0 ? 0 : sheet.safeBottom)
                spacing: 10

                // Grab handle (drag down to dismiss on phones)
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    visible: !sheet.wideLayout

                    Rectangle {
                        anchors.centerIn: parent
                        width: 40; height: 5; radius: 2.5
                        color: t.textMuted; opacity: 0.5
                    }

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
                    visible: root.sheetTitle !== ""
                    Label {
                        Layout.fillWidth: true
                        text: root.sheetTitle
                        font.pixelSize: t.fsTitle; font.bold: true; color: t.text
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        text: "\uf00d"
                        font.family: "FontAwesome"
                        font.pixelSize: 14
                        Accessible.name: qsTr("Close")
                        onClicked: sheet.close()
                    }
                }

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    visible: root.searchable
                    placeholderText: qsTr("Search…")
                    font.pixelSize: t.fsBody
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    rightPadding: clearSearch.visible ? clearSearch.width + 8 : 12
                    onTextChanged: filterTimer.restart()
                    onAccepted: if (root._filteredItems.length > 0) root._choose(root._filteredItems[0])

                    background: Rectangle {
                        implicitHeight: t.controlHeight
                        radius: 10
                        color: t.surface2
                        border.color: searchField.activeFocus ? t.accent : t.stroke
                        border.width: 1
                    }

                    ToolButton {
                        id: clearSearch
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        visible: searchField.text.length > 0
                        text: "\uf057"
                        font.family: "FontAwesome"
                        font.pixelSize: 14
                        opacity: 0.7
                        Accessible.name: qsTr("Clear search")
                        onClicked: searchField.clear()
                    }
                }

                Label {
                    visible: root.searchable
                    text: root._filteredItems.length + " " + (root._filteredItems.length === 1 ? qsTr("result") : qsTr("results"))
                    font.pixelSize: t.fsCaption; color: t.textMuted
                }

                ListView {
                    id: resultsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root._filteredItems
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 400
                    reuseItems: true
                    ScrollBar.vertical: ScrollBar { policy: resultsList.contentHeight > resultsList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                    delegate: ItemDelegate {
                        required property var modelData
                        readonly property bool isCurrent: root.currentText === modelData.label
                        width: ListView.view.width
                        height: t.controlHeight

                        contentItem: RowLayout {
                            spacing: 8
                            Label {
                                readonly property string glyph: (typeof root.itemIconFn === "function")
                                                                ? (root.itemIconFn(modelData.label) || "")
                                                                : ""
                                visible: glyph !== ""
                                text: glyph
                                font.family: "FontAwesome"
                                font.pixelSize: 11
                                color: isCurrent ? t.accent : root.itemIconColor
                                verticalAlignment: Text.AlignVCenter
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.label
                                font.pixelSize: t.fsBody
                                font.bold: isCurrent
                                color: isCurrent ? t.accent : t.text
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            Label {
                                visible: isCurrent
                                text: "\uf00c"
                                font.family: "FontAwesome"
                                font.pixelSize: 13
                                color: t.accent
                            }
                        }

                        background: Rectangle {
                            radius: 10
                            color: isCurrent ? t.tint(t.accent, 0.14)
                                 : (parent.down ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                        }

                        onClicked: root._choose(modelData)
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: root._filteredItems.length === 0
                        text: (root.model && root.model.length > 0) ? qsTr("No matches found") : qsTr("Nothing to choose from")
                        font.pixelSize: 13; color: t.textMuted
                    }
                }
            }
        }

        readonly property real radiusPad: 18
    }
}
