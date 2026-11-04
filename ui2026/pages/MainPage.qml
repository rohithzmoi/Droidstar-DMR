/*
    Copyright (C) 2024-2026 Rohith Namboothiri

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
    title: qsTr("Main")
    padding: 0

    property var app: null
    required property var droidstarRef
    required property var appState
    required property var logHandlerRef
    property var vuidUpdaterRef: null
    property Component headerActions: null

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    // Local UI models
    readonly property var modesModel: ["REF", "DCS", "XRF", "YSF", "FCS", "DMR", "P25", "NXDN", "M17", "IAX"]
    readonly property var modulesModel: ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                         "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"]
    readonly property var ccsModel: ["CC0", "CC1", "CC2", "CC3", "CC4", "CC5", "CC6", "CC7",
                                     "CC8", "CC9", "CC10", "CC11", "CC12", "CC13", "CC14", "CC15"]
    readonly property var m17CanModel: ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"]

    readonly property string mode: appState ? appState.mode : ""
    readonly property bool isDstar: mode === "REF" || mode === "DCS" || mode === "XRF"
    readonly property bool usesTalkgroup: mode === "DMR" || mode === "P25" || mode === "NXDN"
    readonly property bool busy: appState && (appState.connected || appState.connecting)
    readonly property bool m17Direct: mode === "M17" && appState && appState.mmdvmDirect
    // Legacy parity: mode/host are fixed for the life of a connection.
    readonly property bool canChangeLink: !busy || m17Direct
    readonly property bool canChangeModule: !busy || mode === "REF" || m17Direct

    // Holds a talkgroup chosen from the recent sheet until the field has
    // finished restoring focus, so the old value cannot overwrite it.
    property string forcedTgid: ""
    signal syncTgField(string tgValue)

    function loadTalkgroup(tg, asPrivate) {
        var raw = (tg === undefined || tg === null) ? "" : String(tg)
        var v = raw.replace(/[^0-9]/g, "")
        if (v === "" || v.length > 8)
            return false
        page.forcedTgid = v
        if (page.appState)
            page.appState.dmrtgid = v
        page.syncTgField(v)
        if (page.app) {
            if (page.mode === "DMR" || page.mode === "") {
                page.app.quickSwitchToTalkgroup(v, false, !!asPrivate)
            } else {
                page.app.commitTalkgroup(v)
            }
        }
        page.syncTgField(v)
        forcedTgTimer.ticks = 0
        forcedTgTimer.restart()
        return true
    }

    Timer {
        id: forcedTgTimer
        interval: 60
        repeat: true
        property int ticks: 0
        onTriggered: {
            if (page.forcedTgid === "") {
                ticks = 0
                stop()
                return
            }
            if (page.appState && page.appState.dmrtgid !== page.forcedTgid)
                page.appState.dmrtgid = page.forcedTgid
            page.syncTgField(page.forcedTgid)
            ticks++
            if (ticks >= 5) {
                page.forcedTgid = ""
                ticks = 0
                stop()
            }
        }
    }

    readonly property var _smsThreads: (app && app.smsManagerRef) ? (app.smsManagerRef.threads || []) : []
    function unreadFrom(dmrId) {
        if (!(dmrId > 0)) return 0
        var key = "p:" + dmrId
        for (var i = 0; i < page._smsThreads.length; i++) {
            var th = page._smsThreads[i]
            if (th && th.threadKey === key) return parseInt(th.unreadCount || "0") || 0
        }
        return 0
    }

    // Two columns (controls | live activity) from ~700pt: every landscape phone qualifies,
    readonly property bool wide: scroller.width >= 700
    readonly property int captionLines: (page.wide || scroller.height >= 740 || !connectionSection.expanded) ? 3 : 2
    FontMetrics { id: captionMetrics; font.pixelSize: t.fsBody }

    // UX: once connected, focus on live status; re-open Connection after disconnect.
    Connections {
        target: page.appState
        function onConnectedChanged() {
            connectionSection.expanded = !page.appState.connected
        }
    }
    Component.onCompleted: if (appState && appState.connected) connectionSection.expanded = false

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentRow.implicitHeight + homeFooter.implicitHeight + 36
        boundsBehavior: Flickable.DragOverBounds
        ScrollIndicator.vertical: ScrollIndicator {}

        RowLayout {
            id: contentRow
            x: Math.max(10, (scroller.width - width) / 2)
            y: 12
            width: Math.min(scroller.width - 20, page.wide ? 1180 : t.maxContentWidth)
            spacing: 12

            ColumnLayout {
                id: leftCol
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignTop
                spacing: 12

                // CONNECTION
                CollapsibleSection {
                    id: connectionSection
                    title: qsTr("Connection")
                    statusText: (appState && appState.connected)
                                ? (appState.mode + " • " + appState.selectedHost)
                                : ((appState && appState.connecting) ? qsTr("Connecting…") : "")
                    // Narrow headers (Slide Over, small phones) drop the mode prefix instead
                    statusTextShort: (appState && appState.connected) ? appState.selectedHost : ""
                    statusIcon: (appState && appState.connected && appState.odtpActive) ? "\uf0e7" : ""
                    statusColor: (appState && appState.connected) ? t.success : t.warning
                    statusWhenCollapsedOnly: true

                    content: ColumnLayout {
                        id: connCol
                        spacing: 12
                        readonly property bool narrow: width < 340

                        // Missing identity hint
                        Rectangle {
                            Layout.fillWidth: true
                            visible: !!(appState && (appState.callsign === "" || (page.mode === "DMR" && appState.dmrid === "")))
                            implicitHeight: hintRow.implicitHeight + 18
                            radius: 10
                            color: t.tint(t.warning, 0.1)
                            border.color: t.tint(t.warning, 0.5)
                            border.width: 1

                            RowLayout {
                                id: hintRow
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 6
                                spacing: 8
                                Label { text: "\uf071"; font.family: "FontAwesome"; color: t.warning; font.pixelSize: 13 }
                                Label {
                                    Layout.fillWidth: true
                                    text: appState && appState.callsign === "" ? qsTr("Set your callsign to connect.")
                                                                                 : qsTr("DMR needs your DMR ID.")
                                    color: t.text
                                    font.pixelSize: t.fsSmall
                                    wrapMode: Text.WordWrap
                                }
                                Button {
                                    text: qsTr("Settings")
                                    flat: true
                                    onClicked: if (page.app) page.app.navigate("settings")
                                }
                            }
                        }

                        // Mode & Host
                        GridLayout {
                            Layout.fillWidth: true
                            columns: connCol.narrow ? 1 : 2
                            columnSpacing: 10
                            rowSpacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 0
                                spacing: 4
                                Label { text: qsTr("Mode"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                SearchableComboSheet {
                                    model: page.modesModel
                                    currentText: page.mode
                                    sheetTitle: qsTr("Select Mode")
                                    searchable: false
                                    enabled: !page.busy
                                    onActivated: function(index, text) { if (page.app) page.app.setMode(text) }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 2
                                Layout.minimumWidth: 0
                                spacing: 4
                                Label {
                                    text: qsTr("Host") + ((appState && appState.hostsModel.length > 0) ? "  (" + appState.hostsModel.length + ")" : "")
                                    font.pixelSize: t.fsCaption; color: t.textMuted
                                }
                                SearchableComboSheet {
                                    model: appState ? appState.hostsModel : []
                                    currentText: appState ? appState.selectedHost : ""
                                    sheetTitle: qsTr("Select %1 Host").arg(page.mode)
                                    placeholderText: (appState && appState.hostsModel.length === 0) ? qsTr("No hosts, please update host files") : qsTr("Select host…")
                                    enabled: page.canChangeLink
                                    itemIconColor: t.accent
                                    itemIconFn: function(hostName) {
                                        if (page.mode !== "DMR" || !(appState && appState.bmOdtp)) return ""
                                        if (page.droidstarRef && typeof page.droidstarRef.is_odtp_host === "function") {
                                            return page.droidstarRef.is_odtp_host(hostName) ? "\uf0e7" : ""
                                        }
                                        return String(hostName || "").toUpperCase().indexOf("BM") === 0 ? "\uf0e7" : ""
                                    }
                                    onActivated: function(index, text) { if (page.app) page.app.setHost(text) }
                                }
                            }
                        }

                        // Mode-specific controls
                        GridLayout {
                            Layout.fillWidth: true
                            columns: connCol.narrow ? 2 : 3
                            columnSpacing: 10
                            rowSpacing: 10
                            visible: page.isDstar || page.mode === "M17" || page.mode === "DMR"

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                Layout.minimumWidth: 0
                                visible: page.isDstar || page.mode === "M17"
                                spacing: 4
                                Label { text: qsTr("Module"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                SearchableComboSheet {
                                    model: page.modulesModel
                                    currentText: appState ? appState.module : "A"
                                    sheetTitle: qsTr("Select Module")
                                    searchable: false
                                    enabled: page.canChangeModule
                                    onActivated: function(index, text) { if (page.app) page.app.setModule(text) }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                Layout.minimumWidth: 0
                                visible: page.mode === "DMR"
                                spacing: 4
                                Label { text: qsTr("Slot"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                SegmentedControl {
                                    options: [{ label: qsTr("TS1"), value: 0 }, { label: qsTr("TS2"), value: 1 }]
                                    currentValue: appState ? appState.dmrSlot : 1
                                    onSelected: function(value) { if (page.app) page.app.setDmrSlot(value) }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                Layout.minimumWidth: 0
                                visible: page.mode === "DMR"
                                spacing: 4
                                Label { text: qsTr("Color Code"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                SearchableComboSheet {
                                    model: page.ccsModel
                                    currentText: appState ? page.ccsModel[appState.dmrCc] : "CC1"
                                    sheetTitle: qsTr("Select Color Code")
                                    searchable: false
                                    onActivated: function(index) { if (page.app) page.app.setDmrCc(index) }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                Layout.minimumWidth: 0
                                visible: page.mode === "M17"
                                spacing: 4
                                Label { text: qsTr("CAN"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                SearchableComboSheet {
                                    model: page.m17CanModel
                                    currentText: appState ? appState.modemM17CAN : "0"
                                    sheetTitle: qsTr("Select Channel Access Number")
                                    searchable: false
                                    onActivated: function(index, text) { if (page.app) page.app.setM17Can(text) }
                                }
                            }
                        }

                        // Talkgroup / Private Call Target ID
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: page.usesTalkgroup
                            spacing: 4

                            Label {
                                text: (page.mode === "DMR" && appState && appState.privateCall)
                                      ? qsTr("Private Call DMR ID")
                                      : qsTr("Talkgroup")
                                font.pixelSize: t.fsCaption
                                color: (page.mode === "DMR" && appState && appState.privateCall) ? t.warning : t.textMuted
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppTextField {
                                    id: tgField
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 60
                                    placeholderText: (page.mode === "DMR" && appState && appState.privateCall)
                                                     ? qsTr("DMR ID (e.g. 9990)")
                                                     : qsTr("Talkgroup ID")
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
                                    Accessible.name: qsTr("Talkgroup ID")

                                    property string _committed: ""
                                    Component.onCompleted: { text = appState ? appState.dmrtgid : ""; _committed = text }
                                    Connections {
                                        target: page
                                        function onSyncTgField(tgValue) {
                                            tgAutoCommitTimer.stop()
                                            tgField.focus = false
                                            tgField.text = tgValue
                                            tgField._committed = tgValue
                                        }
                                    }
                                    Connections {
                                        target: page.appState
                                        function onDmrtgidChanged() {
                                            if (page.forcedTgid !== "") {
                                                tgAutoCommitTimer.stop()
                                                tgField.text = page.forcedTgid
                                                tgField._committed = page.forcedTgid
                                                return
                                            }
                                            var nextTg = String(page.appState.dmrtgid || "")
                                            if (!tgField.activeFocus || tgField.text !== nextTg) {
                                                tgAutoCommitTimer.stop()
                                                tgField.text = nextTg
                                                tgField._committed = nextTg
                                            }
                                        }
                                    }

                                    Timer {
                                        id: tgAutoCommitTimer
                                        interval: 900
                                        repeat: false
                                        onTriggered: tgField.commit()
                                    }

                                    function commit() {
                                        tgAutoCommitTimer.stop()
                                        if (text === _committed) return
                                        _committed = text
                                        if (page.app) {
                                            var cleaned = text.trim()
                                            if (page.mode === "DMR" && page.appState &&
                                                (page.appState.connected || page.appState.connecting) &&
                                                cleaned !== "" && parseInt(cleaned) > 0) {
                                                page.app.quickSwitchToTalkgroup(cleaned, false, !!page.appState.privateCall)
                                            } else {
                                                page.app.commitTalkgroup(text)
                                            }
                                        }
                                    }
                                    onTextEdited: {
                                        if (page.appState) page.appState.dmrtgid = text
                                        tgAutoCommitTimer.restart()
                                    }
                                    onAccepted: { commit(); focus = false }
                                    onEditingFinished: {
                                        if (page.forcedTgid !== "") {
                                            text = page.forcedTgid
                                            _committed = text
                                            tgAutoCommitTimer.stop()
                                        } else {
                                            commit()
                                        }
                                    }
                                    onActiveFocusChanged: {
                                        if (!activeFocus) {
                                            if (page.forcedTgid !== "") {
                                                text = page.forcedTgid
                                                _committed = text
                                                tgAutoCommitTimer.stop()
                                            } else {
                                                commit()
                                            }
                                        }
                                    }
                                }

                                // 1-tap Star / Pin current Talkgroup as Favourite
                                Button {
                                    id: favCurrentButton
                                    readonly property bool curIsFav: !!(page.app && tgField.text.trim() !== "" &&
                                                                        page.app.isFavouriteTalkgroup(tgField.text.trim()))
                                    Layout.preferredHeight: t.controlHeight
                                    Layout.preferredWidth: t.controlHeight
                                    flat: true
                                    text: curIsFav ? "\uf005" : "\uf006"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 16
                                    enabled: tgField.text.trim() !== "" && parseInt(tgField.text.trim()) > 0
                                    Accessible.name: curIsFav ? qsTr("Unpin favourite talkgroup") : qsTr("Pin talkgroup as favourite")
                                    palette.buttonText: curIsFav ? t.warning : t.textMuted
                                    background: Rectangle {
                                        radius: 10
                                        color: favCurrentButton.curIsFav ? t.tint(t.warning, 0.16)
                                             : (favCurrentButton.down ? t.surface3 : t.surface)
                                        border.color: favCurrentButton.curIsFav ? t.tint(t.warning, 0.65) : t.stroke
                                        border.width: 1
                                        opacity: favCurrentButton.enabled ? 1 : 0.45
                                    }
                                    onClicked: {
                                        tgField.commit()
                                        if (page.app && tgField.text.trim() !== "") {
                                            page.app.toggleFavouriteTalkgroup(tgField.text.trim())
                                        }
                                    }
                                }

                                // Recent & Favourite Talkgroups searchable sheet
                                Button {
                                    id: recentButton
                                    Layout.preferredHeight: t.controlHeight
                                    Layout.preferredWidth: t.controlHeight
                                    flat: true
                                    text: "\uf1da"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 16
                                    enabled: page.usesTalkgroup
                                    Accessible.name: qsTr("Recent and favourite talkgroups")
                                    background: Rectangle {
                                        radius: 10
                                        color: recentButton.down ? t.surface3 : t.surface
                                        border.color: recentTgSheet.opened ? t.accent : t.stroke
                                        border.width: 1
                                        opacity: recentButton.enabled ? 1 : 0.45
                                    }
                                    onClicked: {
                                        tgField.commit()
                                        recentTgSheet.openSheet()
                                    }
                                }

                                ChipToggle {
                                    visible: page.mode === "DMR"
                                    Layout.fillWidth: false
                                    Layout.preferredHeight: t.controlHeight
                                    text: qsTr("Private")
                                    glyph: "\uf007"
                                    checked: !!(appState && appState.privateCall)
                                    activeColor: t.warning
                                    onClicked: {
                                        tgField.commit()
                                        if (page.app) page.app.setPrivateCall(!appState.privateCall)
                                    }
                                }
                            }
                        }

                        // IAX DTMF
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.mode === "IAX"
                            spacing: 8
                            AppTextField {
                                id: dtmfField
                                Layout.fillWidth: true
                                placeholderText: qsTr("DTMF digits (0-9 * #)")
                                inputMethodHints: Qt.ImhDialableCharactersOnly
                                validator: RegularExpressionValidator { regularExpression: /[0-9A-Da-d*#]*/ }
                                onAccepted: sendDtmf.clicked()
                            }
                            AppButton {
                                id: sendDtmf
                                text: qsTr("Send")
                                tone: "primary"
                                enabled: dtmfField.text.length > 0 && !!(appState && appState.connected)
                                onClicked: {
                                    page.droidstarRef.dtmf_send_clicked(dtmfField.text)
                                    dtmfField.clear()
                                }
                            }
                        }

                        // Connect button
                        AppButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            font.pixelSize: 16
                            tone: (appState && appState.connected) ? "danger"
                                  : ((appState && appState.connecting) ? "warning" : "primary")
                            busy: !!(appState && appState.connecting)
                            glyph: (appState && appState.connected) ? "\uf011" : "\uf1e6"
                            text: (appState && appState.connecting) ? qsTr("Connecting… tap to cancel")
                                  : ((appState && appState.connected) ? qsTr("Disconnect") : qsTr("Connect"))
                            enabled: !!appState
                            onClicked: if (page.app) page.app.connectOrDisconnect()
                        }
                    }
                }

                // RX.TX CONTROLS
                CollapsibleSection {
                    id: txSection
                    title: qsTr("RX.TX Controls")
                    statusText: (appState && appState.fetchedFirstName !== "")
                                ? (appState.fetchedFirstName + (appState.fetchedCountry !== "" ? " • " + appState.fetchedCountry : ""))
                                : ""
                    statusWhenCollapsedOnly: false

                    content: ColumnLayout {
                        id: txCol
                        spacing: 12

                        property string heldTitle: ""
                        property string heldBadge: ""
                        property bool heldActive: false
                        property string heldDescription: ""
                        property bool showPreampInfo: false

                        function togglePreampInfo() {
                            txCol.showPreampInfo = !txCol.showPreampInfo
                            if (txCol.showPreampInfo) {
                                preampInfoDismissTimer.restart()
                            } else {
                                preampInfoDismissTimer.stop()
                            }
                        }

                        Timer {
                            id: preampInfoDismissTimer
                            interval: 8000
                            repeat: false
                            onTriggered: txCol.showPreampInfo = false
                        }

                        function showHoldGuide(titleText, badgeText, isActive, descText) {
                            txCol.heldTitle = titleText
                            txCol.heldBadge = badgeText
                            txCol.heldActive = isActive
                            txCol.heldDescription = descText
                            vocoderDrawer.expanded = true
                            txHoldDismissTimer.restart()
                        }

                        Timer {
                            id: txHoldDismissTimer
                            interval: 8000
                            repeat: false
                            onTriggered: txCol.heldDescription = ""
                        }

                        // Audio level meter
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 12
                            radius: 6
                            color: t.surface
                            border.color: t.stroke
                            border.width: 1
                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: qsTr("Audio level")

                            Rectangle {
                                id: levelFill
                                readonly property real level: Math.min(1, (appState ? appState.outputLevel : 0) / 32767.0)
                                height: parent.height - 4
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 2
                                radius: height / 2
                                width: Math.max(height, (parent.width - 4) * level)
                                color: (appState && appState.txActive) ? t.danger : (level > 0.85 ? t.warning : t.success)
                                Behavior on width { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
                            }
                        }

                        // Mic gain
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: "\uf130"; font.family: "FontAwesome"; font.pixelSize: 15; color: t.textMuted; Layout.preferredWidth: 18; horizontalAlignment: Text.AlignHCenter }
                            Slider {
                                id: micSlider
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                from: 0.0
                                to: 1.0
                                stepSize: 0.01
                                value: appState ? appState.micGain : 0.5
                                enabled: !(appState && appState.connecting)
                                Accessible.name: qsTr("Microphone gain")
                                onMoved: if (page.app) page.app.setMicGain(value)
                                onValueChanged: if (pressed && page.app) page.app.setMicGain(value)
                            }
                            Label {
                                text: Math.round(micSlider.value * 100) + "%"
                                font.pixelSize: t.fsSmall
                                color: t.text
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 46
                            }
                        }

                        // Speaker volume
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: "\uf028"; font.family: "FontAwesome"; font.pixelSize: 15; color: t.textMuted; Layout.preferredWidth: 18; horizontalAlignment: Text.AlignHCenter }
                            Slider {
                                id: spkSlider
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                from: 0.0
                                to: 1.0
                                stepSize: 0.01
                                value: appState ? appState.spkVolume : 1.0
                                Accessible.name: qsTr("Speaker volume")
                                onMoved: if (page.app) page.app.setSpkVolume(value)
                                onValueChanged: if (pressed && page.app) page.app.setSpkVolume(value)
                            }
                            Label {
                                text: Math.round(spkSlider.value * 100) + "%"
                                font.pixelSize: t.fsSmall
                                color: t.text
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 46
                            }
                        }

                        // Speaker Clean Pre-Amp (0 dB to +24 dB pure linear boost after processing & STT)
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Item {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 36
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Speaker pre-amp boost info")
                                Accessible.onPressAction: txCol.togglePreampInfo()

                                Label {
                                    anchors.centerIn: parent
                                    text: "\uf0e7"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 15
                                    color: (preampSlider.value > 0) ? t.accent : t.textMuted
                                    scale: preampIconMouse.pressed ? 0.9 : 1.0
                                    Behavior on scale { NumberAnimation { duration: t.durFast } }
                                }

                                MouseArea {
                                    id: preampIconMouse
                                    anchors.fill: parent
                                    anchors.margins: -8
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: txCol.togglePreampInfo()
                                }
                            }
                            Slider {
                                id: preampSlider
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                from: 0
                                to: 24
                                stepSize: 1
                                value: appState ? appState.spkPreampDb : 6
                                Accessible.name: qsTr("Speaker clean pre-amp boost in decibels")
                                onMoved: if (page.app) page.app.setSpkPreampDb(value)
                                onValueChanged: if (pressed && page.app) page.app.setSpkPreampDb(value)
                            }
                            Item {
                                Layout.preferredWidth: 46
                                Layout.preferredHeight: 36
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Speaker pre-amp boost value and info")
                                Accessible.onPressAction: txCol.togglePreampInfo()

                                Label {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (Math.round(preampSlider.value) > 0 ? "+" : "") + Math.round(preampSlider.value) + " dB"
                                    font.pixelSize: t.fsSmall
                                    font.bold: preampSlider.value > 0
                                    color: (preampSlider.value > 0) ? t.accent : t.textMuted
                                    horizontalAlignment: Text.AlignRight
                                    opacity: preampValueMouse.pressed ? 0.7 : 1.0
                                }

                                MouseArea {
                                    id: preampValueMouse
                                    anchors.fill: parent
                                    anchors.margins: -6
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: txCol.togglePreampInfo()
                                }
                            }
                        }

                        // Inline description card shown when tapping the thunderbolt icon or dB value
                        Rectangle {
                            Layout.fillWidth: true
                            visible: txCol.showPreampInfo
                            implicitHeight: preampGuideRow.implicitHeight + 16
                            radius: 10
                            color: t.tint(t.accent, 0.12)
                            border.color: t.tint(t.accent, 0.5)
                            border.width: 1

                            RowLayout {
                                id: preampGuideRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 8

                                Label {
                                    text: "\uf0e7"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 14
                                    color: t.accent
                                    Layout.alignment: Qt.AlignTop
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 3

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Label {
                                            text: qsTr("Speaker Pre-Amp Boost")
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: t.text
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Rectangle {
                                            radius: 4
                                            implicitHeight: 16
                                            implicitWidth: preampStateLbl.implicitWidth + 8
                                            color: (Math.round(preampSlider.value) > 0) ? t.tint(t.accent, 0.2) : t.surface3
                                            Label {
                                                id: preampStateLbl
                                                anchors.centerIn: parent
                                                text: (Math.round(preampSlider.value) > 0 ? "+" : "") + Math.round(preampSlider.value) + " dB"
                                                font.pixelSize: t.fsCaption - 1
                                                font.bold: true
                                                color: (Math.round(preampSlider.value) > 0) ? t.accent : t.textMuted
                                            }
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Speaker Pre-Amp adds up to 24 dB of clean volume boost to received audio so you can hear stations clearly in noisy places or on quiet speakers. Set it to 0 dB for normal volume, or raise it when you need extra loudness.")
                                        font.pixelSize: t.fsCaption
                                        color: t.text
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                ToolButton {
                                    implicitWidth: 22
                                    implicitHeight: 22
                                    padding: 0
                                    text: "\uf00d"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 10
                                    Accessible.name: qsTr("Dismiss description")
                                    onClicked: txCol.showPreampInfo = false
                                }
                            }
                        }

                        TxButton {
                            Layout.fillWidth: true
                            app: page.app
                            appState: page.appState
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: (appState && appState.toggleTx) ? qsTr("Tap to toggle TX") : qsTr("Hold to transmit (PTT)")
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                        }

                        // EXPANDABLE DRAWER: VOCODER & GAIN (SWTX, SWRX, AGC)
                        Rectangle {
                            id: vocoderDrawer
                            property bool expanded: false
                            readonly property string summaryBadge: {
                                var active = []
                                if (appState && appState.swtx) active.push(qsTr("SWTX"))
                                if (appState && appState.swrx) active.push(qsTr("SWRX"))
                                if (appState && appState.agc)  active.push(qsTr("AGC"))
                                return active.length > 0 ? active.join(" · ") : qsTr("All Off")
                            }

                            Layout.fillWidth: true
                            implicitHeight: vocHeader.height + vocBody.height
                            radius: 12
                            clip: true
                            color: t.surface
                            border.color: vocoderDrawer.expanded ? t.tint(t.accent, 0.55) : t.stroke
                            border.width: 1
                            Behavior on border.color { ColorAnimation { duration: t.durFast } }

                            Column {
                                anchors.fill: parent
                                spacing: 0

                                Rectangle {
                                    id: vocHeader
                                    width: parent.width
                                    height: 44
                                    color: vocHeaderMouse.pressed ? Qt.rgba(1, 1, 1, 0.06)
                                         : (vocHeaderMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.03) : "transparent")

                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Vocoder and Gain settings")
                                    Accessible.onPressAction: vocoderDrawer.expanded = !vocoderDrawer.expanded

                        RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: 8

                                        Label {
                                            text: "\uf1de"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 13
                                            color: t.accent
                                        }

                                        Label {
                                            text: qsTr("Vocoder & Gain")
                                            font.pixelSize: t.fsSmall + 1
                                            font.bold: true
                                            color: t.text
                                            elide: Text.ElideRight
                            Layout.fillWidth: true
                                            Layout.minimumWidth: 60
                                        }

                                        Rectangle {
                                            visible: vocHeader.width >= 300
                                            radius: 11
                                            Layout.preferredHeight: 22
                                            Layout.maximumWidth: Math.max(96, vocHeader.width * 0.42)
                                            Layout.preferredWidth: Math.min(vocSummaryLabel.implicitWidth + 16, Layout.maximumWidth)
                                            color: (appState && (appState.swtx || appState.swrx || appState.agc))
                                                   ? t.tint(t.accent, 0.14) : t.surface2
                                            border.color: (appState && (appState.swtx || appState.swrx || appState.agc))
                                                          ? t.tint(t.accent, 0.45) : t.stroke
                                            border.width: 1

                                            Label {
                                                id: vocSummaryLabel
                                                anchors.fill: parent
                                                anchors.leftMargin: 8
                                                anchors.rightMargin: 8
                                                verticalAlignment: Text.AlignVCenter
                                                horizontalAlignment: Text.AlignHCenter
                                                text: vocoderDrawer.summaryBadge
                                                font.pixelSize: t.fsCaption
                                                font.bold: true
                                                color: (appState && (appState.swtx || appState.swrx || appState.agc)) ? t.accent : t.textMuted
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Label {
                                            text: "\uf078"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: t.textMuted
                                            rotation: vocoderDrawer.expanded ? 180 : 0
                                            Behavior on rotation { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
                                        }
                                    }

                                    MouseArea {
                                        id: vocHeaderMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: vocoderDrawer.expanded = !vocoderDrawer.expanded
                                    }
                                }

                                Item {
                                    id: vocBody
                                    width: parent.width
                                    height: vocoderDrawer.expanded ? (vocInnerCol.implicitHeight + 16) : 0
                                    clip: true
                                    opacity: vocoderDrawer.expanded ? 1 : 0
                                    visible: height > 0
                                    Behavior on height { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
                                    Behavior on opacity { NumberAnimation { duration: t.durFast } }

                                    ColumnLayout {
                                        id: vocInnerCol
                                        x: 10
                                        y: 4
                                        width: Math.max(0, parent.width - 20)
                            spacing: 8

                                        // Updated after the layout pass: changing columns while the parent lays
                                        property int cols: 2
                                        function updateCols() {
                                            var c = width >= 500 ? 3 : (width >= 290 ? 2 : 1)
                                            if (c !== cols) cols = c
                                        }
                                        onWidthChanged: Qt.callLater(updateCols)

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label {
                                                text: "\uf05a"
                                                font.family: "FontAwesome"
                                                font.pixelSize: 11
                                                color: t.textMuted
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                Layout.preferredWidth: 1
                                                text: qsTr("Tap any card to switch on or off. Hold any card to see what it does.")
                                                font.pixelSize: t.fsCaption
                                                color: t.textMuted
                                                wrapMode: Text.WordWrap
                                            }
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: vocInnerCol.cols
                                            columnSpacing: 8
                                            rowSpacing: 8

                                            Repeater {
                                                model: [
                                                    {
                                                        key: "swtx",
                                                        title: qsTr("Software TX"),
                                                        badge: qsTr("SWTX"),
                                                        icon: "\uf130",
                                                        subtitle: qsTr("Encode transmit voice in app"),
                                                        checked: !!(appState && appState.swtx),
                                                        spanLast: false,
                                                        desc: qsTr("Software Transmit encodes your microphone audio using the built in software vocoder when you transmit. Keep this enabled unless you have an external USB hardware vocoder plugged in.")
                                                    },
                                                    {
                                                        key: "swrx",
                                                        title: qsTr("Software RX"),
                                                        badge: qsTr("SWRX"),
                                                        icon: "\uf025",
                                                        subtitle: qsTr("Decode received voice in app"),
                                                        checked: !!(appState && appState.swrx),
                                                        spanLast: false,
                                                        desc: qsTr("Software Receive decodes incoming digital radio voice streams into speaker audio using the built in software vocoder. Keep this enabled unless you have an external USB hardware vocoder plugged in.")
                                                    },
                                                    {
                                                        key: "agc",
                                                        title: qsTr("Auto Gain Control"),
                                                        badge: qsTr("AGC"),
                                                        icon: "\uf028",
                                                        subtitle: qsTr("Keep station volume steady"),
                                                        checked: !!(appState && appState.agc),
                                                        spanLast: true,
                                                        desc: qsTr("Auto Gain Control automatically balances quiet and loud radio stations to a comfortable listening volume, while keeping background noise quiet during pauses between words.")
                                                    }
                                                ]

                                                delegate: Rectangle {
                                                    id: vocCard
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    Layout.preferredWidth: 100
                                                    Layout.columnSpan: (modelData.spanLast && vocInnerCol.cols === 2) ? 2 : 1
                                                    implicitHeight: vocCardCol.implicitHeight + 18
                                                    radius: 10
                                                    color: modelData.checked ? t.tint(t.accent, 0.16)
                                                         : (vocCardMouse.pressed ? t.surface3 : t.surface2)
                                                    border.color: modelData.checked ? t.accent : t.stroke
                                                    border.width: 1
                                                    scale: vocCardMouse.pressed ? 0.98 : 1.0
                                                    Behavior on color { ColorAnimation { duration: t.durFast } }
                                                    Behavior on border.color { ColorAnimation { duration: t.durFast } }
                                                    Behavior on scale { NumberAnimation { duration: t.durFast; easing.type: Easing.OutCubic } }

                                                    Accessible.role: Accessible.CheckBox
                                                    Accessible.name: modelData.title + ". " + modelData.subtitle
                                                    Accessible.description: modelData.desc
                                                    Accessible.checkable: true
                                                    Accessible.checked: modelData.checked

                                                    function triggerToggle() {
                                                        if (!page.app || !page.appState) return
                                                        if (modelData.key === "swtx") page.app.setSwtx(!page.appState.swtx)
                                                        else if (modelData.key === "swrx") page.app.setSwrx(!page.appState.swrx)
                                                        else if (modelData.key === "agc") page.app.setAgc(!page.appState.agc)
                                                    }

                                                    function triggerHoldInfo() {
                                                        txCol.showHoldGuide(modelData.title, modelData.badge, modelData.checked, modelData.desc)
                                                    }

                                                    ColumnLayout {
                                                        id: vocCardCol
                                                        anchors.left: parent.left
                                                        anchors.right: parent.right
                                                        anchors.top: parent.top
                                                        anchors.margins: 9
                                                        spacing: 4

                                                        RowLayout {
                            Layout.fillWidth: true
                                                            spacing: 6

                                                            Rectangle {
                                                                radius: 5
                                                                implicitHeight: 18
                                                                implicitWidth: vocBadgeRow.implicitWidth + 10
                                                                color: vocCard.modelData.checked ? t.tint(t.accent, 0.25) : t.surface3
                                                                border.color: vocCard.modelData.checked ? t.tint(t.accent, 0.6) : t.stroke
                                                                border.width: 1

                                                                Row {
                                                                    id: vocBadgeRow
                                                                    anchors.centerIn: parent
                                                                    spacing: 4
                                                                    Label {
                                                                        text: vocCard.modelData.icon
                                                                        font.family: "FontAwesome"
                                                                        font.pixelSize: 10
                                                                        color: vocCard.modelData.checked ? t.accent : t.textMuted
                                                                        anchors.verticalCenter: parent.verticalCenter
                                                                    }
                                                                    Label {
                                                                        text: vocCard.modelData.badge
                                                                        font.pixelSize: t.fsCaption - 1
                                                                        font.bold: true
                                                                        color: vocCard.modelData.checked ? t.text : t.textMuted
                                                                        anchors.verticalCenter: parent.verticalCenter
                                                                    }
                                                                }
                                                            }

                                                            Item { Layout.fillWidth: true }

                        Label {
                                                                text: vocCard.modelData.checked ? qsTr("ON") : qsTr("OFF")
                                                                font.pixelSize: t.fsCaption - 1
                                                                font.bold: true
                                                                color: vocCard.modelData.checked ? t.success : t.textMuted
                                                            }

                                                            Rectangle {
                                                                width: 20
                                                                height: 20
                                                                radius: 10
                                                                color: infoTapVoc.pressed ? t.surface3 : "transparent"
                                                                Label {
                                                                    anchors.centerIn: parent
                                                                    text: "\uf05a"
                                                                    font.family: "FontAwesome"
                                                                    font.pixelSize: 11
                                                                    color: t.textMuted
                                                                }
                                                                MouseArea {
                                                                    id: infoTapVoc
                                                                    anchors.fill: parent
                                                                    anchors.margins: -4
                                                                    z: 2
                                                                    cursorShape: Qt.PointingHandCursor
                                                                    onClicked: vocCard.triggerHoldInfo()
                                                                }
                                                            }
                                                        }

                                                        Label {
                            Layout.fillWidth: true
                                                            text: vocCard.modelData.title
                                                            font.pixelSize: t.fsSmall + 1
                                                            font.bold: true
                                                            color: t.text
                                                            elide: Text.ElideRight
                        }

                        Label {
                                                            Layout.fillWidth: true
                                                            Layout.preferredWidth: 1
                                                            text: vocCard.modelData.subtitle
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 2
                                                            elide: Text.ElideRight
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: vocCardMouse
                                                        anchors.fill: parent
                                                        hoverEnabled: Qt.platform.os !== "ios" && Qt.platform.os !== "android"
                                                        pressAndHoldInterval: 360
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: vocCard.triggerToggle()
                                                        onPressAndHold: vocCard.triggerHoldInfo()
                                                    }

                                                    ToolTip {
                                                        visible: vocCardMouse.hoverEnabled && vocCardMouse.containsMouse && !vocCardMouse.pressed
                                                        delay: 500
                                                        timeout: 5000
                                                        text: vocCard.modelData.desc
                                                    }
                                                }
                                            }
                                        }

                                        // On-hold natural English description banner for Vocoder & Gain
                                        Rectangle {
                                            Layout.fillWidth: true
                                            visible: txCol.heldDescription !== ""
                                            implicitHeight: vocGuideRow.implicitHeight + 16
                                            radius: 10
                                            color: t.tint(t.accent, 0.12)
                                            border.color: t.tint(t.accent, 0.5)
                                            border.width: 1

                                            RowLayout {
                                                id: vocGuideRow
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 8
                                                spacing: 8

                                                Label {
                                                    text: "\uf05a"
                                                    font.family: "FontAwesome"
                                                    font.pixelSize: 14
                                                    color: t.accent
                                                    Layout.alignment: Qt.AlignTop
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    spacing: 3

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 6
                                                        Label {
                                                            text: txCol.heldTitle + " (" + txCol.heldBadge + ")"
                                                            font.pixelSize: t.fsSmall
                                                            font.bold: true
                                                            color: t.text
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                        Rectangle {
                                                            radius: 4
                                                            implicitHeight: 16
                                                            implicitWidth: vocStateLbl.implicitWidth + 8
                                                            color: txCol.heldActive ? t.tint(t.success, 0.2) : t.surface3
                                                            Label {
                                                                id: vocStateLbl
                                                                anchors.centerIn: parent
                                                                text: txCol.heldActive ? qsTr("Enabled") : qsTr("Disabled")
                                                                font.pixelSize: t.fsCaption - 1
                                                                font.bold: true
                                                                color: txCol.heldActive ? t.success : t.textMuted
                                                            }
                                                        }
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        Layout.preferredWidth: 1
                                                        text: txCol.heldDescription
                                                        font.pixelSize: t.fsCaption
                                                        color: t.text
                                                        wrapMode: Text.WordWrap
                                                    }
                                                }

                                                ToolButton {
                                                    implicitWidth: 22
                                                    implicitHeight: 22
                                                    padding: 0
                                                    text: "\uf00d"
                                                    font.family: "FontAwesome"
                                                    font.pixelSize: 10
                                                    Accessible.name: qsTr("Dismiss description")
                                                    onClicked: txCol.heldDescription = ""
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // EXPANDABLE DRAWER: AUDIO QUALITY & LIVE TRANSCRIPTION
                        Rectangle {
                            id: audioClarityDrawer
                            property bool expanded: false
                            readonly property string summaryBadge: {
                                var tags = []
                                if (appState) {
                                    if (appState.rxClean) tags.push(qsTr("RX Clarity"))
                                    if (appState.noiseReductionMode === 2) tags.push(qsTr("AI Denoise"))
                                    else if (appState.noiseReductionMode === 1) tags.push(qsTr("Natural NR"))
                                    if (appState.hdUpscale) tags.push(qsTr("HD Voice"))
                                    if (appState.txEnhance) tags.push(qsTr("Mic Leveler"))
                                    if (appState.transcriptionEnabled) tags.push(qsTr("Captions On"))
                                }
                                return tags.length > 0 ? tags.join(" · ") : qsTr("Standard")
                            }

                            Layout.fillWidth: true
                            implicitHeight: clarityHeader.height + clarityBody.height
                            radius: 12
                            clip: true
                            color: t.surface
                            border.color: audioClarityDrawer.expanded ? t.tint(t.accent, 0.55) : t.stroke
                            border.width: 1
                            Behavior on border.color { ColorAnimation { duration: t.durFast } }

                            Column {
                                anchors.fill: parent
                                spacing: 0

                                Rectangle {
                                    id: clarityHeader
                                    width: parent.width
                                    height: 44
                                    color: clarityHeaderMouse.pressed ? Qt.rgba(1, 1, 1, 0.06)
                                         : (clarityHeaderMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.03) : "transparent")

                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Audio Quality and Live Transcription")
                                    Accessible.onPressAction: audioClarityDrawer.expanded = !audioClarityDrawer.expanded

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: 8

                                        Label {
                                            text: "\uf001"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 13
                                            color: t.accent
                                        }

                                        Label {
                                            text: qsTr("Audio Clarity & Live Transcription")
                                            font.pixelSize: t.fsSmall + 1
                                            font.bold: true
                                            color: t.text
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 60
                                        }

                                        Rectangle {
                                            visible: clarityHeader.width >= 300
                                            radius: 11
                                            Layout.preferredHeight: 22
                                            Layout.maximumWidth: Math.max(96, clarityHeader.width * 0.42)
                                            Layout.preferredWidth: Math.min(claritySummaryLabel.implicitWidth + 16, Layout.maximumWidth)
                                            color: (appState && (appState.rxClean || appState.noiseReductionMode > 0 || appState.hdUpscale || appState.txEnhance || appState.transcriptionEnabled))
                                                   ? t.tint(t.accent, 0.14) : t.surface2
                                            border.color: (appState && (appState.rxClean || appState.noiseReductionMode > 0 || appState.hdUpscale || appState.txEnhance || appState.transcriptionEnabled))
                                                          ? t.tint(t.accent, 0.45) : t.stroke
                                            border.width: 1

                                            Label {
                                                id: claritySummaryLabel
                                                anchors.fill: parent
                                                anchors.leftMargin: 8
                                                anchors.rightMargin: 8
                                                verticalAlignment: Text.AlignVCenter
                                                horizontalAlignment: Text.AlignHCenter
                                                text: audioClarityDrawer.summaryBadge
                                                font.pixelSize: t.fsCaption
                                                font.bold: true
                                                color: (appState && (appState.rxClean || appState.noiseReductionMode > 0 || appState.hdUpscale || appState.txEnhance || appState.transcriptionEnabled))
                                                       ? t.accent : t.textMuted
                                                elide: Text.ElideRight
                                            }
                                        }

                                        Label {
                                            text: "\uf078"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: t.textMuted
                                            rotation: audioClarityDrawer.expanded ? 180 : 0
                                            Behavior on rotation { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
                                        }
                                    }

                                    MouseArea {
                                        id: clarityHeaderMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: audioClarityDrawer.expanded = !audioClarityDrawer.expanded
                                    }
                                }

                                Item {
                                    id: clarityBody
                                    width: parent.width
                                    height: audioClarityDrawer.expanded ? (clarityCol.implicitHeight + 16) : 0
                                    clip: true
                                    opacity: audioClarityDrawer.expanded ? 1 : 0
                                    visible: height > 0
                                    Behavior on height { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
                                    Behavior on opacity { NumberAnimation { duration: t.durFast } }

                                    ColumnLayout {
                                        id: clarityCol
                                        x: 10
                                        y: 4
                                        width: Math.max(0, parent.width - 20)
                                        spacing: 8

                                        property int cols: 2
                                        function updateCols() {
                                            var c = width >= 620 ? 4 : (width >= 290 ? 2 : 1)
                                            if (c !== cols) cols = c
                                        }
                                        onWidthChanged: Qt.callLater(updateCols)

                                        property string heldTitle: ""
                                        property string heldBadge: ""
                                        property bool heldActive: false
                                        property string heldDescription: ""

                                        function showHoldGuide(titleText, badgeText, isActive, descText) {
                                            clarityCol.heldTitle = titleText
                                            clarityCol.heldBadge = badgeText
                                            clarityCol.heldActive = isActive
                                            clarityCol.heldDescription = descText
                                            audioClarityDrawer.expanded = true
                                            clarityHoldDismissTimer.restart()
                                        }

                                        Timer {
                                            id: clarityHoldDismissTimer
                                            interval: 8000
                                            repeat: false
                                            onTriggered: clarityCol.heldDescription = ""
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label {
                                                text: "\uf05a"
                                                font.family: "FontAwesome"
                                                font.pixelSize: 11
                                                color: t.textMuted
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                Layout.preferredWidth: 1
                                                text: qsTr("Tap any card to switch on or off. Hold any card to see what it does.")
                                                font.pixelSize: t.fsCaption
                                                color: t.textMuted
                                                wrapMode: Text.WordWrap
                                            }
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: clarityCol.cols
                                            columnSpacing: 8
                                            rowSpacing: 8

                                            Repeater {
                                                model: [
                                                    {
                                                        key: "rx",
                                                        title: qsTr("RX Clarity & NR"),
                                                        badge: qsTr("RX"),
                                                        icon: "\uf028",
                                                        subtitle: qsTr("Light RX de-hiss & clarity"),
                                                        checked: !!(appState && appState.rxClean),
                                                        desc: qsTr("RX Clarity and NR applies zero latency light noise reduction, dynamic vocoder de-hissing, and gentle vocal clarity enhancement to incoming received audio without altering your transmit settings or live transcription.")
                                                    },
                                                    {
                                                        key: "nr",
                                                        title: qsTr("TX Noise Reduction"),
                                                        badge: (appState && appState.noiseReductionMode === 2) ? qsTr("AI")
                                                             : ((appState && appState.noiseReductionMode === 1) ? qsTr("NAT") : qsTr("NR")),
                                                        icon: "\uf0b0",
                                                        subtitle: (appState && appState.noiseReductionMode === 2)
                                                                  ? qsTr("AI mic denoiser and gate")
                                                                  : ((appState && appState.noiseReductionMode === 1)
                                                                     ? qsTr("Natural mic noise gate")
                                                                     : qsTr("Suppress TX background noise")),
                                                        checked: !!(appState && appState.noiseReductionMode > 0),
                                                        desc: qsTr("TX Noise Reduction removes background room noise, fan hum, and mic hiss from your outgoing transmit audio in real time.")
                                                    },
                                                    {
                                                        key: "hd",
                                                        title: qsTr("TX HD Voice"),
                                                        badge: qsTr("HD"),
                                                        icon: "\uf001",
                                                        subtitle: qsTr("Transmit vocal clarity"),
                                                        checked: !!(appState && appState.hdUpscale),
                                                        desc: qsTr("TX HD Voice shapes your outgoing microphone audio for clear vocal presence and removes close talk muddiness before encoding.")
                                                    },
                                                    {
                                                        key: "tx",
                                                        title: qsTr("Mic Leveler"),
                                                        badge: qsTr("MIC"),
                                                        icon: "\uf130",
                                                        subtitle: qsTr("Steady TX voice & gate"),
                                                        checked: !!(appState && appState.txEnhance),
                                                        desc: qsTr("Mic Leveler gates background silence between words and keeps your outgoing transmit voice volume steady before it is sent over the air.")
                                                    },
                                                    {
                                                        key: "cc",
                                                        title: qsTr("Live Transcription"),
                                                        badge: qsTr("CC"),
                                                        icon: "\uf20a",
                                                        subtitle: qsTr("Real time speech to text"),
                                                        checked: !!(appState && appState.transcriptionEnabled),
                                                        desc: qsTr("Live Transcription converts incoming radio speech into readable text on your screen in real time using on device speech recognition. You can choose Apple Neural, Whisper, or Parakeet in Settings.")
                                                    }
                                                ]

                                                delegate: Rectangle {
                                                    id: clarityCard
                                                    required property int index
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    Layout.columnSpan: (index === 4 && clarityCol.cols === 2) ? 2 : 1
                                                    Layout.minimumWidth: 0
                                                    Layout.preferredWidth: 100
                                                    implicitHeight: clarityCardCol.implicitHeight + 18
                                                    radius: 10
                                                    color: modelData.checked ? t.tint(t.accent, 0.16)
                                                         : (clarityCardMouse.pressed ? t.surface3 : t.surface2)
                                                    border.color: modelData.checked ? t.accent : t.stroke
                                                    border.width: 1
                                                    scale: clarityCardMouse.pressed ? 0.98 : 1.0
                                                    Behavior on color { ColorAnimation { duration: t.durFast } }
                                                    Behavior on border.color { ColorAnimation { duration: t.durFast } }
                                                    Behavior on scale { NumberAnimation { duration: t.durFast; easing.type: Easing.OutCubic } }

                                                    Accessible.role: Accessible.CheckBox
                                                    Accessible.name: modelData.title + ". " + modelData.subtitle
                                                    Accessible.description: modelData.desc
                                                    Accessible.checkable: true
                                                    Accessible.checked: modelData.checked

                                                    function triggerToggle() {
                                                        if (!page.app || !page.appState) return
                                                        if (modelData.key === "rx") {
                                                            page.app.setRxClean(!page.appState.rxClean)
                                                        } else if (modelData.key === "nr") {
                                                            // Cycle Off (0) -> Natural (1) -> Neural AI (2) -> Off (0)
                                                            var nextNr = (page.appState.noiseReductionMode + 1) % 3
                                                            page.app.setNoiseReductionMode(nextNr)
                                                        } else if (modelData.key === "hd") {
                                                            page.app.setHdUpscale(!page.appState.hdUpscale)
                                                        } else if (modelData.key === "tx") {
                                                            page.app.setTxEnhance(!page.appState.txEnhance)
                                                        } else if (modelData.key === "cc") {
                                                            page.app.setTranscriptionEnabled(!page.appState.transcriptionEnabled)
                                                        }
                                                    }

                                                    function triggerHoldInfo() {
                                                        clarityCol.showHoldGuide(modelData.title, modelData.badge, modelData.checked, modelData.desc)
                                                    }

                                                    ColumnLayout {
                                                        id: clarityCardCol
                                                        anchors.left: parent.left
                                                        anchors.right: parent.right
                                                        anchors.top: parent.top
                                                        anchors.margins: 9
                                                        spacing: 4

                                                        RowLayout {
                                                            Layout.fillWidth: true
                                                            spacing: 6

                                                            Rectangle {
                                                                radius: 5
                                                                implicitHeight: 18
                                                                implicitWidth: clarityBadgeRow.implicitWidth + 10
                                                                color: clarityCard.modelData.checked ? t.tint(t.accent, 0.25) : t.surface3
                                                                border.color: clarityCard.modelData.checked ? t.tint(t.accent, 0.6) : t.stroke
                                                                border.width: 1

                                                                Row {
                                                                    id: clarityBadgeRow
                                                                    anchors.centerIn: parent
                                                                    spacing: 4
                                                                    Label {
                                                                        text: clarityCard.modelData.icon
                                                                        font.family: "FontAwesome"
                                                                        font.pixelSize: 10
                                                                        color: clarityCard.modelData.checked ? t.accent : t.textMuted
                                                                        anchors.verticalCenter: parent.verticalCenter
                                                                    }
                                                                    Label {
                                                                        text: clarityCard.modelData.badge
                                                                        font.pixelSize: t.fsCaption - 1
                                                                        font.bold: true
                                                                        color: clarityCard.modelData.checked ? t.text : t.textMuted
                                                                        anchors.verticalCenter: parent.verticalCenter
                                                                    }
                                                                }
                                                            }

                                                            Item { Layout.fillWidth: true }

                                                            Label {
                                                                text: clarityCard.modelData.checked ? qsTr("ON") : qsTr("OFF")
                                                                font.pixelSize: t.fsCaption - 1
                                                                font.bold: true
                                                                color: clarityCard.modelData.checked ? t.success : t.textMuted
                                                            }

                                                            Rectangle {
                                                                width: 20
                                                                height: 20
                                                                radius: 10
                                                                color: infoTapClarity.pressed ? t.surface3 : "transparent"
                                                                Label {
                                                                    anchors.centerIn: parent
                                                                    text: "\uf05a"
                                                                    font.family: "FontAwesome"
                                                                    font.pixelSize: 11
                                                                    color: t.textMuted
                                                                }
                                                                MouseArea {
                                                                    id: infoTapClarity
                                                                    anchors.fill: parent
                                                                    anchors.margins: -4
                                                                    z: 2
                                                                    cursorShape: Qt.PointingHandCursor
                                                                    onClicked: clarityCard.triggerHoldInfo()
                                                                }
                                                            }
                                                        }

                                                        Label {
                                                            Layout.fillWidth: true
                                                            text: clarityCard.modelData.title
                                                            font.pixelSize: t.fsSmall + 1
                                                            font.bold: true
                                                            color: t.text
                                                            elide: Text.ElideRight
                                                        }

                                                        Label {
                                                            Layout.fillWidth: true
                                                            Layout.preferredWidth: 1
                                                            text: clarityCard.modelData.subtitle
                                                            font.pixelSize: t.fsCaption
                                                            color: t.textMuted
                                                            wrapMode: Text.WordWrap
                                                            maximumLineCount: 2
                                                            elide: Text.ElideRight
                                                        }
                                                    }

                                                    MouseArea {
                                                        id: clarityCardMouse
                                                        anchors.fill: parent
                                                        hoverEnabled: Qt.platform.os !== "ios" && Qt.platform.os !== "android"
                                                        pressAndHoldInterval: 360
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: clarityCard.triggerToggle()
                                                        onPressAndHold: clarityCard.triggerHoldInfo()
                                                    }

                                                    ToolTip {
                                                        visible: clarityCardMouse.hoverEnabled && clarityCardMouse.containsMouse && !clarityCardMouse.pressed
                                                        delay: 500
                                                        timeout: 5000
                                                        text: clarityCard.modelData.desc
                                                    }
                                                }
                                            }
                                        }

                                        // Noise Reduction mode selector (Off / Natural / Neural AI) always accessible in drawer
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Label {
                                                text: qsTr("TX Noise Reduction Mode")
                                                font.pixelSize: t.fsCaption
                                                color: t.textMuted
                                            }
                                            SegmentedControl {
                                                options: [
                                                    { label: qsTr("Off"), value: 0 },
                                                    { label: qsTr("Natural"), value: 1 },
                                                    { label: qsTr("Neural AI"), value: 2 }
                                                ]
                                                currentValue: appState ? appState.noiseReductionMode : 2
                                                onSelected: function(value) { if (page.app) page.app.setNoiseReductionMode(value) }
                                            }
                                        }

                                        // Live Transcription engine & language summary bar when Live Captions is enabled
                                        Rectangle {
                                            Layout.fillWidth: true
                                            visible: !!(appState && appState.transcriptionEnabled)
                                            implicitHeight: ccConfigRow.implicitHeight + 14
                                            radius: 10
                                            color: t.surface2
                                            border.color: t.stroke
                                            border.width: 1

                                            RowLayout {
                                                id: ccConfigRow
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 10
                                                spacing: 8

                                                Label {
                                                    text: "\uf20a"
                                                    font.family: "FontAwesome"
                                                    font.pixelSize: 12
                                                    color: t.accent
                                                }

                                                Label {
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    text: {
                                                        var lang = appState && appState.transcriptionLanguage
                                                                   ? String(appState.transcriptionLanguage).toUpperCase()
                                                                   : "AUTO"
                                                        var eng = appState ? (appState.transcriptionEngine || "apple") : "apple"
                                                        var engName = (eng === "parakeet")
                                                                      ? qsTr("Parakeet v3")
                                                                      : ((eng === "apple" && appState && appState.transcriptionAppleSupported)
                                                                         ? qsTr("Apple Neural")
                                                                         : qsTr("Whisper AI"))
                                                        return qsTr("Captions Engine: %1 (%2)").arg(engName).arg(lang)
                                                    }
                                                    font.pixelSize: t.fsCaption
                                                    color: t.text
                                                    elide: Text.ElideRight
                                                }

                                                Rectangle {
                                                    radius: 6
                                                    implicitHeight: 22
                                                    implicitWidth: ccSettingsLbl.implicitWidth + 12
                                                    color: t.surface3
                                                    border.color: t.stroke
                                                    border.width: 1

                                                    Accessible.role: Accessible.Button
                                                    Accessible.name: ccSettingsLbl.text

                                                    Label {
                                                        id: ccSettingsLbl
                                                        anchors.centerIn: parent
                                                        text: qsTr("Configure")
                                                        font.pixelSize: t.fsCaption - 1
                                                        font.bold: true
                                                        color: t.accent
                                                    }

                                                    // Visual pill stays compact; the hit area meets the 28pt minimum
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        anchors.margins: -7
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: if (page.app) page.app.navigate("settings")
                                                    }
                                                }
                                            }
                                        }

                                        // On-hold natural English description banner for Audio & Live Transcription
                                        Rectangle {
                                            Layout.fillWidth: true
                                            visible: clarityCol.heldDescription !== ""
                                            implicitHeight: clarityGuideRow.implicitHeight + 16
                                            radius: 10
                                            color: t.tint(t.accent, 0.12)
                                            border.color: t.tint(t.accent, 0.5)
                                            border.width: 1

                                            RowLayout {
                                                id: clarityGuideRow
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 8
                                                spacing: 8

                                                Label {
                                                    text: "\uf05a"
                                                    font.family: "FontAwesome"
                                                    font.pixelSize: 14
                                                    color: t.accent
                                                    Layout.alignment: Qt.AlignTop
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    spacing: 3

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 6
                                                        Label {
                                                            text: clarityCol.heldTitle + " (" + clarityCol.heldBadge + ")"
                                                            font.pixelSize: t.fsSmall
                                                            font.bold: true
                                                            color: t.text
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                        Rectangle {
                                                            radius: 4
                                                            implicitHeight: 16
                                                            implicitWidth: clarityStateLbl.implicitWidth + 8
                                                            color: clarityCol.heldActive ? t.tint(t.success, 0.2) : t.surface3
                                                            Label {
                                                                id: clarityStateLbl
                                                                anchors.centerIn: parent
                                                                text: clarityCol.heldActive ? qsTr("Enabled") : qsTr("Disabled")
                                                                font.pixelSize: t.fsCaption - 1
                                                                font.bold: true
                                                                color: clarityCol.heldActive ? t.success : t.textMuted
                                                            }
                                                        }
                                                    }

                                                    Label {
                                                        Layout.fillWidth: true
                                                        Layout.preferredWidth: 1
                                                        text: clarityCol.heldDescription
                                                        font.pixelSize: t.fsCaption
                                                        color: t.text
                                                        wrapMode: Text.WordWrap
                                                    }
                                                }

                                                ToolButton {
                                                    implicitWidth: 22
                                                    implicitHeight: 22
                                                    padding: 0
                                                    text: "\uf00d"
                                                    font.family: "FontAwesome"
                                                    font.pixelSize: 10
                                                    Accessible.name: qsTr("Dismiss description")
                                                    onClicked: clarityCol.heldDescription = ""
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

            ColumnLayout {
                id: rightCol
                visible: page.wide
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignTop
                spacing: 12
            }

            // LIVE ACTIVITY (moves to its own column on wide screens)
            CollapsibleSection {
                id: liveSection
                parent: page.wide ? rightCol : leftCol
                title: qsTr("Live Activity")

                content: ColumnLayout {
                    spacing: 10

                    // Status badge
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(36, statusLabel.implicitHeight + 16)
                        radius: 10
                        color: (appState && appState.connected) ? t.tint(t.success, 0.12) : t.tint(t.danger, 0.08)
                        border.color: (appState && appState.connected) ? t.tint(t.success, 0.7) : t.stroke
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: (appState && appState.connected) ? t.success : (appState && appState.connecting ? t.warning : t.danger)
                            }
                            Label {
                                visible: !!(appState && appState.connected && appState.odtpActive)
                                text: "\uf0e7"
                                font.family: "FontAwesome"
                                font.pixelSize: 11
                                color: t.success
                            }
                            Label {
                                id: statusLabel
                                Layout.fillWidth: true
                                text: {
                                    if (!appState) return qsTr("Disconnected")
                                    if (appState.netstatus !== "") return appState.netstatus
                                    return appState.connected ? qsTr("Connected") : qsTr("Disconnected")
                                }
                                font.pixelSize: t.fsSmall
                                color: t.text
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // SMS standby started from Messages: voice is quiet until a TG is linked again
                    Rectangle {
                        Layout.fillWidth: true
                        visible: !!(page.app && page.app.smsStandbyActive && appState &&
                                    appState.connected && appState.privateCall)
                        implicitHeight: standbyRow.implicitHeight + 16
                        radius: 10
                        color: t.tint(t.accent, 0.10)
                        border.color: t.tint(t.accent, 0.45)
                        border.width: 1

                        RowLayout {
                            id: standbyRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 8
                            Label { text: "\uf0e0"; font.family: "FontAwesome"; color: t.accent }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("SMS standby: no voice talkgroup linked.")
                                font.pixelSize: t.fsSmall
                                color: t.text
                                wrapMode: Text.WordWrap
                            }
                            AppButton {
                                implicitHeight: 32
                                leftPadding: 10
                                rightPadding: 10
                                font.pixelSize: t.fsSmall
                                glyph: "\uf028"
                                text: (page.app && page.app.smsStandbyRestoreTg !== "")
                                      ? qsTr("Link TG %1").arg(page.app.smsStandbyRestoreTg) : qsTr("Pick TG")
                                onClicked: {
                                    if (!page.app) return
                                    if (page.app.smsStandbyRestoreTg !== "") page.app.leaveSmsStandby("")
                                    else recentTgSheet.openSheet()
                                }
                            }
                        }
                    }

                    // HOME PAGE FAVOURITE TG SCANNER & QUICK-SWITCH BAR
                    Rectangle {
                        id: homeFavScannerCard
                        Layout.fillWidth: true
                        visible: page.mode === "DMR"
                        radius: 12
                        clip: true
                        color: (appState && appState.favScanActive) ? t.tint(t.success, 0.10) : t.surface
                        border.color: (appState && appState.favScanActive) ? t.tint(t.success, 0.65) : t.stroke
                        border.width: 1
                        implicitHeight: homeFavCol.implicitHeight + 16

                        ColumnLayout {
                            id: homeFavCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            spacing: 6
                            // Header buttons drop their text labels so the title keeps its room
                            readonly property bool narrow: width < 330

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    text: "\uf005"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 12
                                    color: t.warning
                                }

                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: {
                                        if (appState && appState.favScanActive) {
                                            return qsTr("Scanning Favourites") +
                                                   ((appState.hoselineActiveFavCount > 0) ? (" • " + appState.hoselineActiveFavCount + " " + qsTr("Live")) : "")
                                        }
                                        if (appState && appState.hoselineActiveFavCount > 0) {
                                            return qsTr("Favourites • %1 On Air").arg(appState.hoselineActiveFavCount)
                                        }
                                        return qsTr("Favourite TGs & Scanner")
                                    }
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: (appState && appState.favScanActive) ? t.success : t.text
                                    elide: Text.ElideRight
                                }

                                // 1-tap Scan Favourites toggle pill
                                Rectangle {
                                    id: homeFavScanBtn
                                    readonly property bool scanning: !!(appState && appState.favScanActive)
                                    radius: 8
                                    implicitHeight: 30
                                    implicitWidth: Math.max(30, homeFavScanRow.implicitWidth + 14)
                                    Layout.preferredWidth: implicitWidth
                                    color: scanning ? t.tint(t.success, 0.24) : t.surface2
                                    border.color: scanning ? t.success : t.stroke
                                    border.width: 1

                                    Row {
                                        id: homeFavScanRow
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Label {
                                            text: homeFavScanBtn.scanning ? "\uf04c" : "\uf04b"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 10
                                            color: homeFavScanBtn.scanning ? t.success : t.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: !homeFavCol.narrow
                                            text: homeFavScanBtn.scanning ? qsTr("Scanning") : qsTr("Scan Favs")
                                            font.pixelSize: t.fsCaption
                                            font.bold: true
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app && page.appState) page.app.setFavScanActive(!page.appState.favScanActive)
                                    }
                                }

                                // Next Active Fav skip button
                                Rectangle {
                                    visible: !!(appState && (appState.favScanActive || appState.hoselineActiveFavCount > 0))
                                    radius: 8
                                    implicitHeight: 30
                                    implicitWidth: 30
                                    Layout.preferredWidth: 30
                                    color: t.surface2
                                    border.color: t.stroke
                                    border.width: 1

                                    Label {
                                        anchors.centerIn: parent
                                        text: "\uf051"
                                        font.family: "FontAwesome"
                                        font.pixelSize: 10
                                        color: t.accent
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app) page.app.scanHopNext(true)
                                    }
                                }

                                // Open Hoseline Scan page button
                                Rectangle {
                                    radius: 8
                                    implicitHeight: 30
                                    implicitWidth: Math.max(30, openScanRow.implicitWidth + 14)
                                    Layout.preferredWidth: implicitWidth
                                    color: t.tint(t.accent, 0.14)
                                    border.color: t.tint(t.accent, 0.45)
                                    border.width: 1

                                    Row {
                                        id: openScanRow
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Label {
                                            text: "\uf002"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 10
                                            color: t.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: !homeFavCol.narrow
                                            text: qsTr("Scan")
                                            font.pixelSize: t.fsCaption
                                            font.bold: true
                                            color: t.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app) page.app.navigate("scan")
                                    }
                                }
                            }

                            // Horizontal Quick-Switch Strip of Favourite TGs (Live ON-AIR sorted to front!)
                            Flickable {
                                id: homeFavStrip
                                Layout.fillWidth: true
                                visible: !!(appState && appState.hoselineFavCalls && appState.hoselineFavCalls.length > 0)
                                implicitHeight: 34
                                contentWidth: homeFavRow.implicitWidth
                                contentHeight: 34
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds

                                Row {
                                    id: homeFavRow
                                    height: 34
                                    spacing: 6

                                    Repeater {
                                        model: appState ? (appState.hoselineFavCalls || []) : []
                                        delegate: Rectangle {
                                            id: homeFavPill
                                            required property var modelData
                                            readonly property bool isLive: !!modelData.isLive
                                            readonly property bool isTuned: !!(appState && String(appState.dmrtgid) === String(modelData.tgid))

                                            height: 32
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Math.min(220, pillInner.implicitWidth + 18)
                                            radius: 9
                                            clip: true
                                            color: isLive ? t.tint(t.success, 0.22)
                                                 : (isTuned ? t.tint(t.accent, 0.22) : t.surface2)
                                            border.color: isLive ? t.success
                                                        : (isTuned ? t.accent : t.stroke)
                                            border.width: 1

                                            Row {
                                                id: pillInner
                                                anchors.centerIn: parent
                                                spacing: 5

                                                Rectangle {
                                                    visible: homeFavPill.isLive
                                                    width: 6
                                                    height: 6
                                                    radius: 3
                                                    color: t.success
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }

                                                Label {
                                                    text: {
                                                        var tgStr = "TG " + homeFavPill.modelData.tgid
                                                        if (homeFavPill.isLive && homeFavPill.modelData.sourceCall) {
                                                            return tgStr + " · " + homeFavPill.modelData.sourceCall
                                                        }
                                                        if (homeFavPill.modelData.tgName) {
                                                            return tgStr + " · " + homeFavPill.modelData.tgName
                                                        }
                                                        return tgStr
                                                    }
                                                    font.pixelSize: t.fsCaption
                                                    font.bold: homeFavPill.isLive || homeFavPill.isTuned
                                                    color: homeFavPill.isLive ? t.text : (homeFavPill.isTuned ? t.accent : t.textMuted)
                                                    elide: Text.ElideRight
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (page.app) {
                                                        page.app.quickSwitchToTalkgroup(String(homeFavPill.modelData.tgid), false)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Hint when no Favourite TGs are pinned yet
                            Label {
                                Layout.fillWidth: true
                                visible: !(appState && appState.hoselineFavCalls && appState.hoselineFavCalls.length > 0)
                                text: qsTr("Star any Talkgroup in Recent TGs or Scan to quick-switch and scan active favourites here.")
                                font.pixelSize: t.fsCaption - 1
                                color: t.textMuted
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Current talker hero card
                    Rectangle {
                        Layout.fillWidth: true
                        visible: !!(appState && (appState.data1 !== "" || appState.fetchedFirstName !== ""))
                        implicitHeight: heroCol.implicitHeight + 20
                        radius: 12
                        color: (appState && appState.txActive) ? t.tint(t.danger, 0.12) : t.tint(t.accent, 0.1)
                        border.color: (appState && appState.txActive) ? t.danger : t.accent
                        border.width: 1

                        ColumnLayout {
                            id: heroCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: appState ? appState.data1 : ""
                                    font.pixelSize: 22
                                    font.bold: true
                                    color: t.text
                                    elide: Text.ElideRight
                                }
                                // Instant message the station that is talking right now
                                Rectangle {
                                    id: rxMessagePill
                                    readonly property int rxId: parseInt(appState ? (appState.data2 || "0") : "0") || 0
                                    readonly property int unread: page.unreadFrom(rxId)
                                    visible: !!(appState && appState.mode === "DMR" && !appState.txActive &&
                                                rxId > 0 && rxId !== parseInt(appState.dmrid || "0"))
                                    Layout.alignment: Qt.AlignVCenter
                                    implicitHeight: 34
                                    implicitWidth: rxMsgRow.implicitWidth + 22
                                    radius: 17
                                    color: rxMsgMouse.pressed ? Qt.darker(t.accentStrong, 1.15) : t.accentStrong
                                    border.color: t.tint(t.accent, 0.9)
                                    border.width: 1
                                    scale: rxMsgMouse.pressed ? 0.95 : 1
                                    Behavior on scale { NumberAnimation { duration: t.durFast } }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Send DMR message to %1").arg(appState ? appState.data1 : "")

                                    // Soft ripple while the station is on air
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width
                                        height: parent.height
                                        radius: parent.radius
                                        color: "transparent"
                                        border.color: t.accent
                                        border.width: 2
                                        SequentialAnimation on opacity {
                                            running: rxMessagePill.visible && Qt.application.state === Qt.ApplicationActive
                                            loops: Animation.Infinite
                                            NumberAnimation { from: 0.6; to: 0; duration: 1400; easing.type: Easing.OutCubic }
                                            PauseAnimation { duration: 400 }
                                        }
                                        SequentialAnimation on scale {
                                            running: rxMessagePill.visible && Qt.application.state === Qt.ApplicationActive
                                            loops: Animation.Infinite
                                            NumberAnimation { from: 1.0; to: 1.25; duration: 1400; easing.type: Easing.OutCubic }
                                            PauseAnimation { duration: 400 }
                                        }
                                    }

                                    Row {
                                        id: rxMsgRow
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Label {
                                            text: "\uf0e0"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 13
                                            color: "white"
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: heroCol.width >= 300
                                            text: qsTr("Message")
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: "white"
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Rectangle {
                                            visible: rxMessagePill.unread > 0
                                            width: Math.max(16, rxUnreadLbl.implicitWidth + 8)
                                            height: 16
                                            radius: 8
                                            color: t.danger
                                            anchors.verticalCenter: parent.verticalCenter
                                            Label {
                                                id: rxUnreadLbl
                                                anchors.centerIn: parent
                                                text: rxMessagePill.unread
                                                font.pixelSize: 9
                                                font.bold: true
                                                color: "white"
                                            }
                                        }
                                    }

                                    MouseArea {
                                        id: rxMsgMouse
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app)
                                            page.app.openMessageTo(rxMessagePill.rxId, appState.data1, false)
                                    }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: appState ? (appState.fetchedFirstName + (appState.fetchedCountry !== "" ? " • " + appState.fetchedCountry : "")) : ""
                                font.pixelSize: t.fsBody
                                color: t.text
                                opacity: 0.85
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // LIVE TRANSCRIPTION (Adaptive 2-3 line turn box + 1-tap toggle)
                    Rectangle {
                        id: captionCard
                        Layout.fillWidth: true
                        radius: 12
                        clip: true
                        color: (appState && appState.transcriptionEnabled) ? t.tint(t.accent, 0.08) : t.surface
                        border.color: (appState && appState.transcriptionEnabled)
                                      ? (captionCardCol.rxActiveNow ? t.accent : t.tint(t.accent, 0.45))
                                      : t.stroke
                        border.width: 1
                        implicitHeight: captionCardCol.implicitHeight + 18
                        Behavior on color { ColorAnimation { duration: t.durFast } }
                        Behavior on border.color { ColorAnimation { duration: t.durFast } }

                        ColumnLayout {
                            id: captionCardCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 9
                            spacing: 6

                            readonly property bool wideCard: width >= 480
                            // Below this the header drops its secondary pills so the essentials fit
                            readonly property bool narrowCard: width < 300
                            readonly property bool rxActiveNow: !!(appState && appState.connected && appState.data1 !== "")
                            readonly property bool hasCaptionText: !!(appState && appState.connected && appState.currentTranscript !== "")
                            readonly property bool showCaptionBody: !!(appState && appState.transcriptionEnabled &&
                                                                       !appState.transcriptionDownloading &&
                                                                       (rxActiveNow || hasCaptionText))
                            readonly property string activeSpeakerCall: captionCardCol.hasCaptionText
                                ? ((appState && appState.transcriptCallsign !== "") ? appState.transcriptCallsign : (appState ? appState.data1 : ""))
                                : (captionCardCol.rxActiveNow ? (appState.data1 || appState.transcriptCallsign || "") : "")
                            readonly property string speakerText: activeSpeakerCall !== ""
                                ? (activeSpeakerCall + (appState && appState.fetchedFirstName !== "" ? " • " + appState.fetchedFirstName : ""))
                                : ""

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Rectangle {
                                    Layout.preferredWidth: 7
                                    Layout.preferredHeight: 7
                                    Layout.minimumWidth: 7
                                    Layout.maximumWidth: 7
                                    radius: 3.5
                                    color: !(appState && appState.transcriptionEnabled) ? t.textMuted
                                           : ((appState && appState.transcriptionDownloading) ? t.warning
                                           : (captionCardCol.rxActiveNow ? t.success : t.accent))
                                }

                                Label {
                                    text: qsTr("Live Captions")
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: (appState && appState.transcriptionEnabled) ? t.text : t.textMuted
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 44
                                    Layout.preferredWidth: implicitWidth
                                    Layout.maximumWidth: Math.ceil(implicitWidth) + 1
                                }

                                // BETA badge pill next to Live Captions
                                Rectangle {
                                    visible: !captionCardCol.narrowCard
                                    radius: 5
                                    implicitHeight: 16
                                    implicitWidth: betaBadgeLabel.implicitWidth + 8
                                    Layout.preferredWidth: implicitWidth
                                    Layout.minimumWidth: implicitWidth
                                    Layout.maximumWidth: implicitWidth
                                    color: t.tint(t.warning, 0.16)
                                    border.color: t.tint(t.warning, 0.55)
                                    border.width: 1
                                    Label {
                                        id: betaBadgeLabel
                                        anchors.centerIn: parent
                                        text: qsTr("BETA")
                                        font.pixelSize: 9
                                        font.bold: true
                                        color: t.warning
                                    }
                                }

                                // Inline speaker badge on wide cards (>= 480px); on compact phones
                                Rectangle {
                                    visible: captionCardCol.wideCard && captionCardCol.showCaptionBody && captionCardCol.speakerText !== ""
                                    radius: 6
                                    clip: true
                                    color: t.tint(t.accent, 0.18)
                                    implicitHeight: 18
                                    implicitWidth: Math.ceil(turnCallWideLabel.implicitWidth) + 14
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredWidth: implicitWidth
                                    Layout.maximumWidth: Math.min(220, implicitWidth)
                                    Label {
                                        id: turnCallWideLabel
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        verticalAlignment: Text.AlignVCenter
                                        text: captionCardCol.speakerText
                                        font.pixelSize: t.fsCaption - 1
                                        font.bold: true
                                        color: t.accent
                                        elide: Text.ElideRight
                                    }
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 2
                                }

                                // Language + Engine badge (tap to configure in Settings)
                                Rectangle {
                                    visible: !!(appState && appState.transcriptionEnabled) && !captionCardCol.narrowCard
                                    radius: 6
                                    clip: true
                                    color: t.surface3
                                    border.color: t.stroke
                                    border.width: 1
                                    implicitHeight: 28
                                    implicitWidth: Math.ceil(langPillLabel.implicitWidth) + 10
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 48
                                    Layout.preferredWidth: implicitWidth
                                    Layout.maximumWidth: Math.min(implicitWidth, Math.max(72, captionCardCol.width * 0.36))
                                    Label {
                                        id: langPillLabel
                                        anchors.fill: parent
                                        anchors.leftMargin: 5
                                        anchors.rightMargin: 5
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        text: {
                                            var lang = appState && appState.transcriptionLanguage
                                                       ? String(appState.transcriptionLanguage).toUpperCase()
                                                       : "AUTO"
                                            var eng = appState ? (appState.transcriptionEngine || "apple") : "apple"
                                            var tag = (eng === "parakeet")
                                                      ? qsTr("PARAKEET")
                                                      : ((eng === "apple" && appState && appState.transcriptionAppleSupported)
                                                         ? qsTr("NEURAL")
                                                         : qsTr("WHISPER"))
                                            return lang + " · " + tag
                                        }
                                        font.pixelSize: t.fsCaption - 1
                                        font.bold: true
                                        color: t.textMuted
                                        elide: Text.ElideRight
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app) page.app.navigate("settings")
                                    }
                                }

                                // Easy 1-tap enable/disable CC pill inside Live Activity
                                Rectangle {
                                    id: inlineCcToggle
                                    radius: 8
                                    implicitHeight: 28
                                    implicitWidth: inlineCcRow.implicitWidth + 12
                                    Layout.preferredWidth: implicitWidth
                                    Layout.minimumWidth: implicitWidth
                                    Layout.maximumWidth: implicitWidth
                                    color: (appState && appState.transcriptionEnabled) ? t.tint(t.accent, 0.24) : t.surface2
                                    border.color: (appState && appState.transcriptionEnabled) ? t.accent : t.stroke
                                    border.width: 1

                                    Row {
                                        id: inlineCcRow
                                        anchors.centerIn: parent
                                        spacing: 4
                                        Label {
                                            text: "\uf20a"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: (appState && appState.transcriptionEnabled) ? t.accent : t.textMuted
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            text: (appState && appState.transcriptionEnabled) ? qsTr("ON") : qsTr("OFF")
                                            font.pixelSize: t.fsCaption
                                            font.bold: true
                                            color: (appState && appState.transcriptionEnabled) ? t.text : t.textMuted
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app && page.appState) page.app.setTranscriptionEnabled(!page.appState.transcriptionEnabled)
                                    }
                                }
                            }

                            // Dedicated speaker pill row on compact phone screens (< 480px) so
                            Rectangle {
                                visible: !captionCardCol.wideCard && captionCardCol.showCaptionBody && captionCardCol.speakerText !== ""
                                radius: 6
                                clip: true
                                color: t.tint(t.accent, 0.18)
                                implicitHeight: 20
                                implicitWidth: Math.min(captionCardCol.width, Math.ceil(turnCallCompactLabel.implicitWidth) + 15)
                                Layout.maximumWidth: captionCardCol.width

                                Label {
                                    id: turnCallCompactLabel
                                    anchors.fill: parent
                                    anchors.leftMargin: 7
                                    anchors.rightMargin: 7
                                    verticalAlignment: Text.AlignVCenter
                                    text: captionCardCol.speakerText
                                    font.pixelSize: t.fsCaption - 1
                                    font.bold: true
                                    color: t.accent
                                    elide: Text.ElideRight
                                }
                            }

                            // Model download progress bar (when downloading on first enable)
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: !!(appState && appState.transcriptionDownloading)
                                spacing: 4

                                Label {
                                    Layout.fillWidth: true
                                    text: appState ? appState.transcriptionStatus : qsTr("Downloading model…")
                                    font.pixelSize: t.fsCaption
                                    color: t.warning
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: t.surface2
                                    Rectangle {
                                        width: parent.width * Math.max(0.04, (appState ? appState.transcriptionDownloadProgress : 0))
                                        height: parent.height
                                        radius: 3
                                        color: t.accent
                                        Behavior on width { NumberAnimation { duration: 120 } }
                                    }
                                }
                            }

                            // Adaptive 2-3 line turn transcript viewport (auto-hides 4s after last RX)
                            Flickable {
                                id: turnTranscriptFlick
                                Layout.fillWidth: true
                                visible: captionCardCol.showCaptionBody
                                readonly property real lineH: Math.max(18, captionMetrics.lineSpacing)
                                Layout.preferredHeight: lineH * page.captionLines + 4
                                contentWidth: width
                                contentHeight: turnTranscriptLabel.implicitHeight
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds

                                function scrollToLatest() {
                                    if (contentHeight > height) {
                                        contentY = contentHeight - height
                                    } else {
                                        contentY = 0
                                    }
                                }

                                Label {
                                    id: turnTranscriptLabel
                                    width: turnTranscriptFlick.width
                                    text: {
                                        if (!appState || !appState.connected) return ""
                                        if (appState.currentTranscript && appState.currentTranscript !== "")
                                            return appState.currentTranscript
                                        if (captionCardCol.rxActiveNow)
                                            return qsTr("Listening…")
                                        return ""
                                    }
                                    font.pixelSize: t.fsBody
                                    color: captionCardCol.hasCaptionText ? t.text : t.textMuted
                                    wrapMode: Text.WordWrap
                                    onImplicitHeightChanged: Qt.callLater(turnTranscriptFlick.scrollToLatest)
                                }

                                Connections {
                                    target: page.appState
                                    function onCurrentTranscriptChanged() {
                                        Qt.callLater(turnTranscriptFlick.scrollToLatest)
                                    }
                                }
                            }

                            // Small disclaimer that automated transcription can mishear radio audio
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Automated captions can mishear weak signals. Always confirm callsigns and signal reports by ear.")
                                font.pixelSize: t.fsCaption - 2
                                color: t.textMuted
                                opacity: 0.78
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Data fields
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Repeater {
                            model: 6
                            delegate: RowLayout {
                                required property int index
                                readonly property string labelText: appState ? appState["label" + (index + 1)] : ""
                                readonly property string valueText: appState ? appState["data" + (index + 1)] : ""
                                visible: labelText !== "" || valueText !== ""
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 10
                                Label {
                                    text: labelText !== "" ? labelText : qsTr("Info %1").arg(index + 1)
                                    color: t.textMuted
                                    font.pixelSize: t.fsCaption
                                    Layout.preferredWidth: 78
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: valueText !== "" ? valueText : "-"
                                    font.pixelSize: t.fsSmall + 1
                                    font.family: (appState && appState.monoFont !== "") ? appState.monoFont : font.family
                                    color: valueText !== "" ? t.text : t.textMuted
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    // Last heard
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!(appState && appState.lastHeardEntries && appState.lastHeardEntries.length > 0)
                        spacing: 2

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: t.stroke }

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Last Heard"); font.pixelSize: t.fsCaption; color: t.textMuted; Layout.fillWidth: true }
                            Button {
                                text: qsTr("QSO log")
                                flat: true
                                font.pixelSize: t.fsCaption
                                onClicked: if (page.app) page.app.navigate("qso")
                            }
                        }

                        Repeater {
                            model: appState ? (appState.lastHeardEntries || []) : []
                            delegate: Rectangle {
                                id: heardRow
                                required property var modelData
                                required property int index
                                readonly property int heardId: modelData.dmrID || 0
                                readonly property bool canMessage: heardId > 0 &&
                                                                   heardId !== parseInt(appState.dmrid || "0")
                                readonly property bool onAirNow: !!(appState && appState.data1 !== "" &&
                                                                    parseInt(appState.data2 || "0") === heardId)
                                readonly property int unread: page.unreadFrom(heardId)

                                Layout.fillWidth: true
                                implicitHeight: 46
                                radius: 10
                                color: onAirNow ? t.tint(t.success, 0.10) : "transparent"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 2
                                    spacing: 10

                                    ChatAvatar {
                                        size: 30
                                        glyph: heardRow.onAirNow ? "\uf130" : "\uf007"
                                        tone: heardRow.onAirNow ? t.success : t.accent
                                        opacity: 1.0 - heardRow.index * 0.08
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 0
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                text: heardRow.modelData.callsign !== "" ? heardRow.modelData.callsign
                                                                                         : qsTr("ID %1").arg(heardRow.heardId)
                                                font.pixelSize: t.fsBody
                                                font.bold: true
                                                color: t.text
                                                elide: Text.ElideRight
                                            }
                                            Label {
                                                visible: heardRow.modelData.tgid > 0 && page.wide
                                                text: "TG " + heardRow.modelData.tgid
                                                font.pixelSize: t.fsCaption
                                                color: t.textMuted
                                            }
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: text !== ""
                                            text: heardRow.modelData.fname +
                                                  ((heardRow.modelData.fname !== "" && heardRow.modelData.country !== "") ? " • " : "") +
                                                  heardRow.modelData.country
                                            font.pixelSize: t.fsCaption
                                            color: t.textMuted
                                            elide: Text.ElideRight
                                        }
                                    }

                                    RoundIconButton {
                                        visible: heardRow.canMessage
                                        implicitWidth: 38
                                        implicitHeight: 38
                                        glyphSize: 15
                                        glyph: heardRow.unread > 0 ? "\uf0e0" : "\uf003"
                                        tone: heardRow.unread > 0 ? t.accent : t.textMuted
                                        badge: heardRow.unread
                                        accessibleName: qsTr("Message %1").arg(heardRow.modelData.callsign || heardRow.heardId)
                                        onClicked: if (page.app)
                                            page.app.openMessageTo(heardRow.heardId, heardRow.modelData.callsign, false)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // COMPACT RESPONSIVE HOMEPAGE FOOTER CARD (Made with ♥ VU3LVO + support statement
        Rectangle {
            id: homeFooter
            x: contentRow.x
            y: contentRow.y + contentRow.implicitHeight + 12
            width: contentRow.width
            implicitHeight: (sideBySide ? Math.max(footerText.implicitHeight, footerButtons.implicitHeight)
                                        : footerText.implicitHeight + 10 + footerButtons.implicitHeight) + 24
            radius: t.rMd
            clip: true
            color: t.surface2
            border.color: t.stroke
            border.width: 1

            // Decided after the layout pass: flipping it mid-pass changes several layout
            property bool sideBySide: false
            function updateArrangement() {
                var v = width >= 560
                if (v !== sideBySide) sideBySide = v
            }
            onWidthChanged: Qt.callLater(updateArrangement)

            // Positioner + anchors: wrapped text inside nested grid layouts needs a third layout pass
            Column {
                id: footerText
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: homeFooter.sideBySide ? footerButtons.left : parent.right
                anchors.rightMargin: homeFooter.sideBySide ? 14 : 12
                y: homeFooter.sideBySide ? Math.round((homeFooter.height - height) / 2) : 12
                spacing: 4

                Item {
                    width: parent.width
                    height: sigRow.implicitHeight

                    Row {
                        id: sigRow
                        x: homeFooter.sideBySide ? 0 : (parent.width - width) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Label {
                            text: qsTr("Made with")
                            font.pixelSize: t.fsSmall
                            color: t.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Label {
                            text: "\uf004"
                            font.family: "FontAwesome"
                            font.pixelSize: 11
                            color: t.danger
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Label {
                            text: qsTr("by")
                            font.pixelSize: t.fsSmall
                            color: t.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Rectangle {
                            radius: 6
                            implicitHeight: 20
                            implicitWidth: vuCallLabel.implicitWidth + 12
                            color: t.tint(t.accent, 0.16)
                            border.color: t.tint(t.accent, 0.45)
                            border.width: 1
                            anchors.verticalCenter: parent.verticalCenter

                            Label {
                                id: vuCallLabel
                                anchors.centerIn: parent
                                text: "VU3LVO"
                                font.pixelSize: t.fsCaption
                                font.bold: true
                                color: t.accent
                            }
                        }
                    }
                }

                Label {
                    width: parent.width
                    horizontalAlignment: homeFooter.sideBySide ? Text.AlignLeft : Text.AlignHCenter
                    text: qsTr("Built for the amateur radio community. Your support keeps DroidStar free and on the air for everyone.")
                    font.pixelSize: t.fsCaption
                    color: t.textMuted
                    opacity: 0.9
                    wrapMode: Text.WordWrap
                }
            }

            // Support buttons: equal split under the text on phones, compact pills
            RowLayout {
                id: footerButtons
                anchors.right: parent.right
                anchors.rightMargin: 12
                // Explicit width: dropping a left anchor at runtime would keep the stretched width
                width: homeFooter.sideBySide ? implicitWidth : homeFooter.width - 24
                y: homeFooter.sideBySide ? Math.round((homeFooter.height - height) / 2)
                                         : footerText.y + footerText.height + 10
                spacing: 8

                Repeater {
                    model: [
                        {
                            label: qsTr("Buy Me a Coffee"),
                            icon: "\uf0f4",
                            url: "https://buymeacoffee.com/rohithz",
                            tintColor: "#F59E0B",
                            iconColor: "#FBBF24"
                        },
                        {
                            label: qsTr("PayPal Donation"),
                            icon: "\uf1ed",
                            url: "https://www.paypal.com/ncp/payment/NU89529268M2W",
                            tintColor: "#38BDF8",
                            iconColor: "#38BDF8"
                        }
                    ]

                    delegate: Rectangle {
                        id: miniDonatePill
                        required property var modelData
                        readonly property color accentTone: modelData.tintColor
                        // Natural size from the texts' implicit widths, never from the clamped label width
                        readonly property real naturalWidth: donateIcon.implicitWidth + 6 + donateLabel.implicitWidth + 24

                        Layout.fillWidth: !homeFooter.sideBySide
                        Layout.preferredWidth: homeFooter.sideBySide ? naturalWidth : 1
                        Layout.minimumWidth: homeFooter.sideBySide ? naturalWidth : 0
                        Layout.preferredHeight: 40
                        implicitHeight: 40
                        implicitWidth: naturalWidth
                        radius: 10
                        clip: true
                        color: t.tint(accentTone, miniDonateMouse.pressed ? 0.28 : 0.15)
                        border.color: t.tint(accentTone, miniDonateMouse.pressed ? 0.85 : 0.55)
                        border.width: 1
                        scale: miniDonateMouse.pressed ? 0.98 : 1.0
                        Behavior on scale { NumberAnimation { duration: t.durFast } }
                        Behavior on color { ColorAnimation { duration: t.durFast } }

                        Row {
                            id: btnInnerRow
                            anchors.centerIn: parent
                            spacing: 6

                            Label {
                                id: donateIcon
                                text: miniDonatePill.modelData.icon
                                font.family: "FontAwesome"
                                font.pixelSize: 12
                                color: miniDonatePill.modelData.iconColor
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Label {
                                id: donateLabel
                                width: Math.min(implicitWidth,
                                                Math.max(0, miniDonatePill.width - 24 - donateIcon.implicitWidth - 6))
                                text: miniDonatePill.modelData.label
                                font.pixelSize: t.fsSmall
                                font.bold: true
                                color: t.text
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: miniDonateMouse
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(miniDonatePill.modelData.url)
                        }
                    }
                }
            }
        }
    }

    // SEARCHABLE RECENT & FAVOURITE TALKGROUPS SHEET
    Popup {
        id: recentTgSheet
        parent: Overlay.overlay
        modal: true
        dim: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string filterTab: "all" // "all", "fav", "recent"
        property var filteredModel: []

        readonly property var win: page.Window.window
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
            if (!page.droidstarRef || typeof page.droidstarRef.searchTalkgroups !== "function") {
                recentTgSheet.filteredModel = []
                return
            }
            var q = recentSearchInput.text.trim()
            var raw = page.droidstarRef.searchTalkgroups(q, 80)
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
            page.loadTalkgroup(v, !!asPrivate)
        }

        Connections {
            target: page.appState
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
                        visible: page.mode === "DMR"
                        flat: true
                        text: qsTr("Scan")
                        font.pixelSize: t.fsCaption
                        onClicked: {
                            recentTgSheet.close()
                            if (page.app) page.app.navigate("scan")
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
                        text: qsTr("Favourites (%1)").arg(page.appState && page.appState.favouriteTgids ? page.appState.favouriteTgids.length : 0)
                        glyph: "\uf005"
                        activeColor: t.warning
                        checked: recentTgSheet.filterTab === "fav"
                        onClicked: { recentTgSheet.filterTab = "fav"; recentTgSheet.rebuildList() }
                    }
                    ChipToggle {
                        text: qsTr("Recent (%1)").arg(page.appState && page.appState.recentTgids ? page.appState.recentTgids.length : 0)
                        glyph: "\uf1da"
                        checked: recentTgSheet.filterTab === "recent"
                        onClicked: { recentTgSheet.filterTab = "recent"; recentTgSheet.rebuildList() }
                    }

                    Item { Layout.fillWidth: true }

                    // Clear Recent History
                    ToolButton {
                        visible: !!(page.appState && page.appState.recentTgids && page.appState.recentTgids.length > 0)
                        text: "\uf1f8"
                        font.family: "FontAwesome"
                        font.pixelSize: 13
                        palette.buttonText: t.danger
                        Accessible.name: qsTr("Clear recent talkgroups")
                        onClicked: {
                            if (page.app) page.app.clearRecentTalkgroups()
                            recentTgSheet.rebuildList()
                        }
                    }
                }

                // Quick DMR Actions Row (Unlink 4000 & Parrot 9990)
                RowLayout {
                    Layout.fillWidth: true
                    visible: page.mode === "DMR" && recentSearchInput.text.trim() === ""
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
                        readonly property bool isCurrent: !!(page.appState && String(page.appState.dmrtgid) === rowTgid)
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
                                        if (page.app && tgRowItem.rowTgid !== "") {
                                            page.app.toggleFavouriteTalkgroup(tgRowItem.rowTgid)
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
}
