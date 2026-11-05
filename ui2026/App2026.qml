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
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.settings

import "theme"
import "components"
import "pages"

ApplicationWindow {
    id: window

    readonly property bool isMobile: (Qt.platform.os === "ios" || Qt.platform.os === "android")

    visible: true
    visibility: isMobile ? Window.FullScreen : Window.Windowed
    width: isMobile ? Screen.width : 430
    height: isMobile ? Screen.height : 820
    minimumWidth: isMobile ? 0 : 320
    minimumHeight: isMobile ? 0 : 480
    title: qsTr("DroidStar")

    Tokens { id: t }

    // Persisted UI settings (independent from backend/QSettings)
    Settings {
        id: uiSettings
        category: "qso"
        property bool disableSelfLog: true  // Default to disabled (exclude self logging)
        property int qsoLogLimit: 250
    }

    // Runtime radio controls the backend doesn't persist (or resets on every connect).
    Settings {
        id: controlSettings
        category: "ui2026"
        property real micGain: 0.5
        property real spkVolume: 1.0
        property real spkPreampDb: 6.0
        property bool agc: true
        property int dmrSlot: 1
        property int dmrCc: 1
        property string m17Can: "0"
        property bool m17VoiceFull: true
        property string ttsVoice: "Mic"
        property bool transcriptionEnabled: false
        property string transcriptionEngine: "apple"
        property string transcriptionLanguage: "auto"
        property string transcriptionModel: "base"
        property string smsRestoreTg: ""
        property bool smsRestorePc: false
        property bool simpleHome: false
    }

    // Material base
    Material.theme: Material.Dark
    Material.accent: t.accent
    Material.primary: t.surface
    color: t.bg

    // FontAwesome - use absolute QRC path
    FontLoader { id: fa; source: "qrc:/DroidStar/fontawesome-webfont.ttf" }

    // Dialogs
    MessageDialog {
        id: errorDialog
        title: qsTr("Error")
    }

    // Use the C++ DroidStar instance exposed via context property (has loaded settings)
    property var droidstar: (typeof droidStar !== "undefined") ? droidStar : null

    // Context properties exposed from C++ (existing backend)
    property var vuidUpdaterRef: (typeof vuidUpdater !== "undefined") ? vuidUpdater : null
    property var logHandlerRef: (typeof logHandler !== "undefined") ? logHandler : null
    property var liveActivityRef: (typeof liveActivity !== "undefined") ? liveActivity : null
    property var transcriptionEngineRef: (typeof transcriptionEngine !== "undefined") ? transcriptionEngine : null
    property var smsManagerRef: (typeof smsManager !== "undefined") ? smsManager : null
    // Engine turn ids restart at 0 each launch; logged turns are keyed by session so a new turn never overwrites an old one
    readonly property string _transcriptSessionId: Date.now().toString(36)
    property var netReminderManagerRef: (typeof netReminderManager !== "undefined") ? netReminderManager : null
    // Hardware PTT buttons (headset media button, Bluetooth LE buttons, keyboard-type buttons)
    property var pttButtonsRef: (typeof pttButtons !== "undefined") ? pttButtons : null
    // QSO Replay (on-device replay of received / own overs attached to the QSO log)
    property var replayEngineRef: (typeof replayEngine !== "undefined") ? replayEngine : null
    property var replayPlayerRef: (typeof replayPlayer !== "undefined") ? replayPlayer : null

    // Realtime QSO logging (legacy parity) - driven by VUIDUpdater replies
    property int _pendingLogDmr: 0
    property int _pendingLogTgid: 0
    property string _pendingLogKey: ""
    property string _lastLoggedKey: ""

    // Flag to track when initialization is complete
    property bool initialized: false
    property bool initializing: false

    // ─────────────────────────────────────────────────────────
    // SAFE AREA (notch / Dynamic Island / home indicator / landscape sensor housing)
    // ─────────────────────────────────────────────────────────
    QtObject {
        id: safeAreaObj
        property real top: 0
        property real left: 0
        property real bottom: 0
        property real right: 0
    }
    readonly property var safeArea: safeAreaObj

    function refreshSafeArea() {
        if (window.droidstar && typeof window.droidstar.get_safe_area === "function") {
            var m = window.droidstar.get_safe_area()
            if (m && m.valid) {
                safeAreaObj.top = m.top
                safeAreaObj.left = m.left
                safeAreaObj.bottom = m.bottom
                safeAreaObj.right = m.right
                return
            }
        }
        // Fallback for builds without the native hook: legacy portrait values.
        if (Qt.platform.os === "ios") {
            var portrait = window.height >= window.width
            safeAreaObj.top = portrait ? 47 : 0
            safeAreaObj.bottom = portrait ? 20 : 21
            safeAreaObj.left = portrait ? 0 : 47
            safeAreaObj.right = portrait ? 0 : 47
        }
    }

    // UIKit updates insets after the rotation layout pass; query shortly after.
    Timer { id: safeAreaTimer; interval: 80; onTriggered: window.refreshSafeArea() }
    onWidthChanged: safeAreaTimer.restart()
    onHeightChanged: safeAreaTimer.restart()

    // ─────────────────────────────────────────────────────────
    // RESPONSIVE NAVIGATION
    // ─────────────────────────────────────────────────────────
    // Tablets/desktop only: a phone in landscape is wide but too short for a permanent sidebar.
    readonly property bool navDocked: width >= t.dockedNavBreakpoint && height >= 560
    readonly property bool compact: width < 400
    // open()/close() replace the Drawer's position binding, so both directions are driven here
    onNavDockedChanged: navDocked ? navDrawer.open() : navDrawer.close()

    readonly property var pageKeys: ["main", "simple", "scan", "messages", "settings", "qso", "nets", "log", "hosts", "about"]
    property string currentPage: "main"
    // "main" is Home; Simple mode shows it through the "simple" page slot
    readonly property string _pageSlot: (currentPage === "main" && controlSettings.simpleHome) ? "simple" : currentPage
    property var _visited: []
    property string pendingMessageThread: ""
    property double _lastScanHopMs: 0
    property double _lastRxEndMs: 0
    // Manual Tune holds the scanner here so a live update cannot hop away immediately.
    property double _scanHoldUntilMs: 0
    property double _lastRelinkAttemptMs: 0
    property int _relinkRetryCount: 0
    property bool _heardCurrentTg: false
    property string _resumeTuneTg: ""
    property bool _resumeTuneSilent: false

    readonly property var currentPageItem: {
        var idx = pageKeys.indexOf(_pageSlot)
        if (idx < 0 || pageRepeater.count <= idx) return null
        var l = pageRepeater.itemAt(idx)
        return l ? l.item : null
    }

    function _markVisited(key) {
        if (window._visited.indexOf(key) >= 0) return
        var v = window._visited.slice()
        v.push(key)
        window._visited = v
    }

    function setSimpleHome(on) {
        if (controlSettings.simpleHome === !!on) return
        flushPendingEdits()
        controlSettings.simpleHome = !!on
        window.updateHoselineStreamLifecycle()
    }

    function navigate(key) {
        if (pageKeys.indexOf(key) < 0 || key === "simple") return
        if (window.smsManagerRef && typeof window.smsManagerRef.setChatKeyboardMode === "function") {
            window.smsManagerRef.setChatKeyboardMode(key === "messages")
        }
        _markVisited(key)
        window.currentPage = key
        if (!navDocked) navDrawer.close()
        window.updateHoselineStreamLifecycle()
    }

    function notify(message, actionText, action, kind) {
        toaster.show(message, actionText, action, kind)
    }

    // ── BrandMeister / TGIF login check ──
    // Settings Verify, auto check on password/ID commit, and the sheet for a rejected connect.
    property var authStatus: ({ BM: "", TGIF: "" })  // "", checking, ok, default, rejected, unreachable
    property var _authBatch: []
    property var _authCheckedKey: ({})               // network -> "id|password" last answered

    function _authKey(n) { return String(appState.dmrid) + "|" + (n === "TGIF" ? appState.tgifPass : appState.bmPass) }
    function _setAuthStatus(n, s) {
        var o = Object.assign({}, window.authStatus)
        o[n] = s
        window.authStatus = o
    }
    function _authName(n) { return n === "TGIF" ? "TGIF" : "BrandMeister" }

    function verifyDmrAuth(nets, auto) {
        if (!droidstar || typeof droidstar.verify_dmr_auth !== "function") return
        var hasId = parseInt(appState.dmrid) > 0
        var batch = window._authBatch.slice()
        for (var i = 0; i < nets.length; i++) {
            var n = nets[i]
            var pass = (n === "TGIF") ? appState.tgifPass : appState.bmPass
            if (!hasId || !pass) { window._setAuthStatus(n, ""); continue }
            // Auto checks run once per id/password pair; a missed reply is worth retrying.
            if (auto && window._authCheckedKey[n] === window._authKey(n) && window.authStatus[n] !== "unreachable") continue
            if (batch.indexOf(n) < 0) batch.push(n)
            window._setAuthStatus(n, "checking")
            droidstar.verify_dmr_auth(n)
        }
        window._authBatch = batch
    }

    function showAuthSheet(nets) {
        var merged = authSheet.opened ? authSheet.networks.slice() : []
        for (var i = 0; i < nets.length; i++) if (merged.indexOf(nets[i]) < 0) merged.push(nets[i])
        authSheet.show(merged)
    }

    function _finishAuthBatch() {
        var batch = window._authBatch
        window._authBatch = []
        var failed = false, ok = [], securityOff = false
        for (var i = 0; i < batch.length; i++) {
            var s = window.authStatus[batch[i]]
            if (s === "rejected" || s === "unreachable") failed = true
            else if (s === "ok" || s === "default") { ok.push(window._authName(batch[i])); if (s === "default") securityOff = true }
        }
        if (failed || authSheet.opened) { window.showAuthSheet(batch); return }
        if (ok.length === 0) return
        var msg = ok.length > 1 ? qsTr("%1 and %2 login verified").arg(ok[0]).arg(ok[1])
                                : qsTr("%1 login verified").arg(ok[0])
        if (securityOff) msg += qsTr(". TGIF hotspot security is off")
        window.notify(msg, "", null, "success")
    }

    Connections {
        target: droidstar
        enabled: droidstar !== null
        ignoreUnknownSignals: true
        function onDmr_auth_result(network, result) {
            var n = String(network)
            if (window.authStatus[n] !== "checking") return
            var r = (result === "missing") ? "" : String(result)
            if (r !== "unreachable") {
                var k = Object.assign({}, window._authCheckedKey)
                k[n] = window._authKey(n)
                window._authCheckedKey = k
            }
            window._setAuthStatus(n, r)
            for (var i = 0; i < window._authBatch.length; i++)
                if (window.authStatus[window._authBatch[i]] === "checking") return
            window._finishAuthBatch()
        }
    }

    // A connect that hangs with IPv6 on is almost always a host without a usable IPv6 route:
    // turn IPv6 off and reconnect once. M17 is skipped since its hosts carry explicit IPv6 addresses.
    Timer {
        id: ipv6Watch
        interval: 6000
        onTriggered: {
            if (!droidstar || !appState.connecting || appState.connected || !appState.ipv6 || appState.mode === "M17") return
            appState.ipv6 = false
            droidstar.set_ipv6(false)
            window.notify(qsTr("IPv6 was blocking the connection. Turned it off and reconnecting."), "", null, "info")
            window.connectOrDisconnect()               // cancel the stuck attempt
            Qt.callLater(window.connectOrDisconnect)   // and connect again over IPv4
        }
    }

    function openMessageTo(dmrId, callsign, isGroup) {
        if (!window.smsManagerRef) return
        var id = parseInt(dmrId || "0") || 0
        if (id <= 0 && callsign) {
            var cleanCs = window._extractBaseCallsign(callsign)
            if (cleanCs !== "")
                id = parseInt(window.smsManagerRef.resolveTarget(cleanCs, !!isGroup).id || "0") || 0
            if (id <= 0)
                id = parseInt(window.smsManagerRef.resolveTarget(String(callsign), !!isGroup).id || "0") || 0
        }
        if (id <= 0) {
            window.notify(qsTr("No DMR ID is available for this station."), "", null, "warning")
            return
        }
        var key = window.smsManagerRef.threadKeyFor(id, !!isGroup)
        window.pendingMessageThread = key
        window.navigate("messages")
        window.smsManagerRef.openThread(key)
    }

    // SMS standby: private call to our own ID, so no voice talkgroup is linked while messaging.
    // The Home page target is remembered and restored on disconnect or when voice is re-linked.
    property bool smsStandbyActive: false
    property string smsStandbyRestoreTg: ""
    property bool smsStandbyRestorePc: false

    function _rememberHomeTarget() {
        if (window.smsStandbyActive) return
        var ownId = String(parseInt(appState.dmrid) || "")
        var cur = String(appState.dmrtgid || "")
        window.smsStandbyRestoreTg = (cur !== ownId) ? cur : ""
        window.smsStandbyRestorePc = !!appState.privateCall
        controlSettings.smsRestoreTg = window.smsStandbyRestoreTg
        controlSettings.smsRestorePc = window.smsStandbyRestorePc
    }

    function _forgetSmsStandby() {
        window.smsStandbyActive = false
        controlSettings.smsRestoreTg = ""
        controlSettings.smsRestorePc = false
    }

    function _restoreHomeTarget() {
        if (!window.smsStandbyActive) return
        var tg = window.smsStandbyRestoreTg
        var pc = window.smsStandbyRestorePc
        window._forgetSmsStandby()
        if (!droidstar || tg === "") return
        appState.privateCall = pc
        appState.dmrtgid = tg
        droidstar.set_dmr_pc(pc ? 1 : 0)
        droidstar.set_dmrtgid(tg)
        droidstar.tgid_text_changed(tg)
    }

    function setDmrSmsStandby() {
        if (!droidstar) return
        var ownId = String(parseInt(appState.dmrid) || 9990)
        window._rememberHomeTarget()
        appState.privateCall = true
        appState.dmrtgid = ownId
        droidstar.set_dmr_pc(1)
        droidstar.set_dmrtgid(ownId)
        droidstar.tgid_text_changed(ownId)
        window.smsStandbyActive = true
    }

    function leaveSmsStandby(groupTg) {
        var tg = String(groupTg || window.smsStandbyRestoreTg || "")
        var pc = groupTg ? false : window.smsStandbyRestorePc
        window._forgetSmsStandby()
        if (tg === "" || !(parseInt(tg) > 0)) {
            notify(qsTr("No voice talkgroup to link. Pick one on the Home page."),
                   qsTr("Home"), function() { window.navigate("main") }, "info")
            return
        }
        window.quickSwitchToTalkgroup(tg, false, pc)
    }

    function connectDmrShortcut(preferredHost, targetId, isGroup) {
        if (!droidstar) return
        if (appState.mode === "DMR" && (appState.connected || appState.connecting)) {
            window.connectOrDisconnect()
            return
        }
        if (appState.connected || appState.connecting) {
            if (appState.txActive) window.txSet(false)
            droidstar.process_connect()
        }
        window._bulkPush = true
        try {
            if (appState.mode !== "DMR") {
                window.setMode("DMR")
            }
            if (preferredHost && preferredHost !== "") {
                window.setHost(preferredHost)
            }
            // Connecting from Messages never links the Home page's voice talkgroup; a group thread
            // links only that thread's talkgroup so its group SMS can be received.
            var numTarget = parseInt(targetId || "0") || 0
            window._rememberHomeTarget()
            if (isGroup && numTarget > 0) {
                appState.privateCall = false
                appState.dmrtgid = String(numTarget)
            } else {
                appState.privateCall = true
                appState.dmrtgid = String(parseInt(appState.dmrid) || 9990)
            }
            window.smsStandbyActive = true
            droidstar.set_dmr_pc(appState.privateCall ? 1 : 0)
            droidstar.set_dmrtgid(appState.dmrtgid)
            droidstar.tgid_text_changed(appState.dmrtgid)
        } finally {
            window._bulkPush = false
        }
        Qt.callLater(function() {
            if (!appState.connected && !appState.connecting) {
                window.connectOrDisconnect()
            }
        })
    }

    // App state model - renamed from "state" to "appState" to avoid QML reserved property conflict
    QtObject {
        id: appState
        property string mode: "DMR"
        property string module: "A"
        property string selectedHost: ""
        property string host: ""
        property string netstatus: ""
        property string dmrtgid: ""
        property bool privateCall: false
        property bool swtx: true
        property bool swrx: true
        property bool agc: true
        property int noiseReductionMode: 2
        property bool hdUpscale: true
        property bool txEnhance: false
        property bool voiceIsolation: false
        property bool rxClean: false
        property real micGain: 0.5
        property real spkVolume: 1.0
        property real spkPreampDb: 6.0
        property int outputLevel: 0
        property bool connected: false
        // Legacy-style connection state (0=disconnected, 1=connecting, 2=connected, 5=error)
        property int connectStatus: 0
        property bool connecting: false
        property bool txEnabled: false
        property bool txActive: false
        property int txSeconds: 0

        // DMR / M17 / TTS runtime options (re-applied after each connect)
        property int dmrSlot: 1
        property int dmrCc: 1
        property bool m17VoiceFull: true
        property string ttsVoice: "Mic"
        property string ttsText: ""

        property string data1: ""
        property string data2: ""
        property string data3: ""
        property string data4: ""
        property string data5: ""
        property string data6: ""
        property string ambestatus: ""
        property string mmdvmstatus: ""

        // Dynamic labels
        property string label1: ""
        property string label2: ""
        property string label3: ""
        property string label4: ""
        property string label5: ""
        property string label6: ""

        // Recent & Favourite TGIDs + BrandMeister Hoseline Scanner State
        property var recentTgids: []
        property var favouriteTgids: []
        property var hoselineCalls: []
        property var hoselineFavCalls: []
        property bool hoselineConnected: false
        property int hoselineLiveCount: 0
        property int hoselineActiveFavCount: 0
        property bool favScanActive: false
        property bool allScanActive: false
        property string monoFont: ""

        property var hostsModel: []

        // Settings fields
        property string callsign: ""
        property string dmrid: ""
        property string essid: "None"
        property string bmPass: ""
        property string tgifPass: ""
        property string aslPass: ""
        property string latitude: ""
        property string longitude: ""
        property string location: ""
        property string description: ""
        property string url: ""
        property string swid: ""
        property string pkgid: ""
        property string dmrOptions: ""
        property string mycall: ""
        property string urcall: ""
        property string rptr1: ""
        property string rptr2: ""
        property string usrtxt: ""
        property string txTimeout: ""

        // Modem tuning fields
        property string modemRxFreq: ""
        property string modemTxFreq: ""
        property string modemRxOffset: ""
        property string modemTxOffset: ""
        property string modemRxDCOffset: ""
        property string modemTxDCOffset: ""
        property string modemRxLevel: ""
        property string modemTxLevel: ""
        property string modemRFLevel: ""
        property string modemTxDelay: ""
        property string modemCWIdTxLevel: ""
        property string modemDstarTxLevel: ""
        property string modemDMRTxLevel: ""
        property string modemYSFTxLevel: ""
        property string modemP25TxLevel: ""
        property string modemNXDNTxLevel: ""
        property string modemBaud: ""
        property string modemM17CAN: "0"

        // UI-only
        property string lastHeard1: ""
        property string lastHeard2: ""
        property string lastHeard3: ""
        property string lastHeard4: ""
        property string lastHeard5: ""
        property var lastHeardEntries: []
        property string fetchedFirstName: ""
        property string fetchedCountry: ""

        // QSO log settings (UI-controlled, persisted via Qt.labs.settings)
        property bool disableSelfLog: true  // Default to disabled (exclude self logging)
        property int qsoLogLimit: 250

        // QSO Replay state (mirrored from replayEngine)
        property bool replayEnabled: true
        property bool replayRecordTx: false
        property int replayStorageMb: 100
        property int replayMaxOverSeconds: 600
        property var replayUsedBytes: 0
        property int replayCount: 0
        property bool replayRecording: false
        property string replayLastError: ""
        property int replayBytesPerSecond: 3090

        // Live RX transcription state
        property bool transcriptionEnabled: false
        property bool transcriptionLogEnabled: true
        property string transcriptionEngine: "apple"
        property bool transcriptionAppleSupported: Qt.platform.os === "ios"
        property bool transcriptionAppleAvailable: false
        property bool transcriptionParakeetSupported: Qt.platform.os === "ios"
        property string transcriptionDeviceName: ""
        property string transcriptionParakeetDisabledReason: ""
        property string transcriptionSmallDisabledReason: ""
        property string transcriptionTurboDisabledReason: ""
        property string transcriptionLanguage: "auto"
        property string transcriptionModel: "base"
        property string transcriptionModelName: ""
        property bool transcriptionModelReady: false
        property bool transcriptionDownloading: false
        property real transcriptionDownloadProgress: 0.0
        property var transcriptionActiveDownloads: []
        property string transcriptionStatus: "Off"
        property string currentTranscript: ""
        property string transcriptCallsign: ""
        property bool transcriptActiveTurn: false

        property bool ipv6: false
        property bool xrf2ref: false
        // IMPORTANT: Default to enabled on fresh installs.
        property bool toggleTx: true
        property bool bmOdtp: true
        property bool odtpActive: false
        property bool mmdvmDirect: false
        property bool debug: false

        // Device selection
        property string vocoder: ""
        property string modem: ""
        property string playback: ""
        property string capture: ""
        property var vocodersModel: []
        property var modemsModel: []
        property var playbacksModel: []
        property var capturesModel: []

        property string _lastQsoKey: ""
    }

    // IMPORTANT: expose the QtObject `id` through a window property.
    // Otherwise, writing `appState: appState` inside child components self-references their required
    // `appState` property and creates a binding loop.
    readonly property var appStateObj: appState

    // Bounded app log model
    ListModel { id: appLogModel }
    function appendLog(line) {
        if (line === undefined || line === null) return
        var parts = ("" + line).split(/\r?\n/)
        for (var i = 0; i < parts.length; i++) {
            if (parts[i].length === 0) continue
            appLogModel.append({ "t": parts[i] })
        }
        if (appLogModel.count > 1200) appLogModel.remove(0, appLogModel.count - 1200)
    }

    // ─────────────────────────────────────────────────────────
    // QSO LOG HELPERS (Last Heard + self-filter + limit)
    // ─────────────────────────────────────────────────────────
    function _fmtLastHeard(entry) {
        if (!entry) return ""
        var parts = []
        if (entry.callsign) parts.push(entry.callsign)
        if (entry.fname) parts.push(entry.fname)
        if (entry.country) parts.push(entry.country)
        return parts.join(" - ")
    }

    function _setLastHeardFromArray(arr) {
        appState.lastHeard1 = (arr && arr.length > 0) ? _fmtLastHeard(arr[0]) : ""
        appState.lastHeard2 = (arr && arr.length > 1) ? _fmtLastHeard(arr[1]) : ""
        appState.lastHeard3 = (arr && arr.length > 2) ? _fmtLastHeard(arr[2]) : ""
        appState.lastHeard4 = (arr && arr.length > 3) ? _fmtLastHeard(arr[3]) : ""
        appState.lastHeard5 = (arr && arr.length > 4) ? _fmtLastHeard(arr[4]) : ""

        var structured = []
        if (arr && arr.length !== undefined) {
            var lim = Math.min(arr.length, 5)
            for (var i = 0; i < lim; i++) {
                var e = arr[i]
                if (!e) continue
                var textLine = _fmtLastHeard(e)
                var dmrVal = parseInt(e.dmrID || "0") || 0
                if (textLine === "" && dmrVal <= 0) continue
                // Newest QSO Replay clip of this entry (clips are appended, so the last one is newest)
                var clips = (e.replayClips && e.replayClips.length !== undefined) ? e.replayClips : []
                var lastClip = null
                for (var c = clips.length - 1; c >= 0; c--) {
                    if (clips[c] && clips[c].id) { lastClip = clips[c]; break }
                }
                structured.push({
                    latestClipId: lastClip ? String(lastClip.id) : "",
                    latestClipTime: lastClip ? String(lastClip.time || "") : "",
                    latestClipKind: (lastClip && lastClip.kind === "tx") ? "tx" : "rx",
                    clipCount: clips.length,
                    callsign: String(e.callsign || ""),
                    dmrID: dmrVal,
                    fname: String(e.fname || ""),
                    country: String(e.country || ""),
                    tgid: parseInt(e.tgid || "0") || 0,
                    currentTime: String(e.currentTime || ""),
                    displayText: textLine !== "" ? textLine : ("ID " + dmrVal)
                })
            }
        }
        appState.lastHeardEntries = structured
    }

    function _normCallsign(s) {
        if (s === undefined || s === null) return ""
        return ("" + s).trim().toUpperCase()
    }

    function _extractBaseCallsign(s) {
        if (s === undefined || s === null) return ""
        var trimmed = ("" + s).trim()
        var dashIdx = trimmed.indexOf(" - ")
        if (dashIdx > 0) trimmed = trimmed.substring(0, dashIdx).trim()
        var spIdx = trimmed.indexOf(" ")
        if (spIdx > 0) trimmed = trimmed.substring(0, spIdx).trim()
        return trimmed.toUpperCase()
    }

    function _appendTurnToLogEntry(entry, turnObj) {
        if (!entry || !turnObj || !turnObj.text) return
        var turns = []
        if (entry.transcriptTurns && entry.transcriptTurns.length !== undefined) {
            for (var i = 0; i < entry.transcriptTurns.length; i++) {
                turns.push(entry.transcriptTurns[i])
            }
        } else if (entry.transcript && String(entry.transcript).trim() !== "") {
            turns.push({
                turnId: 0,
                time: entry.currentTime || "",
                text: String(entry.transcript).trim(),
                tgid: entry.tgid || 0
            })
        }

        var replaced = false
        if (turnObj.turnKey) {
            for (var j = 0; j < turns.length; j++) {
                if (turns[j].turnKey === turnObj.turnKey) {
                    turns[j] = turnObj
                    replaced = true
                    break
                }
            }
        }
        if (!replaced) {
            turns.push(turnObj)
        }
        // A station that talks all day would otherwise grow one log entry without bound.
        if (turns.length > 50) turns = turns.slice(turns.length - 50)

        entry.transcriptTurns = turns
        var summaryLines = []
        for (var k = 0; k < turns.length; k++) {
            var t = turns[k]
            var shortTime = (t.time && String(t.time).length >= 19) ? String(t.time).substring(11, 19) : (t.time || "")
            summaryLines.push((shortTime !== "" ? ("[" + shortTime + "] ") : "") + t.text)
        }
        entry.transcript = summaryLines.join("\n")
    }

    function _selfDmrId() {
        var n = parseInt(appState.dmrid || "0")
        return (isNaN(n) || n < 0) ? 0 : n
    }

    function _isSelfContact(dmrId, callsign) {
        // Treat as "self" if EITHER DMR ID OR callsign matches configured identity.
        var myDmr = _selfDmrId()
        var myCs = _normCallsign(appState.callsign)
        var d = parseInt(dmrId || "0")
        var dmrMatches = (myDmr > 0 && d > 0 && d === myDmr)
        var incomingCs = _normCallsign(callsign)
        var csMatches = (myCs !== "" && incomingCs !== "" && incomingCs === myCs)
        return dmrMatches || csMatches
    }

    function _clampQsoLogLimit(n) {
        // Keep sane bounds to avoid memory spikes; default to 250 on invalid.
        var v = parseInt(n)
        if (isNaN(v) || v <= 0) v = 250
        if (v < 10) v = 10
        if (v > 5000) v = 5000
        return v
    }

    function _effectiveQsoLogLimit() {
        return _clampQsoLogLimit(appState.qsoLogLimit)
    }

    function _trimAndPersistQsoLogIfNeeded() {
        if (!window.logHandlerRef) return
        var lim = _effectiveQsoLogLimit()
        var current = window.logHandlerRef.loadLog("logs.json")
        if (!current || current.length === undefined) current = []
        if (current.length <= lim) return
        while (current.length > lim) current.pop()
        window._setLastHeardFromArray(current)
        window.logHandlerRef.saveLogAsync("logs.json", current)
    }

    function syncLastHeardFromLogFile() {
        if (!window.logHandlerRef) return
        var saved = window.logHandlerRef.loadLog("logs.json")
        if (!saved || saved.length === undefined) return
        _setLastHeardFromArray(saved)
    }

    // ─────────────────────────────────────────────────────────
    // QSO REPLAY
    // ─────────────────────────────────────────────────────────
    function _syncReplayState() {
        var e = window.replayEngineRef
        if (!e) return
        appState.replayEnabled = !!e.enabled
        appState.replayRecordTx = !!e.recordTx
        appState.replayStorageMb = e.storageBudgetMb || 100
        appState.replayMaxOverSeconds = e.maxOverSeconds || 600
        appState.replayUsedBytes = e.storageUsedBytes || 0
        appState.replayCount = e.recordingCount || 0
        appState.replayRecording = !!e.recording
        appState.replayLastError = e.lastError || ""
        appState.replayBytesPerSecond = e.bytesPerSecond || 3090
    }

    function setReplayEnabled(on) {
        if (!window.replayEngineRef) return
        window.replayEngineRef.enabled = !!on
        window._syncReplayState()
    }

    function setReplayRecordTx(on) {
        if (!window.replayEngineRef) return
        window.replayEngineRef.recordTx = !!on
        window._syncReplayState()
    }

    function setReplayStorageMb(mb) {
        if (!window.replayEngineRef) return
        var n = parseInt(mb)
        if (isNaN(n) || n <= 0) n = 100
        window.replayEngineRef.storageBudgetMb = Math.max(50, Math.min(2000, n))
        window._syncReplayState()
    }

    function clearAllReplays() {
        if (!window.replayEngineRef) return
        if (window.replayPlayerRef) window.replayPlayerRef.stop()
        window.replayEngineRef.clearAll()
    }

    // "≈ 9 h" / "≈ 45 min" of overs that fit in mb megabytes at the current codec rate
    function replayHoursText(mb) {
        if (!window.replayEngineRef) return ""
        var h = window.replayEngineRef.hoursForMegabytes(parseInt(mb) || 0)
        if (h <= 0) return ""
        if (h < 1) return qsTr("≈ %1 min").arg(Math.max(1, Math.round(h * 60)))
        if (h < 10) return qsTr("≈ %1 h").arg((Math.round(h * 10) / 10).toString())
        return qsTr("≈ %1 h").arg(Math.round(h))
    }

    function formatReplayBytes(bytes) {
        var b = Number(bytes) || 0
        if (b < 1024 * 1024) return qsTr("%1 KB").arg(Math.round(b / 1024))
        if (b < 1024 * 1024 * 1024) return qsTr("%1 MB").arg((Math.round(b / 1048576 * 10) / 10).toString())
        return qsTr("%1 GB").arg((Math.round(b / 1073741824 * 100) / 100).toString())
    }

    function replayDurationText(ms) {
        var s = Math.max(0, Math.round((Number(ms) || 0) / 1000))
        var m = Math.floor(s / 60)
        var r = s % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    // Live audio always wins over a replay. reason: "live" | "tx"
    function _pauseReplayFor(reason) {
        if (window.replayPlayerRef && window.replayPlayerRef.playing) window.replayPlayerRef.pauseForLive(reason)
    }

    function _parseLogTimeMs(str) {
        // "yyyy-MM-dd HH:mm:ss" (local time) -> ms since epoch, or 0
        if (!str || String(str).length < 19) return 0
        var s = String(str)
        var d = new Date(parseInt(s.substring(0, 4)), parseInt(s.substring(5, 7)) - 1, parseInt(s.substring(8, 10)),
                         parseInt(s.substring(11, 13)), parseInt(s.substring(14, 16)), parseInt(s.substring(17, 19)))
        var ms = d.getTime()
        return isNaN(ms) ? 0 : ms
    }

    function _appendClipToLogEntry(entry, clip) {
        if (!entry || !clip || !clip.id) return []
        var clips = []
        if (entry.replayClips && entry.replayClips.length !== undefined) {
            for (var i = 0; i < entry.replayClips.length; i++) {
                if (entry.replayClips[i] && entry.replayClips[i].id !== clip.id) clips.push(entry.replayClips[i])
            }
        }
        clips.push(clip)
        // A station that talks all day would otherwise pin unbounded audio to one entry.
        var dropped = []
        while (clips.length > 20) dropped.push(clips.shift().id)
        entry.replayClips = clips
        return dropped
    }

    // A finished clip from the engine: attach it to the station's entry (same top-10 match as
    // transcripts, within 15 min) or create the entry. Own TX creates an entry under our callsign.
    function _attachReplayClip(id, kind, mode, srcId, callsign, tgid, startTime, durationMs, bytes, truncated) {
        if (!window.logHandlerRef || !id) return
        var isTx = (parseInt(kind) === 1)
        var clip = {
            id: id,
            kind: isTx ? "tx" : "rx",
            mode: mode || "",
            time: startTime || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss"),
            durationMs: parseInt(durationMs) || 0,
            bytes: Number(bytes) || 0,
            truncated: !!truncated
        }
        var baseCall = window._extractBaseCallsign(callsign)
        var isNumericCall = /^[0-9]+$/.test(baseCall)
        var cleanCall = isNumericCall ? "" : baseCall
        var dmrId = parseInt(srcId || "0")
        if (isNaN(dmrId) || dmrId < 0) dmrId = 0
        if (dmrId === 0 && isNumericCall) dmrId = parseInt(baseCall)
        var tg = parseInt(tgid || "0") || 0

        if (isTx) {
            if (appState.disableSelfLog) return
            cleanCall = window._normCallsign(appState.callsign) || cleanCall
            dmrId = window._selfDmrId() || dmrId
        } else if (appState.disableSelfLog && window._isSelfContact(dmrId, cleanCall)) {
            return
        }
        if (dmrId <= 0 && cleanCall === "") return   // no identity: the engine reconciles it away

        var current = window.logHandlerRef.loadLog("logs.json")
        if (!current || current.length === undefined) current = []

        var clipMs = window._parseLogTimeMs(clip.time)
        var matchedIdx = -1
        var checkCount = Math.min(current.length, 10)
        for (var i = 0; i < checkCount; i++) {
            var row = current[i]
            var rowDmr = parseInt(row.dmrID || "0")
            var rowCall = window._extractBaseCallsign(row.callsign)
            var same = (dmrId > 0 && rowDmr > 0 && rowDmr === dmrId) ||
                       (cleanCall !== "" && rowCall !== "" && rowCall === cleanCall)
            if (!same) continue
            var rowMs = window._parseLogTimeMs(row.currentTime)
            if (clipMs > 0 && rowMs > 0 && Math.abs(clipMs - rowMs) > 15 * 60 * 1000) continue
            matchedIdx = i
            break
        }

        var dropped = []
        if (matchedIdx >= 0) {
            dropped = window._appendClipToLogEntry(current[matchedIdx], clip)
        } else {
            var nextSerial = 0
            for (var k = 0; k < current.length; k++) {
                var sn = current[k].serialNumber
                if (sn !== undefined && sn !== null) nextSerial = Math.max(nextSerial, sn + 1)
            }
            var sameAsPending = (!isTx && dmrId > 0 && dmrId === window._pendingLogDmr)
            var newEntry = {
                serialNumber: nextSerial,
                callsign: cleanCall !== "" ? cleanCall : (dmrId > 0 ? String(dmrId) : ""),
                dmrID: dmrId,
                tgid: tg,
                country: sameAsPending ? (appState.fetchedCountry || "") : "",
                fname: sameAsPending ? (appState.fetchedFirstName || "") : "",
                mode: clip.mode,
                currentTime: clip.time,
                transcript: "",
                transcriptTurns: [],
                replayClips: [],
                checked: false
            }
            window._appendClipToLogEntry(newEntry, clip)
            current.unshift(newEntry)
            var lim = window._effectiveQsoLogLimit()
            while (current.length > lim) current.pop()
            window._setLastHeardFromArray(current)
        }
        window.logHandlerRef.saveLogAsync("logs.json", current)
        if (dropped.length > 0 && window.replayEngineRef) window.replayEngineRef.deleteRecordings(dropped)
    }

    // Clips pruned by budget / reconcile / delete: strip the references and save once.
    function _stripReplayClips(ids) {
        if (!window.logHandlerRef || !ids || ids.length === 0) return
        var gone = {}
        for (var i = 0; i < ids.length; i++) gone[ids[i]] = true
        var current = window.logHandlerRef.loadLog("logs.json")
        if (!current || current.length === undefined) return
        var changed = false
        for (var j = 0; j < current.length; j++) {
            var e = current[j]
            if (!e.replayClips || e.replayClips.length === undefined || e.replayClips.length === 0) continue
            var kept = []
            for (var k = 0; k < e.replayClips.length; k++) {
                if (e.replayClips[k] && !gone[e.replayClips[k].id]) kept.push(e.replayClips[k])
            }
            if (kept.length !== e.replayClips.length) { e.replayClips = kept; changed = true }
        }
        if (changed) window.logHandlerRef.saveLogAsync("logs.json", current)
    }

    // ─────────────────────────────────────────────────────────
    // BACKEND SYNC
    // ─────────────────────────────────────────────────────────
    // Every backend setter calls save_settings(), which emits update_settings. While we push
    // many values at once (connect), suppress the echo and sync once afterwards.
    property bool _bulkPush: false

    function syncSettingsFromBackend() {
        if (!droidstar) return
        appState.callsign = droidstar.get_callsign()
        appState.dmrid = droidstar.get_dmrid()
        appState.essid = droidstar.get_essid()
        appState.bmPass = droidstar.get_bm_password()
        appState.tgifPass = droidstar.get_tgif_password()
        if (typeof droidstar.get_asl_password === "function")
            appState.aslPass = droidstar.get_asl_password()
        appState.latitude = droidstar.get_latitude()
        appState.longitude = droidstar.get_longitude()
        appState.location = droidstar.get_location()
        appState.description = droidstar.get_description()
        appState.url = droidstar.get_url()
        appState.swid = droidstar.get_swid()
        appState.pkgid = droidstar.get_pkgid()
        appState.dmrOptions = droidstar.get_dmr_options()
        appState.dmrtgid = droidstar.get_dmrtgid()
        appState.mycall = droidstar.get_mycall()
        appState.urcall = droidstar.get_urcall()
        appState.rptr1 = droidstar.get_rptr1()
        appState.rptr2 = droidstar.get_rptr2()
        appState.txTimeout = droidstar.get_txtimeout()

        appState.modemRxFreq = droidstar.get_modemRxFreq()
        appState.modemTxFreq = droidstar.get_modemTxFreq()
        appState.modemRxOffset = droidstar.get_modemRxOffset()
        appState.modemTxOffset = droidstar.get_modemTxOffset()
        appState.modemRxDCOffset = droidstar.get_modemRxDCOffset()
        appState.modemTxDCOffset = droidstar.get_modemTxDCOffset()
        appState.modemRxLevel = droidstar.get_modemRxLevel()
        appState.modemTxLevel = droidstar.get_modemTxLevel()
        appState.modemRFLevel = droidstar.get_modemRFLevel()
        appState.modemTxDelay = droidstar.get_modemTxDelay()
        appState.modemCWIdTxLevel = droidstar.get_modemCWIdTxLevel()
        appState.modemDstarTxLevel = droidstar.get_modemDstarTxLevel()
        appState.modemDMRTxLevel = droidstar.get_modemDMRTxLevel()
        appState.modemYSFTxLevel = droidstar.get_modemYSFTxLevel()
        appState.modemP25TxLevel = droidstar.get_modemP25TxLevel()
        appState.modemNXDNTxLevel = droidstar.get_modemNXDNTxLevel()
        appState.modemBaud = droidstar.get_modemBaud()

        appState.ipv6 = droidstar.get_ipv6()
        appState.xrf2ref = droidstar.get_xrf2ref()
        appState.toggleTx = droidstar.get_toggletx()
        if (typeof droidstar.get_bm_odtp === "function")
            appState.bmOdtp = !!droidstar.get_bm_odtp()
        if (typeof droidstar.is_odtp_active === "function")
            appState.odtpActive = !!droidstar.is_odtp_active()
        if (typeof droidstar.get_noise_reduction_mode === "function")
            appState.noiseReductionMode = droidstar.get_noise_reduction_mode()
        if (typeof droidstar.get_hd_upscale === "function")
            appState.hdUpscale = !!droidstar.get_hd_upscale()
        if (typeof droidstar.get_tx_enhance === "function")
            appState.txEnhance = !!droidstar.get_tx_enhance()
        if (typeof droidstar.get_voice_isolation === "function")
            appState.voiceIsolation = !!droidstar.get_voice_isolation()
        if (typeof droidstar.get_rx_clean === "function")
            appState.rxClean = !!droidstar.get_rx_clean()
        if (typeof droidstar.get_spk_preamp_db === "function")
            appState.spkPreampDb = Math.max(0, Math.min(24, Number(droidstar.get_spk_preamp_db()) || 0))
    }

    function _onSettingsUpdated() {
        if (!droidstar) return
        syncSettingsFromBackend()
        var saved = hostForMode(appState.mode)
        if (saved && appState.hostsModel && appState.hostsModel.indexOf(saved) >= 0)
            appState.selectedHost = saved
        appState.recentTgids = droidstar.loadRecentTGIDs()
        if (typeof droidstar.loadFavouriteTGIDs === "function")
            appState.favouriteTgids = droidstar.loadFavouriteTGIDs()
    }

    function hostForMode(mode) {
        if (!droidstar) return ""
        if (mode === "REF") return droidstar.get_ref_host()
        if (mode === "DCS") return droidstar.get_dcs_host()
        if (mode === "XRF") return droidstar.get_xrf_host()
        if (mode === "YSF") return droidstar.get_ysf_host()
        if (mode === "FCS") return droidstar.get_fcs_host()
        if (mode === "DMR") return droidstar.get_dmr_host()
        if (mode === "P25") return droidstar.get_p25_host()
        if (mode === "NXDN") return droidstar.get_nxdn_host()
        if (mode === "M17") return droidstar.get_m17_host()
        if (mode === "IAX") return droidstar.get_iax_host()
        return ""
    }

    function _toStringArray(list) {
        var out = []
        if (list && list.length !== undefined) {
            for (var i = 0; i < list.length; i++) out.push(String(list[i]))
        }
        return out
    }

    function reloadHostsForMode() {
        if (!droidstar) return
        droidstar.set_modelchange(true)
        appState.hostsModel = _toStringArray(droidstar.get_hosts())
        droidstar.set_modelchange(false)

        var savedHost = hostForMode(appState.mode)
        if (savedHost && appState.hostsModel.indexOf(savedHost) >= 0) appState.selectedHost = savedHost
        else appState.selectedHost = appState.hostsModel.length > 0 ? appState.hostsModel[0] : ""
    }

    function _pickInList(list, preferred, fallback) {
        if (preferred && list && list.indexOf(preferred) >= 0) return preferred
        return (list && list.length > 0) ? list[0] : fallback
    }

    function refreshAudioDevices() {
        if (!droidstar) return
        appState.playbacksModel = _toStringArray(droidstar.get_playbacks())
        appState.capturesModel = _toStringArray(droidstar.get_captures())
        appState.playback = _pickInList(appState.playbacksModel, appState.playback, "OS Default")
        appState.capture = _pickInList(appState.capturesModel, appState.capture, "OS Default")
    }

    function refreshSerialDevices() {
        if (!droidstar) return
        appState.vocodersModel = _toStringArray(droidstar.get_vocoders())
        appState.modemsModel = _toStringArray(droidstar.get_modems())
        appState.vocoder = _pickInList(appState.vocodersModel, appState.vocoder, "Software vocoder")
        appState.modem = _pickInList(appState.modemsModel, appState.modem, "None")
    }

    function initializeFromBackend() {
        if (window.initialized || window.initializing) return
        if (!droidstar) return

        window.initializing = true
        // CC state comes from TranscriptionEngine's own saved settings; sync it before anything below can throw
        window._syncTranscriptionState()
        try {
            // Clean up any orphan Live Activities from previous app runs
            if (window.liveActivityRef && window.liveActivityRef.endAll) {
                window.liveActivityRef.endAll()
            }

            window._bulkPush = true
            // Load all settings from QSettings (also calls process_mode_change -> emits mode_changed)
            droidstar.process_settings()
            window._bulkPush = false

            appState.mode = droidstar.get_mode()
            appState.module = droidstar.get_module()
            reloadHostsForMode()

            // Sync all other settings from QSettings
            syncSettingsFromBackend()

            if (controlSettings.smsRestoreTg !== "") {
                window.smsStandbyRestoreTg = controlSettings.smsRestoreTg
                window.smsStandbyRestorePc = !!controlSettings.smsRestorePc
                window.smsStandbyActive = true
                window._restoreHomeTarget()
            }

            // Load persisted UI settings (QSO log)
            appState.disableSelfLog = !!uiSettings.disableSelfLog
            appState.qsoLogLimit = window._clampQsoLogLimit(uiSettings.qsoLogLimit)
            uiSettings.qsoLogLimit = appState.qsoLogLimit
            if (window.replayEngineRef) window.replayEngineRef.selfLoggingAllowed = !appState.disableSelfLog
            if (window.replayPlayerRef) window.replayPlayerRef.volume = Math.max(0, Math.min(1, appState.spkVolume))
            window._syncReplayState()

            // Load persisted runtime controls (TX audio settings are synced from backend via syncSettingsFromBackend)
            appState.micGain = Math.max(0, Math.min(1, controlSettings.micGain))
            appState.spkVolume = Math.max(0, Math.min(1, controlSettings.spkVolume))
            appState.agc = true
            controlSettings.agc = true
            droidstar.set_input_volume(appState.micGain)
            droidstar.set_output_volume(appState.spkVolume)
            droidstar.set_agc(appState.agc)
            appState.dmrSlot = (controlSettings.dmrSlot === 0) ? 0 : 1
            appState.dmrCc = Math.max(0, Math.min(15, controlSettings.dmrCc))
            appState.modemM17CAN = controlSettings.m17Can || "0"
            appState.m17VoiceFull = !!controlSettings.m17VoiceFull
            appState.ttsVoice = controlSettings.ttsVoice || "Mic"

            // TranscriptionEngine persists its own state in QSettings("dudetronics", "droidstar");
            // sync appState from TranscriptionEngine so user's enable/disable choice is preserved across launches.
            if (window.transcriptionEngineRef) {
                window.transcriptionEngineRef.setUserCallsign(appState.callsign)
                window._syncTranscriptionState()
                controlSettings.transcriptionEnabled = appState.transcriptionEnabled
                controlSettings.transcriptionEngine = appState.transcriptionEngine
                controlSettings.transcriptionLanguage = appState.transcriptionLanguage
                controlSettings.transcriptionModel = appState.transcriptionModel
            }

            window._trimAndPersistQsoLogIfNeeded()

            // Populate "Last Heard" on app load
            syncLastHeardFromLogFile()
            // Ensure logs.json exists (prevents first-run "file not found" and enables realtime updates)
            if (window.logHandlerRef) {
                var existingLog = window.logHandlerRef.loadLog("logs.json")
                window.logHandlerRef.saveLogAsync("logs.json", existingLog)
            }

            appState.monoFont = droidstar.get_monofont()
            appState.recentTgids = droidstar.loadRecentTGIDs()
            if (typeof droidstar.loadFavouriteTGIDs === "function")
                appState.favouriteTgids = droidstar.loadFavouriteTGIDs()
            window.syncHoselineFromBackend()
            window.updateHoselineStreamLifecycle()

            refreshSerialDevices()
            refreshAudioDevices()

            window.initialized = true
        } catch (e) {
            console.log("Initialization failed:", e)
        } finally {
            window._bulkPush = false
            window.initializing = false
        }
    }

    // ─────────────────────────────────────────────────────────
    // ACTIONS (shared by every page so behaviour is identical everywhere)
    // ─────────────────────────────────────────────────────────
    function flushPendingEdits() {
        Qt.inputMethod.commit()
        // Focus-out makes any focused FormField commit synchronously.
        if (window.activeFocusItem) window.contentItem.forceActiveFocus()
        Qt.inputMethod.hide()
    }

    function pushAllSettingsToBackend(host) {
        droidstar.set_callsign(appState.callsign)
        droidstar.set_dmrid(appState.dmrid)
        droidstar.set_protocol(appState.mode)
        droidstar.set_module(appState.module)
        droidstar.set_essid(appState.essid)
        droidstar.set_bm_password(appState.bmPass)
        droidstar.set_tgif_password(appState.tgifPass)
        if (typeof droidstar.set_asl_password === "function")
            droidstar.set_asl_password(appState.aslPass)
        droidstar.set_latitude(appState.latitude)
        droidstar.set_longitude(appState.longitude)
        droidstar.set_location(appState.location)
        droidstar.set_description(appState.description)
        droidstar.set_url(appState.url)
        droidstar.set_swid(appState.swid)
        droidstar.set_pkgid(appState.pkgid)
        droidstar.set_dmr_options(appState.dmrOptions)
        droidstar.set_dmrtgid(appState.dmrtgid)
        // Legacy parity: don't overwrite backend D-STAR routing with empty values.
        if (appState.mycall !== "") droidstar.set_mycall(appState.mycall)
        if (appState.urcall !== "") droidstar.set_urcall(appState.urcall)
        if (appState.rptr1 !== "") droidstar.set_rptr1(appState.rptr1)
        if (appState.rptr2 !== "") droidstar.set_rptr2(appState.rptr2)
        if (appState.usrtxt !== "") droidstar.set_usrtxt(appState.usrtxt)
        droidstar.set_txtimeout(appState.txTimeout)
        droidstar.set_modemRxFreq(appState.modemRxFreq)
        droidstar.set_modemTxFreq(appState.modemTxFreq)
        droidstar.set_modemRxOffset(appState.modemRxOffset)
        droidstar.set_modemTxOffset(appState.modemTxOffset)
        droidstar.set_modemRxDCOffset(appState.modemRxDCOffset)
        droidstar.set_modemTxDCOffset(appState.modemTxDCOffset)
        droidstar.set_modemRxLevel(appState.modemRxLevel)
        droidstar.set_modemTxLevel(appState.modemTxLevel)
        droidstar.set_modemRFLevel(appState.modemRFLevel)
        droidstar.set_modemTxDelay(appState.modemTxDelay)
        droidstar.set_modemCWIdTxLevel(appState.modemCWIdTxLevel)
        droidstar.set_modemDstarTxLevel(appState.modemDstarTxLevel)
        droidstar.set_modemDMRTxLevel(appState.modemDMRTxLevel)
        droidstar.set_modemYSFTxLevel(appState.modemYSFTxLevel)
        droidstar.set_modemP25TxLevel(appState.modemP25TxLevel)
        droidstar.set_modemNXDNTxLevel(appState.modemNXDNTxLevel)
        droidstar.set_modemBaud(appState.modemBaud)
        droidstar.set_ipv6(appState.ipv6)
        droidstar.set_xrf2ref(appState.xrf2ref)
        droidstar.set_toggletx(appState.toggleTx)
        if (typeof droidstar.set_bm_odtp === "function")
            droidstar.set_bm_odtp(appState.bmOdtp)

        // Device selections are applied before connecting (not persisted by backend).
        droidstar.set_vocoder(appState.vocoder)
        droidstar.set_modem(appState.modem)
        droidstar.set_playback(appState.playback)
        droidstar.set_capture(appState.capture)
        droidstar.set_input_volume(appState.micGain)
        droidstar.set_output_volume(appState.spkVolume)
        droidstar.set_agc(appState.agc)
        droidstar.set_slot(appState.dmrSlot)
        droidstar.set_cc(appState.dmrCc)
        droidstar.set_dmr_pc(appState.privateCall ? 1 : 0)

        if (host && host !== "") {
            droidstar.set_dst(host)
            droidstar.process_host_change(host)
        }
    }

    function connectOrDisconnect() {
        if (!droidstar) return

        // Cancel an in-flight attempt or disconnect: backend toggles to DISCONNECTED.
        var tuningReconnect = (typeof droidstar.is_tuning_reconnect === "function" && droidstar.is_tuning_reconnect())
        if (appState.connecting || appState.connected || tuningReconnect) {
            if (typeof droidstar.cancel_tune_reconnect === "function")
                droidstar.cancel_tune_reconnect()
            appState.favScanActive = false
            appState.allScanActive = false
            if (appState.txActive) txSet(false)
            if (appState.connecting || appState.connected) {
                droidstar.process_connect()
            } else {
                appState.connecting = false
                appState.connectStatus = 0
            }
            return
        }

        flushPendingEdits()

        if ((appState.callsign || "").trim() === "") {
            notify(qsTr("Enter your callsign before connecting."), qsTr("Settings"), function() { window.navigate("settings") }, "warning")
            return
        }
        if (appState.mode === "DMR" && !(parseInt(appState.dmrid) > 0)) {
            notify(qsTr("DMR needs your DMR ID."), qsTr("Settings"), function() { window.navigate("settings") }, "warning")
            return
        }

        var host = appState.selectedHost
        if ((!host || appState.hostsModel.indexOf(host) < 0) && appState.hostsModel.length > 0)
            host = appState.hostsModel[0]
        var direct = (appState.mode === "M17" && appState.mmdvmDirect)
        if (!host && !direct) {
            notify(qsTr("No hosts available for %1.").arg(appState.mode), qsTr("Update"), function() { window.updateHostFiles() }, "warning")
            return
        }

        window._bulkPush = true
        try {
            pushAllSettingsToBackend(host)
        } finally {
            window._bulkPush = false
        }
        appState.selectedHost = host || ""

        // Optimistic: reflect the attempt immediately (backend confirms with status 1).
        appState.connecting = true
        appState.connectStatus = 1
        droidstar.process_connect()
    }

    // Backend creates a fresh Mode object on every connect, so runtime-only state must be re-sent.
    function applyRuntimeState() {
        if (!droidstar || !appState.connected) return
        droidstar.set_input_volume(appState.micGain)
        droidstar.set_output_volume(appState.spkVolume)
        droidstar.set_agc(appState.agc)
        droidstar.set_debug(appState.debug)
        if (appState.mode === "DMR") {
            droidstar.set_slot(appState.dmrSlot)
            droidstar.set_cc(appState.dmrCc)
            droidstar.set_dmr_pc(appState.privateCall ? 1 : 0)
            if (appState.dmrtgid !== "") {
                droidstar.set_dmrtgid(appState.dmrtgid)
                droidstar.tgid_text_changed(appState.dmrtgid)
            }
        } else if (appState.mode === "M17") {
            droidstar.m17_rate_changed(appState.m17VoiceFull)
            droidstar.set_modemM17CAN(appState.modemM17CAN)
        }
        if (typeof USE_FLITE !== "undefined" && USE_FLITE) {
            droidstar.tts_text_changed(appState.ttsText)
            droidstar.tts_changed(appState.ttsVoice)
        }
    }

    function setMicGain(v) {
        appState.micGain = v
        if (droidstar) droidstar.set_input_volume(v)
    }
    function setSpkVolume(v) {
        appState.spkVolume = v
        if (droidstar) droidstar.set_output_volume(v)
    }
    function setSpkPreampDb(db) {
        var clamped = Math.max(0, Math.min(24, Math.round(Number(db) || 0)))
        appState.spkPreampDb = clamped
        if (droidstar && typeof droidstar.set_spk_preamp_db === "function")
            droidstar.set_spk_preamp_db(clamped)
    }
    function setSwtx(on) { appState.swtx = on; if (droidstar) droidstar.set_swtx(on) }
    function setSwrx(on) { appState.swrx = on; if (droidstar) droidstar.set_swrx(on) }
    function setAgc(on) { appState.agc = on; if (droidstar) droidstar.set_agc(on) }
    function setNoiseReductionMode(mode) {
        var m = Math.max(0, Math.min(2, mode | 0))
        if (droidstar && typeof droidstar.set_noise_reduction_mode === "function") {
            droidstar.set_noise_reduction_mode(m)
            if (typeof droidstar.get_noise_reduction_mode === "function")
                appState.noiseReductionMode = droidstar.get_noise_reduction_mode()
            else
                appState.noiseReductionMode = m
        } else {
            appState.noiseReductionMode = m
        }
    }
    function setHdUpscale(on) {
        var v = !!on
        if (droidstar && typeof droidstar.set_hd_upscale === "function") {
            droidstar.set_hd_upscale(v)
            if (typeof droidstar.get_hd_upscale === "function")
                appState.hdUpscale = !!droidstar.get_hd_upscale()
            else
                appState.hdUpscale = v
        } else {
            appState.hdUpscale = v
        }
    }
    function setTxEnhance(on) {
        var v = !!on
        if (droidstar && typeof droidstar.set_tx_enhance === "function") {
            droidstar.set_tx_enhance(v)
            if (typeof droidstar.get_tx_enhance === "function")
                appState.txEnhance = !!droidstar.get_tx_enhance()
            else
                appState.txEnhance = v
        } else {
            appState.txEnhance = v
        }
    }
    function setVoiceIsolation(on) {
        appState.voiceIsolation = !!on
        if (droidstar && typeof droidstar.set_voice_isolation === "function")
            droidstar.set_voice_isolation(appState.voiceIsolation)
    }
    function setRxClean(on) {
        var v = !!on
        if (droidstar && typeof droidstar.set_rx_clean === "function") {
            droidstar.set_rx_clean(v)
            if (typeof droidstar.get_rx_clean === "function")
                appState.rxClean = !!droidstar.get_rx_clean()
            else
                appState.rxClean = v
        } else {
            appState.rxClean = v
        }
    }
    function setDmrSlot(i) { appState.dmrSlot = i; if (droidstar) droidstar.set_slot(i) }
    function setDmrCc(i) { appState.dmrCc = i; if (droidstar) droidstar.set_cc(i) }
    function setPrivateCall(on) {
        window._forgetSmsStandby()
        appState.privateCall = !!on
        flushPendingEdits()
        if (droidstar) {
            droidstar.set_dmr_pc(appState.privateCall ? 1 : 0)
            if (appState.dmrtgid !== "") {
                droidstar.set_dmrtgid(appState.dmrtgid)
                droidstar.tgid_text_changed(appState.dmrtgid)
            }
        }
    }
    function setM17Can(v) { appState.modemM17CAN = v; if (droidstar) droidstar.set_modemM17CAN(v) }
    function setM17VoiceFull(on) { appState.m17VoiceFull = on; if (droidstar) droidstar.m17_rate_changed(on) }
    function setTtsVoice(v) { appState.ttsVoice = v; if (droidstar) droidstar.tts_changed(v) }
    function setTtsText(s) { appState.ttsText = s; if (droidstar) droidstar.tts_text_changed(s) }

    // ── LIVE TRANSCRIPTION & NET TUNING ────────────────────
    function _syncTranscriptTextOnly() {
        if (!window.transcriptionEngineRef) return
        if (appState.connected) {
            appState.currentTranscript = window.transcriptionEngineRef.currentTranscript || ""
            appState.transcriptCallsign = window.transcriptionEngineRef.currentCallsign || ""
            appState.transcriptActiveTurn = !!(window.transcriptionEngineRef.activeTurn && appState.data1 !== "")
        } else {
            appState.currentTranscript = ""
            appState.transcriptCallsign = ""
            appState.transcriptActiveTurn = false
        }
    }

    function _syncDownloadProgressOnly() {
        if (!window.transcriptionEngineRef) return
        appState.transcriptionDownloading = !!window.transcriptionEngineRef.modelDownloading
        appState.transcriptionDownloadProgress = window.transcriptionEngineRef.downloadProgress || 0.0
        appState.transcriptionActiveDownloads = window.transcriptionEngineRef.activeDownloads || []
        appState.transcriptionStatus = window.transcriptionEngineRef.statusText || ""
    }

    function _syncTranscriptionState() {
        if (!window.transcriptionEngineRef) return
        appState.transcriptionEnabled = !!window.transcriptionEngineRef.enabled
        appState.transcriptionLogEnabled = !!window.transcriptionEngineRef.logTranscripts
        appState.transcriptionEngine = window.transcriptionEngineRef.engineType || "whisper"
        appState.transcriptionAppleSupported = !!window.transcriptionEngineRef.appleSpeechSupported
        appState.transcriptionAppleAvailable = !!window.transcriptionEngineRef.appleSpeechAvailable
        appState.transcriptionParakeetSupported = !!window.transcriptionEngineRef.parakeetSupported
        if (appState.transcriptionDeviceName === "") {
            appState.transcriptionDeviceName = window.transcriptionEngineRef.deviceModelName || ""
            appState.transcriptionParakeetDisabledReason = window.transcriptionEngineRef.modelUnsupportedReason
                                                           ? (window.transcriptionEngineRef.modelUnsupportedReason("parakeet") || "")
                                                           : ""
            appState.transcriptionSmallDisabledReason = window.transcriptionEngineRef.modelUnsupportedReason
                                                        ? (window.transcriptionEngineRef.modelUnsupportedReason("small") || "")
                                                        : ""
            appState.transcriptionTurboDisabledReason = window.transcriptionEngineRef.modelUnsupportedReason
                                                        ? (window.transcriptionEngineRef.modelUnsupportedReason("turbo") || "")
                                                        : ""
        }
        appState.transcriptionLanguage = window.transcriptionEngineRef.language || "auto"
        appState.transcriptionModel = window.transcriptionEngineRef.modelType || "base"
        appState.transcriptionModelName = window.transcriptionEngineRef.modelDisplayName || ""
        appState.transcriptionModelReady = !!window.transcriptionEngineRef.modelReady
        window._syncDownloadProgressOnly()
        window._syncTranscriptTextOnly()
    }

    function setTranscriptionEnabled(on) {
        var enabled = !!on
        appState.transcriptionEnabled = enabled
        controlSettings.transcriptionEnabled = enabled
        if (window.transcriptionEngineRef) {
            window.transcriptionEngineRef.setUserCallsign(appState.callsign)
            window.transcriptionEngineRef.enabled = enabled
            window._syncTranscriptionState()
        }
    }

    function setTranscriptionLogEnabled(on) {
        appState.transcriptionLogEnabled = !!on
        if (window.transcriptionEngineRef) {
            window.transcriptionEngineRef.logTranscripts = !!on
            window._syncTranscriptionState()
        }
    }

    function setTranscriptionEngine(engine) {
        var e = (engine === "parakeet") ? "parakeet" : ((engine === "whisper") ? "whisper" : "apple")
        appState.transcriptionEngine = e
        controlSettings.transcriptionEngine = e
        if (window.transcriptionEngineRef) {
            window.transcriptionEngineRef.engineType = e
            window._syncTranscriptionState()
        }
    }

    function setTranscriptionLanguage(lang) {
        var l = (lang || "auto").trim().toLowerCase()
        appState.transcriptionLanguage = l
        controlSettings.transcriptionLanguage = l
        if (window.transcriptionEngineRef) {
            window.transcriptionEngineRef.language = l
            window._syncTranscriptionState()
        }
    }

    function setTranscriptionModel(modelName) {
        var m = (modelName === "turbo" || modelName === "small" || modelName === "base") ? modelName : "tiny"
        appState.transcriptionModel = m
        appState.transcriptionEngine = "whisper"
        controlSettings.transcriptionModel = m
        controlSettings.transcriptionEngine = "whisper"
        if (window.transcriptionEngineRef) {
            window.transcriptionEngineRef.engineType = "whisper"
            window.transcriptionEngineRef.modelType = m
            window._syncTranscriptionState()
        }
    }

    function downloadTranscriptionModel(key) {
        if (window.transcriptionEngineRef) {
            var target = key || ((appState.transcriptionEngine === "parakeet") ? "parakeet" : appState.transcriptionModel)
            window.transcriptionEngineRef.downloadModel(target)
            window._syncTranscriptionState()
        }
    }

    function cancelTranscriptionDownload(key) {
        if (window.transcriptionEngineRef) {
            window.transcriptionEngineRef.cancelDownload(key || "")
            window._syncTranscriptionState()
        }
    }

    function tuneToNet(net) {
        if (!net || !droidstar) return
        var targetMode = (net.mode || "").trim()
        var targetHost = (net.host || "").trim()
        var targetModule = (net.module || "A").trim()
        var targetTgid = (net.tgid !== undefined && net.tgid !== null) ? String(net.tgid).trim() : ""

        window.navigate("main")

        // Smart One-Tap Tune: if already connected to the same mode & host, switch TG/Module live without disconnecting!
        if (appState.connected &&
            (targetMode === "" || targetMode === appState.mode) &&
            (targetHost === "" || targetHost === appState.selectedHost || targetHost === appState.host)) {
            if ((appState.mode === "DMR" || appState.mode === "P25" || appState.mode === "NXDN") && targetTgid !== "") {
                if (appState.mode === "DMR" && appState.privateCall) {
                    window.setPrivateCall(false)
                }
                window.commitTalkgroup(targetTgid)
            }
            if ((appState.mode === "REF" || appState.mode === "XRF" || appState.mode === "DCS" || appState.mode === "M17") && targetModule !== "") {
                window.setModule(targetModule)
            }
            window.notify(qsTr("Tuned to %1").arg(net.name || targetHost || targetTgid), "", null, "success")
            return
        }

        // If currently connected to a different mode/host, disconnect first, then apply and connect
        var wasBusy = (appState.connected || appState.connecting)
        if (wasBusy) {
            if (appState.txActive) window.txSet(false)
            droidstar.process_connect()
        }

        if (targetMode !== "" && targetMode !== appState.mode) {
            window.setMode(targetMode)
        }
        if (targetHost !== "") {
            window.setHost(targetHost)
        }
        if (targetModule !== "") {
            window.setModule(targetModule)
        }
        if (targetTgid !== "") {
            appState.privateCall = false
            window.commitTalkgroup(targetTgid)
        }

        if ((appState.callsign || "").trim() !== "") {
            Qt.callLater(function() {
                if (!appState.connected && !appState.connecting) {
                    window.connectOrDisconnect()
                }
            })
            window.notify(qsTr("Connecting to %1…").arg(net.name || targetHost || targetMode), "", null, "info")
        } else {
            window.notify(qsTr("Loaded %1 settings").arg(net.name || targetMode), "", null, "info")
        }
    }

    function setMode(m) {
        if (!droidstar || m === appState.mode) return
        appState.mode = m
        // process_mode_change saves the mode and emits mode_changed (reloads hosts below).
        droidstar.process_mode_change(m)
    }

    function setHost(h) {
        if (!droidstar || !h) return
        appState.selectedHost = h
        droidstar.set_dst(h)
        if (!droidstar.get_modelchange()) droidstar.process_host_change(h)
    }

    function setModule(m) {
        appState.module = m
        if (droidstar) droidstar.set_module(m)
    }

    function commitTalkgroup(tg) {
        var v = ("" + tg).trim()
        if (v !== String(appState.dmrtgid || "")) window._forgetSmsStandby()
        appState.dmrtgid = v
        if (!droidstar) return
        droidstar.set_dmrtgid(v)
        droidstar.tgid_text_changed(v)
        if (v !== "" && parseInt(v) > 0) {
            droidstar.addRecentTGID(v)
            appState.recentTgids = droidstar.loadRecentTGIDs()
        }
    }

    function clearRecentTalkgroups() {
        if (!droidstar) return
        droidstar.clearRecentTGIDs()
        appState.recentTgids = []
    }

    function removeRecentTalkgroup(tg) {
        var v = ("" + (tg || "")).trim()
        if (!droidstar || v === "") return
        if (typeof droidstar.removeRecentTGID === "function")
            droidstar.removeRecentTGID(v)
        appState.recentTgids = droidstar.loadRecentTGIDs()
    }

    function getTalkgroupName(tg) {
        var v = ("" + (tg || "")).trim()
        if (!droidstar || v === "" || typeof droidstar.getTalkgroupName !== "function") return ""
        return droidstar.getTalkgroupName(v) || ""
    }

    function isFavouriteTalkgroup(tg) {
        var v = ("" + (tg || "")).trim()
        if (v === "" || !appState.favouriteTgids) return false
        return appState.favouriteTgids.indexOf(v) >= 0
    }

    function toggleFavouriteTalkgroup(tg) {
        var v = ("" + (tg || "")).trim()
        if (!droidstar || v === "" || !(parseInt(v) > 0)) return
        var wasFav = isFavouriteTalkgroup(v)
        if (typeof droidstar.toggleFavouriteTGID === "function")
            droidstar.toggleFavouriteTGID(v)
        appState.favouriteTgids = droidstar.loadFavouriteTGIDs()
        window.syncHoselineFromBackend()
        window.updateHoselineStreamLifecycle()
        var nm = getTalkgroupName(v)
        notify(wasFav
               ? qsTr("Removed TG %1 from Favourites").arg(v)
               : qsTr("Pinned TG %1%2 to Favourites").arg(v).arg(nm !== "" ? (" • " + nm) : ""),
               "", null, wasFav ? "info" : "success")
    }

    function addFavouriteTalkgroup(tg) {
        var v = ("" + (tg || "")).trim()
        if (!droidstar || v === "" || !(parseInt(v) > 0)) return
        if (typeof droidstar.addFavouriteTGID === "function")
            droidstar.addFavouriteTGID(v)
        appState.favouriteTgids = droidstar.loadFavouriteTGIDs()
        window.syncHoselineFromBackend()
        window.updateHoselineStreamLifecycle()
        var nm = getTalkgroupName(v)
        notify(qsTr("Pinned TG %1%2 to Favourites").arg(v).arg(nm !== "" ? (" • " + nm) : ""), "", null, "success")
    }

    function removeFavouriteTalkgroup(tg) {
        var v = ("" + (tg || "")).trim()
        if (!droidstar || v === "") return
        if (typeof droidstar.removeFavouriteTGID === "function")
            droidstar.removeFavouriteTGID(v)
        appState.favouriteTgids = droidstar.loadFavouriteTGIDs()
        window.syncHoselineFromBackend()
        window.updateHoselineStreamLifecycle()
        notify(qsTr("Removed TG %1 from Favourites").arg(v), "", null, "info")
    }

    // Join the saved DMR host. Call after the talkgroup is committed so login links that TG.
    function _connectForTune() {
        if (!droidstar || appState.connected || appState.connecting) return
        if (appState.mode !== "DMR")
            window.setMode("DMR")
        window.connectOrDisconnect()
    }

    function quickSwitchToTalkgroup(tg, silent, asPrivate) {
        var v = String(tg === undefined || tg === null ? "" : tg).replace(/[^0-9]/g, "")
        if (!droidstar || v === "" || !(parseInt(v) > 0)) return

        flushPendingEdits()
        if ((appState.callsign || "").trim() === "") {
            notify(qsTr("Enter your callsign before connecting."), qsTr("Settings"), function() { window.navigate("settings") }, "warning")
            return
        }
        if (!(parseInt(appState.dmrid) > 0)) {
            notify(qsTr("DMR needs your DMR ID."), qsTr("Settings"), function() { window.navigate("settings") }, "warning")
            return
        }

        // If connected to a non-DMR mode, disconnect first and resume tuning in DMR mode.
        if (appState.mode !== "DMR" && (appState.connected || appState.connecting)) {
            window._resumeTuneTg = v
            window._resumeTuneSilent = !!silent
            if (appState.txActive) window.txSet(false)
            droidstar.process_connect()
            return
        }

        if (appState.mode !== "DMR") {
            window.setMode("DMR")
        }
        window._forgetSmsStandby()
        var isPriv = !!asPrivate
        appState.privateCall = isPriv
        appState.dmrtgid = v

        var host = appState.selectedHost
        if ((!host || appState.hostsModel.indexOf(host) < 0) && appState.hostsModel.length > 0)
            host = appState.hostsModel[0]

        window._bulkPush = true
        try {
            pushAllSettingsToBackend(host)
        } finally {
            window._bulkPush = false
        }
        appState.selectedHost = host || ""

        window._relinkRetryCount = 0
        window._lastRelinkAttemptMs = Date.now()
        window._heardCurrentTg = false
        window._lastScanHopMs = Date.now()
        if (!silent) {
            window._scanHoldUntilMs = Date.now() + 18000
        }

        var wasConnectedDmr = !!(appState.connected && appState.mode === "DMR")
        var wasConnectedOdtp = !!(wasConnectedDmr &&
                                  typeof droidstar.is_odtp_active === "function" && droidstar.is_odtp_active())
        if (typeof droidstar.tune_to_talkgroup === "function") {
            droidstar.tune_to_talkgroup(v, isPriv)
        } else {
            window.commitTalkgroup(v)
            window._connectForTune()
        }
        appState.recentTgids = droidstar.loadRecentTGIDs()

        if (!silent) {
            var nm = getTalkgroupName(v)
            var label = v + (nm !== "" ? (" • " + nm) : "")
            notify((wasConnectedOdtp || wasConnectedDmr) ? qsTr("Switched to TG %1").arg(label)
                                                         : qsTr("Tuning TG %1…").arg(label),
                   "", null, "success")
        }
    }

    function updateHoselineStreamLifecycle() {
        if (!droidstar || typeof droidstar.setHoselineActive !== "function") return
        var shouldRun = (window.currentPage === "scan") ||
                        appState.favScanActive ||
                        appState.allScanActive ||
                        (window._pageSlot === "main" && appState.mode === "DMR" &&
                         appState.favouriteTgids && appState.favouriteTgids.length > 0)
        droidstar.setHoselineActive(shouldRun)
    }

    function setFavScanActive(on) {
        var enable = !!on
        if (enable && (!appState.favouriteTgids || appState.favouriteTgids.length === 0)) {
            notify(qsTr("Pin at least one Favourite Talkgroup first to scan Favourites."),
                   qsTr("Scan Page"), function() { window.navigate("scan") }, "warning")
            return
        }
        appState.favScanActive = enable
        if (enable) appState.allScanActive = false
        window.updateHoselineStreamLifecycle()
        if (enable) {
            notify(qsTr("Following your pinned talkgroups. Stays while one is on air, then moves to the next."),
                   "", null, "success")
            window._evaluateAutoScan(true)
        } else {
            notify(qsTr("Stopped following favourites"), "", null, "info")
        }
    }

    function setAllScanActive(on) {
        var enable = !!on
        appState.allScanActive = enable
        if (enable) appState.favScanActive = false
        window.updateHoselineStreamLifecycle()
        if (enable) {
            notify(qsTr("Following live talkgroups. Stays while one is on air, then moves to the next."),
                   "", null, "success")
            window._evaluateAutoScan(true)
        } else {
            notify(qsTr("Stopped following live talkgroups"), "", null, "info")
        }
    }

    function scanHopNext(favOnly) {
        var pool = favOnly ? (appState.hoselineFavCalls || []) : (appState.hoselineCalls || [])
        var liveList = []
        for (var i = 0; i < pool.length; i++) {
            if (pool[i] && pool[i].isLive) liveList.push(String(pool[i].tgid))
        }
        if (liveList.length === 0) {
            if (favOnly && appState.favouriteTgids && appState.favouriteTgids.length > 0) {
                var allFavs = appState.favouriteTgids
                var curIdx = allFavs.indexOf(String(appState.dmrtgid || ""))
                var nextFav = allFavs[(curIdx + 1) % allFavs.length]
                window.quickSwitchToTalkgroup(nextFav, false)
                window._scanHoldUntilMs = Date.now() + 5000
            } else {
                notify(qsTr("No other active Talkgroups on air right now."), "", null, "info")
            }
            return
        }
        var curTg = String(appState.dmrtgid || "")
        var idx = liveList.indexOf(curTg)
        var target = liveList[(idx + 1) % liveList.length]
        window.quickSwitchToTalkgroup(target, false)
        window._scanHoldUntilMs = Date.now() + 5000
    }

    function _evaluateAutoScan(forceImmediate) {
        if (!appState.favScanActive && !appState.allScanActive) return
        if (appState.txActive || appState.connecting) return
        if (typeof droidstar.is_tuning_reconnect === "function" && droidstar.is_tuning_reconnect()) return
        if (typeof droidstar.is_auto_tx_punch_pending === "function" && droidstar.is_auto_tx_punch_pending()) return
        // Stay while the radio is decoding a voice call.
        if (appState.connected && appState.data1 !== "") {
            window._lastRxEndMs = Date.now()
            return
        }
        var now = Date.now()
        if (!forceImmediate) {
            if (now < window._scanHoldUntilMs) return
            if ((now - window._lastRxEndMs) < 5000) return
            if ((now - window._lastScanHopMs) < 7000) return
        }

        var curTg = String(appState.dmrtgid || "")
        var pool = appState.favScanActive ? (appState.hoselineFavCalls || []) : (appState.hoselineCalls || [])

        // Never leave a talkgroup that is still on air. Next is the manual hop.
        if (curTg !== "" && curTg !== "4000") {
            for (var c = 0; c < pool.length; c++) {
                if (pool[c] && String(pool[c].tgid) === curTg && pool[c].isLive) {
                    if (!appState.connected && !appState.connecting && forceImmediate)
                        window.quickSwitchToTalkgroup(curTg, true, false)
                    return
                }
            }
        }

        for (var i = 0; i < pool.length; i++) {
            var item = pool[i]
            if (item && item.isLive && String(item.tgid) !== curTg) {
                window.quickSwitchToTalkgroup(String(item.tgid), true, false)
                return
            }
        }

        // Nothing else is on air. Connect so the current talkgroup is ready.
        if (forceImmediate && !appState.connected && !appState.connecting && curTg !== "")
            window.quickSwitchToTalkgroup(curTg, true, false)
    }

    function syncHoselineFromBackend() {
        if (!droidstar) return
        if (typeof droidstar.getHoselineStatus === "function") {
            var st = droidstar.getHoselineStatus()
            appState.hoselineConnected = !!(st && st.connected)
            appState.hoselineLiveCount = (st && st.liveCount !== undefined) ? st.liveCount : 0
            appState.hoselineActiveFavCount = (st && st.activeFavCount !== undefined) ? st.activeFavCount : 0
        }
        if (typeof droidstar.getHoselineCalls === "function") {
            appState.hoselineCalls = droidstar.getHoselineCalls()
        }
        if (typeof droidstar.getActiveFavouriteCalls === "function") {
            appState.hoselineFavCalls = droidstar.getActiveFavouriteCalls()
        }
        // Closed-loop verification: if our currently tuned BrandMeister talkgroup is live on air
        // on Hoseline, but local RX hasn't started yet after initial link, re-link once
        // (BrandMeister Homebrew only; ODTP subscribes via REWIND_TYPE_SUBSCRIPTION, and Hoseline is BM-only).
        var odtpOn = (typeof droidstar.is_odtp_active === "function" && droidstar.is_odtp_active())
        var tgifOn = (typeof droidstar.is_tgif_host === "function" && droidstar.is_tgif_host())
        var isBmHost = String(appState.selectedHost || "").toUpperCase().indexOf("BM") === 0 ||
                       String(appState.host || "").toLowerCase().indexOf("brandmeister") >= 0
        var punchPending = (typeof droidstar.is_auto_tx_punch_pending === "function" && droidstar.is_auto_tx_punch_pending())
        if (appState.connected && appState.mode === "DMR" && isBmHost && !odtpOn && !tgifOn && !appState.privateCall &&
            !appState.txActive && !punchPending && appState.data1 === "" && !window._heardCurrentTg) {
            var tunedTg = String(appState.dmrtgid || "").trim()
            if (tunedTg !== "" && tunedTg !== "4000" && window._relinkRetryCount < 2) {
                var nowMs = Date.now()
                if ((nowMs - window._lastRelinkAttemptMs) >= 3800) {
                    var calls = appState.hoselineCalls || []
                    for (var k = 0; k < calls.length; k++) {
                        if (calls[k] && String(calls[k].tgid) === tunedTg && calls[k].isLive && (calls[k].durationSec | 0) >= 2) {
                            window._relinkRetryCount += 1
                            window._lastRelinkAttemptMs = nowMs
                            if (typeof droidstar.trigger_auto_tx_punch === "function")
                                droidstar.trigger_auto_tx_punch(50)
                            break
                        }
                    }
                }
            }
        }
        window._evaluateAutoScan(false)
    }

    function updateHostFiles() {
        if (!droidstar) return
        droidstar.update_host_files()
        notify(qsTr("Updating host lists…"), "", null, "info")
    }

    function updateIdFiles() {
        if (!droidstar) return
        droidstar.update_dmr_ids()
        notify(qsTr("Updating DMR ID database…"), "", null, "info")
    }

    // ── TX ──────────────────────────────────────────────────
    function _syncDmrTargetBeforeTx() {
        flushPendingEdits()
        if (!droidstar) return
        if (appState.mode === "DMR" || appState.mode === "P25" || appState.mode === "NXDN") {
            if (appState.mode === "DMR") {
                droidstar.set_dmr_pc(appState.privateCall ? 1 : 0)
            }
            var v = ("" + (appState.dmrtgid || "")).trim()
            if (v !== "") {
                droidstar.set_dmrtgid(v)
                droidstar.tgid_text_changed(v)
                if (parseInt(v) > 0) {
                    droidstar.addRecentTGID(v)
                    appState.recentTgids = droidstar.loadRecentTGIDs()
                }
            }
        }
    }

    function txSet(on) {
        if (!droidstar) return
        if (on && !appState.txEnabled) return
        if (on) _syncDmrTargetBeforeTx()
        if (on) _pauseReplayFor("tx")
        appState.txActive = on
        droidstar.click_tx(on)
    }
    function txToggle() {
        if (!appState.txEnabled) return
        txSet(!appState.txActive)
    }
    function txPress() {
        if (!droidstar || !appState.txEnabled || appState.toggleTx) return
        _syncDmrTargetBeforeTx()
        _pauseReplayFor("tx")
        appState.txActive = true
        droidstar.press_tx()
    }
    function txRelease() {
        if (!droidstar || appState.toggleTx || !appState.txActive) return
        appState.txActive = false
        droidstar.release_tx()
    }

    // TX time-out timer (legacy parity; also guards a stuck PTT).
    Timer {
        id: txClock
        interval: 1000
        repeat: true
        running: appState.txActive
        onTriggered: {
            appState.txSeconds += 1
            var lim = parseInt(appState.txTimeout)
            if (lim > 0 && appState.txSeconds >= lim) {
                if (appState.toggleTx) window.txSet(false)
                else window.txRelease()
                window.notify(qsTr("TX timed out after %1 s").arg(lim), "", null, "warning")
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    // BACKEND -> UI
    // ─────────────────────────────────────────────────────────
    // If QSO log is saved/cleared from anywhere, keep Last Heard in sync (no navigation required).
    Connections {
        target: window.logHandlerRef
        enabled: window.logHandlerRef !== null
        function onLogSaved(fileName) {
            if (fileName === "logs.json") window.syncLastHeardFromLogFile()
        }
        function onLogCleared(fileName) {
            if (fileName === "logs.json") window._setLastHeardFromArray([])
        }
    }

    Connections {
        target: window.smsManagerRef
        enabled: window.smsManagerRef !== null
        function onMessageReceived(srcId, senderLabel, preview, isGroup, threadKey) {
            window.pendingMessageThread = threadKey
            if (window.currentPage !== "messages") {
                window.notify(
                    qsTr("SMS from %1: %2").arg(senderLabel).arg(preview),
                    qsTr("View"),
                    function() {
                        window.navigate("messages")
                        if (window.smsManagerRef) window.smsManagerRef.openThread(threadKey)
                    },
                    "info")
            }
        }
        function onThreadRequested(threadKey) {
            window.pendingMessageThread = threadKey
            window.navigate("messages")
        }
        function onOperationError(message) {
            window.notify(message, "", null, "warning")
        }
    }

    Connections {
        target: droidstar
        enabled: droidstar !== null

        function onMode_changed() {
            if (!droidstar) return
            appState.mode = droidstar.get_mode()
            appState.module = droidstar.get_module()
            window.reloadHostsForMode()
        }

        function onUpdate_data() {
            if (!droidstar) return
            var oldKey = appState._lastQsoKey
            var hadRx = (appState.data1 !== "")

            appState.data1 = droidstar.get_data1()
            appState.data2 = droidstar.get_data2()
            appState.data3 = droidstar.get_data3()
            appState.data4 = droidstar.get_data4()
            appState.data5 = droidstar.get_data5()
            appState.data6 = droidstar.get_data6()
            appState.netstatus = droidstar.get_netstatustxt()
            appState.outputLevel = droidstar.get_output_level()
            appState.ambestatus = droidstar.get_ambestatustxt()
            appState.mmdvmstatus = droidstar.get_mmdvmstatustxt()
            appState.host = droidstar.get_host()
            if (typeof droidstar.is_odtp_active === "function")
                appState.odtpActive = !!droidstar.is_odtp_active()

            if (!hadRx && appState.data1 !== "" && !appState.txActive) window._pauseReplayFor("live")

            if (appState.connected && !appState.txActive && appState.data1 !== "") {
                window._lastRxEndMs = Date.now()
                window._heardCurrentTg = true
                window._relinkRetryCount = 0
            } else if (hadRx && appState.data1 === "") {
                window._lastRxEndMs = Date.now()
            }

            appState.label1 = droidstar.get_label1()
            appState.label2 = droidstar.get_label2()
            appState.label3 = droidstar.get_label3()
            appState.label4 = droidstar.get_label4()
            appState.label5 = droidstar.get_label5()
            appState.label6 = droidstar.get_label6()

            // Trigger the radioid.net lookup even if QSO page is never opened.
            var dmr = parseInt(appState.data2 || "0")
            var tgid = parseInt(appState.data3 || "0")
            var key = "" + dmr + ":" + tgid
            if (!appState.txActive && dmr > 0 && tgid > 0 && key !== oldKey) {
                appState._lastQsoKey = key
                window._pendingLogDmr = dmr
                window._pendingLogTgid = tgid
                window._pendingLogKey = key
                if (window.vuidUpdaterRef) window.vuidUpdaterRef.fetchFirstNameFromAPI(dmr)
            }
            // When RX goes idle (DMR EOT clears data2), clear the fetched display values
            // and reset the key so the next RX re-fetches even if the same handle speaks again.
            if (!(dmr > 0)) {
                appState.fetchedFirstName = ""
                appState.fetchedCountry = ""
                appState._lastQsoKey = ""
                window._lastLoggedKey = ""
            }
            if (appState.data1 === "") {
                appState.transcriptActiveTurn = false
                if (appState.currentTranscript === "") {
                    appState.transcriptCallsign = ""
                }
            }
        }

        function onUpdate_log(s) { window.appendLog(s) }
        function onRecentTgidsUpdated() {
            if (droidstar) appState.recentTgids = droidstar.loadRecentTGIDs()
        }
        function onFavouriteTgidsUpdated() {
            if (droidstar && typeof droidstar.loadFavouriteTGIDs === "function") {
                appState.favouriteTgids = droidstar.loadFavouriteTGIDs()
                window.syncHoselineFromBackend()
            }
        }
        function onHoselineUpdated() {
            window.syncHoselineFromBackend()
        }

        function onUpdate_settings() {
            if (window._bulkPush) return
            // Coalesce bursts (each setter emits once) into a single sync.
            Qt.callLater(window._onSettingsUpdated)
        }

        function onUpdate_devices() {
            if (!droidstar) return
            appState.hostsModel = window._toStringArray(droidstar.get_hosts())
            window.refreshSerialDevices()
        }

        function onPlaybackDevicesChanged() { window.refreshAudioDevices() }
        function onCaptureDevicesChanged() { window.refreshAudioDevices() }

        function onConnect_status_changed(c) {
            if (c === 1 && appState.ipv6 && appState.mode !== "M17") ipv6Watch.restart()
            else if (c !== 1) ipv6Watch.stop()
            appState.connectStatus = c
            appState.connecting = (c === 1)
            appState.connected = (c === 2)
            appState.txEnabled = (c === 2)
            if (typeof droidstar.is_odtp_active === "function")
                appState.odtpActive = (c === 2) && !!droidstar.is_odtp_active()

            if (c !== 2) {
                // Never show TX or live RX transcription as active unless fully connected
                appState.txActive = false
                if (window.pttButtonsRef) window.pttButtonsRef.resetAll()
                appState.currentTranscript = ""
                appState.transcriptCallsign = ""
                appState.transcriptActiveTurn = false
                if (window.transcriptionEngineRef && window.transcriptionEngineRef.onDisconnected) {
                    window.transcriptionEngineRef.onDisconnected()
                }
            }
            if (c === 0 || c === 5) {
                droidstar.set_output_level(0)
                appState.outputLevel = 0
                appState.fetchedFirstName = ""
                appState.fetchedCountry = ""
                appState._lastQsoKey = ""
                window._pendingLogDmr = 0
                window._pendingLogTgid = 0
                window._pendingLogKey = ""
                window._lastLoggedKey = ""
                var isTuningReconn = (c === 0 && typeof droidstar.is_tuning_reconnect === "function" && droidstar.is_tuning_reconnect())
                if (isTuningReconn) {
                    appState.connecting = true
                } else if (c === 0 && window._resumeTuneTg !== "") {
                    var pendingTune = window._resumeTuneTg
                    var pendingSilent = window._resumeTuneSilent
                    window._resumeTuneTg = ""
                    Qt.callLater(function() { window.quickSwitchToTalkgroup(pendingTune, pendingSilent) })
                } else if (appState.favScanActive || appState.allScanActive) {
                    // Disconnect stops following so the scanner cannot reconnect on its own.
                    appState.favScanActive = false
                    appState.allScanActive = false
                }
                if (!isTuningReconn) window._restoreHomeTarget()
            }
            if (c === 2) {
                window._lastScanHopMs = Date.now()
                window._lastRelinkAttemptMs = Date.now()
                // Backend may set MYCALL/URCALL/RPTR1/RPTR2 on connect.
                window.syncSettingsFromBackend()
                // Backend emits in_audio_vol_changed(0.5) right after this signal; apply ours after it.
                Qt.callLater(window.applyRuntimeState)
            }
            var authNet = (c === 5 && typeof droidstar.get_auth_failed_network === "function")
                          ? String(droidstar.get_auth_failed_network()) : ""
            if (authNet !== "") {
                window._setAuthStatus(authNet, "rejected")
                var ck = Object.assign({}, window._authCheckedKey)
                ck[authNet] = window._authKey(authNet)
                window._authCheckedKey = ck
                window.showAuthSheet([authNet])
            } else if (c === 5) {
                errorDialog.text = droidstar.get_error_text()
                if (errorDialog.text === "") errorDialog.text = qsTr("Connection refused.")
                errorDialog.open()
            }
            window.updateDynamicIsland()
        }

        function onSwtx_state(s) { appState.swtx = !!s }
        function onSwrx_state(s) { appState.swrx = !!s }
        function onAgc_state(s) { appState.agc = !!s }

        // Remote PTT (headphones / Control Center) goes straight to the backend; mirror it.
        function onTx_pressed() { if (appState.txEnabled) appState.txActive = true }
        function onTx_released() { appState.txActive = false }
        function onTx_clicked(tx) { appState.txActive = !!tx && appState.txEnabled }

        // Legacy parity: backend can update these fields during connect/runtime.
        function onMycall_changed(s) { appState.mycall = String(s) }
        function onUrcall_changed(s) { appState.urcall = String(s) }
        function onRptr1_changed(s) { appState.rptr1 = String(s) }
        function onRptr2_changed(s) { appState.rptr2 = String(s) }
        function onUsrtxt_changed(s) { appState.usrtxt = String(s) }
    }

    // Hardware PTT buttons: the manager turns raw button events into edges; QML applies TX so
    // every source respects the connected state and the toggle/hold setting.
    Binding {
        target: window.pttButtonsRef
        property: "holdToTalkMode"
        value: !appState.toggleTx
        when: window.pttButtonsRef !== null
    }
    Connections {
        target: window.pttButtonsRef
        enabled: window.pttButtonsRef !== null
        function onHoldActiveChanged(active) {
            if (active || appState.txActive) window.txSet(active)
        }
        function onClicked(id) { window.txToggle() }
        function onStopRequested() { if (appState.txActive) window.txSet(false) }
        function onSafetyReleased(reason) { window.notify(reason, "", null, "warning") }
        function onDeviceAdded(id, name) {
            window.notify(qsTr("%1 added as a PTT button").arg(name), "", null, "success")
        }
        function onSuggestToggleTxMode(id, name) {
            if (appState.toggleTx) return
            if (appState.txActive) window.txRelease()
            appState.toggleTx = true
            if (droidstar) droidstar.set_toggletx(true)
            window.notify(qsTr("%1 is a click-only button, so Toggle TX mode was turned on").arg(name),
                          qsTr("Revert"),
                          function() {
                              if (appState.txActive) window.txSet(false)
                              appState.toggleTx = false
                              if (droidstar) droidstar.set_toggletx(false)
                              if (window.pttButtonsRef) window.pttButtonsRef.recordAutoSwitchDecision(id, false)
                              window.notify(qsTr("Hold-to-talk restored. The button still works as a toggle."), "", null, "info")
                          }, "info")
        }
    }

    // App lifecycle
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) {
                safeAreaTimer.restart()
            } else if (!appState.toggleTx && appState.txActive
                       && !(window.pttButtonsRef && window.pttButtonsRef.holdActive)) {
                // A held on-screen PTT can't be released once we lose the touch; don't leave TX keyed.
                // A Bluetooth button hold keeps working with the screen off, so it is left alone.
                window.txRelease()
            }
            if (Qt.application.state === Qt.ApplicationClosing && droidstar) {
                droidstar.reset_connect_status()
            }
        }
    }

    // Android back: close drawer, pop subview, then return to Main, then exit.
    onClosing: function(close) {
        if (Qt.platform.os !== "android") return
        if (navDrawer.opened && !navDocked) { navDrawer.close(); close.accepted = false; return }
        if (window.currentPageItem && typeof window.currentPageItem.handleBack === "function" && window.currentPageItem.handleBack()) {
            close.accepted = false
            return
        }
        if (currentPage !== "main") { navigate("main"); close.accepted = false }
    }

    // vuidUpdater -> UI state
    Connections {
        target: window.vuidUpdaterRef
        enabled: window.vuidUpdaterRef !== null
        function onFetchedFirstNameChanged(name) {
            appState.fetchedFirstName = name
            // Update iOS Now Playing / Lock Screen with latest RX info
            if (window.droidstar && appState.data1) {
                window.droidstar.updateNowPlayingRX(appState.data1, appState.fetchedFirstName, appState.fetchedCountry)
            }
        }
        function onFetchedCountryChanged(country) {
            appState.fetchedCountry = country
            if (window.droidstar && appState.data1) {
                window.droidstar.updateNowPlayingRX(appState.data1, appState.fetchedFirstName, appState.fetchedCountry)
            }
        }
        function onUserLookupReady(dmrId, callsign, name, country) {
            // Only log if this matches the last requested RX key
            if (!window.logHandlerRef) return
            if (!window._pendingLogDmr || window._pendingLogDmr !== dmrId) return
            if (!window._pendingLogTgid) return

            // Optional filter: don't log self
            if (appState.disableSelfLog && window._isSelfContact(dmrId, callsign || "")) return

            var key = "" + dmrId + ":" + window._pendingLogTgid
            var current = window.logHandlerRef.loadLog("logs.json")
            if (!current || current.length === undefined) current = []

            if (current.length > 0) {
                var top = current[0]
                if ((top.dmrID || 0) === dmrId && (top.tgid || 0) === window._pendingLogTgid) {
                    // Enrich top entry if it was created early by live transcription before radioid.net replied
                    var updated = false
                    if (callsign && (!top.callsign || top.callsign === String(dmrId))) { top.callsign = callsign; updated = true }
                    if (name && !top.fname) { top.fname = name; updated = true }
                    if (country && !top.country) { top.country = country; updated = true }
                    window._lastLoggedKey = key
                    if (updated) {
                        window._setLastHeardFromArray(current)
                        window.logHandlerRef.saveLogAsync("logs.json", current)
                    }
                    return
                }
            }

            if (key === window._lastLoggedKey) return

            var nextSerial = 0
            for (var i = 0; i < current.length; i++) {
                var sn = current[i].serialNumber
                if (sn !== undefined && sn !== null) nextSerial = Math.max(nextSerial, sn + 1)
            }

            current.unshift({
                serialNumber: nextSerial,
                callsign: callsign || "",
                dmrID: dmrId || 0,
                tgid: window._pendingLogTgid,
                country: country || "",
                fname: name || "",
                currentTime: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss"),
                transcript: "",
                transcriptTurns: [],
                checked: false
            })

            var lim = window._effectiveQsoLogLimit()
            while (current.length > lim) current.pop()
            window._setLastHeardFromArray(current)
            window._lastLoggedKey = key
            window.logHandlerRef.saveLogAsync("logs.json", current)
        }
    }

    // TranscriptionEngine -> UI & QSO log updates
    Connections {
        target: window.transcriptionEngineRef
        enabled: window.transcriptionEngineRef !== null

        function onTranscriptionEnabledChanged() { window._syncTranscriptionState() }
        function onLogTranscriptsChanged() { window._syncTranscriptionState() }
        function onEngineTypeChanged() { window._syncTranscriptionState() }
        function onLanguageChanged() { window._syncTranscriptionState() }
        function onModelTypeChanged() { window._syncTranscriptionState() }
        function onModelStateChanged() { window._syncTranscriptionState() }
        function onDownloadProgressChanged() { window._syncDownloadProgressOnly() }
        function onTranscriptChanged() { window._syncTranscriptTextOnly() }
        function onModelDownloadError(err) {
            window._syncTranscriptionState()
            window.notify(qsTr("Transcription model download failed: %1").arg(err), "", null, "error")
        }
        function onFinalTranscriptReady(turnId, srcId, callsign, text, timestamp) {
            window._syncTranscriptTextOnly()
            if (!appState.transcriptionLogEnabled) return
            if (!window.logHandlerRef || !text || text.trim() === "") return

            var baseCall = window._extractBaseCallsign(callsign)
            var isNumericCall = /^[0-9]+$/.test(baseCall)
            var cleanCall = isNumericCall ? "" : baseCall
            var dmrId = parseInt(srcId || "0")
            if (isNaN(dmrId) || dmrId < 0) dmrId = 0
            if (dmrId === 0 && isNumericCall) dmrId = parseInt(baseCall)

            if (appState.disableSelfLog && window._isSelfContact(dmrId, cleanCall)) return

            var current = window.logHandlerRef.loadLog("logs.json")
            if (!current || current.length === undefined) current = []

            var matchedIdx = -1
            var checkCount = Math.min(current.length, 10)
            for (var i = 0; i < checkCount; i++) {
                var row = current[i]
                var rowDmr = parseInt(row.dmrID || "0")
                var rowCall = window._extractBaseCallsign(row.callsign)
                if (dmrId > 0 && rowDmr > 0 && rowDmr === dmrId) {
                    matchedIdx = i
                    break
                }
                if (cleanCall !== "" && rowCall !== "" && rowCall === cleanCall) {
                    matchedIdx = i
                    break
                }
            }

            var activeTg = window._pendingLogTgid || parseInt(appState.data3 || appState.dmrtgid || "0") || 0
            var turnObj = {
                turnId: turnId || 0,
                turnKey: window._transcriptSessionId + ":" + (turnId || 0),
                time: timestamp || Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss"),
                text: text.trim(),
                tgid: activeTg
            }

            if (matchedIdx >= 0) {
                window._appendTurnToLogEntry(current[matchedIdx], turnObj)
            } else if (dmrId > 0 || cleanCall !== "") {
                // Create the QSO entry for this station so the transcript is never mapped to a different handle
                var nextSerial = 0
                for (var k = 0; k < current.length; k++) {
                    var sn = current[k].serialNumber
                    if (sn !== undefined && sn !== null) nextSerial = Math.max(nextSerial, sn + 1)
                }
                var newEntry = {
                    serialNumber: nextSerial,
                    callsign: cleanCall !== "" ? cleanCall : (dmrId > 0 ? String(dmrId) : ""),
                    dmrID: dmrId,
                    tgid: activeTg,
                    country: appState.fetchedCountry || "",
                    fname: appState.fetchedFirstName || "",
                    currentTime: turnObj.time,
                    transcript: "",
                    transcriptTurns: [],
                    checked: false
                }
                window._appendTurnToLogEntry(newEntry, turnObj)
                current.unshift(newEntry)
                var lim = window._effectiveQsoLogLimit()
                while (current.length > lim) current.pop()
                window._setLastHeardFromArray(current)
            } else {
                return
            }

            window.logHandlerRef.saveLogAsync("logs.json", current)
        }
    }

    // QsoReplayEngine -> UI state & QSO log attachment
    Connections {
        target: window.replayEngineRef
        enabled: window.replayEngineRef !== null

        function onReplayEnabledChanged() { window._syncReplayState() }
        function onRecordTxChanged() { window._syncReplayState() }
        function onStorageBudgetMbChanged() { window._syncReplayState() }
        function onMaxOverSecondsChanged() { window._syncReplayState() }
        function onStorageChanged() { window._syncReplayState() }
        function onRecordingChanged() { window._syncReplayState() }
        function onLastErrorChanged() { window._syncReplayState() }
        function onOverRecorded(id, kind, mode, srcId, callsign, tgid, startTime, durationMs, bytes, truncated) {
            window._attachReplayClip(id, kind, mode, srcId, callsign, tgid, startTime, durationMs, bytes, truncated)
        }
        function onRecordingsRemoved(ids) { window._stripReplayClips(ids) }
        function onRecordingFailed(reason) {
            window.notify(qsTr("QSO Replay: %1").arg(reason), "", null, "warning")
        }
    }

    Connections {
        target: window.replayPlayerRef
        enabled: window.replayPlayerRef !== null
        function onError(reason) { window.notify(reason, "", null, "warning") }
    }

    // NetReminderManager -> Notification tap deep-link
    Connections {
        target: window.netReminderManagerRef
        enabled: window.netReminderManagerRef !== null

        function onNetNotificationTapped(net) {
            if (!net) return
            window.notify(
                qsTr("Net Reminder: %1").arg(net.name || qsTr("Ham Net")),
                qsTr("Tune Now"),
                function() { window.tuneToNet(net) },
                "info"
            )
            window.navigate("nets")
        }
    }

    // Persist UI settings when changed + enforce log trimming immediately.
    Connections {
        target: appState
        function onDisableSelfLogChanged() {
            uiSettings.disableSelfLog = !!appState.disableSelfLog
            if (window.replayEngineRef) window.replayEngineRef.selfLoggingAllowed = !appState.disableSelfLog
        }
        function onQsoLogLimitChanged() {
            var clamped = window._clampQsoLogLimit(appState.qsoLogLimit)
            if (clamped !== appState.qsoLogLimit) appState.qsoLogLimit = clamped
            uiSettings.qsoLogLimit = clamped
            if (window.initialized) window._trimAndPersistQsoLogIfNeeded()
        }
        function onMicGainChanged() { controlSettings.micGain = appState.micGain }
        function onSpkVolumeChanged() {
            controlSettings.spkVolume = appState.spkVolume
            if (window.replayPlayerRef) window.replayPlayerRef.volume = Math.max(0, Math.min(1, appState.spkVolume))
        }
        function onSpkPreampDbChanged() { controlSettings.spkPreampDb = appState.spkPreampDb }
        function onAgcChanged() { controlSettings.agc = appState.agc }
        function onDmrSlotChanged() { controlSettings.dmrSlot = appState.dmrSlot }
        function onDmrCcChanged() { controlSettings.dmrCc = appState.dmrCc }
        function onModemM17CANChanged() { controlSettings.m17Can = appState.modemM17CAN }
        function onM17VoiceFullChanged() { controlSettings.m17VoiceFull = appState.m17VoiceFull }
        function onTtsVoiceChanged() { controlSettings.ttsVoice = appState.ttsVoice }
        function onTxActiveChanged() { appState.txSeconds = 0 }
    }

    Timer {
        id: dynamicIslandDebounce
        interval: 25
        repeat: false
        onTriggered: window.updateDynamicIsland()
    }

    function scheduleDynamicIslandUpdate() {
        dynamicIslandDebounce.restart()
    }

    function updateDynamicIsland() {
        if (!window.liveActivityRef) return
        if (!appState.connected) {
            window.liveActivityRef.end()
            return
        }

        // For YSF/FCS, data2 holds the source callsign and data1 holds the gateway
        var isYsfFamily = (appState.mode === "YSF" || appState.mode === "FCS")
        var rawRxCall = isYsfFamily ? (appState.data2 || appState.data1 || "") : (appState.data1 || "")
        var rxCallsign = rawRxCall.trim()
        var activeTg = (appState.data3 && appState.data3 !== "") ? appState.data3.trim()
                       : ((appState.dmrtgid && appState.dmrtgid !== "") ? appState.dmrtgid.trim() : "")
        var hostLabel = appState.selectedHost || appState.host || ""

        var mode = "IDLE"
        var callsign = ""
        var handle = ""
        var country = ""
        var tgid = activeTg

        if (appState.txActive) {
            mode = "TX"
            callsign = (appState.callsign && appState.callsign !== "") ? appState.callsign : "ON AIR"
            handle = appState.mode ? (appState.mode + " • Transmitting") : "Transmitting"
            country = hostLabel
            tgid = (appState.dmrtgid && appState.dmrtgid !== "") ? appState.dmrtgid : activeTg
        } else if (rxCallsign !== "") {
            mode = "RX"
            callsign = rxCallsign
            handle = appState.fetchedFirstName || (appState.mode === "DMR" && appState.data2 ? ("ID " + appState.data2) : "")
            country = appState.fetchedCountry || hostLabel
            tgid = activeTg
        } else {
            // Connected standby state
            mode = "IDLE"
            callsign = appState.mode || "Standby"
            handle = hostLabel !== "" ? hostLabel : "Listening…"
            country = appState.callsign || ""
            tgid = activeTg
        }

        window.liveActivityRef.startOrUpdate(mode, callsign, handle, country, tgid)
    }

    // Update live activity from app state changes (using CFRunLoop-backed Timer so background updates fire reliably)
    Connections {
        target: appState
        function onConnectedChanged() { window.updateDynamicIsland() }
        function onTxActiveChanged() { window.updateDynamicIsland() }
        function onData1Changed() { window.scheduleDynamicIslandUpdate() }
        function onData2Changed() { window.scheduleDynamicIslandUpdate() }
        function onData3Changed() { window.scheduleDynamicIslandUpdate() }
        function onFetchedFirstNameChanged() { window.scheduleDynamicIslandUpdate() }
        function onFetchedCountryChanged() { window.scheduleDynamicIslandUpdate() }
    }

    // ─────────────────────────────────────────────────────────
    // NAVIGATION DRAWER (modal on phones, docked sidebar on wide screens)
    // ─────────────────────────────────────────────────────────
    Drawer {
        id: navDrawer
        y: window.navDocked ? topBar.height : 0
        width: window.navDocked ? 264 + safeAreaObj.left : Math.min(window.width * 0.84, 320 + safeAreaObj.left)
        height: window.height - y
        edge: Qt.LeftEdge
        modal: !window.navDocked
        interactive: !window.navDocked
        position: window.navDocked ? 1 : 0
        visible: window.navDocked
        dim: !window.navDocked
        closePolicy: window.navDocked ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

        Overlay.modal: Rectangle { color: "#99000000" }

        background: Rectangle {
            color: t.surface2
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: t.stroke
                visible: window.navDocked
            }
        }

        Flickable {
            id: drawerFlick
            anchors.fill: parent
            anchors.leftMargin: 14 + safeAreaObj.left
            anchors.rightMargin: 14
            anchors.topMargin: window.navDocked ? 14 : 14 + safeAreaObj.top
            anchors.bottomMargin: 14 + safeAreaObj.bottom
            contentWidth: width
            contentHeight: drawerColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ColumnLayout {
                id: drawerColumn
                width: drawerFlick.width
                height: Math.max(drawerFlick.height, implicitHeight)
                // Nav rows carry their own padding; tighter gaps keep the whole menu visible on shorter screens
                readonly property bool compactMenu: window.height < 900
                spacing: compactMenu ? 4 : 8

                AppCard {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    radius: 18
                    RowLayout {
                        anchors.fill: parent
                        spacing: 12

                        Item {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48

                            Rectangle {
                                anchors.fill: parent
                                radius: 12
                                color: "#080C12"
                                border.color: t.tint(t.accent, 0.42)
                                border.width: 1
                            }

                            Image {
                                id: drawerLogo
                                anchors.fill: parent
                                anchors.margins: 4
                                source: "qrc:/DroidStar/images/droidstar.png"
                                sourceSize.width: 128
                                sourceSize.height: 128
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                                asynchronous: true
                                onStatusChanged: {
                                    // Dev/build fallback: if QRC is missing, try relative file (keeps UI usable).
                                    if (status === Image.Error) source = Qt.resolvedUrl("../images/droidstar.png")
                                }
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: drawerLogo.status === Image.Error
                                text: "DS"
                                font.bold: true
                                font.pixelSize: 14
                                color: t.text
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: qsTr("DroidStar"); font.pixelSize: 20; font.bold: true; color: t.text }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Label {
                                    visible: appState.connected && appState.odtpActive
                                    text: "\uf0e7"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 11
                                    color: t.success
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: appState.connected ? (appState.mode + " • " + (appState.selectedHost || appState.host))
                                          : (appState.connecting ? qsTr("Connecting…") : qsTr("Not connected"))
                                    color: appState.connected ? t.success : t.textMuted
                                    font.pixelSize: t.fsSmall
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: [
                        { key: "main",     title: qsTr("Main"),     icon: "\uf015" },
                        {
                            key: "scan",
                            title: qsTr("Scan"),
                            icon: "\uf002",
                            badge: (appState.hoselineActiveFavCount > 0)
                                   ? (appState.hoselineActiveFavCount + " " + qsTr("LIVE"))
                                   : qsTr("LIVE")
                        },
                        {
                            key: "messages",
                            title: qsTr("Messages"),
                            icon: "\uf0e0",
                            badge: (window.smsManagerRef && window.smsManagerRef.unreadCount > 0)
                                   ? (window.smsManagerRef.unreadCount + " " + qsTr("NEW"))
                                   : qsTr("NEW")
                        },
                        { key: "settings", title: qsTr("Settings"), icon: "\uf013" },
                        { key: "qso",      title: qsTr("QSO"),      icon: "\uf2b9" },
                        { key: "nets",     title: qsTr("Nets"),     icon: "\uf0f3", badge: qsTr("NEW") },
                        { key: "log",      title: qsTr("Log"),      icon: "\uf15c" },
                        { key: "hosts",    title: qsTr("Hosts"),    icon: "\uf233" },
                        { key: "about",    title: qsTr("About"),    icon: "\uf05a" }
                    ]

                    delegate: DrawerItem {
                        required property var modelData
                        Layout.fillWidth: true
                        compact: drawerColumn.compactMenu
                        text: modelData.title
                        iconText: modelData.icon
                        badgeText: modelData.badge || ""
                        highlighted: window.currentPage === modelData.key
                        onClicked: window.navigate(modelData.key)
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Made with \u2665  VU3LVO")
                    color: t.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                DonateButtons {
                    Layout.fillWidth: true
                    buttonHeight: drawerColumn.compactMenu ? 40 : 46
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    // TOP BAR
    // ─────────────────────────────────────────────────────────
    header: ToolBar {
        id: topBar
        Material.elevation: 0
        topPadding: safeAreaObj.top
        bottomPadding: 0
        leftPadding: safeAreaObj.left
        rightPadding: safeAreaObj.right

        background: Rectangle {
            color: t.surface2
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: t.stroke
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 2

            ToolButton {
                visible: !window.navDocked
                text: "\uf0c9"
                font.family: "FontAwesome"
                font.pixelSize: 18
                Accessible.name: qsTr("Open navigation")
                onClicked: navDrawer.open()
            }

            Item { visible: window.navDocked; Layout.preferredWidth: 14 }

            Label {
                visible: !homeModeSwitch.visible || window.width >= 400
                text: (window.currentPageItem && window.currentPageItem.title) ? window.currentPageItem.title : qsTr("DroidStar")
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                font.pixelSize: 18
                font.bold: true
                color: t.text
                elide: Text.ElideRight
            }

            Item { visible: homeModeSwitch.visible && window.width < 400; Layout.fillWidth: true }

            // Simple / Advanced home: one switch, on = Simple
            Rectangle {
                id: homeModeSwitch
                readonly property bool simple: controlSettings.simpleHome
                visible: window.currentPage === "main"
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 4
                implicitWidth: modeRow.implicitWidth + 20
                implicitHeight: 32
                radius: 16
                color: switchMouse.pressed ? t.surface3 : t.surface
                border.color: t.stroke
                border.width: 1

                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Simple home")
                Accessible.checkable: true
                Accessible.checked: simple
                Accessible.onToggleAction: window.setSimpleHome(!simple)
                Accessible.onPressAction: window.setSimpleHome(!simple)

                Row {
                    id: modeRow
                    anchors.centerIn: parent
                    spacing: 8
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Simple")
                        font.pixelSize: 12
                        font.bold: homeModeSwitch.simple
                        color: homeModeSwitch.simple ? t.text : t.textMuted
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36; height: 20; radius: 10
                        color: homeModeSwitch.simple ? t.accentStrong : t.surface3
                        border.color: homeModeSwitch.simple ? t.accent : t.strokeStrong
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: t.durMed } }
                        Rectangle {
                            x: homeModeSwitch.simple ? parent.width - width - 2 : 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16; height: 16; radius: 8
                            color: "white"
                            Behavior on x { NumberAnimation { duration: t.durMed; easing.type: Easing.OutCubic } }
                        }
                    }
                }
                MouseArea {
                    id: switchMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.setSimpleHome(!homeModeSwitch.simple)
                }
            }

            Loader {
                id: headerActionsLoader
                Layout.alignment: Qt.AlignVCenter
                sourceComponent: window.currentPageItem ? window.currentPageItem.headerActions : null
            }

            // Connection / TX status pill (tap -> Main)
            Rectangle {
                id: statusPill
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 10
                Layout.leftMargin: 4
                readonly property color tone: appState.txActive ? t.danger
                                            : appState.connected ? t.success
                                            : appState.connecting ? t.warning
                                            : t.textMuted
                readonly property bool showLabel: window.width >= 420 || (!headerActionsLoader.item && !homeModeSwitch.visible)
                implicitHeight: 28
                implicitWidth: pillRow.implicitWidth + 18
                radius: 14
                color: t.tint(tone, 0.14)
                border.color: t.tint(tone, 0.5)
                border.width: 1
                Behavior on color { ColorAnimation { duration: t.durMed } }

                Accessible.role: Accessible.Button
                Accessible.name: pillLabel.text

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: 6

                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: statusPill.tone
                        anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity {
                            // Infinite animations keep ticking on a 16 ms timer when the window is hidden; pause in background.
                            running: (appState.connecting || appState.txActive) && Qt.application.state === Qt.ApplicationActive
                            loops: Animation.Infinite
                            alwaysRunToEnd: true
                            NumberAnimation { to: 0.25; duration: 450; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 450; easing.type: Easing.InOutSine }
                        }
                    }
                    Label {
                        visible: appState.connected && !appState.txActive && appState.odtpActive
                        text: "\uf0e7"
                        font.family: "FontAwesome"
                        font.pixelSize: 10
                        color: statusPill.tone
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        id: pillLabel
                        visible: statusPill.showLabel
                        text: appState.txActive ? qsTr("ON AIR")
                              : appState.connected ? (appState.mode !== "" ? appState.mode : qsTr("Online"))
                              : appState.connecting ? qsTr("Connecting")
                              : qsTr("Offline")
                        font.pixelSize: 12
                        font.bold: appState.txActive
                        color: t.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.navigate("main")
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    // PAGES (created on first visit, then kept alive for instant switching)
    // ─────────────────────────────────────────────────────────
    function _componentFor(key) {
        if (key === "main") return mainPageComponent
        if (key === "simple") return simplePageComponent
        if (key === "scan") return scanPageComponent
        if (key === "messages") return messagesPageComponent
        if (key === "settings") return settingsPageComponent
        if (key === "qso") return qsoPageComponent
        if (key === "nets") return netsPageComponent
        if (key === "log") return logPageComponent
        if (key === "hosts") return hostsPageComponent
        if (key === "about") return aboutPageComponent
        return null
    }

    Item {
        id: pageHost
        anchors.fill: parent
        anchors.leftMargin: window.navDocked ? navDrawer.width : safeAreaObj.left
        anchors.rightMargin: safeAreaObj.right
        anchors.bottomMargin: safeAreaObj.bottom

        BusyIndicator {
            anchors.centerIn: parent
            running: !window.initialized
            visible: running
        }

        Repeater {
            id: pageRepeater
            model: window.initialized ? window.pageKeys : []

            delegate: Loader {
                id: pageLoader
                required property string modelData
                readonly property bool isCurrent: window._pageSlot === modelData

                width: pageHost.width
                height: pageHost.height
                active: isCurrent || window._visited.indexOf(modelData) >= 0
                asynchronous: !isCurrent
                visible: isCurrent
                sourceComponent: window._componentFor(modelData)
                // Keep a page alive once built so switching back is instant
                onLoaded: window._markVisited(modelData)

                transform: Translate { id: enterShift }

                ParallelAnimation {
                    id: enterAnim
                    NumberAnimation { target: pageLoader; property: "opacity"; from: 0; to: 1; duration: t.durMed; easing.type: Easing.OutCubic }
                    NumberAnimation { target: enterShift; property: "y"; from: 10; to: 0; duration: 220; easing.type: Easing.OutCubic }
                }

                onIsCurrentChanged: if (isCurrent) enterAnim.restart()
            }
        }
    }

    // Running red light around the whole window while transmitting (every page incl. Simple Home,
    // above sheets). Visual only: no input or layout; animation pauses when backgrounded.
    TxEdgeGlow {
        id: txEdge
        parent: Overlay.overlay
        anchors.fill: parent
        z: 10000
        active: appState.txActive
        animate: Qt.application.state === Qt.ApplicationActive
        // Follow the rounded display corners on iPhone/iPad (home-indicator devices)
        radius: (Qt.platform.os === "ios" && safeAreaObj.bottom > 0) ? (Math.min(window.width, window.height) < 600 ? 44 : 18) : 0
    }

    AuthCheckSheet {
        id: authSheet
        statuses: window.authStatus
        showSettingsButton: window.currentPage !== "settings"
        onRetryRequested: function(nets) { window.verifyDmrAuth(nets, false) }
        onSettingsRequested: window.navigate("settings")
    }

    Toast {
        id: toaster
        anchors.fill: parent
        anchors.leftMargin: window.navDocked ? navDrawer.width : safeAreaObj.left
        anchors.rightMargin: safeAreaObj.right
        // Pages can lift the toast above their own bottom bars (e.g. the Messages composer / keyboard)
        bottomOffset: safeAreaObj.bottom + ((window.currentPageItem && window.currentPageItem.toastBottomInset !== undefined)
                                            ? window.currentPageItem.toastBottomInset : 0)
        z: 100
    }

    // ─────────────────────────────────────────────────────────
    // MODERN ANIMATED SPLASH OVERLAY (Full New Logo + RF Pulse & Spectrum)
    // ─────────────────────────────────────────────────────────
    Rectangle {
        id: splashOverlay
        anchors.fill: parent
        z: 500
        color: t.bg
        visible: opacity > 0.01

        property bool dismissed: false
        property real progressValue: 0.12
        property int activeModeStep: 0

        function dismissSplash() {
            if (splashOverlay.dismissed) return
            splashOverlay.dismissed = true
            splashOverlay.progressValue = 1.0
            splashExitAnim.start()
        }

        NumberAnimation on progressValue {
            id: splashProgressAnim
            from: 0.12
            to: 0.92
            duration: 950
            easing.type: Easing.OutCubic
            running: true
        }

        Timer {
            id: splashStepTimer
            interval: 140
            repeat: true
            running: splashOverlay.visible && !splashOverlay.dismissed
            onTriggered: splashOverlay.activeModeStep = (splashOverlay.activeModeStep + 1) % 6
        }

        Timer {
            id: splashDismissTimer
            interval: 1050
            repeat: true
            running: true
            onTriggered: {
                if (window.initialized) {
                    stop()
                    splashOverlay.dismissSplash()
                }
            }
        }

        ParallelAnimation {
            id: splashExitAnim
            NumberAnimation {
                target: splashOverlay
                property: "opacity"
                to: 0.0
                duration: 320
                easing.type: Easing.InOutCubic
            }
            NumberAnimation {
                target: splashCenterScale
                property: "xScale"
                to: 1.04
                duration: 320
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: splashCenterScale
                property: "yScale"
                to: 1.04
                duration: 320
                easing.type: Easing.OutCubic
            }
        }

        // Ambient radial glow behind the logo
        Rectangle {
            id: ambientGlow
            width: Math.min(parent.width * 0.85, 380)
            height: width
            radius: width / 2
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -24
            color: Qt.rgba(0.14, 0.39, 0.92, 0.11)
            scale: 1.0

            SequentialAnimation on scale {
                running: splashOverlay.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.14; duration: 1100; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.96; duration: 1100; easing.type: Easing.InOutSine }
            }
        }

        // Concentric animated RF signal wave rings
        Repeater {
            model: 3
            delegate: Rectangle {
                id: rfRing
                required property int index
                width: 160 + index * 70
                height: width
                radius: width / 2
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -24
                color: "transparent"
                border.width: 1.2
                border.color: index === 0 ? Qt.rgba(0.20, 0.83, 0.60, 0.28)
                            : (index === 1 ? Qt.rgba(0.22, 0.74, 0.97, 0.22)
                                           : Qt.rgba(0.96, 0.62, 0.04, 0.18))
                scale: 0.85
                opacity: 0.0

                SequentialAnimation {
                    running: splashOverlay.visible
                    loops: Animation.Infinite
                    PauseAnimation { duration: rfRing.index * 280 }
                    ParallelAnimation {
                        NumberAnimation { target: rfRing; property: "scale"; from: 0.78; to: 1.38; duration: 1500; easing.type: Easing.OutCubic }
                        SequentialAnimation {
                            NumberAnimation { target: rfRing; property: "opacity"; from: 0.0; to: 0.75; duration: 350; easing.type: Easing.OutQuad }
                            NumberAnimation { target: rfRing; property: "opacity"; from: 0.75; to: 0.0; duration: 1150; easing.type: Easing.InQuad }
                        }
                    }
                }
            }
        }

        // Center hero stack: Full New Logo + Tagline + RF Equalizer + Mode Badges
        ColumnLayout {
            id: splashHeroCol
            width: Math.min(parent.width - 40, 360)
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -14
            spacing: 16
            transform: Scale {
                id: splashCenterScale
                origin.x: splashHeroCol.width / 2
                origin.y: splashHeroCol.height / 2
                xScale: 1.0
                yScale: 1.0
            }

            ParallelAnimation {
                running: true
                NumberAnimation { target: splashCenterScale; property: "xScale"; from: 0.92; to: 1.0; duration: 480; easing.type: Easing.OutBack }
                NumberAnimation { target: splashCenterScale; property: "yScale"; from: 0.92; to: 1.0; duration: 480; easing.type: Easing.OutBack }
            }

            // Full Horizontal Logo (Droidstar DMR by VU3LVO) with shimmer sweep
            Item {
                id: logoContainer
                readonly property real logoW: Math.min(splashHeroCol.width, 320)
                readonly property real logoH: Math.round(logoW * 341 / 1024)
                Layout.preferredWidth: logoW
                Layout.preferredHeight: logoH
                Layout.alignment: Qt.AlignHCenter
                clip: true

                Image {
                    id: splashFullLogo
                    anchors.fill: parent
                    source: "qrc:/DroidStar/images/droidstar_logo.png"
                    sourceSize.width: 1024
                    sourceSize.height: 341
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    onStatusChanged: {
                        if (status === Image.Error) source = Qt.resolvedUrl("../images/droidstar_logo.png")
                    }
                }

                // Subtle golden starlight shimmer sweep across the logo
                Rectangle {
                    id: logoShimmer
                    width: 56
                    height: parent.height * 1.8
                    y: -parent.height * 0.4
                    x: -80
                    rotation: 22
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: Qt.rgba(1.0, 0.84, 0.35, 0.22) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                    NumberAnimation on x {
                        running: splashOverlay.visible
                        loops: Animation.Infinite
                        from: -80
                        to: logoContainer.width + 100
                        duration: 1650
                        easing.type: Easing.InOutQuad
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("DIGITAL VOICE TRANSCEIVER")
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.8
                color: t.accent
                opacity: 0.88
            }

            // Engaging 9-bar animated RF audio spectrum wave
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 5
                height: 24

                Repeater {
                    model: [
                        { minH: 6,  maxH: 18, dur: 340, col: "#34D399" },
                        { minH: 8,  maxH: 22, dur: 290, col: "#34D399" },
                        { minH: 10, maxH: 24, dur: 380, col: "#38BDF8" },
                        { minH: 7,  maxH: 20, dur: 310, col: "#38BDF8" },
                        { minH: 12, maxH: 24, dur: 260, col: "#60A5FA" },
                        { minH: 7,  maxH: 20, dur: 330, col: "#38BDF8" },
                        { minH: 10, maxH: 23, dur: 360, col: "#2563EB" },
                        { minH: 8,  maxH: 19, dur: 300, col: "#F59E0B" },
                        { minH: 6,  maxH: 16, dur: 350, col: "#FBBF24" }
                    ]
                    delegate: Rectangle {
                        id: waveBar
                        required property var modelData
                        width: 4
                        height: modelData.minH
                        radius: 2
                        color: modelData.col
                        anchors.verticalCenter: parent.verticalCenter

                        SequentialAnimation on height {
                            running: splashOverlay.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: waveBar.modelData.maxH; duration: waveBar.modelData.dur; easing.type: Easing.InOutSine }
                            NumberAnimation { to: waveBar.modelData.minH; duration: waveBar.modelData.dur; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }

            // Sequential Digital Mode Pills (DMR • YSF • D-STAR • P25 • NXDN • M17)
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6

                Repeater {
                    model: ["DMR", "YSF", "D-STAR", "P25", "NXDN", "M17"]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        readonly property bool activePill: splashOverlay.activeModeStep === index
                        radius: 6
                        implicitHeight: 20
                        implicitWidth: modePillTxt.implicitWidth + 10
                        color: activePill ? t.tint(t.accent, 0.24) : t.surface2
                        border.color: activePill ? t.accent : t.stroke
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 130 } }
                        Behavior on border.color { ColorAnimation { duration: 130 } }

                        Label {
                            id: modePillTxt
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 10
                            font.bold: true
                            color: activePill ? t.text : t.textMuted
                        }
                    }
                }
            }
        }

        // Bottom progress bar + status caption
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Math.max(32, (parent.width - 260) / 2)
            anchors.rightMargin: Math.max(32, (parent.width - 260) / 2)
            anchors.bottomMargin: Math.max(28, safeAreaObj.bottom + 18)
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: t.surface3
                clip: true

                Rectangle {
                    width: parent.width * Math.max(0.08, Math.min(1.0, splashOverlay.progressValue))
                    height: parent.height
                    radius: 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#34D399" }
                        GradientStop { position: 0.55; color: "#38BDF8" }
                        GradientStop { position: 1.0; color: "#F59E0B" }
                    }
                    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: window.initialized ? qsTr("Ready • 73 de VU3LVO") : qsTr("Initializing Digital Voice Engine…")
                font.pixelSize: t.fsCaption
                color: t.textMuted
                opacity: 0.85
            }
        }

        // Allow tap-to-skip once backend is initialized
        MouseArea {
            anchors.fill: parent
            onClicked: if (window.initialized) splashOverlay.dismissSplash()
        }
    }

    // Wait for backend, then warm the most-used secondary pages in the background.
    Timer {
        id: initTimer
        interval: 50
        repeat: true
        running: true
        onTriggered: {
            if (!window.initialized) {
                if (window.droidstar) window.initializeFromBackend()
                return
            }
            window.refreshSafeArea()
            stop()
            preloadTimer.start()
        }
    }

    Timer {
        id: preloadTimer
        interval: 600
        onTriggered: {
            // UIKit may report zero insets until the first layout pass completes.
            window.refreshSafeArea()
            // Only Settings is warmed: QSO and Nets build their models on first visit instead of
            // competing with the first interactive seconds on older devices.
            window._markVisited("settings")
            // Warm the other Home layout so the Simple/Advanced switch is instant
            window._markVisited(controlSettings.simpleHome ? "main" : "simple")
        }
    }

    // Page components
    Component {
        id: mainPageComponent
        MainPage {
            objectName: "main"
            app: window
            droidstarRef: window.droidstar
            appState: window.appStateObj
            vuidUpdaterRef: window.vuidUpdaterRef
            logHandlerRef: window.logHandlerRef
        }
    }
    Component {
        id: simplePageComponent
        SimplePage {
            objectName: "simple"
            app: window
            droidstarRef: window.droidstar
            appState: window.appStateObj
        }
    }
    Component {
        id: scanPageComponent
        ScanPage {
            objectName: "scan"
            app: window
            droidstarRef: window.droidstar
            appState: window.appStateObj
        }
    }
    Component {
        id: messagesPageComponent
        MessagesPage {
            objectName: "messages"
            app: window
            droidstarRef: window.droidstar
            appState: window.appStateObj
            smsManagerRef: window.smsManagerRef
            initialThreadKey: window.pendingMessageThread
        }
    }
    Component {
        id: settingsPageComponent
        SettingsPage {
            objectName: "settings"
            app: window
            droidstarRef: window.droidstar
            appState: window.appStateObj
            smsManagerRef: window.smsManagerRef
            pttButtonsRef: window.pttButtonsRef
        }
    }
    Component {
        id: qsoPageComponent
        QsoPage {
            objectName: "qso"
            app: window
            logHandlerRef: window.logHandlerRef
            vuidUpdaterRef: window.vuidUpdaterRef
            appState: window.appStateObj
            droidstarRef: window.droidstar
        }
    }
    Component {
        id: netsPageComponent
        NetsPage {
            objectName: "nets"
            app: window
            netReminderManagerRef: window.netReminderManagerRef
            appState: window.appStateObj
            droidstarRef: window.droidstar
        }
    }
    Component {
        id: logPageComponent
        LogPage {
            objectName: "log"
            logModel: appLogModel
            monoFont: appState.monoFont
            onClearRequested: appLogModel.clear()
        }
    }
    Component {
        id: hostsPageComponent
        HostsPage {
            objectName: "hosts"
            app: window
            droidstarRef: window.droidstar
            hostsText: window.droidstar ? window.droidstar.get_local_hosts() : ""
        }
    }
    Component {
        id: aboutPageComponent
        AboutPage {
            objectName: "about"
            droidstarRef: window.droidstar
        }
    }
}
