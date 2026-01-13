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
import QtQuick.Dialogs

import "../components"

Page {
    id: page
    title: qsTr("QSO")
    padding: 12

    // injected
    required property var logHandlerRef
    required property var vuidUpdaterRef
    required property var appState
    required property var droidstarRef

    property string logFileName: "logs.json"
    property string savedFilePath: ""
    property int latestSerialNumber: 0

    ListModel { id: logModel }
    property string _lastKeyLocal: ""

    function _normCallsign(s) {
        if (s === undefined || s === null) return ""
        return ("" + s).trim().toUpperCase()
    }

    function _selfDmrId() {
        if (!page.appState) return 0
        var n = parseInt(page.appState.dmrid || "0")
        return (isNaN(n) || n < 0) ? 0 : n
    }

    function _isSelfContact(dmrId, callsign) {
        // Treat as "self" if EITHER DMR ID OR callsign matches configured identity.
        if (!page.appState) return false
        var myDmr = _selfDmrId()
        var myCs = _normCallsign(page.appState.callsign || "")
        
        // Check DMR ID match
        var d = parseInt(dmrId || "0")
        var dmrMatches = (myDmr > 0 && d > 0 && d === myDmr)
        
        // Check callsign match
        var incomingCs = _normCallsign(callsign)
        var csMatches = (myCs !== "" && incomingCs !== "" && incomingCs === myCs)
        
        // Return true if EITHER matches
        return dmrMatches || csMatches
    }

    function _effectiveQsoLogLimit() {
        if (!page.appState) return 250
        var v = parseInt(page.appState.qsoLogLimit)
        if (isNaN(v) || v <= 0) v = 250
        if (v < 10) v = 10
        if (v > 5000) v = 5000
        return v
    }

    function loadLog() {
        logModel.clear()
        latestSerialNumber = 0
        var savedData = page.logHandlerRef.loadLog(logFileName)
        for (var i = 0; i < savedData.length; i++) {
            savedData[i].checked = false
            logModel.append(savedData[i])
            if (savedData[i].serialNumber !== undefined)
                latestSerialNumber = Math.max(latestSerialNumber, savedData[i].serialNumber + 1)
        }
        // Enforce current limit (and persist) in case user lowered it.
        var lim = _effectiveQsoLogLimit()
        var trimmed = false
        while (logModel.count > lim) { logModel.remove(logModel.count - 1); trimmed = true }
        if (trimmed) persistLog()
        updateLastHeard()
    }

    function persistLog() {
        var logData = []
        for (var i = 0; i < logModel.count; i++) {
            logData.push(logModel.get(i))
        }
        page.logHandlerRef.saveLogAsync(logFileName, logData)
    }

    function clearLog() {
        logModel.clear()
        page.logHandlerRef.clearLog(logFileName)
        latestSerialNumber = 0
        updateLastHeard()
    }

    function updateLastHeard() {
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

        if (!page.appState) return
        if (logModel.count > 0) {
            var entry0 = logModel.get(0)
            page.appState.lastHeard1 = entry0 ? fmt(entry0) : ""
        } else {
            page.appState.lastHeard1 = ""
        }
        if (logModel.count > 1) {
            var entry1 = logModel.get(1)
            page.appState.lastHeard2 = entry1 ? fmt(entry1) : ""
        } else {
            page.appState.lastHeard2 = ""
        }
    }

    function addEntry(data) {
        if (!data || typeof data !== "object") return

        // Normalize countries like legacy
        if (data.country === "United States") data.country = "USA"
        if (data.country === "United Kingdom") data.country = "UK"

        // Optional filter: don't log self (requires both DMR ID + Callsign match settings)
        // When disableSelfLog is true, exclude self contacts from logging
        if (page.appState && page.appState.disableSelfLog) {
            var d = data.dmrID || data.dmrId || data.radio_id || data.id || 0
            var cs = data.callsign || ""
            if (_isSelfContact(d, cs)) {
                return  // Skip logging self contact when disableSelfLog is enabled
            }
        }

        logModel.insert(0, {
            serialNumber: latestSerialNumber,
            callsign: data.callsign || "",
            dmrID: data.dmrID || 0,
            tgid: data.tgid || 0,
            country: data.country || "",
            fname: data.fname || "",
            currentTime: data.currentTime || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss"),
            checked: false
        })
        latestSerialNumber += 1

        var lim = _effectiveQsoLogLimit()
        while (logModel.count > lim) logModel.remove(logModel.count - 1)
        persistLog()
        updateLastHeard()
    }

    Component.onCompleted: {
        loadLog()
    }

    // If user changes log limit while this page is open, enforce immediately.
    Connections {
        target: page.appState
        enabled: !!page.appState
        function onQsoLogLimitChanged() {
            var lim = page._effectiveQsoLogLimit()
            var trimmed = false
            while (logModel.count > lim) { logModel.remove(logModel.count - 1); trimmed = true }
            if (trimmed) persistLog()
            updateLastHeard()
        }
    }

    // Refresh list whenever logs.json changes (global logger writes it even if QSO page isn't open)
    Connections {
        target: page.logHandlerRef
        enabled: !!page.logHandlerRef
        function onLogSaved(fileName) {
            if (fileName === logFileName) loadLog()
        }
        function onLogCleared(fileName) {
            if (fileName === logFileName) loadLog()
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 8
            
            Label { 
                text: page.title
                Layout.fillWidth: true
                leftPadding: 12
            }
            
            // TX Button (compact)
            Button {
                Layout.preferredWidth: 60
                enabled: appState ? appState.txEnabled : false
                text: (appState && appState.txActive) ? qsTr("TX ON") : qsTr("TX")
                font.bold: true
                font.pixelSize: 12
                
                background: Rectangle {
                    radius: 6
                    color: {
                        if (!appState || !appState.txEnabled) return "#4A5568"
                        return (appState.txActive) ? "#DC2626" : "#2563EB"
                    }
                }
                
                contentItem: Label {
                    text: parent.text
                    font: parent.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onPressed: {
                    if (appState && appState.txEnabled && droidstarRef) {
                        droidstarRef.tx_clicked(true)
                    }
                }
                onReleased: {
                    if (appState && appState.txEnabled && droidstarRef) {
                        droidstarRef.tx_clicked(false)
                    }
                }
                onCanceled: {
                    if (appState && appState.txEnabled && droidstarRef) {
                        droidstarRef.tx_clicked(false)
                    }
                }
            }
            
            // Connect/Disconnect button
            Button {
                Layout.preferredWidth: 80
                text: (appState && appState.connected) ? qsTr("Disconnect") : qsTr("Connect")
                font.pixelSize: 12
                
                background: Rectangle {
                    radius: 6
                    color: (appState && appState.connected) ? "#DC2626" : "#10B981"
                }
                
                contentItem: Label {
                    text: parent.text
                    font: parent.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    if (!appState || !droidstarRef) return
                    if (appState.connected) {
                        droidstarRef.process_connect() // Disconnect
                    } else {
                        // Push state and connect
                        droidstarRef.set_callsign(appState.callsign)
                        droidstarRef.set_dmrid(appState.dmrid)
                        droidstarRef.set_module(appState.module)
                        // Host selection is persisted via process_host_change()
                        // (there is no set_host() API on the backend)
                        if (appState.selectedHost && appState.selectedHost !== "")
                            droidstarRef.process_host_change(appState.selectedHost)
                        droidstarRef.process_connect()
                    }
                }
            }
            
            Button { 
                text: qsTr("Export")
                font.pixelSize: 12
                onClicked: exportDialog.open()
            }
            
            Button { 
                text: qsTr("Clear")
                font.pixelSize: 12
                onClicked: clearLog()
                rightPadding: 12
            }
        }
    }

    Dialog {
        id: exportDialog
        title: qsTr("Export log")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        property bool csv: true

        contentItem: ColumnLayout {
            spacing: 12
            width: 320
            implicitWidth: 320

            TextField {
                id: fileNameInput
                Layout.fillWidth: true
                placeholderText: qsTr("File name (no extension)")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                RadioButton { text: qsTr("CSV"); checked: true; onClicked: exportDialog.csv = true }
                RadioButton { text: qsTr("ADIF"); onClicked: exportDialog.csv = false }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.8
                text: qsTr("Tip: select entries below to export a subset. If none are selected, all entries will be exported.")
            }
        }

        onAccepted: {
            var base = fileNameInput.text.trim()
            if (base === "") {
                errorDialog.open()
                return
            }

            var selected = []
            var hasSelection = false
            for (var i = 0; i < logModel.count; i++) {
                var e = logModel.get(i)
                if (e.checked) { selected.push(e); hasSelection = true }
            }
            if (!hasSelection) {
                for (var j = 0; j < logModel.count; j++) selected.push(logModel.get(j))
            }

            var dir = page.logHandlerRef.getDSLogPath()
            var filePath = dir + "/" + base

            if (exportDialog.csv) {
                filePath += ".csv"
                if (page.logHandlerRef.exportLogToCsv(filePath, selected)) {
                    savedFilePath = page.logHandlerRef.getFriendlyPath(filePath)
                    savedDialog.open()
                }
            } else {
                filePath += ".adi"
                if (page.logHandlerRef.exportLogToAdif(filePath, selected)) {
                    savedFilePath = page.logHandlerRef.getFriendlyPath(filePath)
                    savedDialog.open()
                }
            }
        }
    }

    Dialog {
        id: errorDialog
        title: qsTr("Error")
        modal: true
        standardButtons: Dialog.Ok
        contentItem: Label {
            text: qsTr("Empty/Invalid File Name.")
            color: "#F87171"
            wrapMode: Text.WordWrap
            width: 300
        }
    }

    Dialog {
        id: savedDialog
        title: qsTr("File saved")
        modal: true
        standardButtons: Dialog.Ok
        contentItem: ColumnLayout {
            spacing: 12
            Layout.preferredWidth: 320
            Label { 
                text: qsTr("Saved to: %1").arg(savedFilePath); 
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: qsTr("Share"); onClicked: page.logHandlerRef.shareFile() }
            }
        }
    }

    // ListView is already scrollable; wrapping it in ScrollView can collapse its height.
    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        CollapsibleSection {
            id: logSettingsSection
            title: qsTr("QSO log settings")
            expanded: false  // Collapsed by default
            Layout.fillWidth: true

            content: ColumnLayout {
                spacing: 10
                width: parent.width

                // Disable self log row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0  // Allow shrinking to prevent overflow
                        spacing: 4
                        
                        Label { 
                            text: qsTr("Enable Self Log")
                            font.pixelSize: 11
                            opacity: 0.6
                            Layout.fillWidth: true
                        }
                        Label { 
                            text: qsTr("When disabled, entries matching your Callsign + DMR ID (from Settings) are included.")
                            font.pixelSize: 10
                            opacity: 0.6
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    
                    Switch {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 4
                        Layout.preferredWidth: implicitWidth
                        checked: page.appState ? !page.appState.disableSelfLog : false
                        onToggled: { if (page.appState) page.appState.disableSelfLog = !checked }
                    }
                }

                // QSO log limit row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Layout.minimumWidth: 0  // Allow shrinking to prevent overflow

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0  // Allow shrinking to prevent overflow
                        spacing: 4
                        
                        Label { 
                            text: qsTr("QSO log limit")
                            font.pixelSize: 11
                            opacity: 0.6
                            Layout.fillWidth: true
                        }
                        Label { 
                            text: qsTr("How many entries to keep (10–5000). Older entries are removed.")
                            font.pixelSize: 10
                            opacity: 0.6
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    TextField {
                        id: logLimitField
                        Layout.preferredWidth: 100
                        Layout.minimumWidth: 80
                        Layout.maximumWidth: 120
                        inputMethodHints: Qt.ImhDigitsOnly
                        placeholderText: "250"
                        text: page.appState ? ("" + page._effectiveQsoLogLimit()) : "250"
                        onEditingFinished: {
                            if (!page.appState) return
                            var n = parseInt(text || "0")
                            if (isNaN(n) || n <= 0) n = 250
                            if (n < 10) n = 10
                            if (n > 5000) n = 5000
                            page.appState.qsoLogLimit = n
                            text = "" + n
                        }
                    }
                }
            }
        }

        AppCard {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 6
                Label { text: qsTr("Recent activity"); font.bold: true }
                Label { text: qsTr("Auto-collects lightweight entries from live RX fields."); opacity: 0.8; wrapMode: Text.WordWrap }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: logModel
            clip: true
            spacing: 8

            delegate: Rectangle {
                width: ListView.view.width
                radius: 14
                color: "#111827"
                border.color: "#233044"
                border.width: 1
                height: col.implicitHeight + 16

                Column {
                    id: col
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        width: parent.width
                        spacing: 10
                        CheckBox {
                            checked: checked
                            onToggled: logModel.setProperty(index, "checked", checked)
                        }
                        Label {
                            text: (callsign || "") + "  •  TG " + (tgid || 0)
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        ToolButton {
                            id: optionsButton
                            text: "\u22EE" // vertical ellipsis
                            font.pixelSize: 18
                            onClicked: {
                                // Show per-record options (legacy QsoTab parity)
                                contextMenu.popup(optionsButton, 0, optionsButton.height)
                            }
                        }
                    }
                    Label { text: currentTime || ""; opacity: 0.75 }
                    Label {
                        text: (fname || "") + (country ? (" (" + country + ")") : "")
                        opacity: 0.85
                        visible: (fname && fname.length > 0) || (country && country.length > 0)
                    }
                }

                Menu {
                    id: contextMenu
                    title: qsTr("Lookup Options")
                    MenuItem {
                        text: qsTr("Lookup QRZ")
                        onTriggered: Qt.openUrlExternally("https://qrz.com/lookup/" + (callsign || ""))
                    }
                    MenuItem {
                        text: qsTr("Lookup BM")
                        onTriggered: Qt.openUrlExternally("https://brandmeister.network/index.php?page=profile&call=" + (callsign || ""))
                    }
                    MenuItem {
                        text: qsTr("Lookup APRS")
                        onTriggered: Qt.openUrlExternally("https://aprs.fi/#!call=a%2F" + (callsign || ""))
                    }
                }
            }
        }
    }
}

