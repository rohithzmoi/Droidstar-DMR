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
    title: qsTr("Main")
    padding: 0

    // Renamed to avoid QML reserved property conflicts
    required property var droidstarRef
    required property var appState
    required property var logHandlerRef
    property var vuidUpdaterRef: null

    Tokens { id: t }
    
    // Store references to ComboBoxes for later updates
    property var modeComboBoxRef: null
    property var hostComboBoxRef: null

    // React to state changes without polling (faster + avoids log spam)
    Connections {
        target: page.appState
        enabled: !!page.appState

        function onModeChanged() {
            if (page.modeComboBoxRef && page.modeComboBoxRef.loaded) page.modeComboBoxRef.updateFromState()
        }
        function onSelectedHostChanged() {
            if (page.hostComboBoxRef && page.hostComboBoxRef.loaded) page.hostComboBoxRef.updateSelection()
        }
        function onHostsModelChanged() {
            if (page.hostComboBoxRef && page.hostComboBoxRef.loaded) page.hostComboBoxRef.updateSelection()
        }
    }
    
    // Ensure ComboBoxes update when page becomes visible
    onVisibleChanged: {
        if (visible) {
            Qt.callLater(function() {
                if (page.modeComboBoxRef) page.modeComboBoxRef.updateFromState()
                if (page.hostComboBoxRef) page.hostComboBoxRef.updateSelection()
            })
            // Also refresh "Last Heard" when returning to Main
            Qt.callLater(function() { refreshLastHeardFromLog() })
        }
    }

    function refreshLastHeardFromLog() {
        if (!page.appState || !page.logHandlerRef) return
        var saved = page.logHandlerRef.loadLog("logs.json")
        if (!saved || saved.length === undefined) return

        function fmt(entry) {
            if (!entry) return ""
            var cs = entry.callsign || ""
            var name = entry.fname || ""
            var ctry = entry.country || ""
            var parts = []
            if (cs) parts.push(cs)
            if (name) parts.push(name)
            if (ctry) parts.push(ctry)
            return parts.join(" - ")
        }

        page.appState.lastHeard1 = saved.length > 0 ? fmt(saved[0]) : ""
        page.appState.lastHeard2 = saved.length > 1 ? fmt(saved[1]) : ""
    }

    function collapseConnectionIfConnected() {
        if (page.appState && page.appState.connected) page.connectionExpanded = false
    }

    Component.onCompleted: {
        refreshLastHeardFromLog()
        // If we're already connected when this page is created, auto-collapse immediately.
        collapseConnectionIfConnected()
    }

    // Live updates: whenever QSO log is saved/cleared, refresh Last Heard immediately.
    Connections {
        target: page.logHandlerRef
        enabled: !!page.logHandlerRef
        function onLogSaved(fileName) {
            if (fileName === "logs.json") refreshLastHeardFromLog()
        }
        function onLogCleared(fileName) {
            if (fileName === "logs.json") refreshLastHeardFromLog()
        }
    }

    // Local UI models
    property var modesModel: ["REF", "DCS", "XRF", "YSF", "FCS", "DMR", "P25", "NXDN", "M17", "IAX"]
    property var modulesModel: ["A", "B", "C", "D", "E", "F", "G"]
    property var slotsModel: ["Slot 1", "Slot 2"]
    property var ccsModel: ["CC1", "CC2", "CC3", "CC4", "CC5", "CC6", "CC7", "CC8", "CC9", "CC10", "CC11", "CC12", "CC13", "CC14", "CC15"]
    property var m17CanModel: ["0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15"]

    // Section expansion states
    property bool connectionExpanded: true
    property bool txControlExpanded: true
    property bool liveExpanded: true

    // UX: once connected, keep Connection collapsed (legacy-style focus on live status)
    Connections {
        target: page.appState
        enabled: !!page.appState
        function onConnectedChanged() {
            collapseConnectionIfConnected()
        }
    }

    function refreshRecentTgids() {
        if (appState) appState.recentTgids = droidstarRef.loadRecentTGIDs()
    }

    function connectOrDisconnect() {
        if (!appState || !droidstarRef) return

        // If we're currently connecting, a click should cancel the attempt.
        // Backend `process_connect()` toggles to DISCONNECTED for any non-DISCONNECTED state.
        if (appState.connecting) {
            droidstarRef.process_connect()
            return
        }

        if (appState.connected) {
            droidstarRef.process_connect()
            return
        }

        // Ensure we have a host selection (backend requires a valid saved host).
        if ((!appState.selectedHost || appState.selectedHost === "") && appState.hostsModel && appState.hostsModel.length > 0) {
            appState.selectedHost = appState.hostsModel[0]
        }

        // Push all state to backend before connecting
        droidstarRef.set_callsign(appState.callsign)
        droidstarRef.set_dmrid(appState.dmrid)
        droidstarRef.set_protocol(appState.mode)
        droidstarRef.set_module(appState.module)
        droidstarRef.set_essid(appState.essid)
        droidstarRef.set_bm_password(appState.bmPass)
        droidstarRef.set_tgif_password(appState.tgifPass)
        droidstarRef.set_latitude(appState.latitude)
        droidstarRef.set_longitude(appState.longitude)
        droidstarRef.set_location(appState.location)
        droidstarRef.set_description(appState.description)
        droidstarRef.set_url(appState.url)
        droidstarRef.set_swid(appState.swid)
        droidstarRef.set_pkgid(appState.pkgid)
        droidstarRef.set_dmr_options(appState.dmrOptions)
        droidstarRef.set_dmrtgid(appState.dmrtgid)
        // Legacy parity: these are used for REF/XRF/DCS (and stored in settings),
        // but don't overwrite backend with empty values.
        if (appState.mycall && appState.mycall !== "") droidstarRef.set_mycall(appState.mycall)
        if (appState.urcall && appState.urcall !== "") droidstarRef.set_urcall(appState.urcall)
        if (appState.rptr1 && appState.rptr1 !== "") droidstarRef.set_rptr1(appState.rptr1)
        if (appState.rptr2 && appState.rptr2 !== "") droidstarRef.set_rptr2(appState.rptr2)
        if (appState.usrtxt && appState.usrtxt !== "") droidstarRef.set_usrtxt(appState.usrtxt)
        droidstarRef.set_txtimeout(appState.txTimeout)
        droidstarRef.set_modemRxFreq(appState.modemRxFreq)
        droidstarRef.set_modemTxFreq(appState.modemTxFreq)
        droidstarRef.set_modemRxOffset(appState.modemRxOffset)
        droidstarRef.set_modemTxOffset(appState.modemTxOffset)
        droidstarRef.set_modemRxDCOffset(appState.modemRxDCOffset)
        droidstarRef.set_modemTxDCOffset(appState.modemTxDCOffset)
        droidstarRef.set_modemRxLevel(appState.modemRxLevel)
        droidstarRef.set_modemTxLevel(appState.modemTxLevel)
        droidstarRef.set_modemRFLevel(appState.modemRFLevel)
        droidstarRef.set_modemTxDelay(appState.modemTxDelay)
        droidstarRef.set_modemCWIdTxLevel(appState.modemCWIdTxLevel)
        droidstarRef.set_modemDstarTxLevel(appState.modemDstarTxLevel)
        droidstarRef.set_modemDMRTxLevel(appState.modemDMRTxLevel)
        droidstarRef.set_modemYSFTxLevel(appState.modemYSFTxLevel)
        droidstarRef.set_modemP25TxLevel(appState.modemP25TxLevel)
        droidstarRef.set_modemNXDNTxLevel(appState.modemNXDNTxLevel)
        droidstarRef.set_modemBaud(appState.modemBaud)
        droidstarRef.set_ipv6(appState.ipv6)
        droidstarRef.set_xrf2ref(appState.xrf2ref)
        droidstarRef.set_toggletx(appState.toggleTx)

        // Legacy parity: device selections are applied before connecting.
        droidstarRef.set_vocoder(appState.vocoder)
        droidstarRef.set_modem(appState.modem)
        droidstarRef.set_playback(appState.playback)
        droidstarRef.set_capture(appState.capture)
        droidstarRef.set_dmr_pc(appState.privateCall ? 1 : 0)

        if (appState.selectedHost && appState.selectedHost !== "") {
            droidstarRef.set_dst(appState.selectedHost)
            droidstarRef.process_host_change(appState.selectedHost)
        }

        droidstarRef.process_connect()
    }

    background: Rectangle { color: t.bg }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 10
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 10

            // ─────────────────────────────────────────────────────────
            // CONNECTION SECTION
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Connection")
                statusText: (appState && appState.connected) ? qsTr("Connected")
                            : ((appState && appState.connecting) ? qsTr("Connecting…") : "")
                statusWhenCollapsedOnly: true
                expanded: page.connectionExpanded
                onExpandedChanged: page.connectionExpanded = expanded

                content: ColumnLayout {
                    spacing: 10

                    // Mode & Host row
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: qsTr("Mode"); font.pixelSize: 10; opacity: 0.6 }
                            ComboBox {
                                id: modeComboBox
                                Layout.fillWidth: true
                                model: page.modesModel
                                property bool loaded: false
                                property bool updatingFromState: false
                                
                                Component.onCompleted: {
                                    loaded = true
                                    page.modeComboBoxRef = modeComboBox
                                    updateFromState()
                                }
                                
                                function updateFromState() {
                                    if (!appState || !loaded || updatingFromState) return
                                    updatingFromState = true
                                    var idx = model.indexOf(appState.mode)
                                    if (idx >= 0) {
                                        if (idx !== currentIndex) {
                                            currentIndex = idx
                                        }
                                    } else {
                                        currentIndex = 0
                                    }
                                    updatingFromState = false
                                }
                                
                                // Mode updates are handled by the page-level Timer watcher
                                // This ensures updates happen when the page is visible
                                
                                onActivated: {
                                    if (!appState || !loaded || updatingFromState) return
                                    appState.mode = currentText
                                    // process_mode_change will save the mode via save_settings()
                                    droidstarRef.process_mode_change(currentText)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: qsTr("Host"); font.pixelSize: 10; opacity: 0.6 }
                            ComboBox {
                                id: hostComboBox
                                Layout.fillWidth: true
                                model: appState ? (appState.hostsModel || []) : []
                                property bool loaded: false
                                property bool updatingFromState: false
                                displayText: currentIndex === -1 ? qsTr("Host...") : currentText
                                
                                Component.onCompleted: {
                                    page.hostComboBoxRef = hostComboBox
                                    loaded = true
                                    updateSelection()
                                }
                                
                                // Watch for model changes (QML will detect when the binding updates)
                                onModelChanged: {
                                    if (loaded && !updatingFromState) {
                                        Qt.callLater(function() {
                                            if (hostComboBox.loaded && !hostComboBox.updatingFromState) {
                                                hostComboBox.updateSelection()
                                            }
                                        })
                                    }
                                }
                                
                                function updateSelection() {
                                    if (!appState || !loaded || updatingFromState) return
                                    if (model.length === 0) {
                                        currentIndex = -1
                                        return
                                    }
                                    updatingFromState = true
                                    var idx = model.indexOf(appState.selectedHost)
                                    if (idx >= 0 && idx !== currentIndex) {
                                        currentIndex = idx
                                    } else if (idx < 0 && model.length > 0) {
                                        currentIndex = 0
                                        if (appState) appState.selectedHost = model[0]
                                    }
                                    updatingFromState = false
                                }
                                
                                onActivated: {
                                    if (!appState || !loaded || updatingFromState) return
                                    appState.selectedHost = currentText
                                    droidstarRef.set_dst(currentText)
                                    if (!droidstarRef.get_modelchange()) {
                                        droidstarRef.process_host_change(currentText)
                                    }
                                }
                            }
                        }
                    }

                    // Mode-specific controls
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 6
                        rowSpacing: 6
                        visible: !!(appState && (appState.mode === "REF" || appState.mode === "DCS" || appState.mode === "XRF" || appState.mode === "M17" || appState.mode === "DMR"))

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: !!(appState && (appState.mode === "REF" || appState.mode === "DCS" || appState.mode === "XRF" || appState.mode === "M17"))
                            spacing: 2
                            Label { text: qsTr("Module"); font.pixelSize: 10; opacity: 0.6 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: page.modulesModel
                                currentIndex: appState ? Math.max(0, model.indexOf(appState.module)) : 0
                                onActivated: {
                                    if (!appState) return
                                    appState.module = currentText
                                    droidstarRef.set_module(currentText)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: !!(appState && appState.mode === "DMR")
                            spacing: 2
                            Label { text: qsTr("Slot"); font.pixelSize: 10; opacity: 0.6 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: page.slotsModel
                                onActivated: droidstarRef.set_slot(currentIndex)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: !!(appState && appState.mode === "DMR")
                            spacing: 2
                            Label { text: qsTr("CC"); font.pixelSize: 10; opacity: 0.6 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: page.ccsModel
                                onActivated: droidstarRef.set_cc(currentIndex)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: !!(appState && appState.mode === "M17")
                            spacing: 2
                            Label { text: qsTr("CAN"); font.pixelSize: 10; opacity: 0.6 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: page.m17CanModel
                                onActivated: {
                                    if (!appState) return
                                    appState.modemM17CAN = currentText
                                    droidstarRef.set_modemM17CAN(currentText)
                                }
                            }
                        }
                    }

                    // TGID row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: !!(appState && (appState.mode === "DMR" || appState.mode === "P25" || appState.mode === "NXDN"))

                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Talkgroup ID")
                            inputMethodHints: Qt.ImhDigitsOnly
                            text: appState ? appState.dmrtgid : ""
                            onEditingFinished: {
                                if (!appState) return
                                appState.dmrtgid = text
                                droidstarRef.set_dmrtgid(text)
                                droidstarRef.tgid_text_changed(text)
                                droidstarRef.addRecentTGID(text)
                                refreshRecentTgids()
                            }
                        }

                        ComboBox {
                            Layout.preferredWidth: 90
                            model: appState ? appState.recentTgids : []
                            displayText: qsTr("Recent")
                            onActivated: {
                                if (!appState) return
                                appState.dmrtgid = currentText
                                droidstarRef.set_dmrtgid(currentText)
                                droidstarRef.tgid_text_changed(currentText)
                            }
                        }

                        Switch {
                            visible: !!(appState && appState.mode === "DMR")
                            text: qsTr("Pvt")
                            checked: appState ? appState.privateCall : false
                            onToggled: droidstarRef.set_dmr_pc(checked)
                        }
                    }

                    // IAX DTMF
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !!(appState && appState.mode === "IAX")
                        spacing: 6
                        TextField {
                            id: dtmfField
                            Layout.fillWidth: true
                            placeholderText: qsTr("DTMF digits")
                        }
                        Button {
                            text: qsTr("Send")
                            onClicked: droidstarRef.dtmf_send_clicked(dtmfField.text)
                        }
                    }

                    // Connect button
                    Button {
                        Layout.fillWidth: true
                        height: 44
                        text: (appState && appState.connecting) ? qsTr("Cancel")
                              : ((appState && appState.connected) ? qsTr("Disconnect") : qsTr("Connect"))
                        font.bold: true
                        enabled: !!appState

                        background: Rectangle {
                            radius: 12
                            color: (appState && (appState.connected || appState.connecting)) ? t.danger : t.accent
                        }

                        onClicked: page.connectOrDisconnect()
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // TX CONTROL SECTION
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("TX Control")
                // Show the same "name • country" highlight next to the section title.
                statusText: (appState && appState.fetchedFirstName !== "")
                            ? (appState.fetchedFirstName + (appState.fetchedCountry !== "" ? " • " + appState.fetchedCountry : ""))
                            : ""
                statusWhenCollapsedOnly: false
                expanded: page.txControlExpanded
                onExpandedChanged: page.txControlExpanded = expanded

                content: ColumnLayout {
                    spacing: 10

                    // Audio level meter
                    Rectangle {
                        Layout.fillWidth: true
                        height: 14
                        radius: 7
                        color: "#0F172A"
                        border.color: t.stroke
                        border.width: 1

                        Rectangle {
                            height: parent.height - 2
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 1
                            radius: parent.radius - 1
                            width: Math.max(6, (parent.width - 2) * ((appState ? appState.outputLevel : 0) / 32767.0))
                            color: (appState && appState.txActive) ? t.danger : t.accent
                        }
                    }

                    // Mic gain slider
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: qsTr("Mic"); font.pixelSize: 11; opacity: 0.6 }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.0
                            to: 1.0
                            value: appState ? appState.micGain : 0.5
                            onMoved: droidstarRef.set_input_volume(value)
                        }
                        Label {
                            text: Math.round((appState ? appState.micGain : 0.5) * 100) + "%"
                            font.pixelSize: 11
                            opacity: 0.8
                            Layout.preferredWidth: 36
                        }
                    }

                    // Quick toggles
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Repeater {
                            model: [
                                { label: "SWTX", prop: "swtx" },
                                { label: "SWRX", prop: "swrx" },
                                { label: "AGC",  prop: "agc" }
                            ]

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                height: 36
                                radius: 8
                                color: (appState && appState[modelData.prop]) ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.25) : t.surface2
                                border.color: (appState && appState[modelData.prop]) ? t.accent : t.stroke
                                border.width: 1

                                Label {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    font.pixelSize: 12
                                    font.bold: !!(appState && appState[modelData.prop])
                                    opacity: (appState && appState[modelData.prop]) ? 1 : 0.7
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (!appState) return
                                        var newVal = !appState[modelData.prop]
                                        if (modelData.prop === "swtx") droidstarRef.set_swtx(newVal)
                                        else if (modelData.prop === "swrx") droidstarRef.set_swrx(newVal)
                                        else if (modelData.prop === "agc") droidstarRef.set_agc(newVal)
                                    }
                                }
                            }
                        }
                    }

                    // Big TX Button
                    Button {
                        Layout.fillWidth: true
                        height: 60
                        enabled: appState ? appState.txEnabled : false
                        text: (appState && appState.txActive) ? qsTr("TX — ON AIR") : qsTr("TX")
                        font.bold: true
                        font.pixelSize: 16

                        background: Rectangle {
                            radius: 14
                            color: (appState && appState.txActive) ? "#B91C1C" : "#1D4ED8"
                            border.color: (appState && appState.txActive) ? "#EF4444" : "#3B82F6"
                            border.width: (appState && appState.txActive) ? 2 : 0
                        }

                        onClicked: {
                            if (!appState) return
                            if (appState.toggleTx) {
                                appState.txActive = !appState.txActive
                                droidstarRef.click_tx(appState.txActive)
                            }
                        }
                        onPressed: {
                            if (appState && !appState.toggleTx) droidstarRef.press_tx()
                        }
                        onReleased: {
                            if (appState && !appState.toggleTx) droidstarRef.release_tx()
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: (appState && appState.toggleTx) ? qsTr("Tap to toggle TX") : qsTr("Hold to transmit (PTT)")
                        font.pixelSize: 10
                        opacity: 0.5
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // LIVE INFO SECTION
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Live Activity")
                expanded: page.liveExpanded
                onExpandedChanged: page.liveExpanded = expanded

                content: ColumnLayout {
                    spacing: 8

                    // Status badge
                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        radius: 8
                        color: (appState && appState.connected) ? Qt.rgba(t.success.r, t.success.g, t.success.b, 0.15) : Qt.rgba(t.danger.r, t.danger.g, t.danger.b, 0.1)
                        border.color: (appState && appState.connected) ? t.success : t.stroke
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: (appState && appState.connected) ? t.success : t.danger
                            }
                            Label {
                                text: {
                                    if (!appState) return qsTr("Disconnected")
                                    if (appState.netstatus !== "") return appState.netstatus
                                    return appState.connected ? qsTr("Connected") : qsTr("Disconnected")
                                }
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Data fields
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 4

                        Label { text: appState && appState.label1 !== "" ? appState.label1 : "Info 1"; opacity: 0.5; font.pixelSize: 10 }
                        Label { text: appState ? appState.data1 : ""; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Label { text: appState && appState.label2 !== "" ? appState.label2 : "Info 2"; opacity: 0.5; font.pixelSize: 10 }
                        Label { text: appState ? appState.data2 : ""; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Label { text: appState && appState.label3 !== "" ? appState.label3 : "Info 3"; opacity: 0.5; font.pixelSize: 10 }
                        Label { text: appState ? appState.data3 : ""; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Label { text: appState && appState.label4 !== "" ? appState.label4 : "Info 4"; opacity: 0.5; font.pixelSize: 10 }
                        Label { text: appState ? appState.data4 : ""; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Label { text: appState && appState.label5 !== "" ? appState.label5 : "Info 5"; opacity: 0.5; font.pixelSize: 10 }
                        Label { text: appState ? appState.data5 : ""; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Label { text: appState && appState.label6 !== "" ? appState.label6 : "Info 6"; opacity: 0.5; font.pixelSize: 10 }
                        Label { text: appState ? appState.data6 : ""; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                    }

                    // Looked-up name
                    Rectangle {
                        Layout.fillWidth: true
                        visible: !!(appState && appState.fetchedFirstName !== "")
                        height: 36
                        radius: 8
                        color: Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.1)
                        border.color: t.accent
                        border.width: 1

                        Label {
                            anchors.fill: parent
                            anchors.margins: 8
                            text: appState ? (appState.fetchedFirstName + (appState.fetchedCountry !== "" ? " • " + appState.fetchedCountry : "")) : ""
                            font.pixelSize: 12
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Last heard
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!(appState && appState.lastHeard1 !== "")
                        spacing: 2

                        Label { text: qsTr("Last Heard"); font.pixelSize: 10; opacity: 0.5 }
                        Label { text: appState ? appState.lastHeard1 : ""; font.pixelSize: 11; opacity: 0.85; wrapMode: Text.WordWrap }
                        Label { text: appState ? appState.lastHeard2 : ""; font.pixelSize: 11; opacity: 0.7; wrapMode: Text.WordWrap; visible: !!(appState && appState.lastHeard2 !== "") }
                    }
                }
            }

            Item { Layout.preferredHeight: 10 }
        }
    }
}
