/*
    Copyright (C) 2025-2026 Rohith Namboothiri

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

import "../components"
import "../theme"

Page {
    id: page
    title: qsTr("QSO")
    padding: 0

    property var app: null
    required property var logHandlerRef
    required property var vuidUpdaterRef
    required property var appState
    required property var droidstarRef

    property string logFileName: "logs.json"
    property string savedFilePath: ""
    property int selectedCount: 0
    property var customExportEntries: null

    // Full Handle Transcript Viewer state
    property string viewerCallsign: ""
    property int viewerDmrId: 0
    property string viewerFname: ""
    property string viewerCountry: ""
    property int viewerTgid: 0

    readonly property bool roomy: width >= 560

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    property Component headerActions: Component {
        Row {
            spacing: 0
            HeaderAction {
                glyph: (page.selectedCount > 0 && page.selectedCount === logModel.count) ? "\uf14a" : "\uf096"
                label: (page.selectedCount > 0 && page.selectedCount === logModel.count) ? qsTr("Select none") : qsTr("Select all")
                showLabel: page.roomy
                enabled: logModel.count > 0
                onClicked: page.setAllChecked(!(page.selectedCount > 0 && page.selectedCount === logModel.count))
            }
            HeaderAction {
                glyph: "\uf093"
                label: qsTr("Export")
                showLabel: page.roomy
                enabled: logModel.count > 0
                onClicked: page.openExport(null)
            }
            HeaderAction {
                glyph: "\uf1f8"
                label: qsTr("Clear")
                showLabel: page.roomy
                tone: t.danger
                enabled: logModel.count > 0
                onClicked: confirmClearDialog.open()
            }
        }
    }

    ListModel { id: logModel }
    ListModel { id: viewerTurnsModel }

    function _recount() {
        var n = 0
        for (var i = 0; i < logModel.count; i++) if (logModel.get(i).checked) n++
        page.selectedCount = n
    }

    function _parseTurnsJson(jsonStr, fallbackTranscript, fallbackTime, fallbackTgid) {
        var out = []
        if (jsonStr && jsonStr !== "") {
            try {
                var parsed = JSON.parse(jsonStr)
                if (parsed && parsed.length !== undefined) {
                    for (var i = 0; i < parsed.length; i++) {
                        if (parsed[i] && parsed[i].text) {
                            out.push({
                                time: parsed[i].time || fallbackTime || "",
                                text: String(parsed[i].text),
                                tgid: parsed[i].tgid || fallbackTgid || 0
                            })
                        }
                    }
                }
            } catch (e) {}
        }
        if (out.length === 0 && fallbackTranscript && String(fallbackTranscript).trim() !== "") {
            out.push({
                time: fallbackTime || "",
                text: String(fallbackTranscript).trim(),
                tgid: fallbackTgid || 0
            })
        }
        return out
    }

    // Mirror logs.json, keeping selection and scroll position across live updates.
    function loadLog() {
        if (!page.logHandlerRef) return
        var keep = {}
        for (var i = 0; i < logModel.count; i++) {
            var e = logModel.get(i)
            if (e.checked) keep[e.serialNumber] = true
        }
        var y = list.contentY
        var atTop = list.atYBeginning

        var saved = page.logHandlerRef.loadLog(page.logFileName)
        var rows = []
        if (saved && saved.length !== undefined) {
            for (var j = 0; j < saved.length; j++) {
                var s = saved[j]
                var turnsArr = (s.transcriptTurns && s.transcriptTurns.length !== undefined) ? s.transcriptTurns : []
                var turnsCount = turnsArr.length > 0 ? turnsArr.length : ((s.transcript && String(s.transcript).trim() !== "") ? 1 : 0)
                rows.push({
                    serialNumber: s.serialNumber !== undefined ? s.serialNumber : j,
                    callsign: s.callsign || "",
                    dmrID: s.dmrID || 0,
                    tgid: s.tgid || 0,
                    country: s.country || "",
                    fname: s.fname || "",
                    currentTime: s.currentTime || "",
                    transcript: s.transcript || "",
                    transcriptTurnsJson: JSON.stringify(turnsArr),
                    turnCount: turnsCount,
                    checked: !!keep[s.serialNumber]
                })
            }
        }
        logModel.clear()
        if (rows.length > 0) logModel.append(rows)
        _recount()
        if (!atTop) list.contentY = Math.min(y, Math.max(0, list.contentHeight - list.height))
        if (transcriptViewerSheet.opened) {
            page.refreshTranscriptViewer()
        }
    }

    function openTranscriptViewer(rowObj) {
        if (!rowObj) return
        viewerCallsign = rowObj.callsign || ""
        viewerDmrId = parseInt(rowObj.dmrID || "0") || 0
        viewerFname = rowObj.fname || ""
        viewerCountry = rowObj.country || ""
        viewerTgid = parseInt(rowObj.tgid || "0") || 0
        refreshTranscriptViewer()
        transcriptViewerSheet.open()
    }

    function refreshTranscriptViewer() {
        viewerTurnsModel.clear()
        var targetCall = (viewerCallsign || "").trim().toUpperCase()
        var targetDmr = viewerDmrId
        var collected = []

        for (var i = 0; i < logModel.count; i++) {
            var e = logModel.get(i)
            var eCall = (e.callsign || "").trim().toUpperCase()
            var eDmr = parseInt(e.dmrID || "0") || 0
            var matches = false
            if (targetDmr > 0 && eDmr > 0 && eDmr === targetDmr) matches = true
            else if (targetCall !== "" && eCall !== "" && eCall === targetCall) matches = true

            if (matches) {
                if (viewerFname === "" && e.fname) viewerFname = e.fname
                if (viewerCountry === "" && e.country) viewerCountry = e.country
                if (viewerCallsign === "" && e.callsign) viewerCallsign = e.callsign

                var turns = _parseTurnsJson(e.transcriptTurnsJson, e.transcript, e.currentTime, e.tgid)
                // Append in newest-first order across rows, or chronological within a row
                for (var k = turns.length - 1; k >= 0; k--) {
                    collected.push({
                        turnNumber: 0,
                        time: turns[k].time || e.currentTime || "",
                        text: turns[k].text || "",
                        tgid: turns[k].tgid || e.tgid || 0
                    })
                }
            }
        }

        // Number turns from oldest (#1) to newest (#N), displayed newest at top
        var total = collected.length
        for (var m = 0; m < total; m++) {
            collected[m].turnNumber = total - m
            viewerTurnsModel.append(collected[m])
        }
    }

    function _entriesForHandleViewer() {
        var out = []
        var targetCall = (viewerCallsign || "").trim().toUpperCase()
        var targetDmr = viewerDmrId
        for (var i = 0; i < logModel.count; i++) {
            var e = logModel.get(i)
            var eCall = (e.callsign || "").trim().toUpperCase()
            var eDmr = parseInt(e.dmrID || "0") || 0
            var matches = (targetDmr > 0 && eDmr > 0 && eDmr === targetDmr) ||
                          (targetCall !== "" && eCall !== "" && eCall === targetCall)
            if (matches) {
                var turns = _parseTurnsJson(e.transcriptTurnsJson, e.transcript, e.currentTime, e.tgid)
                out.push({
                    serialNumber: e.serialNumber,
                    callsign: e.callsign,
                    dmrID: e.dmrID,
                    tgid: e.tgid,
                    country: e.country,
                    fname: e.fname,
                    currentTime: e.currentTime,
                    transcript: e.transcript || "",
                    transcriptTurns: turns
                })
            }
        }
        return out
    }

    function setAllChecked(on) {
        for (var i = 0; i < logModel.count; i++) logModel.setProperty(i, "checked", on)
        page.selectedCount = on ? logModel.count : 0
    }

    function clearLog() {
        logModel.clear()
        page.selectedCount = 0
        page.logHandlerRef.clearLog(page.logFileName)
    }

    function _entriesForExport() {
        if (page.customExportEntries && page.customExportEntries.length > 0) {
            return page.customExportEntries
        }
        var all = [], selected = []
        for (var i = 0; i < logModel.count; i++) {
            var e = logModel.get(i)
            var turns = _parseTurnsJson(e.transcriptTurnsJson, e.transcript, e.currentTime, e.tgid)
            var o = {
                serialNumber: e.serialNumber,
                callsign: e.callsign,
                dmrID: e.dmrID,
                tgid: e.tgid,
                country: e.country,
                fname: e.fname,
                currentTime: e.currentTime,
                transcript: e.transcript || "",
                transcriptTurns: turns
            }
            all.push(o)
            if (e.checked) selected.push(o)
        }
        return selected.length > 0 ? selected : all
    }

    function openExport(subsetEntries, customPrefix, includeTranscripts) {
        page.customExportEntries = subsetEntries || null
        var prefix = customPrefix || "droidstar_qso"
        fileNameInput.text = prefix + "_" + Qt.formatDateTime(new Date(), "yyyyMMdd_HHmm")
        includeTranscriptsCheck.checked = !!includeTranscripts
        exportDialog.open()
    }

    function doExport() {
        var base = fileNameInput.text.trim().replace(/[\\\/:*?"<>|]+/g, "_")
        if (base === "") {
            if (page.app) page.app.notify(qsTr("Enter a file name."), "", null, "warning")
            return false
        }
        var entries = _entriesForExport()
        var filePath = page.logHandlerRef.getDSLogPath() + "/" + base + (exportDialog.csv ? ".csv" : ".adi")
        var withTranscripts = includeTranscriptsCheck.checked
        var ok = exportDialog.csv ? page.logHandlerRef.exportLogToCsv(filePath, entries, withTranscripts)
                                  : page.logHandlerRef.exportLogToAdif(filePath, entries, withTranscripts)
        page.customExportEntries = null
        if (ok) {
            page.savedFilePath = page.logHandlerRef.getFriendlyPath(filePath)
            savedDialog.open()
        } else if (page.app) {
            page.app.notify(qsTr("Export failed."), "", null, "error")
        }
        return ok
    }

    Component.onCompleted: loadLog()

    // Refresh list whenever logs.json changes (global logger writes it even if QSO page isn't open)
    Connections {
        target: page.logHandlerRef
        function onLogSaved(fileName) { if (fileName === page.logFileName) page.loadLog() }
        function onLogCleared(fileName) { if (fileName === page.logFileName) page.loadLog() }
    }

    // ─────────────────────────────────────────────────────────
    // RESPONSIVE HANDLE TRANSCRIPTION VIEWER SHEET
    // ─────────────────────────────────────────────────────────
    Popup {
        id: transcriptViewerSheet
        parent: Overlay.overlay
        modal: true
        dim: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        readonly property var win: page.Window.window
        readonly property real ow: parent ? parent.width : 400
        readonly property real oh: parent ? parent.height : 740
        readonly property bool wideSheet: ow >= 600
        readonly property real safeTop: (win && win.safeArea) ? win.safeArea.top : 0
        readonly property real safeBottom: (win && win.safeArea) ? win.safeArea.bottom : 0
        readonly property real safeLeft: (win && win.safeArea) ? win.safeArea.left : 0
        readonly property real safeRight: (win && win.safeArea) ? win.safeArea.right : 0

        width: wideSheet ? Math.min(620, ow - 40) : ow
        height: wideSheet ? Math.min(640, oh - safeTop - safeBottom - 32) : Math.min(oh * 0.84, oh - safeTop - 16)
        x: (ow - width) / 2
        y: wideSheet ? Math.max(safeTop + 16, (oh - height) / 2) : (oh - height)

        Overlay.modal: Rectangle { color: "#99000000" }
        background: Rectangle {
            color: t.surface
            radius: 18
            border.color: t.stroke
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16 + (transcriptViewerSheet.wideSheet ? 0 : transcriptViewerSheet.safeLeft)
            anchors.rightMargin: 16 + (transcriptViewerSheet.wideSheet ? 0 : transcriptViewerSheet.safeRight)
            anchors.topMargin: 12
            anchors.bottomMargin: 12 + (transcriptViewerSheet.wideSheet ? 0 : transcriptViewerSheet.safeBottom)
            spacing: 10

            // Grab handle on mobile
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                visible: !transcriptViewerSheet.wideSheet
                width: 40; height: 5; radius: 2.5
                color: t.textMuted; opacity: 0.45
            }

            // Header: Station Callsign + Handle + Badges + Actions
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: page.viewerCallsign !== "" ? page.viewerCallsign : qsTr("ID %1").arg(page.viewerDmrId)
                            font.pixelSize: 20
                            font.bold: true
                            color: t.text
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            visible: page.viewerTgid > 0
                            radius: 6
                            color: t.tint(t.accent, 0.18)
                            implicitHeight: 20
                            implicitWidth: vTgLabel.implicitWidth + 10
                            Label {
                                id: vTgLabel
                                anchors.centerIn: parent
                                text: "TG " + page.viewerTgid
                                font.pixelSize: t.fsCaption
                                font.bold: true
                                color: t.accent
                            }
                        }

                        Rectangle {
                            radius: 6
                            color: t.surface3
                            implicitHeight: 20
                            implicitWidth: vCountLabel.implicitWidth + 10
                            Label {
                                id: vCountLabel
                                anchors.centerIn: parent
                                text: viewerTurnsModel.count === 1
                                      ? qsTr("1 turn")
                                      : qsTr("%1 turns").arg(viewerTurnsModel.count)
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: page.viewerFname + ((page.viewerFname !== "" && page.viewerCountry !== "") ? " • " : "") + page.viewerCountry
                        font.pixelSize: t.fsSmall
                        color: t.textMuted
                        elide: Text.ElideRight
                    }
                }

                AppButton {
                    visible: viewerTurnsModel.count > 0
                    text: qsTr("Export")
                    glyph: "\uf093"
                    tone: "primary"
                    implicitHeight: 36
                    onClicked: {
                        var subset = page._entriesForHandleViewer()
                        var tag = (page.viewerCallsign || ("id_" + page.viewerDmrId)).toLowerCase()
                        transcriptViewerSheet.close()
                        page.openExport(subset, "droidstar_" + tag, true)
                    }
                }

                ToolButton {
                    text: "\uf00d"
                    font.family: "FontAwesome"
                    font.pixelSize: 16
                    Accessible.name: qsTr("Close transcript viewer")
                    onClicked: transcriptViewerSheet.close()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: t.stroke }

            // Scrollable list of timestamped turns for this handle
            ListView {
                id: turnsListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: viewerTurnsModel
                clip: true
                spacing: 8
                boundsBehavior: Flickable.DragOverBounds
                ScrollIndicator.vertical: ScrollIndicator {}

                delegate: Rectangle {
                    required property int turnNumber
                    required property string time
                    required property string text
                    required property int tgid

                    width: ListView.view.width
                    height: turnCardCol.implicitHeight + 20
                    radius: 12
                    color: t.surface2
                    border.color: t.tint(t.accent, 0.35)
                    border.width: 1

                    ColumnLayout {
                        id: turnCardCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                radius: 6
                                color: t.tint(t.accent, 0.2)
                                implicitHeight: 20
                                implicitWidth: turnNumBadge.implicitWidth + 10
                                Label {
                                    id: turnNumBadge
                                    anchors.centerIn: parent
                                    text: qsTr("Turn #%1").arg(turnNumber)
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: t.accent
                                }
                            }

                            Label {
                                text: "\uf017  " + time
                                font.family: "FontAwesome"
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                visible: tgid > 0
                                text: "TG " + tgid
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: parent.parent.text
                            font.pixelSize: t.fsBody
                            color: t.text
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Empty state inside viewer if no transcripts recorded for this handle yet
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 32, 300)
                    visible: viewerTurnsModel.count === 0
                    spacing: 8
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "\uf20a"
                        font.family: "FontAwesome"
                        font.pixelSize: 32
                        color: t.textMuted
                        opacity: 0.6
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No transcriptions recorded for this station yet.")
                        color: t.textMuted
                        font.pixelSize: t.fsSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    // DIALOGS
    // ─────────────────────────────────────────────────────────
    Dialog {
        id: exportDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 360, 420)
        title: qsTr("Export log")
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Save
        Overlay.modal: Rectangle { color: "#99000000" }

        property bool csv: true

        ColumnLayout {
            width: parent.width
            spacing: 12

            AppTextField {
                id: fileNameInput
                Layout.fillWidth: true
                placeholderText: qsTr("File name (no extension)")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: if (page.doExport()) exportDialog.close()
            }

            SegmentedControl {
                options: [{ label: qsTr("CSV"), value: true }, { label: qsTr("ADIF"), value: false }]
                currentValue: exportDialog.csv
                onSelected: function(value) { exportDialog.csv = value }
            }

            CheckBox {
                id: includeTranscriptsCheck
                Layout.fillWidth: true
                checked: false
                text: exportDialog.csv ? qsTr("Include transcriptions (Transcript column)")
                                       : qsTr("Include transcriptions (ADIF COMMENT field)")
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: t.textMuted
                font.pixelSize: t.fsSmall
                readonly property string suffix: includeTranscriptsCheck.checked
                                                 ? qsTr("with timestamped transcripts")
                                                 : qsTr("without transcripts")
                text: (page.customExportEntries && page.customExportEntries.length > 0)
                      ? qsTr("Exporting %1 entries %2.").arg(page.customExportEntries.length).arg(suffix)
                      : (page.selectedCount > 0
                         ? qsTr("Exporting %1 selected entries %2.").arg(page.selectedCount).arg(suffix)
                         : qsTr("Exporting all %1 entries %2.").arg(logModel.count).arg(suffix))
            }
        }

        onAccepted: page.doExport()
        onRejected: page.customExportEntries = null
    }

    Dialog {
        id: savedDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 360, 420)
        title: qsTr("File saved")
        modal: true
        standardButtons: Dialog.Close
        Overlay.modal: Rectangle { color: "#99000000" }

        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("Saved to: %1").arg(page.savedFilePath)
                wrapMode: Text.WrapAnywhere
                color: t.text
            }
            AppButton {
                Layout.fillWidth: true
                text: qsTr("Share")
                glyph: "\uf064"
                tone: "primary"
                onClicked: page.logHandlerRef.shareFile()
            }
        }
    }

    Dialog {
        id: confirmClearDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 360, 400)
        title: qsTr("Clear QSO log?")
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Discard
        Overlay.modal: Rectangle { color: "#99000000" }

        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                text: qsTr("All %1 entries will be permanently removed.").arg(logModel.count)
                wrapMode: Text.WordWrap
                color: t.text
            }
        }

        onDiscarded: {
            page.clearLog()
            close()
            if (page.app) page.app.notify(qsTr("QSO log cleared"), "", null, "success")
        }
    }

    // Shared row menu (one instance instead of one per delegate)
    Menu {
        id: rowMenu
        property string callsign: ""
        property var rowData: null
        MenuItem {
            text: qsTr("View Transcriptions")
            onTriggered: if (rowMenu.rowData) page.openTranscriptViewer(rowMenu.rowData)
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Lookup on QRZ"); onTriggered: Qt.openUrlExternally("https://qrz.com/lookup/" + encodeURIComponent(rowMenu.callsign)) }
        MenuItem { text: qsTr("Lookup on Brandmeister"); onTriggered: Qt.openUrlExternally("https://brandmeister.network/index.php?page=profile&call=" + encodeURIComponent(rowMenu.callsign)) }
        MenuItem { text: qsTr("Lookup on APRS.fi"); onTriggered: Qt.openUrlExternally("https://aprs.fi/#!call=a%2F" + encodeURIComponent(rowMenu.callsign)) }
    }

    // ─────────────────────────────────────────────────────────
    // CONTENT
    // ─────────────────────────────────────────────────────────
    ColumnLayout {
        id: body
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: Math.min(parent.width - 20, t.maxContentWidth)
        spacing: 10

        // Quick controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppButton {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                tone: (appState && appState.connected) ? "danger" : ((appState && appState.connecting) ? "warning" : "primary")
                busy: !!(appState && appState.connecting)
                glyph: (appState && appState.connected) ? "\uf011" : "\uf1e6"
                text: (appState && appState.connecting) ? qsTr("Cancel")
                      : ((appState && appState.connected) ? qsTr("Disconnect") : qsTr("Connect"))
                onClicked: if (page.app) page.app.connectOrDisconnect()
            }

            TxButton {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                compact: true
                app: page.app
                appState: page.appState
            }
        }

        CollapsibleSection {
            id: logSettingsSection
            title: qsTr("QSO log settings")
            statusText: page.appState ? qsTr("Limit %1").arg(page.appState.qsoLogLimit) : ""
            expanded: false

            content: ColumnLayout {
                spacing: 10

                ToggleRow {
                    text: qsTr("Exclude my own transmissions")
                    description: qsTr("Skips entries whose callsign or DMR ID matches your Settings.")
                    checked: !!(page.appState && page.appState.disableSelfLog)
                    onToggled: function(on) { if (page.appState) page.appState.disableSelfLog = on }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2
                        Label { text: qsTr("QSO log limit"); font.pixelSize: t.fsBody; color: t.text }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Entries to keep (10–5000). Oldest are removed.")
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                            wrapMode: Text.WordWrap
                        }
                    }

                    AppTextField {
                        id: logLimitField
                        Layout.preferredWidth: 96
                        inputMethodHints: Qt.ImhDigitsOnly
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator { bottom: 1; top: 5000 }
                        placeholderText: "250"
                        Component.onCompleted: text = page.appState ? ("" + page.appState.qsoLogLimit) : "250"
                        Connections {
                            target: page.appState
                            function onQsoLogLimitChanged() { if (!logLimitField.activeFocus) logLimitField.text = "" + page.appState.qsoLogLimit }
                        }
                        function commit() {
                            if (!page.appState) return
                            var n = parseInt(text || "0")
                            if (isNaN(n) || n <= 0) n = 250
                            n = Math.max(10, Math.min(5000, n))
                            page.appState.qsoLogLimit = n
                            text = "" + n
                        }
                        onAccepted: { commit(); focus = false }
                        onEditingFinished: commit()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: logModel.count === 0 ? qsTr("No contacts yet")
                      : (page.selectedCount > 0 ? qsTr("%1 contacts • %2 selected").arg(logModel.count).arg(page.selectedCount)
                                                : qsTr("%1 contacts").arg(logModel.count))
                color: t.textMuted
                font.pixelSize: t.fsSmall
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: logModel
            clip: true
            spacing: 8
            cacheBuffer: 600
            boundsBehavior: Flickable.DragOverBounds
            ScrollIndicator.vertical: ScrollIndicator {}

            footer: Item { width: 1; height: 12 }

            delegate: Rectangle {
                id: row
                required property int index
                required property string callsign
                required property var tgid
                required property var dmrID
                required property string fname
                required property string country
                required property string currentTime
                required property string transcript
                required property string transcriptTurnsJson
                required property int turnCount
                required property bool checked

                width: ListView.view.width
                height: rowLayout.implicitHeight + 16
                radius: 12
                color: checked ? t.tint(t.accent, 0.1) : (rowMouse.pressed ? t.surface3 : t.surface2)
                border.color: checked ? t.tint(t.accent, 0.6) : t.stroke
                border.width: 1
                Behavior on color { ColorAnimation { duration: t.durFast } }

                function toggle() {
                    var on = !checked
                    logModel.setProperty(index, "checked", on)
                    page.selectedCount = Math.max(0, page.selectedCount + (on ? 1 : -1))
                }

                function openHandleTranscripts() {
                    page.openTranscriptViewer({
                        callsign: row.callsign,
                        dmrID: row.dmrID,
                        fname: row.fname,
                        country: row.country,
                        tgid: row.tgid
                    })
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    onClicked: row.toggle()
                    onPressAndHold: {
                        rowMenu.callsign = row.callsign
                        rowMenu.rowData = {
                            callsign: row.callsign,
                            dmrID: row.dmrID,
                            fname: row.fname,
                            country: row.country,
                            tgid: row.tgid
                        }
                        rowMenu.popup()
                    }
                }

                RowLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 6

                    CheckBox {
                        checked: row.checked
                        onToggled: row.toggle()
                        Accessible.name: qsTr("Select %1").arg(row.callsign)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: row.callsign !== "" ? row.callsign : qsTr("ID %1").arg(row.dmrID)
                                font.bold: true
                                font.pixelSize: 16
                                color: t.text
                                elide: Text.ElideRight
                                Layout.maximumWidth: rowLayout.width * 0.45
                            }
                            Rectangle {
                                visible: !!row.tgid
                                radius: 8
                                implicitHeight: 20
                                implicitWidth: tgLabel.implicitWidth + 12
                                color: t.tint(t.accent, 0.16)
                                Label {
                                    id: tgLabel
                                    anchors.centerIn: parent
                                    text: "TG " + row.tgid
                                    font.pixelSize: t.fsCaption
                                    color: t.accent
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                visible: page.roomy
                                text: row.currentTime
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                visible: text !== ""
                                text: row.fname + ((row.fname !== "" && row.country !== "") ? " • " : "") + row.country
                                font.pixelSize: t.fsSmall
                                color: t.text
                                opacity: 0.85
                                elide: Text.ElideRight
                            }

                            // Responsive inline "Transcripts" pill button to view full timestamped turns of this handle
                            Rectangle {
                                visible: row.transcript !== ""
                                radius: 7
                                color: t.tint(t.accent, 0.18)
                                border.color: t.tint(t.accent, 0.5)
                                border.width: 1
                                implicitHeight: 22
                                implicitWidth: viewTransRow.implicitWidth + 12

                                Row {
                                    id: viewTransRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Label {
                                        text: "\uf20a"
                                        font.family: "FontAwesome"
                                        font.pixelSize: 10
                                        color: t.accent
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: row.turnCount > 1
                                              ? qsTr("View (%1)").arg(row.turnCount)
                                              : qsTr("View")
                                        font.pixelSize: t.fsCaption
                                        font.bold: true
                                        color: t.accent
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: row.openHandleTranscripts()
                                }
                            }
                        }

                        Label {
                            visible: !page.roomy && row.currentTime !== ""
                            text: row.currentTime
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                        }

                        // Compact preview box (tap to open full timestamped handle transcript viewer)
                        Rectangle {
                            Layout.fillWidth: true
                            visible: row.transcript !== ""
                            radius: 8
                            color: t.tint(t.accent, 0.08)
                            border.color: t.tint(t.accent, 0.25)
                            border.width: 1
                            implicitHeight: qsoTranscriptLabel.implicitHeight + 10

                            Label {
                                id: qsoTranscriptLabel
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                text: row.transcript
                                font.pixelSize: t.fsSmall
                                color: t.text
                                opacity: 0.92
                                wrapMode: Text.WordWrap
                                maximumLineCount: page.roomy ? 3 : 2
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: row.openHandleTranscripts()
                            }
                        }
                    }

                    ToolButton {
                        id: optionsButton
                        text: "\uf142"
                        font.family: "FontAwesome"
                        font.pixelSize: 16
                        enabled: row.callsign !== "" || row.dmrID > 0
                        Accessible.name: qsTr("Lookup options for %1").arg(row.callsign)
                        onClicked: {
                            rowMenu.callsign = row.callsign
                            rowMenu.rowData = {
                                callsign: row.callsign,
                                dmrID: row.dmrID,
                                fname: row.fname,
                                country: row.country,
                                tgid: row.tgid
                            }
                            rowMenu.popup(optionsButton, 0, optionsButton.height)
                        }
                    }
                }
            }

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 320)
                visible: logModel.count === 0
                spacing: 8
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "\uf2b9"
                    font.family: "FontAwesome"
                    font.pixelSize: 36
                    color: t.textMuted
                    opacity: 0.6
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Stations you hear are logged here automatically.")
                    color: t.textMuted
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
