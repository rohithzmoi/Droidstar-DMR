/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../components"
import "../theme"

// Simple home: connect, talk, listen. Everything else lives in Advanced (MainPage) and Settings.
// Uses only the same App2026 functions as MainPage, so behaviour is identical.
Page {
    id: page
    title: qsTr("Home")
    padding: 0

    property var app: null
    property var droidstarRef: null
    property var appState: null
    property Component headerActions: null

    Tokens { id: t }
    background: Rectangle {
        color: t.bg
        Canvas {
            id: glow
            width: Math.ceil(parent.width / 4)
            height: Math.ceil(parent.height / 4)
            scale: 4
            transformOrigin: Item.TopLeft
            smooth: true
            renderStrategy: Canvas.Immediate
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            function blob(ctx, x, y, r, c) {
                var g = ctx.createRadialGradient(x, y, 0, x, y, r)
                g.addColorStop(0, c)
                g.addColorStop(1, "rgba(0,0,0,0)")
                ctx.fillStyle = g
                ctx.fillRect(x - r, y - r, r * 2, r * 2)
            }
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var r = Math.max(width, height) * 0.55
                blob(ctx, width * 0.05, height * 0.02, r, "rgba(11,77,245,0.48)")
                blob(ctx, width * 0.98, height * 0.32, r * 0.85, "rgba(61,155,255,0.26)")
                blob(ctx, width * 0.20, height * 0.96, r * 0.9, "rgba(30,216,181,0.05)")
                blob(ctx, width * 0.88, height * 0.04, r * 0.35, "rgba(208,154,44,0.16)")
            }
        }
    }

    // DroidStar logo palette
    readonly property color brandBlue: "#0B4DF5"
    readonly property color brandSky: "#3D9BFF"
    readonly property color brandMint: "#1ED8B5"
    readonly property color brandGold: "#D09A2C"

    // BM/TGIF login check (App2026 runs it and owns the sheet); offer a way back after the sheet is dismissed
    readonly property string authNet: {
        if (!isDmr || !appState) return ""
        var h = String(appState.selectedHost || "").toUpperCase()
        return h.indexOf("BM") === 0 ? "BM" : (h.indexOf("TGIF") >= 0 ? "TGIF" : "")
    }
    readonly property bool authRejected: authNet !== "" && !connected && !!app && !!app.authStatus &&
                                         app.authStatus[authNet] === "rejected"

    // Most used first
    readonly property var modes: ["DMR", "YSF", "M17", "REF", "DCS", "XRF", "FCS", "P25", "NXDN", "IAX"]
    readonly property var modules: ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"]

    readonly property string mode: appState ? appState.mode : ""
    readonly property bool connected: !!(appState && appState.connected)
    readonly property bool connecting: !!(appState && appState.connecting)
    readonly property bool busy: connected || connecting
    readonly property bool txActive: !!(appState && appState.txActive)
    readonly property bool rxActive: connected && !txActive && !!appState && appState.data1 !== ""
    readonly property bool isDmr: mode === "DMR"
    readonly property bool usesTalkgroup: isDmr || mode === "P25" || mode === "NXDN"
    readonly property bool usesModule: mode === "REF" || mode === "DCS" || mode === "XRF" || mode === "M17"
    readonly property bool m17Direct: mode === "M17" && !!appState && appState.mmdvmDirect
    readonly property bool canChangeLink: !busy || m17Direct
    readonly property bool canChangeModule: !busy || mode === "REF" || m17Direct
    readonly property bool needsIdentity: !!appState && (appState.callsign === "" || (isDmr && appState.dmrid === ""))
    readonly property bool wide: scroller.width >= 720
    // From the page width, not the layout-assigned width, so resizes settle in one layout pass
    readonly property int pttSize: Math.round(Math.max(96, Math.min(128, (wide ? content.width / 2 : content.width) * 0.32)))
    readonly property bool animate: page.visible && Qt.application.state === Qt.ApplicationActive

    // YSF/FCS put the gateway in data1 and the talker in data2
    readonly property string rxCall: !appState ? ""
        : ((mode === "YSF" || mode === "FCS") ? (appState.data2 || appState.data1 || "") : (appState.data1 || "")).trim()
    readonly property int rxId: (isDmr && appState) ? (parseInt(appState.data2 || "0") || 0) : 0
    readonly property string currentTg: appState ? String(appState.dmrtgid || "") : ""

    // Favourites first, then recents; at most 8 chips
    readonly property var tgChips: {
        var out = [], seen = {}
        function add(list, fav) {
            for (var i = 0; list && i < list.length && out.length < 8; i++) {
                var v = String(list[i] || "").trim()
                if (v === "" || seen[v]) continue
                seen[v] = true
                out.push({ tg: v, fav: fav })
            }
        }
        if (appState) { add(appState.favouriteTgids, true); add(appState.recentTgids, false) }
        return out
    }

    function pickTalkgroup(tg, asPrivate) {
        if (!app) return
        tgField.text = tg
        tgField.committed = tg
        if (isDmr) app.quickSwitchToTalkgroup(tg, false, !!asPrivate)
        else app.commitTalkgroup(tg)
    }

    // Commits a typed talkgroup and closes the keyboard (tap outside / scroll)
    function dismissKeyboard() {
        if (!Qt.inputMethod.visible && !tgField.activeFocus && !dtmfField.activeFocus) return
        if (app) app.flushPendingEdits()
        else Qt.inputMethod.hide()
    }

    // QSO Replay, same as MainPage: hasRecording() is a plain invokable, so a revision re-evaluates rows
    readonly property var replayEngine: (app && app.replayEngineRef) ? app.replayEngineRef : null
    readonly property var replayPlayer: (app && app.replayPlayerRef) ? app.replayPlayerRef : null
    property int replayRevision: 0
    Connections {
        target: page.replayEngine
        enabled: page.replayEngine !== null
        function onStorageChanged() { page.replayRevision++ }
        function onReplayEnabledChanged() { page.replayRevision++ }
    }
    function playHeardReplay(entry) {
        if (!page.replayPlayer || !entry || !entry.latestClipId) return
        var who = entry.callsign !== "" ? entry.callsign : qsTr("ID %1").arg(entry.dmrID || 0)
        var tm = String(entry.latestClipTime || "")
        var when = tm.length >= 16 ? tm.substring(11, 16) : ""
        var label = (entry.latestClipKind === "tx" ? qsTr("My TX") + " · " : "") + who + (when !== "" ? " · " + when : "")
        page.replayPlayer.play(entry.latestClipId, label)
    }

    RecentTgSheet {
        id: tgSheet
        app: page.app
        appState: page.appState
        droidstarRef: page.droidstarRef
        mode: page.mode
        onTalkgroupChosen: function(tg, asPrivate) { page.pickTalkgroup(tg, asPrivate) }
    }

    // Shared card look
    component Card: Rectangle {
        default property alias content: cardCol.data
        property alias spacing: cardCol.spacing
        property color tint: "#DCE8FF"
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + 36
        radius: 24
        border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.16)
        border.width: 1
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(tint.r, tint.g, tint.b, 0.10) }
            GradientStop { position: 1; color: Qt.rgba(tint.r, tint.g, tint.b, 0.035) }
        }
        Behavior on tint { ColorAnimation { duration: 180 } }
        Rectangle {
            x: parent.radius; y: 0
            width: parent.width - parent.radius * 2
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.28) }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        ColumnLayout {
            id: cardCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 18
            spacing: 12
        }
    }

    component Caption: Label {
        font.pixelSize: 11
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
        color: "#9CA3AF"  // Tokens.textMuted (inline components can't see the page's ids)
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 40
        boundsBehavior: Flickable.DragOverBounds
        ScrollIndicator.vertical: ScrollIndicator {}
        onDragStarted: page.dismissKeyboard()

        // Behind the cards: only taps on empty space reach it
        MouseArea {
            width: scroller.width
            height: Math.max(scroller.contentHeight, scroller.height)
            onClicked: page.dismissKeyboard()
        }

        ColumnLayout {
            id: content
            x: Math.round((scroller.width - width) / 2)
            y: 16
            width: Math.min(scroller.width - 32, page.wide ? 1040 : 560)
            spacing: 16

            // Two columns on wide screens, stacked on phones. Placed by bindings, not GridLayout:
            // a GridLayout changing its column count during a big resize hits a polish loop.
            Item {
                id: columns
                Layout.fillWidth: true
                readonly property real colW: page.wide ? (width - 16) / 2 : width
                Layout.preferredHeight: page.wide ? Math.max(leftCol.implicitHeight, rightCol.implicitHeight)
                                                  : leftCol.implicitHeight + 16 + rightCol.implicitHeight

                // ── LEFT: status + connection ──
                ColumnLayout {
                    id: leftCol
                    width: columns.colW
                    spacing: 16

                    // STATUS HERO
                    Card {
                        id: hero
                        tint: page.txActive ? t.danger : (page.connecting ? page.brandGold : (page.connected ? page.brandSky : "#DCE8FF"))
                        readonly property color tone: page.txActive ? t.danger
                                                    : page.rxActive ? page.brandMint
                                                    : page.connected ? page.brandSky
                                                    : page.connecting ? page.brandGold : t.textMuted

                        RowLayout {
                            id: heroRow
                            Layout.fillWidth: true
                            spacing: 14

                            Item {
                                Layout.preferredWidth: 52
                                Layout.preferredHeight: 52
                                Layout.alignment: Qt.AlignVCenter

                                // Pulse while someone is on air; stopped when hidden or backgrounded
                                Rectangle {
                                    id: pulse
                                    anchors.centerIn: parent
                                    width: 52; height: 52; radius: 26
                                    color: "transparent"
                                    border.color: hero.tone
                                    border.width: 2
                                    Component.onCompleted: opacity = 0
                                    readonly property bool active: (page.rxActive || page.txActive || page.connecting) && page.animate
                                    onActiveChanged: if (!active) { opacity = 0; scale = 1 }
                                    SequentialAnimation on opacity {
                                        running: pulse.active
                                        loops: Animation.Infinite
                                        NumberAnimation { from: 0.7; to: 0; duration: 1300; easing.type: Easing.OutCubic }
                                        PauseAnimation { duration: 300 }
                                    }
                                    SequentialAnimation on scale {
                                        running: pulse.active
                                        loops: Animation.Infinite
                                        NumberAnimation { from: 1; to: 1.35; duration: 1300; easing.type: Easing.OutCubic }
                                        PauseAnimation { duration: 300 }
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 26
                                    color: t.tint(hero.tone, 0.18)
                                    border.color: t.tint(hero.tone, 0.6)
                                    border.width: 1
                                    Label {
                                        anchors.centerIn: parent
                                        text: page.txActive ? "" : page.rxActive ? ""
                                              : page.connected ? "" : page.connecting ? "" : ""
                                        font.family: "FontAwesome"
                                        font.pixelSize: 20
                                        color: hero.tone
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2

                                Caption {
                                    Layout.fillWidth: true
                                    text: page.txActive ? qsTr("You're on air")
                                          : page.rxActive ? qsTr("Now hearing")
                                          : page.connected ? qsTr("Connected")
                                          : page.connecting ? qsTr("Connecting")
                                          : qsTr("Not connected")
                                    color: hero.tone
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: page.txActive ? (appState && appState.callsign !== "" ? appState.callsign : qsTr("Transmitting"))
                                          : page.rxActive ? page.rxCall
                                          : page.connected ? qsTr("Listening")
                                          : page.connecting ? (appState ? appState.selectedHost : "")
                                          : qsTr("Ready when you are")
                                    font.pixelSize: page.rxActive || page.txActive ? 26 : 20
                                    font.bold: true
                                    color: t.text
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: {
                                        if (!appState) return ""
                                        if (page.rxActive) {
                                            var who = appState.fetchedFirstName + ((appState.fetchedFirstName !== "" && appState.fetchedCountry !== "") ? " • " : "") + appState.fetchedCountry
                                            var tg = (page.usesTalkgroup && appState.data3 !== "") ? "TG " + appState.data3 : ""
                                            return who + ((who !== "" && tg !== "") ? "  ·  " : "") + tg
                                        }
                                        if (page.connected || page.txActive) {
                                            var target = page.usesTalkgroup && page.currentTg !== "" ? "TG " + page.currentTg
                                                       : page.usesModule ? qsTr("Module %1").arg(appState.module) : ""
                                            return appState.mode + " · " + appState.selectedHost + (target !== "" ? " · " + target : "")
                                        }
                                        if (appState.netstatus !== "" && page.connecting) return appState.netstatus
                                        return qsTr("Pick a mode and server, then tap Connect.")
                                    }
                                    font.pixelSize: t.fsBody
                                    color: t.textMuted
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }

                            // Message the DMR station that is talking
                            RoundIconButton {
                                visible: page.rxActive && page.rxId > 0 && page.rxId !== parseInt(appState.dmrid || "0")
                                Layout.alignment: Qt.AlignVCenter
                                glyph: ""
                                tone: page.brandSky
                                accessibleName: qsTr("Message %1").arg(page.rxCall)
                                onClicked: if (page.app) page.app.openMessageTo(page.rxId, appState.data1, false)
                            }
                        }
                    }

                    // CONNECTION
                    Card {
                        id: connCard

                        // Missing identity. Anchors, not a RowLayout: the wrapping text would re-trigger layout.
                        Rectangle {
                            Layout.fillWidth: true
                            visible: page.needsIdentity
                            implicitHeight: Math.max(44, idText.implicitHeight + 20)
                            radius: t.rSm
                            color: t.tint(t.warning, 0.10)
                            border.color: t.tint(t.warning, 0.5)
                            Label {
                                id: idIcon
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: "\uf071"
                                font.family: "FontAwesome"
                                color: t.warning
                            }
                            Label {
                                id: idText
                                anchors.left: idIcon.right
                                anchors.leftMargin: 8
                                anchors.right: idSetup.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: appState && appState.callsign === "" ? qsTr("Add your callsign to get started.")
                                                                            : qsTr("DMR needs your DMR ID.")
                                color: t.text
                                font.pixelSize: t.fsSmall
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                id: idSetup
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Set up")
                                font.pixelSize: t.fsSmall
                                font.bold: true
                                color: t.warning
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Open settings to set up your station")
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -12
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (page.app) page.app.navigate("settings")
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: page.authRejected
                            implicitHeight: Math.max(44, authText.implicitHeight + 20)
                            radius: t.rSm
                            color: t.tint(t.danger, 0.10)
                            border.color: t.tint(t.danger, 0.5)
                            Label {
                                id: authIcon
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: "\uf023"
                                font.family: "FontAwesome"
                                color: t.danger
                            }
                            Label {
                                id: authText
                                anchors.left: authIcon.right
                                anchors.leftMargin: 8
                                anchors.right: authFix.left
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("%1 rejected your login.").arg(page.authNet === "BM" ? "BrandMeister" : page.authNet)
                                color: t.text
                                font.pixelSize: t.fsSmall
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                id: authFix
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Fix")
                                font.pixelSize: t.fsSmall
                                font.bold: true
                                color: t.danger
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Troubleshoot login")
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -12
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (page.app) page.app.showAuthSheet([page.authNet])
                                }
                            }
                        }

                        // Mode chips (locked while connected)
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: !page.busy
                            spacing: 8
                            Caption { text: qsTr("Mode") }
                            ListView {
                                id: modeList
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                orientation: ListView.Horizontal
                                spacing: 8
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: page.modes
                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property bool sel: modelData === page.mode
                                    width: modeLbl.implicitWidth + 28
                                    height: 38
                                    radius: 19
                                    color: Qt.rgba(1, 1, 1, modeMouse.pressed ? 0.14 : 0.06)
                                    gradient: sel ? selGrad : null
                                    border.color: sel ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.12)
                                    Gradient {
                                        id: selGrad
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: page.brandSky }
                                        GradientStop { position: 1; color: page.brandBlue }
                                    }
                                    border.width: 1
                                    Behavior on color { ColorAnimation { duration: t.durFast } }
                                    Accessible.role: Accessible.RadioButton
                                    Accessible.name: modelData
                                    Accessible.checked: sel
                                    Label {
                                        id: modeLbl
                                        anchors.centerIn: parent
                                        text: parent.modelData
                                        font.pixelSize: t.fsBody
                                        font.bold: parent.sel
                                        color: parent.sel ? "white" : t.text
                                    }
                                    MouseArea {
                                        id: modeMouse
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: if (page.app) page.app.setMode(parent.modelData)
                                    }
                                }
                                // Keep the chosen mode in view (it may be off-screen on small phones)
                                function reveal() {
                                    var i = page.modes.indexOf(page.mode)
                                    if (i >= 0) positionViewAtIndex(i, ListView.Contain)
                                }
                                Component.onCompleted: reveal()
                                Connections { target: page; function onModeChanged() { modeList.reveal() } }
                            }
                        }

                        // Server + module
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.canChangeLink || (page.usesModule && page.canChangeModule)
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                visible: page.canChangeLink
                                spacing: 8
                                Caption { text: qsTr("Server") }
                                SearchableComboSheet {
                                    Layout.fillWidth: true
                                    model: appState ? appState.hostsModel : []
                                    currentText: appState ? appState.selectedHost : ""
                                    sheetTitle: qsTr("Choose %1 server").arg(page.mode)
                                    placeholderText: (appState && appState.hostsModel.length === 0) ? qsTr("No servers, update host files") : qsTr("Choose server…")
                                    onActivated: function(index, text) { if (page.app) page.app.setHost(text) }
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 96
                                Layout.fillWidth: false
                                visible: page.usesModule && page.canChangeModule
                                spacing: 8
                                Caption { text: qsTr("Module") }
                                SearchableComboSheet {
                                    Layout.fillWidth: true
                                    model: page.modules
                                    currentText: appState ? appState.module : "A"
                                    sheetTitle: qsTr("Choose module")
                                    searchable: false
                                    onActivated: function(index, text) { if (page.app) page.app.setModule(text) }
                                }
                            }
                        }

                        // Talkgroup
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: page.usesTalkgroup
                            spacing: 8
                            Caption {
                                text: (page.isDmr && appState && appState.privateCall) ? qsTr("Private call ID") : qsTr("Talkgroup")
                            }
                            RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            AppTextField {
                                id: tgField
                                Layout.fillWidth: true
                                Layout.minimumWidth: 60
                                placeholderText: (page.isDmr && appState && appState.privateCall) ? qsTr("DMR ID") : qsTr("e.g. 91")
                                // Numbers-and-punctuation pad: unlike the iOS number pad it has a Done key
                                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                                EnterKey.type: Qt.EnterKeyDone
                                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
                                Accessible.name: qsTr("Talkgroup")

                                property string committed: ""
                                Component.onCompleted: { text = page.currentTg; committed = text }
                                Connections {
                                    target: page.appState
                                    function onDmrtgidChanged() {
                                        if (tgField.activeFocus) return
                                        tgField.text = page.currentTg
                                        tgField.committed = tgField.text
                                    }
                                }
                                function commit() {
                                    var v = text.trim()
                                    if (v === committed || !page.app) return
                                    committed = v
                                    if (page.isDmr && page.busy && parseInt(v) > 0)
                                        page.app.quickSwitchToTalkgroup(v, false, !!page.appState.privateCall)
                                    else
                                        page.app.commitTalkgroup(v)
                                }
                                onAccepted: page.dismissKeyboard()
                                onEditingFinished: commit()
                            }
                            RoundIconButton {
                                implicitWidth: t.controlHeight
                                implicitHeight: t.controlHeight
                                glyph: "\uf1da"
                                glyphSize: 16
                                tone: page.brandSky
                                accessibleName: qsTr("Recent and favourite talkgroups")
                                onClicked: { page.dismissKeyboard(); tgSheet.openSheet() }
                            }
                            ChipToggle {
                                visible: page.isDmr
                                Layout.fillWidth: false
                                Layout.preferredHeight: t.controlHeight
                                text: qsTr("Private")
                                glyph: "\uf007"
                                checked: !!(appState && appState.privateCall)
                                activeColor: t.warning
                                onClicked: {
                                    page.dismissKeyboard()
                                    if (page.app) page.app.setPrivateCall(!appState.privateCall)
                                }
                            }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                visible: page.tgChips.length > 0
                                orientation: ListView.Horizontal
                                spacing: 8
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                model: page.tgChips
                                    delegate: Rectangle {
                                        id: tgChip
                                        required property var modelData
                                        readonly property bool sel: modelData.tg === page.currentTg
                                        readonly property string tgName: page.app ? page.app.getTalkgroupName(modelData.tg) : ""
                                        width: Math.min(tgRow.implicitWidth + 24, 200)
                                        height: 34
                                        radius: 17
                                        color: sel ? t.tint(page.brandSky, 0.22) : Qt.rgba(1, 1, 1, tgMouse.pressed ? 0.14 : 0.06)
                                        border.color: sel ? page.brandSky : Qt.rgba(1, 1, 1, 0.12)
                                        border.width: 1
                                        Accessible.role: Accessible.Button
                                        Accessible.name: qsTr("Talkgroup %1 %2").arg(modelData.tg).arg(tgName)
                                        Row {
                                            id: tgRow
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Label {
                                                visible: tgChip.modelData.fav
                                                text: ""
                                                font.family: "FontAwesome"
                                                font.pixelSize: 11
                                                color: page.brandGold
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Label {
                                                text: tgChip.modelData.tg + (tgChip.tgName !== "" ? " · " + tgChip.tgName : "")
                                                width: Math.min(implicitWidth, 200 - 24 - (tgChip.modelData.fav ? 17 : 0))
                                                elide: Text.ElideRight
                                                font.pixelSize: t.fsSmall
                                                font.bold: tgChip.sel
                                                color: tgChip.sel ? page.brandSky : t.text
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }
                                        MouseArea {
                                            id: tgMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: page.pickTalkgroup(tgChip.modelData.tg)
                                        }
                                    }
                            }
                        }

                        // IAX keypad digits (needed to reach a node)
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.mode === "IAX" && page.connected
                            spacing: 8
                            AppTextField {
                                id: dtmfField
                                Layout.fillWidth: true
                                placeholderText: qsTr("DTMF (0-9 * #)")
                                inputMethodHints: Qt.ImhDialableCharactersOnly
                                validator: RegularExpressionValidator { regularExpression: /[0-9A-Da-d*#]*/ }
                                EnterKey.type: Qt.EnterKeySend
                                onAccepted: dtmfSend.clicked()
                            }
                            AppButton {
                                id: dtmfSend
                                text: qsTr("Send")
                                tone: "primary"
                                enabled: dtmfField.text.length > 0
                                onClicked: {
                                    if (page.droidstarRef) page.droidstarRef.dtmf_send_clicked(dtmfField.text)
                                    dtmfField.clear()
                                }
                            }
                        }

                        AppButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            font.pixelSize: 16
                            tone: page.connected ? "danger" : (page.connecting ? "warning" : "primary")
                            busy: page.connecting
                            glyph: page.connected ? "" : ""
                            text: page.connecting ? qsTr("Connecting… tap to cancel")
                                  : (page.connected ? qsTr("Disconnect") : qsTr("Connect"))
                            enabled: !!appState
                            onClicked: if (page.app) page.app.connectOrDisconnect()
                        }
                    }
                }

                // ── RIGHT: talk + recent ──
                ColumnLayout {
                    id: rightCol
                    width: columns.colW
                    x: page.wide ? columns.colW + 16 : 0
                    y: page.wide ? 0 : leftCol.implicitHeight + 16
                    spacing: 16

                    // PUSH TO TALK
                    Card {
                        spacing: 14

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: page.pttSize + 44

                            Rectangle {
                                id: ptt
                                readonly property bool armed: !!(appState && appState.txEnabled)
                                readonly property int secs: appState ? appState.txSeconds : 0
                                anchors.centerIn: parent
                                width: page.pttSize
                                height: width
                                radius: width / 2
                                readonly property color base: page.txActive ? t.txLive : page.brandBlue
                                opacity: armed ? 1 : 0.4
                                border.color: Qt.rgba(1, 1, 1, 0.28)
                                border.width: 1
                                scale: pttMouse.pressed ? 0.95 : 1
                                gradient: Gradient {
                                    GradientStop { position: 0; color: page.txActive ? Qt.lighter(ptt.base, pttMouse.pressed ? 1.15 : 1.45)
                                                                                     : (pttMouse.pressed ? page.brandBlue : page.brandSky) }
                                    GradientStop { position: 1; color: Qt.darker(ptt.base, 1.1) }
                                }

                                // Soft static halo so the button sits "on" the glass
                                Rectangle {
                                    z: -1
                                    anchors.centerIn: parent
                                    width: parent.width + 24
                                    height: width
                                    radius: width / 2
                                    color: Qt.rgba(ptt.base.r, ptt.base.g, ptt.base.b, 0.16)
                                    border.color: Qt.rgba(ptt.base.r, ptt.base.g, ptt.base.b, 0.30)
                                    border.width: 1
                                }
                                Behavior on scale { NumberAnimation { duration: t.durFast; easing.type: Easing.OutCubic } }

                                Accessible.role: Accessible.Button
                                Accessible.name: page.txActive ? qsTr("Stop transmitting") : qsTr("Push to talk")
                                Accessible.onPressAction: if (page.app) page.app.txToggle()

                                // On-air halo
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width + 34
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.color: t.txLiveBorder
                                    border.width: 2
                                    visible: page.txActive
                                    SequentialAnimation on opacity {
                                        running: page.txActive && page.animate
                                        loops: Animation.Infinite
                                        alwaysRunToEnd: true
                                        NumberAnimation { from: 0.85; to: 0.1; duration: 700; easing.type: Easing.InOutSine }
                                        NumberAnimation { from: 0.1; to: 0.85; duration: 700; easing.type: Easing.InOutSine }
                                    }
                                }

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: ""
                                        font.family: "FontAwesome"
                                        font.pixelSize: Math.round(ptt.width * 0.26)
                                        color: "white"
                                    }
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: page.txActive
                                              ? (Math.floor(ptt.secs / 60) + ":" + (ptt.secs % 60 < 10 ? "0" : "") + (ptt.secs % 60))
                                              : qsTr("TALK")
                                        font.pixelSize: 13
                                        font.bold: true
                                        font.letterSpacing: 1.5
                                        color: "white"
                                    }
                                }

                                // Same calls as TxButton; preventStealing keeps a held PTT when the page scrolls
                                MouseArea {
                                    id: pttMouse
                                    anchors.fill: parent
                                    enabled: ptt.armed
                                    preventStealing: true
                                    onPressed: if (page.app) page.app.txPress()
                                    onReleased: if (page.app) page.app.txRelease()
                                    onCanceled: if (page.app) page.app.txRelease()
                                    onClicked: if (page.app && page.appState.toggleTx) page.app.txToggle()
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: !(appState && appState.txEnabled) ? qsTr("Connect to talk")
                                  : (appState.toggleTx ? qsTr("Tap to start, tap again to stop") : qsTr("Hold to talk, release to listen"))
                            font.pixelSize: t.fsSmall
                            color: t.textMuted
                            elide: Text.ElideRight
                        }

                        // Volume
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: ""; font.family: "FontAwesome"; font.pixelSize: 15; color: t.textMuted }
                            Slider {
                                id: volSlider
                                Layout.fillWidth: true
                                from: 0; to: 1; stepSize: 0.01
                                value: appState ? appState.spkVolume : 1.0
                                Accessible.name: qsTr("Volume")
                                onMoved: if (page.app) page.app.setSpkVolume(value)
                            }
                            Label {
                                text: Math.round(volSlider.value * 100) + "%"
                                font.pixelSize: t.fsSmall
                                color: t.text
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 40
                            }
                        }
                    }

                    // RECENT STATIONS
                    Card {
                        visible: !!(appState && appState.lastHeardEntries && appState.lastHeardEntries.length > 0)
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Caption { text: qsTr("Recently heard"); Layout.fillWidth: true }
                            Label {
                                text: qsTr("See all")
                                font.pixelSize: t.fsSmall
                                font.bold: true
                                color: page.brandSky
                                Accessible.role: Accessible.Link
                                Accessible.name: qsTr("See all heard stations")
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -12
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (page.app) page.app.navigate("qso")
                                }
                            }
                        }

                        // Inline player: only takes space while a replay is loaded
                        ReplayPlayerBar {
                            Layout.fillWidth: true
                            Layout.topMargin: active ? 4 : 0
                            Layout.bottomMargin: active ? 4 : 0
                            player: page.replayPlayer
                        }

                        Repeater {
                            model: appState ? (appState.lastHeardEntries || []) : []
                            delegate: Item {
                                id: heard
                                required property var modelData
                                readonly property int heardId: modelData.dmrID || 0
                                readonly property string clipId: modelData.latestClipId || ""
                                readonly property bool hasReplay: page.replayRevision >= 0 && clipId !== "" &&
                                                                  !!page.replayEngine && page.replayEngine.hasRecording(clipId)
                                readonly property bool replayCurrent: hasReplay && !!page.replayPlayer && page.replayPlayer.currentId === clipId
                                readonly property bool replayPlaying: replayCurrent && page.replayPlayer.playing
                                readonly property bool onAir: page.rxActive && page.rxId > 0 && page.rxId === heardId
                                readonly property bool canMessage: heardId > 0 && heardId !== parseInt(appState.dmrid || "0")
                                Layout.fillWidth: true
                                // Fixed height: rows never relayout while voice packets stream in
                                implicitHeight: 52

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 12
                                    ChatAvatar {
                                        size: 36
                                        glyph: heard.onAir ? "" : ""
                                        tone: heard.onAir ? page.brandMint : page.brandSky
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 0
                                        Label {
                                            Layout.fillWidth: true
                                            text: heard.modelData.callsign !== "" ? heard.modelData.callsign : qsTr("ID %1").arg(heard.heardId)
                                            font.pixelSize: t.fsBody
                                            font.bold: true
                                            color: t.text
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: text !== ""
                                            text: {
                                                var d = heard.modelData
                                                var parts = []
                                                if (d.fname !== "") parts.push(d.fname)
                                                if (d.country !== "") parts.push(d.country)
                                                if (d.tgid > 0) parts.push("TG " + d.tgid)
                                                return parts.join(" · ")
                                            }
                                            font.pixelSize: t.fsCaption
                                            color: t.textMuted
                                            elide: Text.ElideRight
                                        }
                                    }
                                    RoundIconButton {
                                        visible: heard.hasReplay
                                        implicitWidth: 38
                                        implicitHeight: 38
                                        glyphSize: 13
                                        glyph: heard.replayPlaying ? "\uf04c" : "\uf04b"
                                        tone: heard.replayCurrent ? page.brandBlue : page.brandSky
                                        filled: heard.replayCurrent
                                        badge: (heard.modelData.clipCount || 0) > 1 ? heard.modelData.clipCount : 0
                                        accessibleName: heard.replayPlaying
                                                        ? qsTr("Pause replay of %1").arg(heard.modelData.callsign || heard.heardId)
                                                        : qsTr("Replay %1").arg(heard.modelData.callsign || heard.heardId)
                                        onClicked: page.playHeardReplay(heard.modelData)
                                    }
                                    RoundIconButton {
                                        visible: heard.canMessage
                                        implicitWidth: 38
                                        implicitHeight: 38
                                        glyphSize: 15
                                        glyph: ""
                                        tone: t.textMuted
                                        accessibleName: qsTr("Message %1").arg(heard.modelData.callsign || heard.heardId)
                                        onClicked: if (page.app) page.app.openMessageTo(heard.heardId, heard.modelData.callsign, false)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Way out to the full controls
            Button {
                Layout.alignment: Qt.AlignHCenter
                flat: true
                text: qsTr("Need more controls? Switch to Advanced")
                font.pixelSize: t.fsSmall
                onClicked: if (page.app) page.app.setSimpleHome(false)
            }
        }
    }
}
