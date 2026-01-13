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
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.settings

import org.dudetronics.droidstar

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

    Tokens { id: t }

    // Persisted UI settings (independent from backend/QSettings)
    Settings {
        id: uiSettings
        category: "qso"
        property bool disableSelfLog: true  // Default to disabled (exclude self logging)
        property int qsoLogLimit: 250
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
    // Note: main.cpp exposes it as "droidStar" (lowercase 's')
    // We create a local alias for consistency with legacy code naming
    property var droidstar: (typeof droidStar !== "undefined") ? droidStar : null

    // Context properties exposed from C++ (existing backend)
    property var vuidUpdaterRef: (typeof vuidUpdater !== "undefined") ? vuidUpdater : null
    property var logHandlerRef: (typeof logHandler !== "undefined") ? logHandler : null
    property var liveActivityRef: (typeof liveActivity !== "undefined") ? liveActivity : null

    // Realtime QSO logging (legacy parity) - driven by VUIDUpdater replies
    property int _pendingLogDmr: 0
    property int _pendingLogTgid: 0
    property string _pendingLogKey: ""
    property string _lastLoggedKey: ""

    // Flag to track when initialization is complete
    property bool initialized: false
    property bool initializing: false

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
        property bool swtx: false
        property bool swrx: false
        property bool agc: true
        property real micGain: 0.5
        property int outputLevel: 0
        property bool connected: false
        // Legacy-style connection state (0=disconnected, 1=connecting, 2=connected, 5=error)
        property int connectStatus: 0
        property bool connecting: false
        property bool txEnabled: false
        property bool txActive: false

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

        // Recent TGIDs
        property var recentTgids: []
        property string monoFont: ""

        property var hostsModel: []

        // Settings fields
        property string callsign: ""
        property string dmrid: ""
        property string essid: "None"
        property string bmPass: ""
        property string tgifPass: ""
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
        property string modemM17CAN: ""

        // UI-only
        property string lastHeard1: ""
        property string lastHeard2: ""
        property string fetchedFirstName: ""
        property string fetchedCountry: ""

        // QSO log settings (UI-controlled, persisted via Qt.labs.settings)
        property bool disableSelfLog: true  // Default to disabled (exclude self logging)
        property int qsoLogLimit: 250

        property bool ipv6: false
        property bool xrf2ref: false
        // IMPORTANT: Default to enabled on fresh installs.
        property bool toggleTx: true
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
        while (appLogModel.count > 1200) appLogModel.remove(0, appLogModel.count - 1200)
    }

    // ─────────────────────────────────────────────────────────
    // GLOBAL QSO LOGGER (works even if QSO page never opened)
    // Watches live RX fields (data2/data3) and appends to logs.json
    // ─────────────────────────────────────────────────────────
    property var qsoXhr: null
    // Separate "seen" vs "logged" so transient failures don't permanently suppress logging.
    property string _lastQsoSeenKey: ""
    property string _lastQsoLoggedKey: ""
    property bool _qsoRequestInFlight: false
    property int _qsoRequestDmr: 0
    property int _qsoRequestTgid: 0
    property int _qsoPendingDmr: 0
    property int _qsoPendingTgid: 0

    // Legacy MainTab parity: keep "Last Heard" updated in realtime (not by polling/reloading pages).
    function _fmtLastHeard(entry) {
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

    function _setLastHeardFromArray(arr) {
        if (!window.appState) return
        window.appState.lastHeard1 = (arr && arr.length > 0) ? _fmtLastHeard(arr[0]) : ""
        window.appState.lastHeard2 = (arr && arr.length > 1) ? _fmtLastHeard(arr[1]) : ""
    }

    function _normCallsign(s) {
        if (s === undefined || s === null) return ""
        return ("" + s).trim().toUpperCase()
    }

    function _selfDmrId() {
        if (!window.appState) return 0
        var n = parseInt(window.appState.dmrid || "0")
        return (isNaN(n) || n < 0) ? 0 : n
    }

    function _isSelfContact(dmrId, callsign) {
        // Treat as "self" if EITHER DMR ID OR callsign matches configured identity.
        var myDmr = _selfDmrId()
        var myCs = _normCallsign(window.appState ? window.appState.callsign : "")
        
        // Check DMR ID match
        var d = parseInt(dmrId || "0")
        var dmrMatches = (myDmr > 0 && d > 0 && d === myDmr)
        
        // Check callsign match
        var incomingCs = _normCallsign(callsign)
        var csMatches = (myCs !== "" && incomingCs !== "" && incomingCs === myCs)
        
        // Return true if EITHER matches
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
        if (!window.appState) return 250
        return _clampQsoLogLimit(window.appState.qsoLogLimit)
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

    function _ensureQsoXhr() {
        if (qsoXhr) return
        qsoXhr = new XMLHttpRequest()
        qsoXhr.timeout = 5000
        qsoXhr.onreadystatechange = function() {
            if (qsoXhr.readyState !== XMLHttpRequest.DONE) return
            window._qsoRequestInFlight = false
            if (qsoXhr.status !== 200) {
                // Allow retries on next RX cycle
                window._lastQsoSeenKey = ""
                window._drainPendingQsoRequest()
                return
            }

            var response
            try { response = JSON.parse(qsoXhr.responseText) } catch (e) {
                window._lastQsoSeenKey = ""
                window._drainPendingQsoRequest()
                return
            }
            if (!response || !response.results || response.results.length === 0) {
                window._lastQsoSeenKey = ""
                window._drainPendingQsoRequest()
                return
            }

            var result = response.results[0]
            var dmrID = result.radio_id || result.id || 0
            if (!dmrID) {
                window._lastQsoSeenKey = ""
                window._drainPendingQsoRequest()
                return
            }

            // Use the TGID that was current when we kicked off the request (stable)
            var tgid = window._qsoRequestTgid || 0
            var loggedKey = "" + dmrID + ":" + tgid

            // Load current log, prepend, keep bounded, save
            if (!window.logHandlerRef) return
            var current = window.logHandlerRef.loadLog("logs.json")
            if (!current || current.length === undefined) current = []

            // Compute next serialNumber (bounded list, so cheap scan)
            var nextSerial = 0
            for (var i = 0; i < current.length; i++) {
                var sn = current[i].serialNumber
                if (sn !== undefined && sn !== null) nextSerial = Math.max(nextSerial, sn + 1)
            }

            // Avoid duplicate insert if top entry already same (dmrID,tgid)
            if (current.length > 0) {
                var top = current[0]
                if ((top.dmrID || 0) === dmrID && (top.tgid || 0) === tgid) return
            }

            // Optional filter: don't log self
            // When disableSelfLog is true, exclude self contacts from logging
            if (window.appState && window.appState.disableSelfLog && window._isSelfContact(dmrID, result.callsign || "")) {
                window._drainPendingQsoRequest()
                return  // Skip logging self contact
            }

            current.unshift({
                serialNumber: nextSerial,
                callsign: result.callsign || "",
                dmrID: dmrID,
                tgid: tgid,
                country: result.country || "",
                fname: result.name || result.fname || "",
                currentTime: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss"),
                checked: false
            })

            var lim = window._effectiveQsoLogLimit()
            while (current.length > lim) current.pop()
            // Update MainPage "Last Heard" immediately (legacy-style realtime)
            window._setLastHeardFromArray(current)
            window._lastQsoLoggedKey = loggedKey
            window.logHandlerRef.saveLogAsync("logs.json", current)
            window._drainPendingQsoRequest()
        }
    }

    function _startQsoRequest(dmr, tgid) {
        if (!dmr || !tgid) return
        _ensureQsoXhr()
        window._qsoRequestInFlight = true
        window._qsoRequestDmr = dmr
        window._qsoRequestTgid = tgid
        qsoXhr.open("GET", "https://radioid.net/api/users?id=" + dmr, true)
        qsoXhr.setRequestHeader("Connection", "keep-alive")
        qsoXhr.send()
    }

    function _drainPendingQsoRequest() {
        if (window._qsoRequestInFlight) return
        if (window._qsoPendingDmr && window._qsoPendingTgid) {
            var d = window._qsoPendingDmr
            var t = window._qsoPendingTgid
            window._qsoPendingDmr = 0
            window._qsoPendingTgid = 0
            _startQsoRequest(d, t)
        }
    }

    function maybeLogQsoFromLive() {
        if (!window.appState || !window.logHandlerRef) return
        var dmr = parseInt(window.appState.data2 || "0")
        var tgid = parseInt(window.appState.data3 || "0")
        if (!dmr || !tgid) return

        var key = "" + dmr + ":" + tgid
        // If we already logged this combination, skip.
        if (key === window._lastQsoLoggedKey) return
        // If we've already *seen* it (but request is in flight), don't spam.
        if (key === window._lastQsoSeenKey && window._qsoRequestInFlight) return

        window._lastQsoSeenKey = key
        if (window._qsoRequestInFlight) {
            // Queue latest; we'll log it when current request completes.
            window._qsoPendingDmr = dmr
            window._qsoPendingTgid = tgid
            return
        }
        _startQsoRequest(dmr, tgid)
    }

    // Helper functions for settings sync (defined at window level so they can be called from anywhere)
    function syncSettingsFromBackend() {
        if (!droidstar) return
        appState.callsign = droidstar.get_callsign()
        appState.dmrid = droidstar.get_dmrid()
        appState.essid = droidstar.get_essid()
        appState.bmPass = droidstar.get_bm_password()
        appState.tgifPass = droidstar.get_tgif_password()
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
        
        // Debug: log raw values from backend
        console.log("Settings synced from backend:")
        console.log("  - callsign:", droidstar.get_callsign())
        console.log("  - dmrid:", droidstar.get_dmrid())
        console.log("  - latitude:", droidstar.get_latitude())
        console.log("  - mycall:", droidstar.get_mycall())
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

    function initializeFromBackend() {
        if (window.initialized || window.initializing) return
        if (!droidstar) return

        window.initializing = true
        try {
            console.log("Initializing from backend...")
            
            // Clean up any orphan Live Activities from previous app runs
            if (window.liveActivityRef && window.liveActivityRef.endAll) {
                window.liveActivityRef.endAll()
            }

            // Load all settings from QSettings (also calls process_mode_change -> emits mode_changed)
            droidstar.process_settings()

            // Get the current mode (already set by process_settings -> process_mode_change)
            var currentMode = droidstar.get_mode()
            // Force mode update - clear first to ensure change detection
            appState.mode = ""
            appState.mode = currentMode
            appState.module = droidstar.get_module()

            // Load hosts for current mode
            droidstar.set_modelchange(true)
            var hostsList = droidstar.get_hosts()
            var hostsArray = []
            if (hostsList && hostsList.length !== undefined) {
                for (var i = 0; i < hostsList.length; i++) {
                    hostsArray.push(String(hostsList[i]))
                }
            }
            appState.hostsModel = []
            appState.hostsModel = hostsArray
            droidstar.set_modelchange(false)

            // Pick selected host for this mode
            var savedHost = hostForMode(currentMode)
            if (!savedHost || savedHost === "" || (appState.hostsModel && appState.hostsModel.indexOf(savedHost) < 0)) {
                appState.selectedHost = (appState.hostsModel && appState.hostsModel.length > 0) ? appState.hostsModel[0] : ""
            } else {
                appState.selectedHost = savedHost
            }

            // Sync all other settings from QSettings
            syncSettingsFromBackend()

            // Load persisted UI settings (QSO log)
            appState.disableSelfLog = !!uiSettings.disableSelfLog
            appState.qsoLogLimit = window._clampQsoLogLimit(uiSettings.qsoLogLimit)
            uiSettings.qsoLogLimit = appState.qsoLogLimit
            window._trimAndPersistQsoLogIfNeeded()

            // Populate "Last Heard" on app load (legacy QsoTab existed at startup)
            syncLastHeardFromLogFile()
            // Ensure logs.json exists (prevents first-run "file not found" and enables realtime updates)
            if (window.logHandlerRef) {
                var existingLog = window.logHandlerRef.loadLog("logs.json")
                window.logHandlerRef.saveLogAsync("logs.json", existingLog)
            }

            appState.monoFont = droidstar.get_monofont()
            appState.recentTgids = droidstar.loadRecentTGIDs()

            // Load additional device lists for settings page
            appState.vocodersModel = droidstar.get_vocoders()
            appState.modemsModel = droidstar.get_modems()
            appState.playbacksModel = droidstar.get_playbacks()
            appState.capturesModel = droidstar.get_captures()

            // Initialize device selections to defaults (these are set when connecting, not persisted)
            appState.vocoder = appState.vocodersModel && appState.vocodersModel.length > 0 ? appState.vocodersModel[0] : "Software vocoder"
            appState.modem = appState.modemsModel && appState.modemsModel.length > 0 ? appState.modemsModel[0] : "None"
            appState.playback = appState.playbacksModel && appState.playbacksModel.length > 0 ? appState.playbacksModel[0] : "OS Default"
            appState.capture = appState.capturesModel && appState.capturesModel.length > 0 ? appState.capturesModel[0] : "OS Default"

            window.initialized = true
        } catch (e) {
            console.log("Initialization failed:", e)
        } finally {
            window.initializing = false
        }
    }

    // If QSO log is saved/cleared from anywhere, keep Last Heard in sync (no navigation required).
    Connections {
        target: window.logHandlerRef
        enabled: window.logHandlerRef !== null
        function onLogSaved(fileName) {
            if (fileName === "logs.json") window.syncLastHeardFromLogFile()
        }
        function onLogCleared(fileName) {
            if (fileName === "logs.json") {
                window.appState.lastHeard1 = ""
                window.appState.lastHeard2 = ""
            }
        }
    }

    // Wire backend -> UI state
    Connections {
        target: droidstar
        enabled: droidstar !== null

        Component.onCompleted: {
            initializeFromBackend()
        }

        function onMode_changed() {
            if (!droidstar) return
            appState.mode = droidstar.get_mode()
            appState.module = droidstar.get_module()
            
            // Load hosts for the new mode (legacy pattern)
            // Convert QStringList to JavaScript array for QML binding
            droidstar.set_modelchange(true)
            var hostsList = droidstar.get_hosts()
            var hostsArray = []
            if (hostsList && hostsList.length !== undefined) {
                for (var i = 0; i < hostsList.length; i++) {
                    hostsArray.push(String(hostsList[i])) // Ensure string conversion
                }
            }
            // Force QML to detect the change by clearing first, then assigning
            appState.hostsModel = []
            appState.hostsModel = hostsArray
            droidstar.set_modelchange(false)
            console.log("Mode changed - hosts loaded, count:", appState.hostsModel ? appState.hostsModel.length : 0)
            
            // Update selected host for this mode
            var savedHost = window.hostForMode(appState.mode)
            if (savedHost && savedHost !== "" && appState.hostsModel && appState.hostsModel.indexOf(savedHost) >= 0) {
                appState.selectedHost = savedHost
            } else if (appState.hostsModel && appState.hostsModel.length > 0) {
                appState.selectedHost = appState.hostsModel[0]
            } else {
                appState.selectedHost = ""
            }
        }

        function onUpdate_data() {
            if (!droidstar) return
            var oldData2 = appState.data2
            var oldData3 = appState.data3
            var oldKey = appState._lastQsoKey
            
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

            appState.label1 = droidstar.get_label1()
            appState.label2 = droidstar.get_label2()
            appState.label3 = droidstar.get_label3()
            appState.label4 = droidstar.get_label4()
            appState.label5 = droidstar.get_label5()
            appState.label6 = droidstar.get_label6()
            
            // Debug: log when data2/data3 change
            if (oldData2 !== appState.data2 || oldData3 !== appState.data3) {
                console.log("App2026: data2/data3 changed - data2:", appState.data2, "data3:", appState.data3)
            }

            // Restore "highlight name/country" banner in MainPage:
            // Ensure we trigger the radioid.net lookup even if QSO page is never opened.
            var dmr = parseInt(appState.data2 || "0")
            var tgid = parseInt(appState.data3 || "0")
            var key = "" + dmr + ":" + tgid
            if (dmr > 0 && tgid > 0 && key !== oldKey) {
                appState._lastQsoKey = key
                window._pendingLogDmr = dmr
                window._pendingLogTgid = tgid
                window._pendingLogKey = key
                if (window.vuidUpdaterRef) window.vuidUpdaterRef.fetchFirstNameFromAPI(dmr)
            }
            // Requested behavior: when RX goes idle (DMR EOT clears data2), hide the TX Control pill.
            // Clear ONLY the fetched display values (and reset the key so the next RX re-fetches).
            if (dmr === 0) {
                appState.fetchedFirstName = ""
                appState.fetchedCountry = ""
                appState._lastQsoKey = ""
            }

            // Note: QSO logging is now driven by VUIDUpdater.userLookupReady (more reliable on iOS)
        }

        function onUpdate_log(s) { appendLog(s) }

        function onUpdate_settings() {
            window.syncSettingsFromBackend()
            appState.selectedHost = window.hostForMode(appState.mode)
            if (droidstar) appState.recentTgids = droidstar.loadRecentTGIDs()
        }

        function onUpdate_devices() {
            if (!droidstar) return
            // Keep host list in a plain JS array for fast ComboBox/ListView usage
            var hostsList = droidstar.get_hosts()
            var hostsArray = []
            if (hostsList && hostsList.length !== undefined) {
                for (var i = 0; i < hostsList.length; i++) hostsArray.push(String(hostsList[i]))
            }
            appState.hostsModel = []
            appState.hostsModel = hostsArray
        }

        function onConnect_status_changed(c) {
            // Keep raw status for UI parity/debugging
            appState.connectStatus = c
            appState.connecting = (c === 1)

            appState.connected = (c === 2)
            appState.txEnabled = (c === 2)
            if (c === 0) {
                appState.txActive = false
                droidstar.set_output_level(0)
                // Clear live lookup when disconnected
                appState.fetchedFirstName = ""
                appState.fetchedCountry = ""
                appState._lastQsoKey = ""
                window._pendingLogDmr = 0
                window._pendingLogTgid = 0
                window._pendingLogKey = ""
                window._lastLoggedKey = ""
            }
            if (c !== 2) {
                // Never show TX as active unless fully connected
                appState.txActive = false
            }
            if (c === 2) {
                // Legacy parity: backend may set MYCALL/URCALL/RPTR1/RPTR2 on connect.
                // Pull them into appState so Settings page reflects it immediately.
                window.syncSettingsFromBackend()
            }

            if (c === 5 && errorDialog) {
                errorDialog.text = droidstar.get_error_text()
                if (errorDialog.text === "") errorDialog.text = "Banned!"
                errorDialog.open()
            }
        }

        function onSwtx_state(s) { appState.swtx = !!s }
        function onSwrx_state(s) { appState.swrx = !!s }
        function onAgc_state(s) { appState.agc = !!s }

        // Legacy parity: backend can update these fields during connect/runtime.
        function onMycall_changed(s) { appState.mycall = String(s) }
        function onUrcall_changed(s) { appState.urcall = String(s) }
        function onRptr1_changed(s) { appState.rptr1 = String(s) }
        function onRptr2_changed(s) { appState.rptr2 = String(s) }
        function onUsrtxt_changed(s) { appState.usrtxt = String(s) }
    }

    // App lifecycle - use correct syntax
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationClosing) {
                droidstar.reset_connect_status()
            }
        }
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
            // Update iOS Now Playing / Lock Screen with latest RX info
            if (window.droidstar && appState.data1) {
                window.droidstar.updateNowPlayingRX(appState.data1, appState.fetchedFirstName, appState.fetchedCountry)
            }
        }
        function onUserLookupReady(dmrId, callsign, name, country) {
            // Only log if this matches the last requested RX key
            if (!window.logHandlerRef) return
            if (!window._pendingLogDmr || window._pendingLogDmr !== dmrId) return
            if (!window._pendingLogTgid) return

            var key = "" + dmrId + ":" + window._pendingLogTgid
            if (key === window._lastLoggedKey) return

            // Optional filter: don't log self (requires both DMR ID + Callsign match settings)
            if (window.appState && window.appState.disableSelfLog && window._isSelfContact(dmrId, callsign || "")) {
                return
            }

            var current = window.logHandlerRef.loadLog("logs.json")
            if (!current || current.length === undefined) current = []

            if (current.length > 0) {
                var top = current[0]
                if ((top.dmrID || 0) === dmrId && (top.tgid || 0) === window._pendingLogTgid) {
                    window._lastLoggedKey = key
                    return
                }
            }

            // Compute next serialNumber
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
                checked: false
            })

            var lim = window._effectiveQsoLogLimit()
            while (current.length > lim) current.pop()
            window._setLastHeardFromArray(current)
            window._lastLoggedKey = key
            window.logHandlerRef.saveLogAsync("logs.json", current)
        }
    }

    // Persist UI settings when changed + enforce log trimming immediately.
    Connections {
        target: appState
        enabled: !!appState
        function onDisableSelfLogChanged() {
            uiSettings.disableSelfLog = !!appState.disableSelfLog
        }
        function onQsoLogLimitChanged() {
            var clamped = window._clampQsoLogLimit(appState.qsoLogLimit)
            if (clamped !== appState.qsoLogLimit) appState.qsoLogLimit = clamped
            uiSettings.qsoLogLimit = clamped
            window._trimAndPersistQsoLogIfNeeded()
        }
    }

    function updateDynamicIsland() {
        if (!window.liveActivityRef || !window.liveActivityRef.available) return
        if (!appState || !appState.connected) {
            window.liveActivityRef.end()
            return
        }

        var mode = appState.txActive ? "TX" : "RX"
        
        // Only show RX callsign from data1, NOT user's own callsign
        var rxCallsign = (appState.data1 && appState.data1 !== "") ? appState.data1.trim() : ""
        
        var callsign = ""
        var handle = ""
        var country = ""
        var tgid = appState.data3 || ""
        
        if (appState.txActive) {
            // TX mode: show user's own callsign
            callsign = appState.callsign || ""
            handle = "Transmitting"
            country = ""
        } else if (rxCallsign) {
            // Active RX: show caller's info
            callsign = rxCallsign
            handle = appState.fetchedFirstName || ""
            country = appState.fetchedCountry || ""
        } else {
            // Idle (connected but no RX) - show waiting state to keep activity alive
            callsign = "Awaiting"
            handle = "RX..."
            country = ""
            tgid = appState.data3 || ""
        }

        window.liveActivityRef.startOrUpdate(mode, callsign, handle, country, tgid)
    }

    // Update live activity from app state changes
    Connections {
        target: appState
        enabled: !!appState
        function onConnectedChanged() { window.updateDynamicIsland() }
        function onTxActiveChanged() { window.updateDynamicIsland() }
        function onData1Changed() { window.updateDynamicIsland() }
        function onData3Changed() { window.updateDynamicIsland() }
        function onFetchedFirstNameChanged() { window.updateDynamicIsland() }
        function onFetchedCountryChanged() { window.updateDynamicIsland() }
    }

    // Side drawer navigation
    Drawer {
        id: navDrawer
        width: Math.min(window.width * 0.82, 320)
        height: window.height
        edge: Qt.LeftEdge
        modal: true

        background: Rectangle { color: t.surface2 }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 16
            // iOS notch-safe top padding (keep responsive across devices)
            anchors.topMargin: 16 + (Qt.platform.os === "ios" ? 47 : 0)
            spacing: 12

            AppCard {
                Layout.fillWidth: true
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
                            color: Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.16)
                            border.color: Qt.rgba(t.accent.r, t.accent.g, t.accent.b, 0.35)
                            border.width: 1
                        }

                        Image {
                            id: drawerLogo
                            anchors.fill: parent
                            anchors.margins: 6
                            // From QRC (see `DroidStar.pro` resources.files)
                            source: "qrc:/DroidStar/images/droidstar.png"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                            onStatusChanged: {
                                // Dev/build fallback: if QRC is missing, try relative file (keeps UI usable).
                                if (status === Image.Error) {
                                    source = Qt.resolvedUrl("../../images/droidstar.png")
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: drawerLogo.status === Image.Error
                            text: "DS"
                            font.bold: true
                            font.pixelSize: 14
                            color: t.text
                            opacity: 0.9
                        }
                    }
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("DroidStar"); font.pixelSize: 20; font.bold: true }
                        Label { text: appState.netstatus; opacity: 0.8; wrapMode: Text.WordWrap }
                    }
                }
            }

            ListView {
                id: navList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                model: [
                    { key: "main",  title: qsTr("Main"),    icon: "\uf015" },
                    { key: "settings", title: qsTr("Settings"), icon: "\uf013" },
                    { key: "qso",   title: qsTr("QSO"),     icon: "\uf0ac" },
                    { key: "log",   title: qsTr("Log"),     icon: "\uf15c" },
                    { key: "hosts", title: qsTr("Hosts"),   icon: "\uf0c0" },
                    { key: "about", title: qsTr("About"),   icon: "\uf128" }
                ]

                delegate: DrawerItem {
                    text: modelData.title
                    iconText: modelData.icon
                    iconFont: fa.name
                    highlighted: (stack.currentItem && stack.currentItem.objectName === modelData.key)
                    onClicked: {
                        navDrawer.close()
                        window.navigate(modelData.key)
                    }
                }
            }

            // Sidebar footer (responsive)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Made with \u2665  VU3LVO")
                    opacity: 0.75
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                // Buy Me a Coffee (same styling as About page)
                Button {
                    Layout.fillWidth: true
                    height: 48
                    text: qsTr("☕ Buy Me a Coffee")
                    font.bold: true
                    font.pixelSize: 14

                    background: Rectangle {
                        radius: 14
                        color: "#FF813F"
                        border.color: "#FFA366"
                        border.width: 2

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#FFA366" }
                                GradientStop { position: 1.0; color: "#FF813F" }
                            }
                        }
                    }

                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: Qt.openUrlExternally("https://buymeacoffee.com/rohithz")
                }

                // PayPal (same styling as About page)
                Button {
                    Layout.fillWidth: true
                    height: 48
                    text: qsTr("💳 PayPal Donation")
                    font.bold: true
                    font.pixelSize: 14

                    background: Rectangle {
                        radius: 14
                        color: "#0070BA"
                        border.color: "#009CDE"
                        border.width: 2

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#009CDE" }
                                GradientStop { position: 1.0; color: "#0070BA" }
                            }
                        }
                    }

                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: Qt.openUrlExternally("https://www.paypal.com/ncp/payment/NU89529268M2W")
                }
            }
        }
    }

    // Top bar
    header: ToolBar {
        id: topBar
        background: Rectangle {
            color: t.surface2
            border.color: t.stroke
            border.width: 1
        }
        
        // Restore safe area padding for iOS notch
        topPadding: Qt.platform.os === "ios" ? 47 : 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0

        RowLayout {
            anchors.fill: parent
            spacing: 8

            ToolButton {
                text: "\uf0c9"
                font.family: fa.name
                onClicked: navDrawer.open()
            }

            Label {
                text: (stack.currentItem && stack.currentItem.title) ? stack.currentItem.title : qsTr("DroidStar")
                Layout.fillWidth: true
                font.pixelSize: 18
                elide: Text.ElideRight
            }

            Rectangle {
                width: 10; height: 10; radius: 5
                color: appState.connected ? t.success : t.danger
                Behavior on color { ColorAnimation { duration: 180 } }
            }
        }
    }

    StackView {
        id: stack
        anchors.fill: parent
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        anchors.bottomMargin: 0
        
        // Ensure content starts right below the topBar (no extra gap)
        clip: true

        pushEnter: Transition {
            NumberAnimation { property: "x"; from: stack.width * 0.12; to: 0; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
        }
        pushExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0.9; duration: 140 }
        }
        popEnter: Transition {
            NumberAnimation { property: "x"; from: -stack.width * 0.08; to: 0; duration: 200; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0.85; to: 1; duration: 180 }
        }
        popExit: Transition {
            NumberAnimation { property: "x"; from: 0; to: stack.width * 0.12; duration: 200; easing.type: Easing.InCubic }
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 170 }
        }
    }

    function navigate(key) {
        var component = null
        if (key === "main") component = mainPageComponent
        else if (key === "settings") component = settingsPageComponent
        else if (key === "qso") component = qsoPageComponent
        else if (key === "log") component = logPageComponent
        else if (key === "hosts") component = hostsPageComponent
        else if (key === "about") component = aboutPageComponent
        if (!component) return

        stack.clear()
        stack.push(component)
    }

    Timer {
        id: initTimer
        interval: 50
        repeat: true
        running: true
        onTriggered: {
            if (!window.initialized) {
                if (window.droidstar) initializeFromBackend()
                return
            }
            // Initialized: navigate once, then stop the timer
            navigate("main")
            stop()
        }
    }

    // Page components - use appState instead of state
    Component {
        id: mainPageComponent
        MainPage {
            objectName: "main"
            droidstarRef: droidstar
            appState: window.appStateObj
            vuidUpdaterRef: window.vuidUpdaterRef
            logHandlerRef: window.logHandlerRef
        }
    }
    Component {
        id: settingsPageComponent
        SettingsPage {
            objectName: "settings"
            droidstarRef: droidstar
            appState: window.appStateObj
        }
    }
    Component {
        id: qsoPageComponent
        QsoPage {
            objectName: "qso"
            logHandlerRef: window.logHandlerRef
            vuidUpdaterRef: window.vuidUpdaterRef
            appState: window.appStateObj
            droidstarRef: window.droidstar
        }
    }
    Component {
        id: logPageComponent
        LogPage {
            objectName: "log"
            logModel: appLogModel
            onClearRequested: appLogModel.clear()
        }
    }
    Component {
        id: hostsPageComponent
        HostsPage {
            objectName: "hosts"
            droidstarRef: droidstar
            hostsText: droidstar ? droidstar.get_local_hosts() : ""
        }
    }
    Component {
        id: aboutPageComponent
        AboutPage {
            objectName: "about"
            droidstarRef: droidstar
        }
    }
}
