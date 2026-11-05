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
    title: qsTr("Settings")
    padding: 0

    property var app: null
    required property var droidstarRef
    required property var appState
    property var smsManagerRef: null
    property var pttButtonsRef: null
    property Component headerActions: null

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    // Usable width inside a CollapsibleSection (outer margins + content padding)
    readonly property real formWidth: contentLayout.width - 32
    readonly property int cols3: formWidth >= 520 ? 3 : (formWidth >= 300 ? 2 : 1)
    readonly property int cols2: formWidth >= 300 ? 2 : 1
    // Password fields carry long placeholders plus a reveal button
    readonly property int colsSecrets: formWidth >= 640 ? 3 : (formWidth >= 420 ? 2 : 1)
    // Switch tiles need room for label + description + switch
    readonly property int colsToggle: formWidth >= 480 ? 2 : 1

    readonly property bool ttsAvailable: (typeof USE_FLITE !== "undefined" && USE_FLITE)

    readonly property var transcriptionLangList: [
        { code: "auto", label: qsTr("Auto-Detect") },
        { code: "en",   label: qsTr("English") },
        { code: "es",   label: qsTr("Spanish (Español)") },
        { code: "de",   label: qsTr("German (Deutsch)") },
        { code: "fr",   label: qsTr("French (Français)") },
        { code: "ja",   label: qsTr("Japanese (日本語)") },
        { code: "it",   label: qsTr("Italian (Italiano)") },
        { code: "pt",   label: qsTr("Portuguese (Português)") },
        { code: "ru",   label: qsTr("Russian (Русский)") },
        { code: "zh",   label: qsTr("Chinese (中文)") },
        { code: "hi",   label: qsTr("Hindi (हिन्दी)") },
        { code: "ar",   label: qsTr("Arabic (العربية)") },
        { code: "nl",   label: qsTr("Dutch (Nederlands)") },
        { code: "pl",   label: qsTr("Polish (Polski)") },
        { code: "ko",   label: qsTr("Korean (한국어)") },
        { code: "tr",   label: qsTr("Turkish (Türkçe)") }
    ]
    readonly property var transcriptionLangLabels: (function() {
        var out = []
        for (var i = 0; i < transcriptionLangList.length; i++) out.push(transcriptionLangList[i].label)
        return out
    })()
    function langLabelForCode(code) {
        var c = (code || "auto").toLowerCase()
        for (var i = 0; i < transcriptionLangList.length; i++) {
            if (transcriptionLangList[i].code === c) return transcriptionLangList[i].label
        }
        return transcriptionLangList[0].label
    }

    // Models (legacy parity)
    readonly property var essidModel: (function() {
        var ids = ["None"]
        for (var i = 0; i < 100; i++) ids.push(i.toString().padStart(2, "0"))
        return ids
    })()

    readonly property var modemFields: [
        { label: qsTr("RX Freq"),         prop: "modemRxFreq",       setter: "set_modemRxFreq" },
        { label: qsTr("TX Freq"),         prop: "modemTxFreq",       setter: "set_modemTxFreq" },
        { label: qsTr("RX Offset"),       prop: "modemRxOffset",     setter: "set_modemRxOffset" },
        { label: qsTr("TX Offset"),       prop: "modemTxOffset",     setter: "set_modemTxOffset" },
        { label: qsTr("RX DC Offset"),    prop: "modemRxDCOffset",   setter: "set_modemRxDCOffset" },
        { label: qsTr("TX DC Offset"),    prop: "modemTxDCOffset",   setter: "set_modemTxDCOffset" },
        { label: qsTr("RX Level"),        prop: "modemRxLevel",      setter: "set_modemRxLevel" },
        { label: qsTr("TX Level"),        prop: "modemTxLevel",      setter: "set_modemTxLevel" },
        { label: qsTr("RF Level"),        prop: "modemRFLevel",      setter: "set_modemRFLevel" },
        { label: qsTr("TX Delay"),        prop: "modemTxDelay",      setter: "set_modemTxDelay" },
        { label: qsTr("CWID TX Level"),   prop: "modemCWIdTxLevel",  setter: "set_modemCWIdTxLevel" },
        { label: qsTr("D-STAR TX Level"), prop: "modemDstarTxLevel", setter: "set_modemDstarTxLevel" },
        { label: qsTr("DMR TX Level"),    prop: "modemDMRTxLevel",   setter: "set_modemDMRTxLevel" },
        { label: qsTr("YSF TX Level"),    prop: "modemYSFTxLevel",   setter: "set_modemYSFTxLevel" },
        { label: qsTr("P25 TX Level"),    prop: "modemP25TxLevel",   setter: "set_modemP25TxLevel" },
        { label: qsTr("NXDN TX Level"),   prop: "modemNXDNTxLevel",  setter: "set_modemNXDNTxLevel" },
        { label: qsTr("Baud"),            prop: "modemBaud",         setter: "set_modemBaud" }
    ]

    // BrandMeister / TGIF login check (state and sheet live in App2026)
    readonly property bool hasDmrId: !!(appState && parseInt(appState.dmrid) > 0)
    readonly property var authNets: {
        var n = []
        if (appState && appState.bmPass) n.push("BM")
        if (appState && appState.tgifPass) n.push("TGIF")
        return n
    }
    function autoVerify(nets) {
        if (page.app && typeof page.app.verifyDmrAuth === "function") page.app.verifyDmrAuth(nets, true)
    }

    // Generic text setting: optimistic UI state on each keystroke, backend on commit.
    function editState(prop, v) { if (page.appState) page.appState[prop] = v }
    function commitState(prop, setter, v) {
        if (!page.appState) return
        page.appState[prop] = v
        if (page.droidstarRef && typeof page.droidstarRef[setter] === "function") page.droidstarRef[setter](v)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentLayout.implicitHeight + 40
        boundsBehavior: Flickable.DragOverBounds
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: contentLayout
            x: Math.max(10, (flick.width - width) / 2)
            y: 12
            width: Math.min(flick.width - 20, t.maxContentWidth)
            spacing: 12

            // ─────────────────────────────────────────────────────────
            // IDENTITY
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Identity")
                statusText: page.appState ? page.appState.callsign : ""
                expanded: true

                content: ColumnLayout {
                    spacing: 12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.cols3
                        columnSpacing: 10
                        rowSpacing: 10

                        FormField {
                            Layout.columnSpan: page.cols3 === 2 ? 2 : 1
                            label: qsTr("Callsign")
                            value: page.appState ? page.appState.callsign : ""
                            uppercase: true
                            placeholderText: qsTr("e.g. W1AW")
                            validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9\/]{0,10}/ }
                            onEdited: function(v) { page.editState("callsign", v) }
                            onCommitted: function(v) { page.commitState("callsign", "set_callsign", v) }
                        }

                        FormField {
                            label: qsTr("DMR ID")
                            value: page.appState ? page.appState.dmrid : ""
                            placeholderText: qsTr("7 digits")
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
                            onEdited: function(v) { page.editState("dmrid", v) }
                            onCommitted: function(v) { page.commitState("dmrid", "set_dmrid", v); page.autoVerify(["BM", "TGIF"]) }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            Label { text: qsTr("ESSID"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SearchableComboSheet {
                                model: page.essidModel
                                sheetTitle: qsTr("Select ESSID")
                                currentText: page.appState ? page.appState.essid : "None"
                                onActivated: function(index, text) { page.commitState("essid", "set_essid", text) }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.colsSecrets
                        columnSpacing: 10
                        rowSpacing: 10

                        FormField {
                            label: qsTr("BM Password")
                            value: page.appState ? page.appState.bmPass : ""
                            password: true
                            placeholderText: qsTr("Brandmeister hotspot password")
                            onEdited: function(v) { page.editState("bmPass", v) }
                            onCommitted: function(v) { page.commitState("bmPass", "set_bm_password", v); page.autoVerify(["BM"]) }
                        }
                        FormField {
                            label: qsTr("TGIF Password")
                            value: page.appState ? page.appState.tgifPass : ""
                            password: true
                            placeholderText: qsTr("TGIF hotspot password")
                            onEdited: function(v) { page.editState("tgifPass", v) }
                            onCommitted: function(v) { page.commitState("tgifPass", "set_tgif_password", v); page.autoVerify(["TGIF"]) }
                        }
                        FormField {
                            Layout.columnSpan: page.colsSecrets === 2 ? 2 : 1
                            label: qsTr("ASL Password")
                            value: page.appState ? page.appState.aslPass : ""
                            password: true
                            placeholderText: qsTr("AllStarLink portal password (IAX wt)")
                            onEdited: function(v) { page.editState("aslPass", v) }
                            onCommitted: function(v) { page.commitState("aslPass", "set_asl_password", v) }
                        }
                    }

                    // Login check for the saved BrandMeister / TGIF hotspot passwords
                    Rectangle {
                        id: authBar
                        Layout.fillWidth: true
                        visible: page.hasDmrId && page.authNets.length > 0
                        implicitHeight: 52
                        radius: 12
                        color: t.surface2
                        border.color: t.stroke
                        border.width: 1

                        readonly property var st: (page.app && page.app.authStatus) ? page.app.authStatus : ({})
                        readonly property bool checking: st.BM === "checking" || st.TGIF === "checking"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 6
                            spacing: 8

                            Label {
                                visible: authBar.width >= 330
                                text: qsTr("Login")
                                font.pixelSize: t.fsSmall
                                color: t.textMuted
                            }
                            Repeater {
                                model: page.authNets
                                delegate: Rectangle {
                                    id: chip
                                    required property string modelData
                                    readonly property string s: authBar.st[modelData] || ""
                                    readonly property color c: s === "rejected" ? t.danger
                                                             : s === "unreachable" ? t.warning
                                                             : (s === "ok" || s === "default") ? t.success : t.textMuted
                                    implicitHeight: 28
                                    implicitWidth: chipRow.implicitWidth + 18
                                    Layout.minimumWidth: 0
                                    radius: 14
                                    color: t.tint(c, 0.14)
                                    border.color: t.tint(c, 0.5)
                                    border.width: 1
                                    Row {
                                        id: chipRow
                                        anchors.centerIn: parent
                                        spacing: 5
                                        Label {
                                            text: chip.modelData === "TGIF" ? "TGIF" : "BM"
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        BusyIndicator {
                                            visible: chip.s === "checking"
                                            running: visible
                                            width: 12; height: 12; padding: 0
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: chip.s !== "checking"
                                            text: chip.s === "rejected" ? "\uf00d" : chip.s === "unreachable" ? "\uf071"
                                                : (chip.s === "ok" || chip.s === "default") ? "\uf00c" : "\uf128"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: chip.c
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: chip.s === "rejected" || chip.s === "unreachable"
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: if (page.app) page.app.showAuthSheet([chip.modelData])
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("Verify")
                                glyph: "\uf00c"
                                busy: authBar.checking
                                enabled: !authBar.checking
                                implicitHeight: 38
                                font.pixelSize: t.fsSmall + 1
                                onClicked: {
                                    // Push any half-typed password first
                                    page.forceActiveFocus()
                                    if (page.app) page.app.verifyDmrAuth(page.authNets, false)
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // LIVE TRANSCRIPTION (Apple Neural Engine streaming or local Whisper)
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                id: transcriptionSection
                readonly property bool appleSupported: !!(page.appState && page.appState.transcriptionAppleSupported)
                readonly property bool parakeetSupported: !!(page.appState && page.appState.transcriptionParakeetSupported)
                readonly property string curEngine: page.appState ? (page.appState.transcriptionEngine || "apple") : "apple"
                readonly property bool usingParakeet: parakeetSupported && (curEngine === "parakeet")
                readonly property bool usingWhisper: (!appleSupported && !usingParakeet) || (curEngine === "whisper")
                readonly property var modelSizes: ({ tiny: "~31 MB", base: "~57 MB", small: "~181 MB", turbo: "~547 MB" })
                readonly property string parakeetDisabledReason: page.appState ? (page.appState.transcriptionParakeetDisabledReason || "") : ""
                readonly property string smallDisabledReason: page.appState ? (page.appState.transcriptionSmallDisabledReason || "") : ""
                readonly property string turboDisabledReason: page.appState ? (page.appState.transcriptionTurboDisabledReason || "") : ""
                property string guardrailWarning: ""

                function showGuardrailWarning(reason) {
                    var msg = reason || qsTr("This model is disabled on your iPhone model to prevent memory crashes.")
                    transcriptionSection.guardrailWarning = msg
                    if (page.app) page.app.notify(msg, "", null, "warning")
                }

                readonly property string shortCollapsedBadge: {
                    if (!(page.appState && page.appState.transcriptionEnabled)) return qsTr("Off")
                    if (transcriptionSection.usingParakeet) return qsTr("Parakeet v3")
                    if (transcriptionSection.usingWhisper) {
                        var m = (page.appState.transcriptionModel || "base").toLowerCase()
                        var tierName = m === "turbo" ? "Turbo"
                                     : (m === "small" ? "Small"
                                     : (m === "tiny" ? "Tiny" : "Base"))
                        return qsTr("Whisper - %1").arg(tierName)
                    }
                    return qsTr("Apple Neural")
                }

                title: qsTr("Live Transcription")
                statusText: transcriptionSection.shortCollapsedBadge
                statusColor: (page.appState && page.appState.transcriptionEnabled) ? t.success : t.textMuted
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    ToggleRow {
                        text: qsTr("Enable Live RX Transcription")
                        description: qsTr("Transcribes incoming radio audio locally on-device with zero audio delay.")
                        checked: !!(page.appState && page.appState.transcriptionEnabled)
                        onToggled: function(on) {
                            if (page.app) page.app.setTranscriptionEnabled(on)
                        }
                    }

                    ToggleRow {
                        text: qsTr("Save Transcripts to QSO Log")
                        description: qsTr("Stores each finished over's transcript with its QSO entry. Applies to every engine and model.")
                        checked: !!(page.appState && page.appState.transcriptionLogEnabled)
                        onToggled: function(on) {
                            if (page.app) page.app.setTranscriptionLogEnabled(on)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: transcriptionSection.appleSupported
                        spacing: 4
                        Label { text: qsTr("Recognition Engine"); font.pixelSize: t.fsCaption; color: t.textMuted }
                        SegmentedControl {
                            options: transcriptionSection.parakeetSupported
                                     ? [
                                         { label: qsTr("Apple Neural"), value: "apple" },
                                         {
                                             label: qsTr("Parakeet v3"),
                                             value: "parakeet",
                                             disabled: transcriptionSection.parakeetDisabledReason !== "",
                                             disabledReason: transcriptionSection.parakeetDisabledReason
                                         },
                                         { label: qsTr("Whisper AI"), value: "whisper" }
                                       ]
                                     : [
                                         { label: qsTr("Apple Neural"), value: "apple" },
                                         { label: qsTr("Whisper AI"), value: "whisper" }
                                       ]
                            currentValue: transcriptionSection.curEngine
                            onSelected: function(value) {
                                transcriptionSection.guardrailWarning = ""
                                if (page.app) page.app.setTranscriptionEngine(value)
                            }
                            onDisabledSelected: function(value, reason) {
                                transcriptionSection.showGuardrailWarning(reason)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: transcriptionSection.usingParakeet
                                  ? qsTr("NVIDIA Parakeet TDT 0.6B v3 (~640 MB INT8 transducer). Auto-detects 25 European languages (EN, ES, IT, DE, FR, PT, NL, PL, RU, UK, etc.) with frame-aligned NATO phonetic & Ham Q-code decoding.")
                                  : (transcriptionSection.usingWhisper
                                     ? qsTr("Local multilingual Whisper AI (99 languages + Auto-Detect with NATO phonetic & Ham Q-code decoding). Turbo (~547 MB) and Small (~181 MB) are most accurate on muffled DMR audio.")
                                     : qsTr("Uses Apple's built-in iOS Speech Recognition (Whisper & Parakeet are disabled & unloaded). Supports English, Spanish, Italian, German, French, and 10+ languages."))
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                            wrapMode: Text.WordWrap
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: (transcriptionSection.usingWhisper && page.formWidth >= 520) ? 2 : 1
                        columnSpacing: 10
                        rowSpacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            Label { text: qsTr("Spoken Language"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SearchableComboSheet {
                                model: page.transcriptionLangLabels
                                currentText: page.langLabelForCode(page.appState ? page.appState.transcriptionLanguage : "auto")
                                sheetTitle: qsTr("Transcription Language")
                                searchable: true
                                onActivated: function(index) {
                                    if (index >= 0 && index < page.transcriptionLangList.length && page.app) {
                                        page.app.setTranscriptionLanguage(page.transcriptionLangList[index].code)
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: transcriptionSection.usingWhisper
                            spacing: 4
                            Label { text: qsTr("Multilingual Whisper Model"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SegmentedControl {
                                options: [
                                    { label: qsTr("Tiny"), value: "tiny" },
                                    { label: qsTr("Base"), value: "base" },
                                    {
                                        label: qsTr("Small"),
                                        value: "small",
                                        disabled: transcriptionSection.smallDisabledReason !== "",
                                        disabledReason: transcriptionSection.smallDisabledReason
                                    },
                                    {
                                        label: qsTr("Turbo"),
                                        value: "turbo",
                                        disabled: transcriptionSection.turboDisabledReason !== "",
                                        disabledReason: transcriptionSection.turboDisabledReason
                                    }
                                ]
                                currentValue: page.appState ? page.appState.transcriptionModel : "base"
                                onSelected: function(value) {
                                    transcriptionSection.guardrailWarning = ""
                                    if (page.app) page.app.setTranscriptionModel(value)
                                }
                                onDisabledSelected: function(value, reason) {
                                    transcriptionSection.showGuardrailWarning(reason)
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: {
                                    var tier = page.appState ? page.appState.transcriptionModel : "base"
                                    var name = page.appState ? page.appState.transcriptionModelName : ""
                                    return qsTr("Whisper - %1").arg(name || tier) + " • " + (transcriptionSection.modelSizes[tier] || "")
                                }
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                                elide: Text.ElideRight
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: (page.formWidth >= 440 || !downloadModelBtn.visible) ? 2 : 1
                        columnSpacing: 10
                        rowSpacing: 8

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: qsTr("Status: %1").arg(page.appState ? page.appState.transcriptionStatus : qsTr("Off"))
                            font.pixelSize: t.fsSmall
                            color: (page.appState && page.appState.transcriptionModelReady) ? t.success : t.textMuted
                            wrapMode: Text.WordWrap
                        }

                        AppButton {
                            id: downloadModelBtn
                            visible: (transcriptionSection.usingWhisper || transcriptionSection.usingParakeet) &&
                                     !(page.appState && page.appState.transcriptionModelReady) &&
                                     !(page.appState && page.appState.transcriptionDownloading)
                            Layout.fillWidth: page.formWidth < 440
                            text: qsTr("Download Model")
                            glyph: "\uf019"
                            tone: "primary"
                            onClicked: if (page.app) page.app.downloadTranscriptionModel()
                        }
                    }

                    // Dynamic device guardrail warning banner (shown when a disabled model is clicked)
                    Rectangle {
                        Layout.fillWidth: true
                        visible: transcriptionSection.guardrailWarning !== ""
                        implicitHeight: guardrailRow.implicitHeight + 16
                        radius: 10
                        clip: true
                        color: t.tint(t.warning, 0.14)
                        border.color: t.tint(t.warning, 0.55)
                        border.width: 1

                        RowLayout {
                            id: guardrailRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            spacing: 8

                            Label {
                                text: "\uf071"
                                font.family: "FontAwesome"
                                font.pixelSize: 13
                                color: t.warning
                                Layout.alignment: Qt.AlignTop
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: transcriptionSection.guardrailWarning
                                font.pixelSize: t.fsCaption
                                color: t.text
                                wrapMode: Text.WordWrap
                            }

                            ToolButton {
                                implicitWidth: 22
                                implicitHeight: 22
                                padding: 0
                                text: "\uf00d"
                                font.family: "FontAwesome"
                                font.pixelSize: 10
                                Accessible.name: qsTr("Dismiss warning")
                                onClicked: transcriptionSection.guardrailWarning = ""
                            }
                        }
                    }

                    // Active parallel / queued model downloads list (persists across engine switches & tab changes)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: !!(page.appState && page.appState.transcriptionActiveDownloads &&
                                    page.appState.transcriptionActiveDownloads.length > 0)

                        Repeater {
                            model: page.appState ? page.appState.transcriptionActiveDownloads : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: dlItemCol.implicitHeight + 14
                                radius: 10
                                clip: true
                                color: t.surface3
                                border.color: t.stroke
                                border.width: 1

                                ColumnLayout {
                                    id: dlItemCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 7
                                    spacing: 5

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Label {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: qsTr("Downloading %1 (%2%)…")
                                                  .arg(modelData.label || modelData.key || "")
                                                  .arg(modelData.percent !== undefined ? modelData.percent : 0)
                                            font.pixelSize: t.fsCaption
                                            font.bold: true
                                            color: t.text
                                            elide: Text.ElideRight
                                        }

                                        ToolButton {
                                            implicitWidth: 24
                                            implicitHeight: 24
                                            padding: 0
                                            text: "\uf00d"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            Accessible.name: qsTr("Cancel download")
                                            onClicked: {
                                                if (page.app && modelData.key) {
                                                    page.app.cancelTranscriptionDownload(modelData.key)
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 5
                                        radius: 2.5
                                        color: t.surface2

                                        Rectangle {
                                            width: parent.width * Math.max(0.03, Math.min(1.0, modelData.progress || 0))
                                            height: parent.height
                                            radius: 2.5
                                            color: t.accent
                                            Behavior on width { NumberAnimation { duration: t.durFast } }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // AUDIO & DEVICES
            // ─────────────────────────────────────────────────────────
            // ─────────────────────────────────────────────────────────
            // QSO REPLAY
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                id: replaySection
                readonly property bool on: !!(page.appState && page.appState.replayEnabled)
                readonly property var budgetOptions: [50, 100, 250, 500]
                readonly property bool customBudget: page.appState ? budgetOptions.indexOf(page.appState.replayStorageMb) < 0 : false

                title: qsTr("QSO Replay")
                statusText: on
                            ? ((page.appState && page.app && page.appState.replayCount > 0)
                               ? qsTr("On · %1").arg(page.app.formatReplayBytes(page.appState.replayUsedBytes))
                               : qsTr("On"))
                            : qsTr("Off")
                statusColor: on ? t.success : t.textMuted
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    ToggleRow {
                        text: qsTr("QSO Replay")
                        description: qsTr("Keeps a replay of every received over with its QSO log entry, so you can listen again any time. Replays stay on this device: never exported, shared or backed up.")
                        checked: replaySection.on
                        activeColor: t.success
                        onToggled: function(on) { if (page.app) page.app.setReplayEnabled(on) }
                    }

                    ToggleRow {
                        text: qsTr("Replay my own transmissions")
                        description: (page.appState && page.appState.disableSelfLog)
                                     ? qsTr("Turn off \"Exclude my own transmissions\" in the QSO log settings to record your TX.")
                                     : qsTr("Also keeps a replay of each over you transmit, logged under your callsign.")
                        enabled: replaySection.on && !!(page.appState && !page.appState.disableSelfLog)
                        checked: !!(page.appState && page.appState.replayRecordTx)
                        activeColor: t.success
                        onToggled: function(on) { if (page.app) page.app.setReplayRecordTx(on) }
                    }

                    // Storage budget: oldest replays are removed first once it is reached
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 6

                        Label { text: qsTr("Storage budget"); font.pixelSize: t.fsCaption; color: t.textMuted }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: page.colsToggle === 2 ? 4 : 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Repeater {
                                model: replaySection.budgetOptions
                                delegate: ChipToggle {
                                    required property int modelData
                                    Layout.fillWidth: true
                                    text: qsTr("%1 MB · %2").arg(modelData).arg(page.app ? page.app.replayHoursText(modelData) : "")
                                    checked: !!(page.appState && page.appState.replayStorageMb === modelData)
                                    activeColor: t.success
                                    onClicked: {
                                        if (page.app) page.app.setReplayStorageMb(modelData)
                                        customBudgetField.text = ""
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: qsTr("Custom (50–2000 MB)")
                                font.pixelSize: t.fsSmall
                                color: t.text
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                visible: customBudgetField.text !== "" && page.app
                                text: page.app ? page.app.replayHoursText(parseInt(customBudgetField.text) || 0) : ""
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }
                            AppTextField {
                                id: customBudgetField
                                Layout.preferredWidth: 96
                                inputMethodHints: Qt.ImhDigitsOnly
                                horizontalAlignment: TextInput.AlignHCenter
                                validator: IntValidator { bottom: 50; top: 2000 }
                                placeholderText: qsTr("MB")
                                Component.onCompleted: if (replaySection.customBudget && page.appState) text = "" + page.appState.replayStorageMb
                                Connections {
                                    target: page.appState
                                    function onReplayStorageMbChanged() {
                                        if (!customBudgetField.activeFocus)
                                            customBudgetField.text = replaySection.customBudget ? ("" + page.appState.replayStorageMb) : ""
                                    }
                                }
                                function commit() {
                                    var n = parseInt(text || "0")
                                    if (isNaN(n) || n <= 0) { text = ""; return }
                                    n = Math.max(50, Math.min(2000, n))
                                    if (page.app) page.app.setReplayStorageMb(n)
                                    text = replaySection.budgetOptions.indexOf(n) < 0 ? ("" + n) : ""
                                }
                                onAccepted: { commit(); focus = false }
                                onEditingFinished: commit()
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: (page.appState && page.app)
                                  ? (page.appState.replayCount === 0
                                     ? qsTr("No replays stored yet.")
                                     : qsTr("Using %1 · %2 replays · %3 left")
                                           .arg(page.app.formatReplayBytes(page.appState.replayUsedBytes))
                                           .arg(page.appState.replayCount)
                                           .arg(page.app.replayHoursText(Math.max(0, page.appState.replayStorageMb - page.appState.replayUsedBytes / 1048576))))
                                  : ""
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: !!(page.appState && page.appState.replayLastError !== "" && replaySection.on)
                            text: page.appState ? page.appState.replayLastError : ""
                            font.pixelSize: t.fsCaption
                            color: t.warning
                            wrapMode: Text.WordWrap
                        }
                    }

                    AppButton {
                        Layout.fillWidth: true
                        text: qsTr("Clear all replays")
                        glyph: "\uf1f8"
                        tone: "danger"
                        enabled: !!(page.appState && page.appState.replayCount > 0)
                        onClicked: confirmClearReplaysDialog.open()
                    }
                }
            }

            CollapsibleSection {
                title: qsTr("Audio & Devices")
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.cols2
                        columnSpacing: 10
                        rowSpacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            Label { text: qsTr("Playback"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SearchableComboSheet {
                                model: page.appState ? page.appState.playbacksModel : []
                                currentText: page.appState ? page.appState.playback : ""
                                sheetTitle: qsTr("Playback device")
                                onActivated: function(index, text) {
                                    page.appState.playback = text
                                    page.droidstarRef.setPlaybackDevice(text)
                                    page.droidstarRef.set_playback(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            Label { text: qsTr("Capture"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SearchableComboSheet {
                                model: page.appState ? page.appState.capturesModel : []
                                currentText: page.appState ? page.appState.capture : ""
                                sheetTitle: qsTr("Capture device")
                                onActivated: function(index, text) {
                                    page.appState.capture = text
                                    page.droidstarRef.setCaptureDevice(text)
                                    page.droidstarRef.set_capture(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            Label { text: qsTr("Vocoder"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SearchableComboSheet {
                                model: page.appState ? page.appState.vocodersModel : []
                                currentText: page.appState ? page.appState.vocoder : ""
                                sheetTitle: qsTr("Vocoder")
                                enabled: !(page.appState && (page.appState.connected || page.appState.connecting))
                                onActivated: function(index, text) {
                                    page.appState.vocoder = text
                                    page.droidstarRef.set_vocoder(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 4
                            Label { text: qsTr("Modem"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SearchableComboSheet {
                                model: page.appState ? page.appState.modemsModel : []
                                currentText: page.appState ? page.appState.modem : ""
                                sheetTitle: qsTr("Modem")
                                enabled: !(page.appState && (page.appState.connected || page.appState.connecting))
                                onActivated: function(index, text) {
                                    page.appState.modem = text
                                    page.droidstarRef.set_modem(text)
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !!(page.appState && (page.appState.connected || page.appState.connecting))
                        text: qsTr("Vocoder and modem are applied when connecting. Disconnect to change them.")
                        font.pixelSize: t.fsCaption
                        color: t.textMuted
                        wrapMode: Text.WordWrap
                    }

                    AppButton {
                        Layout.fillWidth: true
                        text: qsTr("Rescan devices")
                        glyph: "\uf021"
                        onClicked: {
                            if (!page.app) return
                            page.app.refreshAudioDevices()
                            page.app.refreshSerialDevices()
                            page.app.notify(qsTr("Device list refreshed"), "", null, "success")
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.colsToggle
                        columnSpacing: 10
                        rowSpacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: page.colsToggle
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Speaker Clean Pre-Amp (Post-Processing Gain)")
                                    font.pixelSize: t.fsCaption
                                    color: t.textMuted
                                }
                                Label {
                                    text: (Math.round(settingsPreampSlider.value) > 0 ? "+" : "") + Math.round(settingsPreampSlider.value) + " dB"
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: (settingsPreampSlider.value > 0) ? t.accent : t.textMuted
                                }
                            }
                            Slider {
                                id: settingsPreampSlider
                                Layout.fillWidth: true
                                from: 0
                                to: 24
                                stepSize: 1
                                value: page.appState ? page.appState.spkPreampDb : 6
                                onMoved: if (page.app) page.app.setSpkPreampDb(value)
                                onValueChanged: if (pressed && page.app) page.app.setSpkPreampDb(value)
                            }
                        }

                        ToggleRow {
                            Layout.columnSpan: page.colsToggle
                            text: qsTr("RX Clarity & Light Noise Reduction")
                            description: qsTr("Applies zero latency dynamic de-hissing, soft inter-word noise reduction, and gentle vocal clarity to incoming received audio")
                            checked: !!(page.appState && page.appState.rxClean)
                            onToggled: function(on) { if (page.app) page.app.setRxClean(on) }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan: page.colsToggle
                            spacing: 4
                            Label { text: qsTr("TX Mic Noise Reduction"); font.pixelSize: t.fsCaption; color: t.textMuted }
                            SegmentedControl {
                                options: [
                                    { label: qsTr("Off"), value: 0 },
                                    { label: qsTr("Natural Gate"), value: 1 },
                                    { label: qsTr("Neural AI"), value: 2 }
                                ]
                                currentValue: page.appState ? page.appState.noiseReductionMode : 2
                                onSelected: function(value) { if (page.app) page.app.setNoiseReductionMode(value) }
                            }
                        }

                        ToggleRow {
                            text: qsTr("TX HD Voice Clarity")
                            description: qsTr("Shapes outgoing microphone vocal presence and removes muddiness")
                            checked: !!(page.appState && page.appState.hdUpscale)
                            onToggled: function(on) { if (page.app) page.app.setHdUpscale(on) }
                        }
                        ToggleRow {
                            text: qsTr("TX Mic Leveler & Gate")
                            description: qsTr("Gates background noise between words and keeps your transmit voice level steady")
                            checked: !!(page.appState && page.appState.txEnhance)
                            onToggled: function(on) { if (page.app) page.app.setTxEnhance(on) }
                        }
                        ToggleRow {
                            text: qsTr("MMDVM Direct")
                            description: qsTr("M17 only")
                            checked: !!(page.appState && page.appState.mmdvmDirect)
                            onToggled: function(on) {
                                page.appState.mmdvmDirect = on
                                page.droidstarRef.set_mmdvm_direct(on)
                            }
                        }
                        ToggleRow {
                            text: qsTr("Debug")
                            description: qsTr("Verbose protocol log")
                            activeColor: t.warning
                            checked: !!(page.appState && page.appState.debug)
                            onToggled: function(on) {
                                page.appState.debug = on
                                page.droidstarRef.set_debug(on)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: page.appState ? page.appState.ambestatus : ""
                        color: t.textMuted
                        font.pixelSize: t.fsSmall
                        wrapMode: Text.WordWrap
                        visible: text !== ""
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.appState ? page.appState.mmdvmstatus : ""
                        color: t.textMuted
                        font.pixelSize: t.fsSmall
                        wrapMode: Text.WordWrap
                        visible: text !== ""
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // D-STAR / ROUTING
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("D-STAR / Routing")
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.cols2
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: [
                                { label: "MYCALL", prop: "mycall", setter: "set_mycall" },
                                { label: "URCALL", prop: "urcall", setter: "set_urcall" },
                                { label: "RPTR1",  prop: "rptr1",  setter: "set_rptr1" },
                                { label: "RPTR2",  prop: "rptr2",  setter: "set_rptr2" }
                            ]
                            delegate: FormField {
                                required property var modelData
                                label: modelData.label
                                value: page.appState ? page.appState[modelData.prop] : ""
                                uppercase: true
                                maximumLength: 8
                                onEdited: function(v) { page.editState(modelData.prop, v) }
                                onCommitted: function(v) { page.commitState(modelData.prop, modelData.setter, v) }
                            }
                        }
                    }

                    FormField {
                        label: qsTr("User Text")
                        value: page.appState ? page.appState.usrtxt : ""
                        maximumLength: 20
                        placeholderText: qsTr("Up to 20 characters")
                        onEdited: function(v) { page.editState("usrtxt", v) }
                        onCommitted: function(v) { page.commitState("usrtxt", "set_usrtxt", v) }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // PROFILE
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Profile / Location")
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.cols2
                        columnSpacing: 10
                        rowSpacing: 10

                        FormField {
                            label: qsTr("Latitude")
                            value: page.appState ? page.appState.latitude : ""
                            placeholderText: qsTr("e.g. 40.7128")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            validator: RegularExpressionValidator { regularExpression: /-?[0-9]{0,2}(\.[0-9]{0,6})?/ }
                            onEdited: function(v) { page.editState("latitude", v) }
                            onCommitted: function(v) { page.commitState("latitude", "set_latitude", v) }
                        }
                        FormField {
                            label: qsTr("Longitude")
                            value: page.appState ? page.appState.longitude : ""
                            placeholderText: qsTr("e.g. -74.0060")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            validator: RegularExpressionValidator { regularExpression: /-?[0-9]{0,3}(\.[0-9]{0,6})?/ }
                            onEdited: function(v) { page.editState("longitude", v) }
                            onCommitted: function(v) { page.commitState("longitude", "set_longitude", v) }
                        }
                        FormField {
                            label: qsTr("Location")
                            value: page.appState ? page.appState.location : ""
                            placeholderText: qsTr("City, State/Country")
                            onEdited: function(v) { page.editState("location", v) }
                            onCommitted: function(v) { page.commitState("location", "set_location", v) }
                        }
                        FormField {
                            label: qsTr("Description")
                            value: page.appState ? page.appState.description : ""
                            onEdited: function(v) { page.editState("description", v) }
                            onCommitted: function(v) { page.commitState("description", "set_description", v) }
                        }
                        FormField {
                            Layout.columnSpan: page.cols2
                            label: qsTr("URL")
                            value: page.appState ? page.appState.url : ""
                            placeholderText: qsTr("https://…")
                            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                            onEdited: function(v) { page.editState("url", v) }
                            onCommitted: function(v) { page.commitState("url", "set_url", v) }
                        }
                        FormField {
                            label: qsTr("Software ID")
                            value: page.appState ? page.appState.swid : ""
                            onEdited: function(v) { page.editState("swid", v) }
                            onCommitted: function(v) { page.commitState("swid", "set_swid", v) }
                        }
                        FormField {
                            label: qsTr("Package ID")
                            value: page.appState ? page.appState.pkgid : ""
                            onEdited: function(v) { page.editState("pkgid", v) }
                            onCommitted: function(v) { page.commitState("pkgid", "set_pkgid", v) }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // DMR / TX OPTIONS
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("DMR / TX Options")
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    ToggleRow {
                        text: qsTr("BrandMeister ODTP (Instant TG Switch)")
                        description: qsTr("Uses Open DMR Terminal Protocol (UDP 54006) on BrandMeister masters to switch and scan talkgroups instantly without keying TX")
                        checked: !!(page.appState && page.appState.bmOdtp)
                        onToggled: function(on) {
                            page.appState.bmOdtp = on
                            if (page.droidstarRef && typeof page.droidstarRef.set_bm_odtp === "function") {
                                page.droidstarRef.set_bm_odtp(on)
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: t.stroke }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            text: qsTr("DMR Messaging Format")
                            font.pixelSize: t.fsCaption
                            color: t.textMuted
                        }
                        SearchableComboSheet {
                            Layout.fillWidth: true
                            model: [
                                qsTr("Motorola M-SMS"),
                                qsTr("AnyTone / DMR Standard (UTF-16LE)"),
                                qsTr("ETSI Standard (UTF-16BE)"),
                                qsTr("Chinese / Hytera Short Data")
                            ]
                            currentText: {
                                var i = page.smsManagerRef ? page.smsManagerRef.defaultFormat : 0
                                return model[Math.max(0, Math.min(model.length - 1, i))]
                            }
                            sheetTitle: qsTr("Default HomeBrew SMS Format")
                            searchable: false
                            onActivated: function(index) {
                                if (page.smsManagerRef) page.smsManagerRef.defaultFormat = index
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("BrandMeister ODTP automatically translates messages for the recipient's radio. This format is used for HomeBrew, TGIF, FreeDMR, and ODTP fallback.")
                            color: t.textMuted
                            font.pixelSize: t.fsCaption
                            wrapMode: Text.WordWrap
                        }
                        ToggleRow {
                            visible: page.smsManagerRef !== null
                            text: qsTr("CSBK Radio Wake-up Preamble")
                            description: qsTr("Recommended. Sends three preamble blocks before HomeBrew SMS so sleeping handheld radios can synchronize.")
                            checked: page.smsManagerRef ? page.smsManagerRef.sendPreambleCsbk : true
                            onToggled: function(on) {
                                if (page.smsManagerRef) page.smsManagerRef.sendPreambleCsbk = on
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.cols2
                        columnSpacing: 10
                        rowSpacing: 10

                        FormField {
                            label: qsTr("DMR Options")
                            value: page.appState ? page.appState.dmrOptions : ""
                            placeholderText: qsTr("Options string")
                            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                            onEdited: function(v) { page.editState("dmrOptions", v) }
                            onCommitted: function(v) { page.commitState("dmrOptions", "set_dmr_options", v) }
                        }
                        FormField {
                            label: qsTr("TX Time-out (seconds, 0 = off)")
                            value: page.appState ? page.appState.txTimeout : ""
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                            onEdited: function(v) { page.editState("txTimeout", v) }
                            onCommitted: function(v) { page.commitState("txTimeout", "set_txtimeout", v === "" ? "0" : v) }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("M17 / YSF Rate"); font.pixelSize: t.fsCaption; color: t.textMuted }
                        SegmentedControl {
                            options: [
                                { label: qsTr("Voice Full"), value: true },
                                { label: qsTr("Voice + Data"), value: false }
                            ]
                            currentValue: page.appState ? page.appState.m17VoiceFull : true
                            onSelected: function(value) { if (page.app) page.app.setM17VoiceFull(value) }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // NETWORK
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Network & TX Behaviour")
                expanded: false

                content: GridLayout {
                    columns: page.colsToggle
                    columnSpacing: 10
                    rowSpacing: 10

                    ToggleRow {
                        text: qsTr("IPv6")
                        description: qsTr("Prefer IPv6 hosts when available")
                        checked: !!(page.appState && page.appState.ipv6)
                        onToggled: function(on) {
                            page.appState.ipv6 = on
                            page.droidstarRef.set_ipv6(on)
                        }
                    }
                    ToggleRow {
                        text: qsTr("XRF → REF")
                        description: qsTr("Use REF protocol for XRF reflectors")
                        checked: !!(page.appState && page.appState.xrf2ref)
                        onToggled: function(on) {
                            page.appState.xrf2ref = on
                            page.droidstarRef.set_xrf2ref(on)
                        }
                    }
                    ToggleRow {
                        Layout.columnSpan: page.colsToggle
                        text: qsTr("Toggle TX Mode")
                        description: qsTr("Tap once to start and again to stop, instead of hold-to-talk")
                        checked: !!(page.appState && page.appState.toggleTx)
                        onToggled: function(on) {
                            // Don't leave TX keyed when switching press semantics.
                            if (page.appState.txActive && page.app) {
                                if (page.appState.toggleTx) page.app.txSet(false)
                                else page.app.txRelease()
                            }
                            page.appState.toggleTx = on
                            page.droidstarRef.set_toggletx(on)
                        }
                    }
                }
            }


            // ─────────────────────────────────────────────────────────
            // PTT BUTTON (headset / Bluetooth / keyboard-type)
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                id: pttSection
                title: qsTr("PTT Button")
                expanded: false
                statusText: page.pttButtonsRef ? page.pttButtonsRef.statusText : ""

                readonly property var mgr: page.pttButtonsRef
                readonly property string btState: mgr ? mgr.bluetoothState : "unsupported"
                readonly property bool btReady: btState === "poweredOn"

                content: ColumnLayout {
                    spacing: 10

                    ToggleRow {
                        text: qsTr("Headset button")
                        description: qsTr("Earbud play/pause taps TX on and off. Turn off if TX keys by itself when earbuds connect or are worn")
                        checked: !!(pttSection.mgr && pttSection.mgr.mediaButtonEnabled)
                        onToggled: function(on) { if (pttSection.mgr) pttSection.mgr.mediaButtonEnabled = on }
                    }
                    ToggleRow {
                        text: qsTr("Track buttons")
                        description: qsTr("Next and previous track keys on car steering wheels and headsets also tap TX")
                        enabled: !!(pttSection.mgr && pttSection.mgr.mediaButtonEnabled)
                        checked: !!(pttSection.mgr && pttSection.mgr.mediaTrackButtonsEnabled)
                        onToggled: function(on) { if (pttSection.mgr) pttSection.mgr.mediaTrackButtonsEnabled = on }
                    }

                    // Bluetooth availability banner
                    Rectangle {
                        Layout.fillWidth: true
                        visible: !!pttSection.mgr && pttSection.mgr.bleSupported && !pttSection.btReady
                                 && (pttSection.btState !== "unknown" || pttSection.mgr.deviceCount > 0)
                        implicitHeight: btBanner.implicitHeight + 20
                        radius: 12
                        color: t.tint(t.warning, 0.12)
                        border.color: t.tint(t.warning, 0.5)
                        border.width: 1

                        RowLayout {
                            id: btBanner
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 12
                            spacing: 10
                            Label {
                                text: "\uf294"
                                font.family: "FontAwesome"
                                font.pixelSize: 16
                                color: t.warning
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: t.fsSmall + 1
                                color: t.text
                                text: pttSection.btState === "unauthorized"
                                      ? qsTr("Bluetooth permission is needed for PTT buttons.")
                                      : pttSection.btState === "poweredOff"
                                        ? qsTr("Bluetooth is off.")
                                        : pttSection.btState === "unsupported"
                                          ? qsTr("Bluetooth LE is not available on this device.")
                                          : qsTr("Waiting for Bluetooth…")
                            }
                            AppButton {
                                visible: pttSection.btState === "unauthorized"
                                text: qsTr("Open Settings")
                                onClicked: if (pttSection.mgr) pttSection.mgr.openBluetoothSettings()
                            }
                        }
                    }

                    // Saved buttons
                    Repeater {
                        model: pttSection.mgr ? pttSection.mgr.devices : []
                        delegate: Rectangle {
                            id: devRow
                            required property var modelData
                            required property int index
                            readonly property bool isBle: modelData.kind === "ble"
                            readonly property bool clickOnly: modelData.capability !== "hold"
                            readonly property color dotColor: modelData.statusKey === "connected" || modelData.statusKey === "ready" ? t.success
                                                             : modelData.statusKey === "connecting" ? t.accent
                                                             : modelData.statusKey === "sleeping" ? t.warning
                                                             : t.textMuted

                            Layout.fillWidth: true
                            implicitHeight: devCol.implicitHeight + 24
                            radius: 12
                            color: modelData.pressed ? t.tint(t.txLiveBorder, 0.18) : t.surface
                            border.color: modelData.pressed ? t.txLiveBorder : t.stroke
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: t.durFast } }

                            ColumnLayout {
                                id: devCol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Rectangle { width: 9; height: 9; radius: 4.5; color: devRow.dotColor }
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.pixelSize: t.fsBody
                                        font.bold: true
                                        color: t.text
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        radius: 8
                                        color: t.tint(t.accent, 0.14)
                                        border.color: t.tint(t.accent, 0.5)
                                        implicitWidth: kindLabel.implicitWidth + 14
                                        implicitHeight: 20
                                        Label {
                                            id: kindLabel
                                            anchors.centerIn: parent
                                            text: devRow.isBle ? qsTr("Bluetooth") : qsTr("Keyboard")
                                            font.pixelSize: t.fsCaption
                                            color: t.accent
                                        }
                                    }
                                    Switch {
                                        checked: modelData.enabled
                                        Accessible.name: qsTr("Enabled")
                                        onToggled: if (pttSection.mgr) pttSection.mgr.setDeviceEnabled(modelData.id, checked)
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: t.fsSmall
                                    color: t.textMuted
                                    text: modelData.status
                                          + (modelData.signatureName !== "" ? "  •  " + modelData.signatureName : "")
                                          + (devRow.clickOnly ? "  •  " + qsTr("click-only") : "  •  " + qsTr("press and release"))
                                          + (modelData.tentative ? "  •  " + qsTr("needs confirming") : "")
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label {
                                        text: qsTr("Behaviour")
                                        font.pixelSize: t.fsSmall
                                        color: t.textMuted
                                    }
                                    SegmentedControl {
                                        Layout.fillWidth: true
                                        options: [
                                            { label: qsTr("Auto"), value: "auto" },
                                            { label: qsTr("Hold"), value: "hold", disabled: devRow.clickOnly,
                                              disabledReason: qsTr("This button only sends clicks, so it can only toggle") },
                                            { label: qsTr("Toggle"), value: "toggle" }
                                        ]
                                        currentValue: modelData.behaviour
                                        onSelected: function(value) { if (pttSection.mgr) pttSection.mgr.setDeviceBehaviour(modelData.id, value) }
                                        onDisabledSelected: function(value, reason) { if (page.app) page.app.notify(reason, "", null, "info") }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.effective === "hold" ? qsTr("Now: hold to talk") : qsTr("Now: tap to toggle")
                                    font.pixelSize: t.fsCaption
                                    color: t.textMuted
                                    elide: Text.ElideRight
                                }
                                // Wraps onto two lines on narrow phones instead of overflowing
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    AppButton {
                                        visible: devRow.isBle
                                        text: qsTr("Re-learn")
                                        glyph: "\uf021"
                                        onClicked: {
                                            if (!pttSection.mgr) return
                                            pttSection.mgr.relearnDevice(modelData.id)
                                            pttAddSheet.openForRelearn()
                                        }
                                    }
                                    AppButton {
                                        text: qsTr("Remove")
                                        tone: "danger"
                                        glyph: "\uf1f8"
                                        onClicked: {
                                            pttRemoveDialog.targetId = modelData.id
                                            pttRemoveDialog.targetName = modelData.name
                                            pttRemoveDialog.open()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.colsToggle
                        columnSpacing: 8
                        rowSpacing: 8
                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("Add Bluetooth button")
                            tone: "primary"
                            glyph: "\uf294"
                            enabled: !!pttSection.mgr && pttSection.mgr.bleSupported
                            onClicked: pttAddSheet.openForScan()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("Add keyboard button")
                            glyph: "\uf11c"
                            enabled: !!pttSection.mgr
                            onClicked: pttAddSheet.openForKey()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !!pttSection.mgr && pttSection.mgr.lastEventText !== ""
                        text: qsTr("Last event: %1").arg(pttSection.mgr ? pttSection.mgr.lastEventText : "")
                        font.pixelSize: t.fsCaption
                        color: t.textMuted
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: t.fsCaption
                        color: t.textMuted
                        text: qsTr("Bluetooth buttons work with the screen off. Keyboard buttons work only while the app is open. For the iPhone Action Button, Siri or Shortcuts, use the DroidStar \"Push to Talk\" shortcut.")
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // MODEM TUNING (Advanced)
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Modem Tuning (Advanced)")
                expanded: false

                content: GridLayout {
                    columns: page.cols3
                    columnSpacing: 10
                    rowSpacing: 10

                    Repeater {
                        model: page.modemFields
                        delegate: FormField {
                            required property var modelData
                            label: modelData.label
                            value: page.appState ? page.appState[modelData.prop] : ""
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onEdited: function(v) { page.editState(modelData.prop, v) }
                            onCommitted: function(v) { page.commitState(modelData.prop, modelData.setter, v) }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // MAINTENANCE
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Maintenance")
                expanded: false

                content: ColumnLayout {
                    spacing: 12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.cols2
                        columnSpacing: 10
                        rowSpacing: 10

                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("Update Hosts")
                            glyph: "\uf233"
                            onClicked: if (page.app) page.app.updateHostFiles()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("Update ID Files")
                            glyph: "\uf2c2"
                            onClicked: if (page.app) page.app.updateIdFiles()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            Layout.columnSpan: page.cols2
                            text: qsTr("Clear recent talkgroups")
                            glyph: "\uf1da"
                            enabled: !!(page.appState && page.appState.recentTgids && page.appState.recentTgids.length > 0)
                            onClicked: {
                                if (!page.app) return
                                page.app.clearRecentTalkgroups()
                                page.app.notify(qsTr("Recent talkgroups cleared"), "", null, "success")
                            }
                        }
                    }

                    AppButton {
                        id: downloadButton
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("Download")
                        glyph: "\uf019"
                        tone: "primary"
                        onClicked: {
                            if (page.droidstarRef) {
                                page.droidstarRef.update_host_files()
                                page.droidstarRef.update_dmr_ids()
                            }
                            if (page.app) page.app.notify(qsTr("Download started"), "", null, "info")
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // TTS (only if USE_FLITE)
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Text-to-Speech")
                expanded: false
                visible: page.ttsAvailable

                content: ColumnLayout {
                    spacing: 12

                    SegmentedControl {
                        options: [
                            { label: qsTr("Mic"),  value: "Mic" },
                            { label: qsTr("TTS1"), value: "TTS1" },
                            { label: qsTr("TTS2"), value: "TTS2" },
                            { label: qsTr("TTS3"), value: "TTS3" }
                        ]
                        currentValue: page.appState ? page.appState.ttsVoice : "Mic"
                        onSelected: function(value) { if (page.app) page.app.setTtsVoice(value) }
                    }

                    FormField {
                        label: qsTr("TTS Text")
                        value: page.appState ? page.appState.ttsText : ""
                        placeholderText: qsTr("Text to speak on TX")
                        onEdited: function(v) { page.editState("ttsText", v) }
                        onCommitted: function(v) { if (page.app) page.app.setTtsText(v) }
                    }
                }
            }

            Item { Layout.preferredHeight: 24 }
        }
    }
    // PTT button helpers
    PttButtonAddSheet {
        id: pttAddSheet
        manager: page.pttButtonsRef
        app: page.app
    }

    AppDialog {
        id: confirmClearReplaysDialog
        maxWidth: 400
        title: qsTr("Clear all replays?")
        destructiveText: qsTr("Clear replays")
        standardButtons: Dialog.Cancel | Dialog.Discard

        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                text: (page.appState && page.app)
                      ? qsTr("All %1 replays (%2) will be permanently removed from this device. Your QSO log entries stay.")
                            .arg(page.appState.replayCount).arg(page.app.formatReplayBytes(page.appState.replayUsedBytes))
                      : ""
                wrapMode: Text.WordWrap
                color: t.text
            }
        }

        onDiscarded: {
            if (page.app) page.app.clearAllReplays()
            close()
            if (page.app) page.app.notify(qsTr("All replays cleared"), "", null, "success")
        }
    }

    AppDialog {
        id: pttRemoveDialog
        maxWidth: 400
        title: qsTr("Remove PTT button?")
        destructiveText: qsTr("Remove")
        standardButtons: Dialog.Cancel | Dialog.Discard

        property string targetId: ""
        property string targetName: ""

        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                text: qsTr("Remove \"%1\"? You can add it again later.").arg(pttRemoveDialog.targetName)
                wrapMode: Text.WordWrap
                color: t.text
            }
        }

        onDiscarded: {
            if (page.pttButtonsRef && targetId !== "") page.pttButtonsRef.forgetDevice(targetId)
            close()
            if (page.app) page.app.notify(qsTr("PTT button removed"), "", null, "info")
        }
    }
}
