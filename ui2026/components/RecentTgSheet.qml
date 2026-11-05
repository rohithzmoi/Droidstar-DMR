/*
    Copyright (C) 2024-2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

// Searchable Recent & Favourite talkgroups sheet, shared by the Advanced (MainPage) and Simple homes.
Popup {
    id: recentTgSheet

    property var app: null
    property var appState: null
    property var droidstarRef: null
    property string mode: ""
    signal talkgroupChosen(string tg, bool asPrivate)

    Tokens { id: t }
    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string filterTab: "all" // "all", "fav", "recent"
    property var filteredModel: []

    readonly property var win: recentTgSheet.app
    readonly property real ow: parent ? parent.width : 400
    readonly property real oh: parent ? parent.height : 700
    readonly property bool wideSheet: ow >= 600
    readonly property real safeTop: (win && win.safeArea) ? win.safeArea.top : 0
    readonly property real safeBottom: (win && win.safeArea) ? win.safeArea.bottom : 0
    readonly property real kb: Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height : 0

    width: wideSheet ? Math.min(520, ow - 48) : ow
    readonly property real centeredRoom: oh - safeTop - Math.max(kb, safeBottom) - 48
    height: Math.max(Math.min(280, centeredRoom), Math.min(wideSheet ? 580 : oh * 0.78,
                                                 wideSheet ? centeredRoom : oh - kb - safeTop - 24))
    x: (ow - width) / 2
    y: wideSheet ? safeTop + 24 + Math.max(0, (centeredRoom - height) / 2) : oh - kb - height

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle {
        color: t.surface
        radius: 18
        border.color: t.stroke
        border.width: recentTgSheet.wideSheet ? 1 : 0
    }

    function rebuildList() {
        if (!recentTgSheet.droidstarRef || typeof recentTgSheet.droidstarRef.searchTalkgroups !== "function") {
            recentTgSheet.filteredModel = []
            return
        }
        var q = recentSearchInput.text.trim()
        var raw = recentTgSheet.droidstarRef.searchTalkgroups(q, 80)
        var out = []
        for (var i = 0; i < raw.length; i++) {
            var item = raw[i]
            if (!item) continue
            if (q === "") {
                if (recentTgSheet.filterTab === "fav" && !item.isFavourite) continue
                if (recentTgSheet.filterTab === "recent" && !item.isRecent) continue
            }
            out.push(item)
        }
        recentTgSheet.filteredModel = out
    }

    function openSheet() {
        recentSearchInput.text = ""
        recentTgSheet.filterTab = "all"
        rebuildList()
        recentTgSheet.open()
    }

    function selectTg(tgStr, asPrivate) {
        var raw = (tgStr === undefined || tgStr === null) ? "" : String(tgStr)
        var v = raw.replace(/[^0-9]/g, "")
        if (v === "" || v.length > 8)
            return
        recentTgSheet.close()
        recentTgSheet.talkgroupChosen(v, !!asPrivate)
    }

    Connections {
        target: recentTgSheet.appState
        function onRecentTgidsChanged() { if (recentTgSheet.opened) recentTgSheet.rebuildList() }
        function onFavouriteTgidsChanged() { if (recentTgSheet.opened) recentTgSheet.rebuildList() }
    }

    contentItem: Item {
        Rectangle {
            anchors.fill: parent
            anchors.bottomMargin: recentTgSheet.wideSheet ? 0 : -18
            color: t.surface
            radius: 18
            border.color: t.stroke
            border.width: recentTgSheet.wideSheet ? 1 : 0
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 10
            anchors.bottomMargin: 12 + (recentTgSheet.wideSheet || recentTgSheet.kb > 0 ? 0 : recentTgSheet.safeBottom)
            spacing: 10

            // Grab handle on phones
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                visible: !recentTgSheet.wideSheet
                width: 40; height: 5; radius: 2.5
                color: t.textMuted; opacity: 0.5
            }

            // Header Row
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    // Short title on narrow sheets (Slide Over) instead of eliding mid-word
                    text: recentTgSheet.width < 360 ? qsTr("Talkgroups") : qsTr("Recent & Favourite Talkgroups")
                    font.pixelSize: t.fsTitle
                    font.bold: true
                    color: t.text
                    elide: Text.ElideRight
                }

                Button {
                    visible: recentTgSheet.mode === "DMR"
                    flat: true
                    text: qsTr("Scan")
                    font.pixelSize: t.fsCaption
                    onClicked: {
                        recentTgSheet.close()
                        if (recentTgSheet.app) recentTgSheet.app.navigate("scan")
                    }
                }

                ToolButton {
                    text: "\uf00d"
                    font.family: "FontAwesome"
                    font.pixelSize: 14
                    Accessible.name: qsTr("Close")
                    onClicked: recentTgSheet.close()
                }
            }

            // Search Bar
            TextField {
                id: recentSearchInput
                Layout.fillWidth: true
                placeholderText: qsTr("Search recent, favourite or directory TGs…")
                font.pixelSize: t.fsBody
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                rightPadding: clearRecentSearch.visible ? 36 : 12
                onTextChanged: recentTgSheet.rebuildList()
                onAccepted: {
                    if (recentTgSheet.filteredModel.length > 0) {
                        recentTgSheet.selectTg(recentTgSheet.filteredModel[0].tgid)
                    } else if (/^[0-9]{1,8}$/.test(text.trim())) {
                        recentTgSheet.selectTg(text.trim())
                    }
                }

                background: Rectangle {
                    implicitHeight: t.controlHeight
                    radius: 10
                    color: t.surface2
                    border.color: recentSearchInput.activeFocus ? t.accent : t.stroke
                    border.width: 1
                }

                ToolButton {
                    id: clearRecentSearch
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: recentSearchInput.text.length > 0
                    text: "\uf057"
                    font.family: "FontAwesome"
                    font.pixelSize: 14
                    opacity: 0.75
                    Accessible.name: qsTr("Clear search")
                    onClicked: recentSearchInput.clear()
                }
            }

            // Filter Tabs (All / Starred Favourites / Recent) + DMR Quick Actions
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ChipToggle {
                    text: qsTr("All")
                    checked: recentTgSheet.filterTab === "all"
                    onClicked: { recentTgSheet.filterTab = "all"; recentTgSheet.rebuildList() }
                }
                ChipToggle {
                    text: qsTr("Favourites (%1)").arg(recentTgSheet.appState && recentTgSheet.appState.favouriteTgids ? recentTgSheet.appState.favouriteTgids.length : 0)
                    glyph: "\uf005"
                    activeColor: t.warning
                    checked: recentTgSheet.filterTab === "fav"
                    onClicked: { recentTgSheet.filterTab = "fav"; recentTgSheet.rebuildList() }
                }
                ChipToggle {
                    text: qsTr("Recent (%1)").arg(recentTgSheet.appState && recentTgSheet.appState.recentTgids ? recentTgSheet.appState.recentTgids.length : 0)
                    glyph: "\uf1da"
                    checked: recentTgSheet.filterTab === "recent"
                    onClicked: { recentTgSheet.filterTab = "recent"; recentTgSheet.rebuildList() }
                }

                Item { Layout.fillWidth: true }

                // Clear Recent History
                ToolButton {
                    visible: !!(recentTgSheet.appState && recentTgSheet.appState.recentTgids && recentTgSheet.appState.recentTgids.length > 0)
                    text: "\uf1f8"
                    font.family: "FontAwesome"
                    font.pixelSize: 13
                    palette.buttonText: t.danger
                    Accessible.name: qsTr("Clear recent talkgroups")
                    onClicked: {
                        if (recentTgSheet.app) recentTgSheet.app.clearRecentTalkgroups()
                        recentTgSheet.rebuildList()
                    }
                }
            }

            // Quick DMR Actions Row (Unlink 4000 & Parrot 9990)
            RowLayout {
                Layout.fillWidth: true
                visible: recentTgSheet.mode === "DMR" && recentSearchInput.text.trim() === ""
                spacing: 8

                AppButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    glyph: "\uf127"
                    text: recentTgSheet.width >= 360 ? qsTr("Unlink (TG 4000)") : qsTr("Unlink 4000")
                    onClicked: recentTgSheet.selectTg("4000")
                }

                AppButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    glyph: "\uf130"
                    text: recentTgSheet.width >= 360 ? qsTr("Parrot (9990)") : qsTr("Parrot")
                    onClicked: recentTgSheet.selectTg("9990", true)
                }
            }

            // List of Talkgroups with Star/Pin Button on every row
            ListView {
                id: recentTgList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: recentTgSheet.filteredModel
                clip: true
                spacing: 6
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: recentTgList.contentHeight > recentTgList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                delegate: Rectangle {
                    id: tgRowItem
                    required property var modelData
                    readonly property string rowTgid: String(modelData && modelData.tgid !== undefined ? modelData.tgid : "").replace(/[^0-9]/g, "")
                    readonly property bool isCurrent: !!(recentTgSheet.appState && String(recentTgSheet.appState.dmrtgid) === rowTgid)
                    readonly property bool isFav: !!(modelData && modelData.isFavourite)
                    readonly property bool isLive: !!(modelData && modelData.isLive)

                    width: ListView.view ? ListView.view.width : parent.width
                    height: 52
                    radius: 10
                    color: isCurrent ? t.tint(t.accent, 0.16)
                         : (isLive ? t.tint(t.success, 0.10)
                         : (rowSelectMouse.pressed ? t.surface3 : t.surface2))
                    border.color: isCurrent ? t.accent : (isLive ? t.success : t.stroke)
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            RowLayout {
                                anchors.fill: parent
                                spacing: 8

                                Rectangle {
                                    radius: 6
                                    implicitHeight: 24
                                    implicitWidth: rowTgNum.implicitWidth + 12
                                    Layout.preferredWidth: implicitWidth
                                    color: tgRowItem.isLive ? t.tint(t.success, 0.24) : t.surface3
                                    border.color: tgRowItem.isLive ? t.success : t.stroke
                                    border.width: 1

                                    Label {
                                        id: rowTgNum
                                        anchors.centerIn: parent
                                        text: "TG " + tgRowItem.rowTgid
                                        font.pixelSize: t.fsSmall
                                        font.bold: true
                                        color: t.text
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 1

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: (tgRowItem.modelData && tgRowItem.modelData.name) ? tgRowItem.modelData.name
                                              : qsTr("Talkgroup %1").arg(tgRowItem.rowTgid)
                                        font.pixelSize: t.fsBody
                                        font.bold: tgRowItem.isCurrent || tgRowItem.isFav
                                        color: tgRowItem.isCurrent ? t.accent : t.text
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        visible: tgRowItem.isLive
                                        text: qsTr("ON AIR") + ((tgRowItem.modelData && tgRowItem.modelData.sourceCall) ? (" • " + tgRowItem.modelData.sourceCall) : "")
                                        font.pixelSize: t.fsCaption - 1
                                        font.bold: true
                                        color: t.success
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                id: rowSelectMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: recentTgSheet.selectTg(tgRowItem.rowTgid, false)
                            }
                        }

                        Rectangle {
                            implicitWidth: 36
                            implicitHeight: 36
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            radius: 8
                            color: tgRowItem.isFav ? t.tint(t.warning, 0.20) : t.surface3
                            border.color: tgRowItem.isFav ? t.tint(t.warning, 0.70) : t.stroke
                            border.width: 1

                            Label {
                                anchors.centerIn: parent
                                text: tgRowItem.isFav ? "\uf005" : "\uf006"
                                font.family: "FontAwesome"
                                font.pixelSize: 14
                                color: tgRowItem.isFav ? t.warning : t.textMuted
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (recentTgSheet.app && tgRowItem.rowTgid !== "") {
                                        recentTgSheet.app.toggleFavouriteTalkgroup(tgRowItem.rowTgid)
                                        recentTgSheet.rebuildList()
                                    }
                                }
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: recentTgSheet.filteredModel.length === 0
                    text: recentSearchInput.text.trim() !== ""
                          ? qsTr("No matching talkgroups found")
                          : (recentTgSheet.filterTab === "fav"
                             ? qsTr("No favourite talkgroups pinned yet.\nTap the star next to any TG to pin it.")
                             : qsTr("No recent talkgroups yet.\nType a TG number or search by country/name above."))
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: t.fsSmall
                    color: t.textMuted
                }
            }
        }
    }
}
