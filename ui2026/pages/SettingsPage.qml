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
    title: qsTr("Settings")
    padding: 0

    required property var droidstarRef
    required property var appState

    Tokens { id: t }

    // iOS keyboard/focus can skip onEditingFinished; debounce commits instead.
    Timer {
        id: commitTimer
        interval: 350
        repeat: false
        property var fn: null
        onTriggered: { if (fn) fn(); fn = null }
    }
    function scheduleCommit(f) {
        commitTimer.fn = f
        commitTimer.restart()
    }

    // Section expansion states (remember user preference)
    property bool identityExpanded: true
    property bool audioExpanded: false
    property bool dstarExpanded: false
    property bool profileExpanded: false
    property bool dmrExpanded: false
    property bool networkExpanded: false
    property bool modemExpanded: false
    property bool maintenanceExpanded: false
    property bool ttsExpanded: false
    
    // Force Flickable to recalculate height when sections expand/collapse
    function updateScrollHeight() {
        if (flick) {
            flick.contentHeight = flick.contentHeight + 0.01 // Force recalculation
            flick.contentHeight = wrapperItem.height
        }
    }

    // Models (legacy parity)
    readonly property var essidModel: (function(){
        var ids = ["None"];
        for (var i = 0; i < 100; i++) ids.push(i.toString().padStart(2, "0"));
        return ids;
    })()

    background: Rectangle { color: t.bg }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: heightTracker.height
        clip: true

        Item {
            id: heightTracker
            width: 1
            height: contentLayout.childrenRect.height + 40
            
            // Watch for changes in any section's expanded state to trigger recalculation
            property real trigger: page.identityExpanded + page.audioExpanded + page.dstarExpanded + 
                                  page.profileExpanded + page.dmrExpanded + page.networkExpanded + 
                                  page.modemExpanded + page.maintenanceExpanded + page.ttsExpanded
            onTriggerChanged: {
                Qt.callLater(function() {
                    heightTracker.height = contentLayout.childrenRect.height + 40
                })
            }
            
            // Timer to periodically check and update height (catches any missed updates)
            Timer {
                interval: 100
                running: true
                repeat: true
                onTriggered: {
                    var newHeight = contentLayout.childrenRect.height + 40
                    if (Math.abs(heightTracker.height - newHeight) > 1) {
                        heightTracker.height = newHeight
                    }
                }
            }
        }

        ColumnLayout {
            id: contentLayout
            width: parent.width
            spacing: 14

            // ─────────────────────────────────────────────────────────
            // IDENTITY
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Identity")
                expanded: page.identityExpanded
                onExpandedChanged: page.identityExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Callsign"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.callsign : ""
                                font.capitalization: Font.AllUppercase
                                placeholderText: qsTr("e.g. W1AW")
                                onTextEdited: {
                                    if (!page.appState) return
                                    page.appState.callsign = text.toUpperCase()
                                    scheduleCommit(function(){ page.droidstarRef.set_callsign(page.appState.callsign) })
                                }
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.callsign = text.toUpperCase()
                                    page.droidstarRef.set_callsign(page.appState.callsign)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 120
                            spacing: 4
                            Label { text: qsTr("DMR ID"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.dmrid : ""
                                inputMethodHints: Qt.ImhDigitsOnly
                                placeholderText: qsTr("7 digits")
                                onTextEdited: {
                                    if (!page.appState) return
                                    page.appState.dmrid = text
                                    scheduleCommit(function(){ page.droidstarRef.set_dmrid(page.appState.dmrid) })
                                }
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.dmrid = text
                                    page.droidstarRef.set_dmrid(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 90
                            spacing: 4
                            Label { text: qsTr("ESSID"); font.pixelSize: 11; opacity: 0.6 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: page.essidModel
                                currentIndex: page.appState ? Math.max(0, model.indexOf(page.appState.essid)) : 0
                                onActivated: {
                                    if (!page.appState) return
                                    page.appState.essid = currentText
                                    page.droidstarRef.set_essid(currentText)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("BM Password"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                text: page.appState ? page.appState.bmPass : ""
                                placeholderText: qsTr("Brandmeister")
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.bmPass = text
                                    page.droidstarRef.set_bm_password(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("TGIF Password"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                text: page.appState ? page.appState.tgifPass : ""
                                placeholderText: qsTr("TGIF")
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.tgifPass = text
                                    page.droidstarRef.set_tgif_password(text)
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // AUDIO
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Audio Devices")
                expanded: page.audioExpanded
                onExpandedChanged: page.audioExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Playback"); font.pixelSize: 11; opacity: 0.6 }
                            ComboBox {
                                id: playbackCombo
                                Layout.fillWidth: true
                                model: page.droidstarRef.get_playbacks()
                                onActivated: {
                                    page.droidstarRef.setPlaybackDevice(currentText)
                                    page.droidstarRef.set_playback(currentText)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Capture"); font.pixelSize: 11; opacity: 0.6 }
                            ComboBox {
                                id: captureCombo
                                Layout.fillWidth: true
                                model: page.droidstarRef.get_captures()
                                onActivated: {
                                    page.droidstarRef.setCaptureDevice(currentText)
                                    page.droidstarRef.set_capture(currentText)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Vocoder"); font.pixelSize: 11; opacity: 0.6 }
                            ComboBox {
                                id: vocoderCombo
                                Layout.fillWidth: true
                                model: page.droidstarRef.get_vocoders()
                                onActivated: page.droidstarRef.set_vocoder(currentText)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Modem"); font.pixelSize: 11; opacity: 0.6 }
                            ComboBox {
                                id: modemCombo
                                Layout.fillWidth: true
                                model: page.droidstarRef.get_modems()
                                onActivated: page.droidstarRef.set_modem(currentText)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 40
                            radius: 10
                            color: (page.appState && page.appState.mmdvmDirect) ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                            border.color: (page.appState && page.appState.mmdvmDirect) ? t.accent : t.stroke
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                Label { text: qsTr("MMDVM Direct"); Layout.fillWidth: true }
                                Switch {
                                    checked: page.appState ? page.appState.mmdvmDirect : false
                                    onToggled: {
                                        if (!page.appState) return
                                        page.appState.mmdvmDirect = checked
                                        page.droidstarRef.set_mmdvm_direct(checked)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 40
                            radius: 10
                            color: (page.appState && page.appState.debug) ? Qt.rgba(t.warning.r, t.warning.g, t.warning.b, 0.2) : t.surface2
                            border.color: (page.appState && page.appState.debug) ? t.warning : t.stroke
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                Label { text: qsTr("Debug"); Layout.fillWidth: true }
                                Switch {
                                    checked: page.appState ? page.appState.debug : false
                                    onToggled: {
                                        if (!page.appState) return
                                        page.appState.debug = checked
                                        page.droidstarRef.set_debug(checked)
                                    }
                                }
                            }
                        }
                    }

                    // Status fields
                    Label { 
                        text: page.appState && page.appState.ambestatus ? page.appState.ambestatus : ""; 
                        opacity: 0.7; 
                        wrapMode: Text.WordWrap; 
                        visible: !!(page.appState && page.appState.ambestatus && page.appState.ambestatus !== "") 
                    }
                    Label { 
                        text: page.appState && page.appState.mmdvmstatus ? page.appState.mmdvmstatus : ""; 
                        opacity: 0.7; 
                        wrapMode: Text.WordWrap; 
                        visible: !!(page.appState && page.appState.mmdvmstatus && page.appState.mmdvmstatus !== "") 
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // D-STAR / ROUTING
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("D-STAR / Routing")
                expanded: page.dstarExpanded
                onExpandedChanged: page.dstarExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("MYCALL"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                id: mycallField
                                Layout.fillWidth: true
                                text: ""
                                font.capitalization: Font.AllUppercase
                                Component.onCompleted: { if (page.appState) text = page.appState.mycall }
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.mycall = text.toUpperCase()
                                    page.droidstarRef.set_mycall(page.appState.mycall)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("URCALL"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                id: urcallField
                                Layout.fillWidth: true
                                text: ""
                                font.capitalization: Font.AllUppercase
                                Component.onCompleted: { if (page.appState) text = page.appState.urcall }
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.urcall = text.toUpperCase()
                                    page.droidstarRef.set_urcall(page.appState.urcall)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("RPTR1"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                id: rptr1Field
                                Layout.fillWidth: true
                                text: ""
                                font.capitalization: Font.AllUppercase
                                Component.onCompleted: { if (page.appState) text = page.appState.rptr1 }
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.rptr1 = text.toUpperCase()
                                    page.droidstarRef.set_rptr1(page.appState.rptr1)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("RPTR2"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                id: rptr2Field
                                Layout.fillWidth: true
                                text: ""
                                font.capitalization: Font.AllUppercase
                                Component.onCompleted: { if (page.appState) text = page.appState.rptr2 }
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.rptr2 = text.toUpperCase()
                                    page.droidstarRef.set_rptr2(page.appState.rptr2)
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("User Text"); font.pixelSize: 11; opacity: 0.6 }
                        TextField {
                            id: usrtxtField
                            Layout.fillWidth: true
                            text: ""
                            Component.onCompleted: { if (page.appState) text = page.appState.usrtxt }
                            onEditingFinished: {
                                if (!page.appState) return
                                page.appState.usrtxt = text
                                page.droidstarRef.set_usrtxt(text)
                            }
                        }
                    }

                    // Legacy parity: keep fields updated from backend signals.
                    // Use explicit assignment so edits don't break live updates.
                    Connections {
                        target: page.appState
                        enabled: !!page.appState
                        function onMycallChanged() { if (!mycallField.activeFocus) mycallField.text = page.appState.mycall }
                        function onUrcallChanged() { if (!urcallField.activeFocus) urcallField.text = page.appState.urcall }
                        function onRptr1Changed() { if (!rptr1Field.activeFocus) rptr1Field.text = page.appState.rptr1 }
                        function onRptr2Changed() { if (!rptr2Field.activeFocus) rptr2Field.text = page.appState.rptr2 }
                        function onUsrtxtChanged() { if (!usrtxtField.activeFocus) usrtxtField.text = page.appState.usrtxt }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // PROFILE
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Profile / Location")
                expanded: page.profileExpanded
                onExpandedChanged: page.profileExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Latitude"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.latitude : ""
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                placeholderText: qsTr("e.g. 40.7128")
                                onEditingFinished: {
                                    page.appState.latitude = text
                                    page.droidstarRef.set_latitude(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("Longitude"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.longitude : ""
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                placeholderText: qsTr("e.g. -74.0060")
                                onEditingFinished: {
                                    page.appState.longitude = text
                                    page.droidstarRef.set_longitude(text)
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("Location"); font.pixelSize: 11; opacity: 0.6 }
                        TextField {
                            Layout.fillWidth: true
                            text: page.appState ? page.appState.location : ""
                            placeholderText: qsTr("City, State/Country")
                            onEditingFinished: {
                                page.appState.location = text
                                page.droidstarRef.set_location(text)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("Description"); font.pixelSize: 11; opacity: 0.6 }
                        TextField {
                            Layout.fillWidth: true
                            text: page.appState ? page.appState.description : ""
                            onEditingFinished: {
                                if (!page.appState) return
                                page.appState.description = text
                                page.droidstarRef.set_description(text)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("URL"); font.pixelSize: 11; opacity: 0.6 }
                        TextField {
                            Layout.fillWidth: true
                            text: page.appState ? page.appState.url : ""
                            inputMethodHints: Qt.ImhUrlCharactersOnly
                            placeholderText: qsTr("https://...")
                            onEditingFinished: {
                                page.appState.url = text
                                page.droidstarRef.set_url(text)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("SWID"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.swid : ""
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.swid = text
                                    page.droidstarRef.set_swid(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("PKGID"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.pkgid : ""
                                onEditingFinished: {
                                    if (!page.appState) return
                                    page.appState.pkgid = text
                                    page.droidstarRef.set_pkgid(text)
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // DMR / TX OPTIONS
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("DMR / TX Options")
                expanded: page.dmrExpanded
                onExpandedChanged: page.dmrExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("DMR Options"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.dmrOptions : ""
                                onEditingFinished: {
                                    page.appState.dmrOptions = text
                                    page.droidstarRef.set_dmr_options(text)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 120
                            spacing: 4
                            Label { text: qsTr("TX Timeout (s)"); font.pixelSize: 11; opacity: 0.6 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.appState ? page.appState.txTimeout : ""
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: {
                                    page.appState.txTimeout = text
                                    page.droidstarRef.set_txtimeout(text)
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("M17 / YSF Rate"); font.pixelSize: 11; opacity: 0.6 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ButtonGroup { id: rateGroup }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 40
                                radius: 10
                                color: voiceFullRadio.checked ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                                border.color: voiceFullRadio.checked ? t.accent : t.stroke
                                border.width: 1

                                RadioButton {
                                    id: voiceFullRadio
                                    anchors.centerIn: parent
                                    text: qsTr("Voice Full")
                                    checked: true
                                    ButtonGroup.group: rateGroup
                                    onClicked: page.droidstarRef.m17_rate_changed(true)
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 40
                                radius: 10
                                color: voiceDataRadio.checked ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                                border.color: voiceDataRadio.checked ? t.accent : t.stroke
                                border.width: 1

                                RadioButton {
                                    id: voiceDataRadio
                                    anchors.centerIn: parent
                                    text: qsTr("Voice/Data")
                                    ButtonGroup.group: rateGroup
                                    onClicked: page.droidstarRef.m17_rate_changed(false)
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // NETWORK
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Network Options")
                expanded: page.networkExpanded
                onExpandedChanged: page.networkExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            height: 44
                            radius: 10
                            color: (page.appState && page.appState.ipv6) ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                            border.color: (page.appState && page.appState.ipv6) ? t.accent : t.stroke
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                Label { text: qsTr("IPv6"); Layout.fillWidth: true }
                                Switch {
                                    checked: page.appState ? page.appState.ipv6 : false
                                    onToggled: {
                                        if (!page.appState) return
                                        page.appState.ipv6 = checked
                                        page.droidstarRef.set_ipv6(checked)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 44
                            radius: 10
                            color: (page.appState && page.appState.xrf2ref) ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                            border.color: (page.appState && page.appState.xrf2ref) ? t.accent : t.stroke
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                Label { text: qsTr("XRF→REF"); Layout.fillWidth: true }
                                Switch {
                                    checked: page.appState ? page.appState.xrf2ref : false
                                    onToggled: {
                                        if (!page.appState) return
                                        page.appState.xrf2ref = checked
                                        page.droidstarRef.set_xrf2ref(checked)
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 44
                        radius: 10
                        color: (page.appState && page.appState.toggleTx) ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                        border.color: (page.appState && page.appState.toggleTx) ? t.accent : t.stroke
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: qsTr("Toggle TX Mode") }
                                Label { text: qsTr("Tap to toggle instead of hold-to-talk"); font.pixelSize: 10; opacity: 0.5 }
                            }
                            Switch {
                                checked: page.appState ? page.appState.toggleTx : false
                                onToggled: {
                                    if (!page.appState) return
                                    page.appState.toggleTx = checked
                                    page.droidstarRef.set_toggletx(checked)
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // MODEM TUNING (Advanced)
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Modem Tuning (Advanced)")
                expanded: page.modemExpanded
                onExpandedChanged: page.modemExpanded = expanded

                content: ColumnLayout {
                    spacing: 10

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 8

                        Label { text: qsTr("RX Freq"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemRxFreq : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemRxFreq=text; page.droidstarRef.set_modemRxFreq(text) } }

                        Label { text: qsTr("TX Freq"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemTxFreq : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemTxFreq=text; page.droidstarRef.set_modemTxFreq(text) } }

                        Label { text: qsTr("RX Offset"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemRxOffset : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemRxOffset=text; page.droidstarRef.set_modemRxOffset(text) } }

                        Label { text: qsTr("TX Offset"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemTxOffset : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemTxOffset=text; page.droidstarRef.set_modemTxOffset(text) } }

                        Label { text: qsTr("RX DC Offset"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemRxDCOffset : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemRxDCOffset=text; page.droidstarRef.set_modemRxDCOffset(text) } }

                        Label { text: qsTr("TX DC Offset"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemTxDCOffset : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemTxDCOffset=text; page.droidstarRef.set_modemTxDCOffset(text) } }

                        Label { text: qsTr("RX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemRxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemRxLevel=text; page.droidstarRef.set_modemRxLevel(text) } }

                        Label { text: qsTr("TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemTxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemTxLevel=text; page.droidstarRef.set_modemTxLevel(text) } }

                        Label { text: qsTr("RF Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemRFLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemRFLevel=text; page.droidstarRef.set_modemRFLevel(text) } }

                        Label { text: qsTr("TX Delay"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemTxDelay : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemTxDelay=text; page.droidstarRef.set_modemTxDelay(text) } }

                        Label { text: qsTr("CWID TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemCWIdTxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemCWIdTxLevel=text; page.droidstarRef.set_modemCWIdTxLevel(text) } }

                        Label { text: qsTr("D-STAR TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemDstarTxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemDstarTxLevel=text; page.droidstarRef.set_modemDstarTxLevel(text) } }

                        Label { text: qsTr("DMR TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemDMRTxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemDMRTxLevel=text; page.droidstarRef.set_modemDMRTxLevel(text) } }

                        Label { text: qsTr("YSF TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemYSFTxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemYSFTxLevel=text; page.droidstarRef.set_modemYSFTxLevel(text) } }

                        Label { text: qsTr("P25 TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemP25TxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemP25TxLevel=text; page.droidstarRef.set_modemP25TxLevel(text) } }

                        Label { text: qsTr("NXDN TX Level"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemNXDNTxLevel : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemNXDNTxLevel=text; page.droidstarRef.set_modemNXDNTxLevel(text) } }

                        Label { text: qsTr("Baud"); opacity: 0.6; font.pixelSize: 11 }
                        TextField { Layout.fillWidth: true; text: page.appState ? page.appState.modemBaud : ""; onEditingFinished: { if (!page.appState) return; page.appState.modemBaud=text; page.droidstarRef.set_modemBaud(text) } }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // MAINTENANCE
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Maintenance")
                expanded: page.maintenanceExpanded
                onExpandedChanged: page.maintenanceExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Update Hosts")
                            onClicked: page.droidstarRef.update_host_files()
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Update ID Files")
                            onClicked: page.droidstarRef.update_dmr_ids()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("Download File"); font.pixelSize: 11; opacity: 0.6 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TextField {
                                id: downloadUrl
                                Layout.fillWidth: true
                                placeholderText: qsTr("URL (vocoder/hosts/etc)")
                                inputMethodHints: Qt.ImhUrlCharactersOnly
                            }
                            Button {
                                text: qsTr("Download")
                                onClicked: page.droidstarRef.download_file(downloadUrl.text, true)
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────────
            // TTS (only if USE_FLITE)
            // ─────────────────────────────────────────────────────────
            CollapsibleSection {
                title: qsTr("Text-to-Speech")
                expanded: page.ttsExpanded
                visible: (typeof USE_FLITE !== "undefined" && USE_FLITE)
                onExpandedChanged: page.ttsExpanded = expanded

                content: ColumnLayout {
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ButtonGroup { id: ttsGroup }

                        Repeater {
                            model: [qsTr("None"), qsTr("Voice 1"), qsTr("Voice 2")]
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                height: 40
                                radius: 10
                                color: ttsRadio.checked ? Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.2) : t.surface2
                                border.color: ttsRadio.checked ? t.accent : t.stroke
                                border.width: 1

                                RadioButton {
                                    id: ttsRadio
                                    anchors.centerIn: parent
                                    text: modelData
                                    checked: index === 0
                                    ButtonGroup.group: ttsGroup
                                    onClicked: page.droidstarRef.tts_changed(text)
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("TTS Text"); font.pixelSize: 11; opacity: 0.6 }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Text to speak")
                            onEditingFinished: page.droidstarRef.tts_text_changed(text)
                        }
                    }
                }
            }

            // Bottom spacer
            Item { Layout.preferredHeight: 40 }
        }
    }
}
