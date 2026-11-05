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
import QtQuick.Controls
import QtQuick.Layouts

import "../components"
import "../theme"

Page {
    id: page
    title: qsTr("Scan")
    padding: 0

    property var app: null
    required property var droidstarRef
    required property var appState

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    // Active tab: "hoseline" (All Live TGs) or "favourites" (Favourite TGs Scanner & Quick Switch)
    property string activeTab: "hoseline"
    property string searchQuery: ""
    // Filter mode on Hoseline tab: "all", "live", "fav"
    property string filterMode: "all"

    readonly property bool compact: width < 380
    readonly property bool roomy: width >= 560
    readonly property bool stackControls: contentWrap.width < 520
    readonly property int gridCols: contentWrap.width >= 820 ? 3 : (contentWrap.width >= 520 ? 2 : 1)

    property Component headerActions: Component {
        Row {
            spacing: 2
            HeaderAction {
                readonly property bool following: !!(page.appState && (page.activeTab === "favourites"
                                                      ? page.appState.favScanActive
                                                      : page.appState.allScanActive))
                glyph: following ? "\uf04c" : "\uf04b"
                label: following ? qsTr("Stop") : qsTr("Follow")
                showLabel: page.roomy
                tone: following ? t.success : t.accent
                onClicked: {
                    if (!page.app || !page.appState) return
                    if (page.activeTab === "favourites")
                        page.app.setFavScanActive(!page.appState.favScanActive)
                    else
                        page.app.setAllScanActive(!page.appState.allScanActive)
                }
            }
            HeaderAction {
                glyph: "\uf021"
                label: qsTr("Refresh")
                showLabel: false
                onClicked: {
                    if (page.droidstarRef) page.droidstarRef.refreshHoseline()
                    if (page.app) page.app.notify(qsTr("Refreshing live activity…"), "", null, "info")
                }
            }
        }
    }

    function tuneLabel(isTuned) {
        if (!isTuned) return qsTr("Tune")
        if (page.appState && page.appState.connecting) return qsTr("Connecting")
        if (!(page.appState && page.appState.connected)) return qsTr("Connect")
        return qsTr("Tuned")
    }

    function formatDuration(sec) {
        var s = Math.max(0, sec | 0)
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" + m : "" + m) + ":" + (r < 10 ? "0" + r : "" + r)
    }

    function formatAgo(sec) {
        var s = Math.max(0, sec | 0)
        if (s < 60) return s + "s " + qsTr("ago")
        var m = Math.floor(s / 60)
        var r = s % 60
        return m + "m " + (r > 0 ? (r + "s ") : "") + qsTr("ago")
    }

    readonly property var filteredHoselineCalls: {
        var src = (page.appState && page.appState.hoselineCalls) ? page.appState.hoselineCalls : []
        var q = (page.searchQuery || "").trim().toLowerCase()
        var fMode = page.filterMode
        var out = []
        for (var i = 0; i < src.length; i++) {
            var item = src[i]
            if (!item) continue
            if (fMode === "live" && !item.isLive) continue
            if (fMode === "fav" && !item.isFavourite) continue
            if (q !== "") {
                var tgStr = String(item.tgid || "").toLowerCase()
                var tgName = String(item.tgName || "").toLowerCase()
                var sCall = String(item.sourceCall || "").toLowerCase()
                var sName = String(item.sourceName || "").toLowerCase()
                var alias = String(item.talkerAlias || "").toLowerCase()
                if (tgStr.indexOf(q) < 0 && tgName.indexOf(q) < 0 &&
                    sCall.indexOf(q) < 0 && sName.indexOf(q) < 0 && alias.indexOf(q) < 0) {
                    continue
                }
            }
            out.push(item)
        }
        return out
    }

    readonly property var filteredFavCalls: {
        var src = (page.appState && page.appState.hoselineFavCalls) ? page.appState.hoselineFavCalls : []
        var q = (page.searchQuery || "").trim().toLowerCase()
        if (q === "") return src
        var out = []
        for (var i = 0; i < src.length; i++) {
            var item = src[i]
            if (!item) continue
            var tgStr = String(item.tgid || "").toLowerCase()
            var tgName = String(item.tgName || "").toLowerCase()
            var sCall = String(item.sourceCall || "").toLowerCase()
            var sName = String(item.sourceName || "").toLowerCase()
            if (tgStr.indexOf(q) >= 0 || tgName.indexOf(q) >= 0 ||
                sCall.indexOf(q) >= 0 || sName.indexOf(q) >= 0) {
                out.push(item)
            }
        }
        return out
    }

    ListModel { id: liveCardsModel }
    ListModel { id: favCardsModel }

    function _syncCardsModel(targetModel, sourceArray) {
        var src = sourceArray || []
        for (var i = 0; i < src.length; i++) {
            var item = src[i]
            if (!item) continue
            var row = {
                tgid: String(item.tgid || ""),
                tgName: String(item.tgName || ""),
                sourceCall: String(item.sourceCall || ""),
                sourceName: String(item.sourceName || ""),
                talkerAlias: String(item.talkerAlias || ""),
                master: String(item.master || ""),
                isLive: !!item.isLive,
                isFavourite: !!item.isFavourite,
                hasRecent: !!item.hasRecent,
                durationSec: item.durationSec | 0,
                agoSec: item.agoSec | 0
            }
            if (i < targetModel.count)
                targetModel.set(i, row)
            else
                targetModel.append(row)
        }
        while (targetModel.count > src.length)
            targetModel.remove(targetModel.count - 1)
    }

    // Hoseline pushes a fresh table every ~800 ms while live; only rewrite the card models when
    // this page is actually on screen, and catch up once when it becomes visible again.
    onFilteredHoselineCallsChanged: if (page.visible) page._syncCardsModel(liveCardsModel, page.filteredHoselineCalls)
    onFilteredFavCallsChanged: if (page.visible) page._syncCardsModel(favCardsModel, page.filteredFavCalls)
    onVisibleChanged: {
        if (!page.visible) return
        page._syncCardsModel(liveCardsModel, page.filteredHoselineCalls)
        page._syncCardsModel(favCardsModel, page.filteredFavCalls)
    }
    Component.onCompleted: {
        page._syncCardsModel(liveCardsModel, page.filteredHoselineCalls)
        page._syncCardsModel(favCardsModel, page.filteredFavCalls)
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentWrap.implicitHeight + 32
        boundsBehavior: Flickable.DragOverBounds
        clip: true
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: contentWrap
            x: Math.max(10, (scroller.width - width) / 2)
            y: 12
            width: Math.min(scroller.width - 20, 1080)
            spacing: 12

            // ─────────────────────────────────────────────────────────
            // TOP TAB SWITCHER + LIVE STREAM STATUS BAR
            // ─────────────────────────────────────────────────────────
            AppCard {
                Layout.fillWidth: true
                radius: t.rMd

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: t.controlHeight
                            tone: (page.appState && page.appState.connected) ? "danger"
                                  : ((page.appState && page.appState.connecting) ? "warning" : "primary")
                            busy: !!(page.appState && page.appState.connecting)
                            glyph: (page.appState && page.appState.connected) ? "\uf011" : "\uf1e6"
                            text: (page.appState && page.appState.connecting) ? qsTr("Cancel")
                                  : ((page.appState && page.appState.connected) ? qsTr("Disconnect") : qsTr("Connect"))
                            onClicked: if (page.app) page.app.connectOrDisconnect()
                        }

                        TxButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: t.controlHeight
                            compact: true
                            app: page.app
                            appState: page.appState
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        radius: 4
                        color: t.surface
                        border.color: t.stroke
                        border.width: 1
                        Accessible.role: Accessible.ProgressBar
                        Accessible.name: qsTr("Audio level")

                        Rectangle {
                            readonly property real level: Math.min(1, (page.appState ? page.appState.outputLevel : 0) / 32767.0)
                            height: parent.height - 2
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 1
                            radius: height / 2
                            width: Math.max(height, (parent.width - 2) * level)
                            color: (page.appState && page.appState.txActive) ? t.danger : (level > 0.85 ? t.warning : t.success)
                        }
                    }

                    SegmentedControl {
                        Layout.fillWidth: true
                        options: [
                            {
                                label: qsTr("Live") + ((page.appState && page.appState.hoselineLiveCount > 0)
                                       ? (" (" + page.appState.hoselineLiveCount + ")") : ""),
                                value: "hoseline"
                            },
                            {
                                label: (page.compact ? qsTr("Favourites") : qsTr("Favourite Scan"))
                                       + ((page.appState && page.appState.hoselineActiveFavCount > 0)
                                       ? (" (" + page.appState.hoselineActiveFavCount + " " + qsTr("Live") + ")")
                                       : ((page.appState && page.appState.favouriteTgids && page.appState.favouriteTgids.length > 0)
                                          ? (" (" + page.appState.favouriteTgids.length + ")") : "")),
                                value: "favourites"
                            }
                        ]
                        currentValue: page.activeTab
                        onSelected: function(val) { page.activeTab = val }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: scannerCtrlCol.implicitHeight + 16
                        radius: 12
                        color: {
                            var scanning = page.appState && (page.appState.favScanActive || page.appState.allScanActive)
                            return scanning ? t.tint(t.success, 0.12) : t.surface2
                        }
                        border.color: {
                            var scanning = page.appState && (page.appState.favScanActive || page.appState.allScanActive)
                            return scanning ? t.tint(t.success, 0.65) : t.stroke
                        }
                        border.width: 1

                        ColumnLayout {
                            id: scannerCtrlCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 8
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 9
                                    Layout.preferredHeight: 9
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: 4.5
                                    color: (page.appState && page.appState.hoselineConnected) ? t.success : t.warning

                                    SequentialAnimation on opacity {
                                        // Hoseline stays connected while on other pages; only animate when this page is actually shown.
                                        running: !!(page.appState && (page.appState.hoselineConnected || page.appState.favScanActive || page.appState.allScanActive))
                                                 && page.visible && Qt.application.state === Qt.ApplicationActive
                                        loops: Animation.Infinite
                                        alwaysRunToEnd: true
                                        NumberAnimation { to: 0.3; duration: 550; easing.type: Easing.InOutSine }
                                        NumberAnimation { to: 1.0; duration: 550; easing.type: Easing.InOutSine }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 1

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: {
                                            if (!page.appState) return qsTr("Scan")
                                            var curTg = page.appState.dmrtgid || ""
                                            var curName = (curTg !== "" && page.app) ? page.app.getTalkgroupName(curTg) : ""
                                            if (page.appState.favScanActive) {
                                                return qsTr("Following favourites") + (curTg !== "" ? (" • TG " + curTg + (curName !== "" ? (" • " + curName) : "")) : "")
                                            }
                                            if (page.appState.allScanActive) {
                                                return qsTr("Following live") + (curTg !== "" ? (" • TG " + curTg + (curName !== "" ? (" • " + curName) : "")) : "")
                                            }
                                            if (curTg !== "") {
                                                return qsTr("Tuned: TG %1").arg(curTg) + (curName !== "" ? (" • " + curName) : "")
                                            }
                                            return qsTr("Scan")
                                        }
                                        font.pixelSize: t.fsBody
                                        font.bold: true
                                        color: t.text
                                        elide: Text.ElideRight
                                        wrapMode: Text.NoWrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: {
                                            if (!page.appState) return ""
                                            if (page.appState.txActive) {
                                                return qsTr("Transmitting on TG %1").arg(page.appState.dmrtgid || "")
                                            }
                                            if (page.appState.connected && page.appState.data1 !== "") {
                                                return qsTr("Receiving: %1").arg(page.appState.data1) +
                                                       (page.appState.fetchedFirstName !== "" ? (" (" + page.appState.fetchedFirstName + ")") : "")
                                            }
                                            if (!page.appState.hoselineConnected) {
                                                return qsTr("Connecting to live activity…")
                                            }
                                            return qsTr("%1 on air • %2 active")
                                                   .arg(page.appState.hoselineLiveCount || 0)
                                                   .arg(page.appState.hoselineCalls ? page.appState.hoselineCalls.length : 0)
                                        }
                                        font.pixelSize: t.fsCaption
                                        color: (page.appState && page.appState.txActive) ? t.danger
                                             : ((page.appState && page.appState.connected && page.appState.data1 !== "") ? t.success : t.textMuted)
                                        elide: Text.ElideRight
                                        wrapMode: Text.NoWrap
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    id: scanToggleBtn
                                    readonly property bool isFavTab: page.activeTab === "favourites"
                                    readonly property bool scanOn: !!(page.appState && (isFavTab ? page.appState.favScanActive : page.appState.allScanActive))
                                    implicitHeight: 36
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 36
                                    Layout.minimumWidth: 0
                                    radius: 10
                                    clip: true
                                    color: scanOn ? t.tint(t.success, 0.24) : t.tint(t.accent, 0.18)
                                    border.color: scanOn ? t.success : t.accent
                                    border.width: 1

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Label {
                                            text: scanToggleBtn.scanOn ? "\uf04c" : "\uf04b"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: scanToggleBtn.scanOn ? t.success : t.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            text: scanToggleBtn.scanOn
                                                  ? qsTr("Stop")
                                                  : (scanToggleBtn.isFavTab ? qsTr("Follow Favs") : qsTr("Follow Live"))
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (!page.app || !page.appState) return
                                            if (scanToggleBtn.isFavTab) {
                                                page.app.setFavScanActive(!page.appState.favScanActive)
                                            } else {
                                                page.app.setAllScanActive(!page.appState.allScanActive)
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: !!(page.appState && (page.appState.favScanActive || page.appState.allScanActive ||
                                                (page.activeTab === "favourites" && page.appState.hoselineActiveFavCount > 0) ||
                                                (page.activeTab === "hoseline" && page.appState.hoselineLiveCount > 0)))
                                    implicitHeight: 36
                                    Layout.preferredHeight: 36
                                    Layout.preferredWidth: page.stackControls ? 72 : 96
                                    Layout.minimumWidth: 72
                                    radius: 10
                                    color: nextMouse.pressed ? t.surface3 : t.surface
                                    border.color: t.stroke
                                    border.width: 1

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 5
                                        Label {
                                            text: "\uf051"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: t.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: !page.stackControls
                                            text: qsTr("Next")
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        id: nextMouse
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (page.app) {
                                                page.app.scanHopNext(page.activeTab === "favourites" || !!(page.appState && page.appState.favScanActive))
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: !!(page.appState && page.appState.connected &&
                                                page.appState.dmrtgid !== "" && page.appState.dmrtgid !== "4000")
                                    implicitHeight: 36
                                    Layout.preferredHeight: 36
                                    Layout.preferredWidth: page.stackControls ? 72 : 96
                                    Layout.minimumWidth: 72
                                    radius: 10
                                    color: unlinkMouse.pressed ? t.surface3 : t.surface
                                    border.color: t.stroke
                                    border.width: 1

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 5
                                        Label {
                                            text: "\uf127"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: t.warning
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: !page.stackControls
                                            text: qsTr("Unlink")
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        id: unlinkMouse
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (page.app) {
                                                page.app.setFavScanActive(false)
                                                page.app.setAllScanActive(false)
                                                page.app.quickSwitchToTalkgroup("4000", false)
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                                text: {
                                    var fav = page.activeTab === "favourites"
                                    var on = !!(page.appState && (fav ? page.appState.favScanActive : page.appState.allScanActive))
                                    if (on && fav)
                                        return qsTr("Following pinned talkgroups. Stays while one is on air, then moves to the next live favourite. Disconnect stops following.")
                                    if (on)
                                        return qsTr("Following live activity. Stays while a talkgroup is on air, then moves to the next. Disconnect stops following.")
                                    if (fav)
                                        return qsTr("Follow Favs moves between pinned talkgroups that are on air. Tune joins that talkgroup and connects if you are offline.")
                                    return qsTr("Follow Live moves to the next talkgroup that is on air. Tune joins that talkgroup and connects if you are offline.")
                                }
                            }
                        }
                    }

                    // Search & Filter Row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppTextField {
                            id: scanSearchField
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            placeholderText: page.activeTab === "favourites"
                                             ? qsTr("Search favourite TGs or type TG ID to add…")
                                             : qsTr("Search TG number, name, country or callsign…")
                            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                            rightPadding: clearSearchBtn.visible ? 34 : 12
                            onTextChanged: page.searchQuery = text

                            ToolButton {
                                id: clearSearchBtn
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: scanSearchField.text.length > 0
                                text: "\uf057"
                                font.family: "FontAwesome"
                                font.pixelSize: 14
                                opacity: 0.75
                                Accessible.name: qsTr("Clear search")
                                onClicked: {
                                    scanSearchField.clear()
                                    page.searchQuery = ""
                                }
                            }
                        }

                        // Quick "+ Fav" button when a numeric TG ID is typed in search
                        AppButton {
                            visible: /^[0-9]{1,7}$/.test(page.searchQuery.trim()) &&
                                     !(page.app && page.app.isFavouriteTalkgroup(page.searchQuery.trim()))
                            Layout.preferredHeight: t.controlHeight
                            glyph: "\uf005"
                            text: page.compact ? qsTr("Pin") : qsTr("Pin TG %1").arg(page.searchQuery.trim())
                            tone: "primary"
                            onClicked: {
                                var tg = page.searchQuery.trim()
                                if (page.app && tg !== "") {
                                    page.app.addFavouriteTalkgroup(tg)
                                    scanSearchField.clear()
                                    page.searchQuery = ""
                                }
                            }
                        }
                    }

                    // Filter chips on the Live tab
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.activeTab === "hoseline"
                        spacing: 8

                        ChipToggle {
                            text: qsTr("All (%1)").arg(page.appState && page.appState.hoselineCalls ? page.appState.hoselineCalls.length : 0)
                            checked: page.filterMode === "all"
                            onClicked: page.filterMode = "all"
                        }
                        ChipToggle {
                            text: qsTr("On Air (%1)").arg(page.appState ? (page.appState.hoselineLiveCount || 0) : 0)
                            glyph: "\uf111"
                            activeColor: t.success
                            checked: page.filterMode === "live"
                            onClicked: page.filterMode = (page.filterMode === "live" ? "all" : "live")
                        }
                        ChipToggle {
                            text: qsTr("Starred (%1)").arg(page.appState ? (page.appState.hoselineActiveFavCount || 0) : 0)
                            glyph: "\uf005"
                            activeColor: t.warning
                            checked: page.filterMode === "fav"
                            onClicked: page.filterMode = (page.filterMode === "fav" ? "all" : "fav")
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // TAB 1: LIVE BRANDMEISTER HOSELINE REALTIME ACTIVE TGs
            // ─────────────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.activeTab === "hoseline"
                spacing: 10

                // Empty / Loading State
                AppCard {
                    Layout.fillWidth: true
                    visible: page.filteredHoselineCalls.length === 0
                    radius: t.rMd

                    ColumnLayout {
                        // Pane children are not laid out by the parent Layout: bind the width explicitly
                        width: parent.width
                        spacing: 8

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: "\uf002"
                            font.family: "FontAwesome"
                            font.pixelSize: 26
                            color: t.textMuted
                            opacity: 0.65
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: !(page.appState && page.appState.hoselineConnected)
                                  ? qsTr("Connecting to live activity…")
                                  : (page.searchQuery !== ""
                                     ? qsTr("No active talkgroups match \"%1\"").arg(page.searchQuery)
                                     : qsTr("Waiting for active Talkgroup transmissions…"))
                            font.pixelSize: t.fsBody
                            color: t.text
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Tap any active Talkgroup card to quick-switch your radio, or tap the star to pin it to your Favourites.")
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Responsive Multi-Column Grid of Live Hoseline Cards
                GridLayout {
                    Layout.fillWidth: true
                    visible: liveCardsModel.count > 0
                    columns: page.gridCols
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        model: liveCardsModel

                        delegate: Rectangle {
                            id: tgCard
                            required property string tgid
                            required property string tgName
                            required property string sourceCall
                            required property string sourceName
                            required property string talkerAlias
                            required property string master
                            required property bool isLive
                            required property bool isFavourite
                            required property bool hasRecent
                            required property int durationSec
                            required property int agoSec

                            readonly property bool isFav: isFavourite
                            readonly property bool isTuned: !!(page.appState && String(page.appState.dmrtgid) === String(tgid))

                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            Layout.minimumWidth: 0
                            implicitHeight: cardInnerCol.implicitHeight + 20
                            radius: 14
                            clip: true
                            scale: (cardTapMouse.pressed || tuneMouse.pressed) ? 0.99 : 1.0
                            color: isTuned ? t.tint(t.accent, 0.14)
                                 : (isLive ? t.tint(t.success, 0.08) : (cardTapMouse.pressed ? t.surface2 : t.surface))
                            border.color: isTuned ? t.accent
                                        : (isLive ? t.tint(t.success, 0.75) : t.stroke)
                            border.width: (isTuned || isLive) ? 1.5 : 1

                            MouseArea {
                                id: cardTapMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (page.app) {
                                        page.app.quickSwitchToTalkgroup(String(tgCard.tgid), false)
                                    }
                                }
                            }

                            ColumnLayout {
                                id: cardInnerCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 6

                                // Top Row: TG Badge + TG Name + Live/Ago Pill + Star Button
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    // TG Number Badge
                                    Rectangle {
                                        radius: 7
                                        implicitHeight: 24
                                        implicitWidth: tgNumLabel.implicitWidth + 12
                                        Layout.preferredWidth: implicitWidth
                                        Layout.minimumWidth: implicitWidth
                                        color: tgCard.isLive ? t.tint(t.success, 0.22) : t.surface3
                                        border.color: tgCard.isLive ? t.success : t.stroke
                                        border.width: 1

                                        Row {
                                            id: tgNumLabel
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Rectangle {
                                                visible: tgCard.isLive
                                                width: 6
                                                height: 6
                                                radius: 3
                                                color: t.success
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Label {
                                                text: "TG " + tgCard.tgid
                                                font.pixelSize: t.fsSmall
                                                font.bold: true
                                                color: t.text
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }
                                    }

                                    // Talkgroup Name (wraps to a second line before eliding)
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.preferredWidth: 1
                                        text: tgCard.tgName || qsTr("Talkgroup %1").arg(tgCard.tgid)
                                        font.pixelSize: t.fsBody
                                        font.bold: true
                                        color: t.text
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }

                                    // Live Duration or Time Ago
                                    Label {
                                        text: tgCard.isLive
                                              ? page.formatDuration(tgCard.durationSec)
                                              : page.formatAgo(tgCard.agoSec)
                                        font.pixelSize: t.fsCaption
                                        font.bold: tgCard.isLive
                                        font.family: (page.appState && page.appState.monoFont !== "") ? page.appState.monoFont : Qt.application.font.family
                                        color: tgCard.isLive ? t.success : t.textMuted
                                    }

                                    // Star / Pin Favourite Button
                                    Rectangle {
                                        id: starBtn
                                        implicitWidth: 28
                                        implicitHeight: 28
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 28
                                        radius: 8
                                        color: tgCard.isFav ? t.tint(t.warning, 0.20) : t.surface2
                                        border.color: tgCard.isFav ? t.tint(t.warning, 0.65) : t.stroke
                                        border.width: 1

                                        Label {
                                            anchors.centerIn: parent
                                            text: tgCard.isFav ? "\uf005" : "\uf006"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 13
                                            color: tgCard.isFav ? t.warning : t.textMuted
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (page.app) {
                                                    page.app.toggleFavouriteTalkgroup(String(tgCard.tgid))
                                                }
                                            }
                                        }
                                    }
                                }

                                // Bottom Row: Active Callsign + Name + TalkerAlias + Quick Tune Pill
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 1

                                        Label {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: {
                                                var cs = tgCard.sourceCall || ""
                                                var nm = tgCard.sourceName || ""
                                                if (cs === "" && nm === "") return qsTr("Active Group Call")
                                                return cs + (nm !== "" ? (" • " + nm) : "")
                                            }
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: tgCard.isLive ? t.text : t.textMuted
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            visible: text !== ""
                                            text: {
                                                var parts = []
                                                if (tgCard.talkerAlias && tgCard.talkerAlias !== tgCard.sourceCall) {
                                                    parts.push(tgCard.talkerAlias)
                                                }
                                                if (tgCard.master) {
                                                    parts.push(qsTr("Master %1").arg(tgCard.master))
                                                }
                                                return parts.join(" • ")
                                            }
                                            font.pixelSize: t.fsCaption - 1
                                            color: t.textMuted
                                            elide: Text.ElideRight
                                        }
                                    }

                                    // 1-Tap Quick Switch / Tune Pill
                                    Rectangle {
                                        id: tunePill
                                        implicitHeight: 28
                                        implicitWidth: tuneRow.implicitWidth + 16
                                        Layout.preferredHeight: 28
                                        Layout.preferredWidth: implicitWidth
                                        radius: 8
                                        color: tgCard.isTuned ? t.tint(t.accent, 0.28)
                                             : (tuneMouse.pressed ? t.surface3 : t.tint(t.accent, 0.16))
                                        border.color: t.accent
                                        border.width: 1

                                        Row {
                                            id: tuneRow
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Label {
                                                text: tgCard.isTuned ? "\uf00c" : "\uf028"
                                                font.family: "FontAwesome"
                                                font.pixelSize: 11
                                                color: t.accent
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Label {
                                                text: page.tuneLabel(tgCard.isTuned)
                                                font.pixelSize: t.fsCaption
                                                font.bold: true
                                                color: t.text
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        MouseArea {
                                            id: tuneMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (page.app) {
                                                    page.app.quickSwitchToTalkgroup(String(tgCard.tgid), false)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // TAB 2: FAVOURITE TG SCANNER & QUICK SWITCH
            // ─────────────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.activeTab === "favourites"
                spacing: 12

                // Quick-Add Popular / Custom Favourite Talkgroups Bar
                AppCard {
                    Layout.fillWidth: true
                    radius: t.rMd

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: "\uf005"
                                font.family: "FontAwesome"
                                font.pixelSize: 14
                                color: t.warning
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: qsTr("Pinned Favourite Talkgroups")
                                font.pixelSize: t.fsBody
                                font.bold: true
                                color: t.text
                                elide: Text.ElideRight
                            }
                            Label {
                                text: qsTr("%1 pinned").arg(page.appState && page.appState.favouriteTgids ? page.appState.favouriteTgids.length : 0)
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppTextField {
                                id: addFavField
                                Layout.fillWidth: true
                                Layout.minimumWidth: 80
                                placeholderText: qsTr("Enter TG ID to pin (e.g. 91, 93, 3100, 404)…")
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
                                onAccepted: addFavBtn.clicked()
                            }

                            AppButton {
                                id: addFavBtn
                                Layout.preferredHeight: t.controlHeight
                                glyph: "\uf067"
                                text: qsTr("Pin TG")
                                tone: "primary"
                                enabled: addFavField.text.trim().length > 0 && parseInt(addFavField.text.trim()) > 0
                                onClicked: {
                                    var tg = addFavField.text.trim()
                                    if (page.app && tg !== "") {
                                        page.app.addFavouriteTalkgroup(tg)
                                        addFavField.clear()
                                    }
                                }
                            }
                        }
                    }
                }

                // Empty State when user has no Favourite TGs yet
                AppCard {
                    Layout.fillWidth: true
                    visible: page.filteredFavCalls.length === 0
                    radius: t.rMd

                    ColumnLayout {
                        width: parent.width
                        spacing: 10

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: "\uf006"
                            font.family: "FontAwesome"
                            font.pixelSize: 26
                            color: t.warning
                            opacity: 0.8
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: page.searchQuery !== ""
                                  ? qsTr("No favourite talkgroups match \"%1\"").arg(page.searchQuery)
                                  : qsTr("No Favourite Talkgroups pinned yet")
                            font.pixelSize: t.fsBody
                            font.bold: true
                            color: t.text
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Pin talkgroups from Recent TGs on the Main page, star any live card on the Live tab, or tap a starter Talkgroup below:")
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                            wrapMode: Text.WordWrap
                        }

                        // Quick starter pills (Worldwide 91, North America 93, Europe 92, USA 3100, India 404, UK 235)
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: page.searchQuery === ""

                            Repeater {
                                model: [
                                    { tg: "91",   label: "TG 91 • World-wide" },
                                    { tg: "93",   label: "TG 93 • North America" },
                                    { tg: "92",   label: "TG 92 • Europe" },
                                    { tg: "3100", label: "TG 3100 • USA" },
                                    { tg: "404",  label: "TG 404 • India" },
                                    { tg: "235",  label: "TG 235 • UK" }
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    radius: 9
                                    implicitHeight: 30
                                    implicitWidth: starterChipRow.implicitWidth + 16
                                    color: t.surface3
                                    border.color: t.stroke
                                    border.width: 1

                                    Row {
                                        id: starterChipRow
                                        anchors.centerIn: parent
                                        spacing: 5
                                        Label {
                                            text: "\uf067"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 10
                                            color: t.warning
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            text: modelData.label
                                            font.pixelSize: t.fsCaption
                                            font.bold: true
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app) page.app.addFavouriteTalkgroup(modelData.tg)
                                    }
                                }
                            }
                        }
                    }
                }

                // Responsive Grid of Favourite Talkgroups (Live ON AIR sorted first!)
                GridLayout {
                    Layout.fillWidth: true
                    visible: favCardsModel.count > 0
                    columns: page.gridCols
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        model: favCardsModel

                        delegate: Rectangle {
                            id: favCard
                            required property string tgid
                            required property string tgName
                            required property string sourceCall
                            required property string sourceName
                            required property string talkerAlias
                            required property string master
                            required property bool isLive
                            required property bool isFavourite
                            required property bool hasRecent
                            required property int durationSec
                            required property int agoSec

                            readonly property bool isTuned: !!(page.appState && String(page.appState.dmrtgid) === String(tgid))

                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            Layout.minimumWidth: 0
                            implicitHeight: favInnerCol.implicitHeight + 22
                            radius: 14
                            clip: true
                            scale: (favCardTapMouse.pressed || favTuneMouse.pressed) ? 0.99 : 1.0
                            color: isLive ? t.tint(t.success, 0.12)
                                 : (isTuned ? t.tint(t.accent, 0.14) : (favCardTapMouse.pressed ? t.surface2 : t.surface))
                            border.color: isLive ? t.success
                                        : (isTuned ? t.accent : t.stroke)
                            border.width: (isLive || isTuned) ? 1.5 : 1

                            MouseArea {
                                id: favCardTapMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (page.app) {
                                        page.app.quickSwitchToTalkgroup(String(favCard.tgid), false)
                                    }
                                }
                            }

                            ColumnLayout {
                                id: favInnerCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 7

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    // TG Badge with Live Indicator
                                    Rectangle {
                                        radius: 7
                                        implicitHeight: 24
                                        implicitWidth: favTgRow.implicitWidth + 12
                                        Layout.preferredWidth: implicitWidth
                                        Layout.minimumWidth: implicitWidth
                                        color: favCard.isLive ? t.tint(t.success, 0.25) : t.tint(t.warning, 0.16)
                                        border.color: favCard.isLive ? t.success : t.tint(t.warning, 0.55)
                                        border.width: 1

                                        Row {
                                            id: favTgRow
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Label {
                                                text: favCard.isLive ? "\uf111" : "\uf005"
                                                font.family: "FontAwesome"
                                                font.pixelSize: 10
                                                color: favCard.isLive ? t.success : t.warning
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Label {
                                                text: "TG " + favCard.tgid
                                                font.pixelSize: t.fsSmall
                                                font.bold: true
                                                color: t.text
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }
                                    }

                                    // TG Name (wraps to a second line before eliding)
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.preferredWidth: 1
                                        text: favCard.tgName || qsTr("Talkgroup %1").arg(favCard.tgid)
                                        font.pixelSize: t.fsBody
                                        font.bold: true
                                        color: t.text
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }

                                    // Status Badge (LIVE timer / Recent ago / Standby)
                                    Rectangle {
                                        radius: 6
                                        implicitHeight: 20
                                        implicitWidth: favStatusTxt.implicitWidth + 10
                                        color: favCard.isLive ? t.tint(t.success, 0.20) : t.surface2
                                        border.color: favCard.isLive ? t.success : t.stroke
                                        border.width: 1

                                        Label {
                                            id: favStatusTxt
                                            anchors.centerIn: parent
                                            text: favCard.isLive
                                                  ? (qsTr("ON AIR") + " · " + page.formatDuration(favCard.durationSec))
                                                  : (favCard.hasRecent
                                                     ? page.formatAgo(favCard.agoSec)
                                                     : qsTr("Standby"))
                                            font.pixelSize: t.fsCaption - 1
                                            font.bold: favCard.isLive
                                            color: favCard.isLive ? t.success : t.textMuted
                                        }
                                    }

                                    // Unpin / Unstar button (28pt minimum tap target)
                                    ToolButton {
                                        implicitWidth: 30
                                        implicitHeight: 30
                                        padding: 0
                                        text: "\uf005"
                                        font.family: "FontAwesome"
                                        font.pixelSize: 13
                                        palette.buttonText: t.warning
                                        Accessible.name: qsTr("Remove from favourites")
                                        onClicked: {
                                            if (page.app) {
                                                page.app.removeFavouriteTalkgroup(String(favCard.tgid))
                                            }
                                        }
                                    }
                                }

                                // Bottom Row: Active or Last Heard Callsign + 1-Tap Quick Switch Button
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: {
                                            if (favCard.isLive || favCard.hasRecent) {
                                                var cs = favCard.sourceCall || ""
                                                var nm = favCard.sourceName || ""
                                                if (cs !== "") {
                                                    return (favCard.isLive ? qsTr("Talking: ") : qsTr("Last: ")) +
                                                           cs + (nm !== "" ? (" (" + nm + ")") : "")
                                                }
                                            }
                                            return qsTr("Monitoring on Scan")
                                        }
                                        font.pixelSize: t.fsSmall
                                        font.bold: favCard.isLive
                                        color: favCard.isLive ? t.text : t.textMuted
                                        elide: Text.ElideRight
                                    }

                                    // 1-Tap Quick Switch Button
                                    Rectangle {
                                        implicitHeight: 30
                                        implicitWidth: favTuneRow.implicitWidth + 18
                                        Layout.preferredHeight: 30
                                        Layout.preferredWidth: implicitWidth
                                        radius: 9
                                        color: favCard.isTuned ? t.tint(t.accent, 0.28)
                                             : (favCard.isLive ? t.tint(t.success, 0.24)
                                             : (favTuneMouse.pressed ? t.surface3 : t.tint(t.accent, 0.16)))
                                        border.color: favCard.isLive && !favCard.isTuned ? t.success : t.accent
                                        border.width: 1

                                        Row {
                                            id: favTuneRow
                                            anchors.centerIn: parent
                                            spacing: 5
                                            Label {
                                                text: favCard.isTuned ? "\uf00c" : "\uf0ec"
                                                font.family: "FontAwesome"
                                                font.pixelSize: 11
                                                color: favCard.isLive && !favCard.isTuned ? t.success : t.accent
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Label {
                                                text: page.tuneLabel(favCard.isTuned)
                                                font.pixelSize: t.fsCaption
                                                font.bold: true
                                                color: t.text
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        MouseArea {
                                            id: favTuneMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (page.app) {
                                                    page.app.quickSwitchToTalkgroup(String(favCard.tgid), false)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
