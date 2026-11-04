/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts

import "../components"
import "../theme"

Page {
    id: page
    title: qsTr("Messages")
    padding: 0

    property var app: null
    required property var droidstarRef
    required property var appState
    required property var smsManagerRef
    property string initialThreadKey: ""

    property string selectedThreadKey: ""
    property int selectedPeerId: 0
    property string selectedPeerLabel: ""
    property string selectedPeerName: ""
    property bool selectedIsGroup: false
    property bool mobileThreadOpen: false
    property string threadSearch: ""
    property string threadFilter: "all"
    property string pendingDeleteThreadKey: ""
    property string pendingDeleteLabel: ""
    // Surface the inbox list sits on (card colour in split view, page colour on phones)
    readonly property color inboxBase: splitView ? t.surface2 : t.bg
    property bool presetsOpen: false

    readonly property bool splitView: width >= 720
    readonly property bool compact: width < 390
    readonly property bool roomy: width >= 560
    readonly property bool connectedDmr: !!(appState && appState.connected && appState.mode === "DMR")
    readonly property bool connecting: !!(appState && appState.connecting)
    readonly property bool busy: !!(appState && (appState.connected || appState.connecting))
    readonly property bool identityReady: !!(appState && String(appState.callsign || "").trim() !== "" &&
                                             parseInt(appState.dmrid || "0") > 0)
    readonly property bool servicePeer: _isService(selectedPeerId)
    readonly property bool smsStandby: !!(app && app.smsStandbyActive && appState && appState.privateCall)
    readonly property bool voiceLinked: !!(connectedDmr && appState && !appState.privateCall &&
                                           parseInt(appState.dmrtgid || "0") > 0)
    readonly property string restoreTg: app ? String(app.smsStandbyRestoreTg || "") : ""

    // iOS reports the keyboard overlap natively (Qt's own window panning is disabled
    readonly property real safeBottom: (app && app.safeArea) ? app.safeArea.bottom : 0
    readonly property real keyboardOverlap: (page.visible && Qt.platform.os === "ios" && smsManagerRef)
                                            ? (smsManagerRef.keyboardHeight || 0) : 0
    readonly property real kbInset: Math.max(0, keyboardOverlap - safeBottom)
    property real kbAnimated: kbInset
    Behavior on kbAnimated { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    // Landscape phone with the keyboard up leaves ~100pt for the chat: compact
    readonly property bool cramped: kbAnimated > 0 && (height - kbAnimated) < 260
    // Keeps app toasts clear of the keyboard and, while a chat is open, of the composer bar
    readonly property real toastBottomInset: kbAnimated + ((selectedPeerId > 0 && (splitView || mobileThreadOpen))
                                                           ? composerBar.height + (splitView ? 10 : 0) : 0)

    readonly property var formatLabels: [
        qsTr("Motorola M-SMS"),
        qsTr("AnyTone Standard LE"),
        qsTr("ETSI UTF-16BE"),
        qsTr("Chinese / Hytera")
    ]
    readonly property var filterOptions: [
        { label: qsTr("All"), value: "all" },
        { label: qsTr("Unread"), value: "unread" },
        { label: qsTr("Private"), value: "private" },
        { label: qsTr("Talkgroups"), value: "group" },
        { label: qsTr("Services"), value: "service" }
    ]
    // BrandMeister SMS services (private messages). 262993/262994/262995 are BM262 services
    readonly property int aprsGatewayId: smsManagerRef
                                         ? smsManagerRef.aprsServiceIdForHost(appState ? appState.selectedHost : "")
                                         : 262999
    readonly property var serviceActions: [
        { id: 262993, title: qsTr("Echo test"), sub: "ECHO", glyph: "\uf01e", draft: "ECHO" },
        { id: 262993, title: qsTr("Weather"), sub: "WX", glyph: "\uf0c2", draft: "WX" },
        { id: 262993, title: qsTr("My GPS"), sub: "GPS", glyph: "\uf124", draft: "GPS" },
        { id: 262995, title: qsTr("Callsign SMS"), sub: "SMSC", glyph: "\uf0e0", draft: "" },
        { id: aprsGatewayId, title: qsTr("APRS"), sub: String(aprsGatewayId), glyph: "\uf1d8", draft: "" },
        { id: aprsGatewayId, title: qsTr("Cell SMS"), sub: "SMSGTE", glyph: "\uf10b", draft: "" },
        { id: 262994, title: qsTr("Pager"), sub: "DAPNET", glyph: "\uf0f3", draft: "" },
        { id: 262994, title: qsTr("Repeater"), sub: "RPT", glyph: "\uf012", draft: "RPT" }
    ]
    // Composer templates. `select` is pre-highlighted after insertion so typing replaces it.
    function _preset(label, text, select) {
        return { label: label, text: text === undefined ? label : text, select: select || "" }
    }
    readonly property bool parrotPeer: selectedPeerId === 9990 || selectedPeerId === 31000
    readonly property var quickPresets: {
        var id = page.selectedPeerId
        if (page.parrotPeer)
            return []
        if (id === 262993)
            return [_preset("ECHO"), { label: qsTr("Faster replies"), action: "fastReplies" }, _preset("HELP"), _preset("WX"), _preset("WX GPS"),
                    _preset(qsTr("WX city"), "WX CITY", "CITY"), _preset(qsTr("METAR"), "METAR ICAO", "ICAO"),
                    _preset("GPS"), _preset("GPS RPT"), _preset("RSSI"), _preset("INFO"),
                    _preset("CONFIG SHORT"), _preset("CONFIG ON")]
        if (id === 262994)
            return [_preset("RPT"), _preset(qsTr("Page a callsign"), "CALLSIGN message", "CALLSIGN")]
        if (id === 262995)
            return [_preset(qsTr("Send to callsign"), "CALLSIGN message", "CALLSIGN"), _preset("INBOX"),
                    _preset("GET 1"), _preset("GET"), _preset("DELETE"), _preset("DIRECT ON"), _preset("ECHO"),
                    { label: qsTr("Faster replies"), action: "fastReplies" }, _preset("CONFIG")]
        if (id >= 100999 && (id % 1000) === 999)
            return [_preset(qsTr("APRS station"), "CALLSIGN-9 message", "CALLSIGN-9"),
                    _preset(qsTr("Cell phone (SMSGTE)"), "SMSGTE @phone message", "phone")]
        var myCall = page.appState ? String(page.appState.callsign || "") : ""
        return [_preset(qsTr("73 de %1").arg(myCall)), _preset(qsTr("QSL, loud & clear! 73")),
                _preset("QTH?"), _preset(qsTr("PSE QSY TG"), "PSE QSY TG ")]
    }
    // Service threads always offer their commands; personal chats show replies on
    readonly property bool showPresets: selectedPeerId > 0 && quickPresets.length > 0 &&
                                        (presetsOpen || (servicePeer && !parrotPeer) ||
                                         (messageModel.count === 0 && composer.length === 0))

    property Component headerActions: Component {
        Row {
            spacing: 0
            HeaderAction {
                glyph: "\uf044"
                label: qsTr("New")
                showLabel: page.roomy
                onClicked: page.openNewMessage("", false, "")
            }
            HeaderAction {
                glyph: "\uf1f8"
                label: qsTr("Clear")
                showLabel: page.roomy
                tone: t.danger
                enabled: threadModel.count > 0
                onClicked: clearAllDialog.open()
            }
        }
    }

    Tokens { id: t }
    background: Rectangle { color: t.bg }

    ListModel { id: threadModel }
    ListModel { id: messageModel }
    ListModel { id: recipientModel }

    Timer {
        id: recipientSearchTimer
        interval: 70
        repeat: false
        onTriggered: page.updateRecipientResults()
    }

    function _syncModelInPlace(target, rows) {
        var src = rows || []
        for (var i = 0; i < src.length; ++i) {
            if (i < target.count)
                target.set(i, src[i])
            else
                target.append(src[i])
        }
        while (target.count > src.length)
            target.remove(target.count - 1)
    }

    function _isService(id) {
        var n = parseInt(id || "0") || 0
        return n === 9990 || n === 31000 || n === 262993 || n === 262994 ||
               n === 262995 || (n >= 100999 && (n % 1000) === 999)
    }

    function _peerTone(isService, isGroup) {
        return isService ? t.success : (isGroup ? t.warning : t.accent)
    }

    function _peerGlyph(isService, isGroup) {
        return isService ? "\uf0ac" : (isGroup ? "\uf0c0" : "\uf007")
    }

    function _normalizedThreads() {
        var source = page.smsManagerRef ? (page.smsManagerRef.threads || []) : []
        var query = page.threadSearch.trim().toLowerCase()
        var result = []
        for (var i = 0; i < source.length; ++i) {
            var row = source[i]
            if (!row) continue
            var group = !!row.isGroup
            var service = page._isService(row.peerId)
            if (page.threadFilter === "unread" && !(row.unreadCount > 0)) continue
            if (page.threadFilter === "private" && (group || service)) continue
            if (page.threadFilter === "group" && !group) continue
            if (page.threadFilter === "service" && !service) continue
            if (query !== "") {
                var hay = (String(row.peerCallsign || "") + " " +
                           String(row.peerName || "") + " " +
                           String(row.peerId || "") + " " +
                           String(row.lastText || "")).toLowerCase()
                if (hay.indexOf(query) < 0) continue
            }
            result.push({
                threadKey: String(row.threadKey || ""),
                peerId: parseInt(row.peerId || "0") || 0,
                peerCallsign: String(row.peerCallsign || ""),
                peerName: String(row.peerName || ""),
                isGroup: group,
                isService: service,
                lastText: String(row.lastText || ""),
                lastDirection: String(row.lastDirection || ""),
                displayTime: String(row.displayTime || ""),
                lastStatus: parseInt(row.lastStatus || "0"),
                unreadCount: parseInt(row.unreadCount || "0")
            })
        }
        return result
    }

    function rebuildThreads() {
        page._syncModelInPlace(threadModel, page._normalizedThreads())
        if (page.selectedThreadKey !== "") page.reloadMessages(false)
    }

    function reloadMessages(forceEnd) {
        if (!page.smsManagerRef || page.selectedThreadKey === "") {
            page._syncModelInPlace(messageModel, [])
            return
        }
        var rows = page.smsManagerRef.messagesForThread(page.selectedThreadKey)
        var out = []
        for (var i = 0; i < rows.length; ++i) {
            var m = rows[i]
            out.push({
                msgId: parseInt(m.id || "0"),
                direction: String(m.direction || ""),
                body: String(m.text || ""),
                displayTime: String(m.displayTime || ""),
                status: parseInt(m.status || "0"),
                formatName: String(m.formatName || ""),
                senderId: parseInt(m.senderId || m.peerId || "0"),
                senderCallsign: String(m.senderCallsign || m.peerCallsign || ""),
                replyMs: m.replyMs !== undefined ? parseInt(m.replyMs) : -1,
                groupStart: true,
                groupEnd: true
            })
        }
        for (var j = 0; j < out.length; ++j) {
            var prev = j > 0 ? out[j - 1] : null
            var next = j + 1 < out.length ? out[j + 1] : null
            out[j].groupStart = !prev || prev.direction !== out[j].direction || prev.senderId !== out[j].senderId
            out[j].groupEnd = !next || next.direction !== out[j].direction || next.senderId !== out[j].senderId
        }
        var stick = forceEnd || messageList.stickToEnd || messageList.count === 0
        page._syncModelInPlace(messageModel, out)
        if (page.visible && (page.splitView || page.mobileThreadOpen))
            page.smsManagerRef.markThreadRead(page.selectedThreadKey)
        if (stick) {
            messageList.stickToEnd = true
            Qt.callLater(messageList.jumpToEnd)
        }
    }

    function syncActiveThread() {
        if (!page.smsManagerRef) return
        var active = page.visible && (page.splitView || page.mobileThreadOpen)
                     ? page.selectedThreadKey : ""
        page.smsManagerRef.setActiveThread(active)
    }

    function selectThread(key, peerId, callsign, peerName, isGroup) {
        var changed = page.selectedThreadKey !== String(key || "")
        page.selectedThreadKey = String(key || "")
        page.selectedPeerId = parseInt(peerId || "0") || 0
        page.selectedPeerLabel = String(callsign || peerId || "")
        page.selectedPeerName = String(peerName || "")
        page.selectedIsGroup = !!isGroup
        page.mobileThreadOpen = true
        if (changed) {
            page.presetsOpen = false
            composer.clear()
        }
        page.syncActiveThread()
        page.reloadMessages(true)
        if (!page._gestureHintShown && messageModel.count > 0 && page.app) {
            page._gestureHintShown = true
            page.app.notify(qsTr("Hold a message for options. Swipe right to reply."), "", null, "info")
        }
    }
    property bool _gestureHintShown: false

    function closeThread() {
        Qt.inputMethod.hide()
        composer.focus = false
        page.mobileThreadOpen = false
        page.syncActiveThread()
    }

    function openThreadKey(key) {
        if (!key || key.indexOf(":") < 0 || !page.smsManagerRef) return
        var parts = String(key).split(":")
        var isGroup = parts[0] === "g"
        var id = parseInt(parts[1] || "0") || 0
        var meta = page.smsManagerRef.ensureThreadMeta(id, isGroup)
        page.selectThread(String(meta.threadKey || key), id,
                          String(meta.peerCallsign || id),
                          String(meta.peerName || ""), isGroup)
    }

    function openConversation(id, isGroup, draft) {
        if (!page.smsManagerRef) return
        var meta = page.smsManagerRef.ensureThreadMeta(parseInt(id || "0") || 0, !!isGroup)
        if (!meta || !(meta.peerId > 0)) return
        page.selectThread(meta.threadKey, meta.peerId, meta.peerCallsign, meta.peerName, meta.isGroup)
        if (draft !== undefined && String(draft) !== "") {
            composer.text = String(draft)
            composer.cursorPosition = composer.length
            composer.forceActiveFocus()
        }
    }

    function openNewMessage(query, isGroup, draft) {
        newMessageDialog.newDraft = String(draft || "")
        recipientGroup.checked = !!isGroup
        recipientQuery.text = String(query || "")
        page.updateRecipientResults()
        newMessageDialog.open()
        recipientQuery.forceActiveFocus()
    }

    function updateRecipientResults() {
        if (!page.smsManagerRef) return
        var rows = page.smsManagerRef.searchRecipients(
                    recipientQuery.text, recipientGroup.checked,
                    page.appState ? page.appState.selectedHost : "")
        var out = []
        for (var i = 0; i < rows.length; ++i) {
            var r = rows[i]
            out.push({
                peerId: parseInt(r.id || "0") || 0,
                callsign: String(r.callsign || r.id || ""),
                subtitle: String(r.subtitle || ""),
                badge: String(r.badge || ""),
                isGroup: !!r.isGroup
            })
        }
        page._syncModelInPlace(recipientModel, out)
    }

    function applyPreset(preset) {
        if (preset && preset.action === "fastReplies") {
            fastRepliesDialog.open()
            return
        }
        var p = (typeof preset === "string") ? page._preset(preset, preset, "") : preset
        composer.text = String(p.text || "")
        composer.forceActiveFocus()
        var at = p.select ? composer.text.indexOf(p.select) : -1
        if (at >= 0)
            composer.select(at, at + p.select.length)
        else
            composer.cursorPosition = composer.length
    }

    // Menus close the keyboard while open (so they are never hidden behind
    property Item _keyboardOwner: null
    function suspendKeyboard() {
        var fi = page.Window.activeFocusItem
        if (fi && fi.cursorRectangle !== undefined && typeof fi.selectAll === "function") {
            page._keyboardOwner = fi
            fi.focus = false
            Qt.inputMethod.hide()
        } else {
            page._keyboardOwner = null
        }
    }
    function resumeKeyboard(restore) {
        var fi = page._keyboardOwner
        page._keyboardOwner = null
        if (restore && fi)
            Qt.callLater(function() { fi.forceActiveFocus(); Qt.inputMethod.show() })
    }

    // Reply context shown above the composer; prefixed to the outgoing text
    property string replySnippet: ""
    property bool replyToOutgoing: false
    function startReply(body, outgoing) {
        var flat = String(body || "").replace(/\s+/g, " ").trim()
        page.replySnippet = flat.length > 28 ? flat.substring(0, 27) + "…" : flat
        page.replyToOutgoing = !!outgoing
        composer.forceActiveFocus()
    }
    function cancelReply() { page.replySnippet = "" }

    function sendCurrent() {
        if (!page.smsManagerRef || page.selectedPeerId <= 0 || composer.text.trim() === "") return
        if (!page.connectedDmr) {
            page.connectionButtonClicked()
            return
        }
        var body = composer.text.trim()
        if (page.replySnippet !== "")
            body = "Re \"" + page.replySnippet + "\": " + body
        var id = page._sendText(page.selectedPeerId, body, page.selectedIsGroup)
        if (id >= 0) {
            composer.clear()
            page.replySnippet = ""
            page.presetsOpen = false
            page.reloadMessages(true)
        }
    }

    function _sendText(peerId, body, isGroup) {
        var fmt = page.appState && page.appState.odtpActive
                  ? -1 : page.smsManagerRef.defaultFormat
        return page.smsManagerRef.sendMessage(String(peerId), body, !!isGroup, fmt)
    }

    // BM262 services pause 2.5 s before answering so radios can drop out
    function enableFastReplies() {
        if (!page.smsManagerRef) return
        if (!page.connectedDmr) {
            page.connectionButtonClicked()
            return
        }
        var target = (page.selectedPeerId === 262995) ? 262995 : 262993
        if (page._sendText(target, "CONFIG ON", false) < 0) return
        fastRepliesTimer.target = target
        fastRepliesTimer.restart()
        page.reloadMessages(true)
    }

    Timer {
        id: fastRepliesTimer
        property int target: 262993
        // Let the service apply CONFIG ON before the delay change
        interval: 1500
        onTriggered: {
            if (page.smsManagerRef && page.connectedDmr) {
                page._sendText(target, "CONFIG DELAY 1", false)
                page.reloadMessages(true)
            }
        }
    }

    function connectionButtonClicked() {
        if (!page.app) return
        if (page.busy) {
            page.app.connectOrDisconnect()
            return
        }
        var inGroupThread = page.selectedIsGroup && page.selectedPeerId > 0 &&
                            (page.splitView || page.mobileThreadOpen)
        page.app.connectDmrShortcut(page.appState ? page.appState.selectedHost : "",
                                    inGroupThread ? page.selectedPeerId : 0,
                                    inGroupThread)
    }

    function _statusText(status) {
        return status === 0 ? qsTr("Sending") : status === 1 ? qsTr("Sent")
             : status === 2 ? qsTr("Delivered") : status === 3 ? qsTr("Not delivered") : qsTr("Received")
    }

    function _replyTimeText(ms) {
        return qsTr("replied in %1 s").arg((ms / 1000).toFixed(1))
    }

    function openMessageActions(anchorItem, msgId, body, status, outgoing, displayTime, formatName, replyMs) {
        var actions = [ { id: "reply", glyph: "\uf112", text: qsTr("Reply") },
                        { id: "copy", glyph: "\uf0c5", text: qsTr("Copy") } ]
        if (outgoing && status === 3)
            actions.push({ id: "retry", glyph: "\uf01e", text: qsTr("Send again") })
        actions.push({ id: "delete", glyph: "\uf1f8", text: qsTr("Delete"), danger: true })
        var meta = [outgoing ? page._statusText(status) : qsTr("Received")]
        if (formatName !== "") meta.push(formatName)
        if (replyMs !== undefined && replyMs >= 0) meta.push(page._replyTimeText(replyMs))
        var pos = anchorItem ? anchorItem.mapToItem(null, 0, 0) : { x: 20, y: 200 }
        var rect = { x: pos.x, y: pos.y, w: anchorItem ? anchorItem.width : 200, h: anchorItem ? anchorItem.height : 44 }
        actionSheet.showMessage(anchorItem, rect, body, outgoing, displayTime, status, meta.join(" • "), actions, function(action) {
            if (!page.smsManagerRef) return
            if (action === "reply") page.startReply(body, outgoing)
            else if (action === "copy") {
                page.smsManagerRef.copyToClipboard(body)
                if (page.app) page.app.notify(qsTr("Message copied"), "", null, "success")
            }
            else if (action === "retry") page.smsManagerRef.retryMessage(msgId)
            else if (action === "delete") page.smsManagerRef.deleteMessage(msgId)
        })
    }

    function confirmDeleteThread(key, label) {
        page.pendingDeleteThreadKey = key
        page.pendingDeleteLabel = label
        deleteThreadDialog.open()
    }

    function openThreadActions(key, peerId, label, peerName, isGroup, isService, unread) {
        var actions = [ { id: "open", glyph: "\uf075", text: qsTr("Open conversation") } ]
        if (unread > 0)
            actions.push({ id: "read", glyph: "\uf00c", text: qsTr("Mark as read") })
        if (!isService)
            actions.push({ id: "voice", glyph: "\uf130",
                           text: isGroup ? qsTr("Link voice to TG %1").arg(peerId) : qsTr("Private voice call") })
        actions.push({ id: "delete", glyph: "\uf1f8", text: qsTr("Delete conversation"), danger: true })
        actionSheet.showSheet(label, peerName, actions, function(action) {
            if (action === "open") page.selectThread(key, peerId, label, peerName, isGroup)
            else if (action === "read" && page.smsManagerRef) page.smsManagerRef.markThreadRead(key)
            else if (action === "voice" && page.app) {
                if (isGroup) page.app.leaveSmsStandby(String(peerId))
                else page.app.quickSwitchToTalkgroup(String(peerId), false, true)
            }
            else if (action === "delete") page.confirmDeleteThread(key, label)
        })
    }

    function handleBack() {
        if (actionSheet.visible) { actionSheet.close(); return true }
        if (chatMenu.visible) { chatMenu.close(); return true }
        if (formatMenu.visible) { formatMenu.close(); return true }
        if (newMessageDialog.visible) { newMessageDialog.close(); return true }
        if (fastRepliesDialog.visible) { fastRepliesDialog.close(); return true }
        if (deleteThreadDialog.visible) { deleteThreadDialog.close(); return true }
        if (clearAllDialog.visible) { clearAllDialog.close(); return true }
        if (!page.splitView && page.mobileThreadOpen) {
            page.closeThread()
            return true
        }
        return false
    }

    onVisibleChanged: {
        if (visible) {
            if (page.app && page.appState && !page.busy && page.appState.mode !== "DMR")
                page.app.setMode("DMR")
            page.rebuildThreads()
        } else {
            composer.focus = false
        }
        page.syncActiveThread()
    }
    onSplitViewChanged: page.syncActiveThread()
    onMobileThreadOpenChanged: page.syncActiveThread()
    onThreadSearchChanged: page.rebuildThreads()
    onThreadFilterChanged: page.rebuildThreads()

    Component.onCompleted: {
        page.rebuildThreads()
        if (page.initialThreadKey !== "") page.openThreadKey(page.initialThreadKey)
    }

    Connections {
        target: page.smsManagerRef
        enabled: page.smsManagerRef !== null
        function onThreadsChanged() { page.rebuildThreads() }
        function onThreadRequested(threadKey) {
            page.rebuildThreads()
            page.openThreadKey(threadKey)
        }
    }

    // MENUS & DIALOGS
    // Message actions: the held bubble lifts in place over a dimmed page
    Popup {
        id: actionSheet
        parent: Overlay.overlay
        modal: true
        dim: true
        padding: 0
        closePolicy: Popup.CloseOnEscape

        property string previewText: ""
        property bool previewOutgoing: false
        property string previewTime: ""
        property int previewStatus: 4
        property string previewMeta: ""
        property string sheetTitle: ""
        property string sheetSubtitle: ""
        property var actions: []
        property var handler: null
        property bool actionChosen: false
        property bool messageMode: false
        property var anchorRect: null
        property Item sourceItem: null
        // Lift animation state (window coordinates)
        property real liftY: 0
        property real liftScale: 1
        property real menuReveal: 0

        readonly property var _win: page.Window.window
        readonly property real safeBottom: (_win && _win.safeArea) ? _win.safeArea.bottom : 0
        readonly property real safeTop: (_win && _win.safeArea) ? _win.safeArea.top : 0
        readonly property real safeLeft: (_win && _win.safeArea) ? _win.safeArea.left : 0
        readonly property real safeRight: (_win && _win.safeArea) ? _win.safeArea.right : 0
        readonly property real ow: parent ? parent.width : 400
        readonly property real oh: parent ? parent.height : 800
        readonly property real kb: Qt.platform.os === "ios"
                                   ? page.keyboardOverlap
                                   : (Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height : 0)
        readonly property real topBound: Math.max(safeTop, page.mapToItem(null, 0, 0).y) + 10
        readonly property real bottomBound: oh - Math.max(kb, safeBottom) - 12

        function showMessage(source, rect, text, outgoing, time, status, meta, actionList, callback) {
            page.suspendKeyboard()
            actionChosen = false
            messageMode = true
            anchorRect = rect
            // The real bubble hides while its lifted copy is shown
            sourceItem = source
            if (sourceItem) sourceItem.opacity = 0
            previewText = String(text || "")
            previewOutgoing = !!outgoing
            previewTime = String(time || "")
            previewStatus = status
            previewMeta = String(meta || "")
            actions = actionList || []
            handler = callback
            liftScale = 1
            menuReveal = 0
            liftY = rect.y
            open()
        }
        function showSheet(title, subtitle, actionList, callback) {
            page.suspendKeyboard()
            actionChosen = false
            messageMode = false
            anchorRect = null
            sheetTitle = String(title || "")
            sheetSubtitle = String(subtitle || "")
            actions = actionList || []
            handler = callback
            menuReveal = 0
            open()
        }
        function trigger(actionId) {
            var fn = handler
            actionChosen = true
            close()
            if (fn) fn(actionId)
        }
        onOpened: {
            if (messageMode) liftY = liftBubble.targetY
            liftScale = 1.03
            menuReveal = 1
        }
        onClosed: {
            if (sourceItem) sourceItem.opacity = 1
            sourceItem = null
            page.resumeKeyboard(!actionChosen)
        }

        Behavior on liftY { enabled: actionSheet.opened; NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        Behavior on liftScale { enabled: actionSheet.opened; NumberAnimation { duration: 220; easing.type: Easing.OutBack; easing.overshoot: 1.5 } }
        Behavior on menuReveal { enabled: actionSheet.opened; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        x: 0
        y: 0
        width: ow
        height: oh
        background: Item {}
        Overlay.modal: Rectangle {
            color: "#A6000000"
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }

        contentItem: Item {
            // Tap anywhere outside the bubble and menu to dismiss
            TapHandler { onTapped: actionSheet.close() }

            // ── Lifted message ─────────────────────────────────
            Item {
                id: liftLayer
                visible: actionSheet.messageMode
                anchors.fill: parent

                readonly property real menuW: Math.min(250, actionSheet.ow - actionSheet.safeLeft - actionSheet.safeRight - 24)
                readonly property real menuH: liftMenu.implicitHeight
                readonly property real captionH: 18
                readonly property real gap: 10

                Rectangle {
                    id: liftBubble
                    readonly property var r: actionSheet.anchorRect || { x: 20, y: 200, w: 200, h: 44 }
                    readonly property real naturalH: liftBody.implicitHeight + liftMeta.implicitHeight + 19
                    // Lowest y that keeps the menu above the keyboard / home
                    readonly property real maxY: actionSheet.bottomBound - liftLayer.gap - liftLayer.menuH - naturalH
                    readonly property real minY: actionSheet.topBound + liftLayer.captionH
                    readonly property bool menuAbove: maxY < minY
                    readonly property real targetY: menuAbove
                        ? Math.min(actionSheet.bottomBound - naturalH,
                                   Math.max(actionSheet.topBound + liftLayer.menuH + liftLayer.gap + liftLayer.captionH, r.y))
                        : Math.max(minY, Math.min(maxY, r.y))

                    x: r.x
                    y: actionSheet.liftY
                    width: r.w
                    height: naturalH
                    radius: 18
                    color: actionSheet.previewOutgoing ? (actionSheet.previewStatus === 3 ? "#7F1D1D" : t.accentStrong)
                                                       : t.surface3
                    scale: actionSheet.liftScale
                    transformOrigin: actionSheet.previewOutgoing ? Item.Right : Item.Left
                    layer.enabled: true
                    layer.effect: null

                    // Soft shadow
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        anchors.topMargin: 4
                        z: -1
                        radius: parent.radius + 2
                        color: "#55000000"
                    }

                    Column {
                        x: 12
                        y: 8
                        width: parent.width - 24
                        spacing: 3
                        Text {
                            id: liftBody
                            width: parent.width
                            text: actionSheet.previewText
                            color: actionSheet.previewOutgoing ? "white" : t.text
                            font.pixelSize: t.fsBody + 1
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                        Row {
                            id: liftMeta
                            anchors.right: parent.right
                            spacing: 5
                            Text {
                                text: actionSheet.previewTime
                                color: actionSheet.previewOutgoing ? Qt.rgba(1, 1, 1, 0.75) : t.textMuted
                                font.pixelSize: 10
                            }
                            Text {
                                visible: actionSheet.previewOutgoing
                                text: actionSheet.previewStatus === 0 ? "\uf017"
                                      : (actionSheet.previewStatus === 2 ? "\uf058"
                                         : (actionSheet.previewStatus === 3 ? "\uf071" : "\uf00c"))
                                font.family: "FontAwesome"
                                font.pixelSize: 10
                                color: actionSheet.previewStatus === 2 ? "#A7F3D0"
                                     : (actionSheet.previewStatus === 3 ? "#FECACA" : Qt.rgba(1, 1, 1, 0.75))
                            }
                        }
                    }
                    TapHandler { }   // swallow taps on the bubble itself
                }

                // Delivery details caption, just above the bubble on its own side
                Text {
                    x: actionSheet.previewOutgoing ? liftBubble.x + liftBubble.width - width : liftBubble.x
                    y: liftBubble.y - liftLayer.captionH
                    text: actionSheet.previewMeta
                    color: t.textMuted
                    font.pixelSize: 11
                    opacity: actionSheet.menuReveal
                }

                // Menu attached to the bubble
                Rectangle {
                    id: liftMenu
                    readonly property real belowY: liftBubble.y + liftBubble.height + liftLayer.gap
                    readonly property real aboveY: liftBubble.y - liftLayer.captionH - liftLayer.gap - implicitHeight
                    readonly property real wantedX: actionSheet.previewOutgoing ? liftBubble.x + liftBubble.width - width : liftBubble.x
                    x: Math.max(actionSheet.safeLeft + 12, Math.min(actionSheet.ow - actionSheet.safeRight - 12 - width, wantedX))
                    y: liftBubble.menuAbove ? aboveY : belowY
                    width: liftLayer.menuW
                    implicitHeight: menuColumn.implicitHeight
                    height: implicitHeight
                    radius: 16
                    color: t.surface
                    border.color: t.stroke
                    border.width: 1
                    clip: true
                    opacity: actionSheet.menuReveal
                    scale: 0.92 + 0.08 * actionSheet.menuReveal
                    transformOrigin: liftBubble.menuAbove
                        ? (actionSheet.previewOutgoing ? Item.BottomRight : Item.BottomLeft)
                        : (actionSheet.previewOutgoing ? Item.TopRight : Item.TopLeft)
                    TapHandler { }

                    Column {
                        id: menuColumn
                        width: parent.width
                        Repeater {
                            model: actionSheet.actions
                            delegate: Rectangle {
                                id: liftRow
                                required property var modelData
                                required property int index
                                width: menuColumn.width
                                height: 46
                                color: liftRowMouse.pressed ? t.surface3 : "transparent"
                                Rectangle {
                                    visible: liftRow.index > 0
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 1
                                    color: t.stroke
                                }
                                Label {
                                    x: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 56
                                    text: liftRow.modelData.text
                                    font.pixelSize: t.fsBody + 1
                                    color: liftRow.modelData.danger ? t.danger : t.text
                                    elide: Text.ElideRight
                                }
                                Label {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: liftRow.modelData.glyph
                                    font.family: "FontAwesome"
                                    font.pixelSize: 15
                                    color: liftRow.modelData.danger ? t.danger : t.textMuted
                                }
                                MouseArea {
                                    id: liftRowMouse
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: actionSheet.trigger(liftRow.modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            // ── Conversation sheet ─────────────────────────────
            Rectangle {
                id: sheetCard
                visible: !actionSheet.messageMode
                readonly property bool dock: actionSheet.ow < 600
                width: dock ? actionSheet.ow : Math.min(380, actionSheet.ow - 48)
                height: sheetBody.implicitHeight + (dock && actionSheet.kb === 0 ? actionSheet.safeBottom : 0)
                x: (actionSheet.ow - width) / 2
                y: dock ? actionSheet.oh - actionSheet.kb - height + (1 - actionSheet.menuReveal) * 40
                          : Math.max(actionSheet.safeTop + 24, (actionSheet.oh - actionSheet.kb - height) / 2)
                radius: 20
                color: t.surface
                border.color: t.stroke
                border.width: dock ? 0 : 1
                opacity: actionSheet.menuReveal
                TapHandler { }

                Rectangle {
                    visible: sheetCard.dock
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 20
                    color: parent.color
                }

                Column {
                    id: sheetBody
                    width: parent.width
                    topPadding: sheetCard.dock ? 8 : 14
                    bottomPadding: 8
                    spacing: 0

                    Rectangle {
                        visible: sheetCard.dock
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 40; height: 5; radius: 2.5
                        color: t.textMuted; opacity: 0.45
                    }
                    Item {
                        width: parent.width
                        height: 58
                        Column {
                            x: 20
                            width: parent.width - 40
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Label {
                                width: parent.width
                                text: actionSheet.sheetTitle
                                font.pixelSize: t.fsTitle
                                font.bold: true
                                color: t.text
                                elide: Text.ElideRight
                            }
                            Label {
                                width: parent.width
                                visible: text !== ""
                                text: actionSheet.sheetSubtitle
                                font.pixelSize: t.fsSmall
                                color: t.textMuted
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Repeater {
                        model: actionSheet.actions
                        delegate: Rectangle {
                            id: sheetRow
                            required property var modelData
                            required property int index
                            width: sheetBody.width
                            height: 52
                            color: sheetRowMouse.pressed ? t.surface3 : "transparent"
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 58
                                height: 1
                                color: t.stroke
                            }
                            Label {
                                x: 22
                                anchors.verticalCenter: parent.verticalCenter
                                width: 24
                                horizontalAlignment: Text.AlignHCenter
                                text: sheetRow.modelData.glyph
                                font.family: "FontAwesome"
                                font.pixelSize: 16
                                color: sheetRow.modelData.danger ? t.danger : t.accent
                            }
                            Label {
                                x: 58
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 76
                                text: sheetRow.modelData.text
                                font.pixelSize: t.fsBody + 1
                                color: sheetRow.modelData.danger ? t.danger : t.text
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: sheetRowMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: actionSheet.trigger(sheetRow.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: chatMenu
        margins: 8
        property bool acted: false
        onAboutToShow: {
            acted = false
            page.suspendKeyboard()
        }
        // Deferred: an item's triggered signal can arrive after the menu reports closed
        onClosed: Qt.callLater(function() { page.resumeKeyboard(!chatMenu.acted) })
        width: Math.min(270, page.width - 16)
        MenuItem {
            text: page.selectedIsGroup ? qsTr("Link voice to TG %1").arg(page.selectedPeerId)
                                       : qsTr("Private voice call to %1").arg(page.selectedPeerLabel)
            visible: page.selectedPeerId > 0 && !page.servicePeer
            height: visible ? implicitHeight : 0
            onTriggered: {
                chatMenu.acted = true
                if (!page.app) return
                if (page.selectedIsGroup) page.app.leaveSmsStandby(String(page.selectedPeerId))
                else page.app.quickSwitchToTalkgroup(String(page.selectedPeerId), false, true)
            }
        }
        MenuItem {
            text: qsTr("SMS format: %1").arg(page.formatLabels[
                       Math.max(0, Math.min(3, page.smsManagerRef ? page.smsManagerRef.defaultFormat : 0))])
            visible: !(page.appState && page.appState.odtpActive)
            height: visible ? implicitHeight : 0
            onTriggered: {
                chatMenu.acted = true
                formatMenu.popup(chatMenuButton, 0, chatMenuButton.height)
            }
        }
        MenuItem {
            text: page.presetsOpen ? qsTr("Hide quick replies") : qsTr("Show quick replies")
            onTriggered: {
                chatMenu.acted = true
                page.presetsOpen = !page.presetsOpen
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete conversation")
            enabled: page.selectedThreadKey !== "" && messageModel.count > 0
            onTriggered: {
                chatMenu.acted = true
                page.confirmDeleteThread(page.selectedThreadKey, page.selectedPeerLabel)
            }
        }
    }

    Menu {
        id: formatMenu
        margins: 8
        width: Math.min(260, page.width - 16)
        MenuItem {
            text: page.formatLabels[0]
            checkable: true
            checked: !!page.smsManagerRef && page.smsManagerRef.defaultFormat === 0
            onTriggered: if (page.smsManagerRef) page.smsManagerRef.defaultFormat = 0
        }
        MenuItem {
            text: page.formatLabels[1]
            checkable: true
            checked: !!page.smsManagerRef && page.smsManagerRef.defaultFormat === 1
            onTriggered: if (page.smsManagerRef) page.smsManagerRef.defaultFormat = 1
        }
        MenuItem {
            text: page.formatLabels[2]
            checkable: true
            checked: !!page.smsManagerRef && page.smsManagerRef.defaultFormat === 2
            onTriggered: if (page.smsManagerRef) page.smsManagerRef.defaultFormat = 2
        }
        MenuItem {
            text: page.formatLabels[3]
            checkable: true
            checked: !!page.smsManagerRef && page.smsManagerRef.defaultFormat === 3
            onTriggered: if (page.smsManagerRef) page.smsManagerRef.defaultFormat = 3
        }
    }

    AppDialog {
        id: newMessageDialog
        maxWidth: 520
        fitHeightToContent: false
        // Fixed height (capped to the visible area) so the result list has room to scroll
        height: Math.min(640, availableDialogHeight)
        // Landscape phone with the keyboard up leaves ~200pt: drop the title, hint
        readonly property bool cramped: availableDialogHeight < 360
        title: cramped ? "" : qsTr("New DMR Message")
        standardButtons: cramped ? Dialog.NoButton : Dialog.Cancel
        topPadding: cramped ? 14 : 6
        property string newDraft: ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            SegmentedControl {
                Layout.fillWidth: true
                options: [
                    { label: qsTr("Private"), value: false },
                    { label: qsTr("Talkgroup"), value: true }
                ]
                currentValue: recipientGroup.checked
                onSelected: function(value) {
                    recipientGroup.checked = !!value
                    recipientSearchTimer.restart()
                }
            }
            CheckBox { id: recipientGroup; visible: false }

            AppTextField {
                id: recipientQuery
                Layout.fillWidth: true
                placeholderText: recipientGroup.checked
                                 ? qsTr("Search TG name or enter TG ID")
                                 : qsTr("Search callsign or enter DMR ID")
                inputMethodHints: Qt.ImhNoPredictiveText
                rightPadding: recipientClear.visible ? 40 : 12
                onTextChanged: recipientSearchTimer.restart()
                onAccepted: {
                    if (recipientModel.count > 0) {
                        var first = recipientModel.get(0)
                        newMessageDialog.close()
                        page.openConversation(first.peerId, first.isGroup, newMessageDialog.newDraft)
                    }
                }
                ToolButton {
                    id: recipientClear
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: recipientQuery.text !== ""
                    text: "\uf057"
                    font.family: "FontAwesome"
                    opacity: 0.7
                    Accessible.name: qsTr("Clear search")
                    onClicked: recipientQuery.clear()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: !newMessageDialog.cramped
                text: recipientModel.count > 0
                      ? qsTr("%1 suggestions").arg(recipientModel.count)
                      : qsTr("Type 2+ callsign letters or a numeric ID")
                color: t.textMuted
                font.pixelSize: t.fsCaption
                elide: Text.ElideRight
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                model: recipientModel
                clip: true
                spacing: 4
                cacheBuffer: 500
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator {}

                delegate: ItemDelegate {
                    id: recipientRow
                    required property int peerId
                    required property string callsign
                    required property string subtitle
                    required property string badge
                    required property bool isGroup
                    width: ListView.view.width
                    implicitHeight: 58
                    onClicked: {
                        newMessageDialog.close()
                        page.openConversation(peerId, isGroup, newMessageDialog.newDraft)
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        ChatAvatar {
                            glyph: page._peerGlyph(page._isService(recipientRow.peerId), recipientRow.isGroup)
                            tone: page._peerTone(page._isService(recipientRow.peerId), recipientRow.isGroup)
                            size: 36
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: recipientRow.callsign
                                color: t.text
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: recipientRow.subtitle !== ""
                                      ? recipientRow.subtitle
                                      : (recipientRow.isGroup ? qsTr("Talkgroup %1").arg(recipientRow.peerId)
                                                              : qsTr("DMR ID %1").arg(recipientRow.peerId))
                                color: t.textMuted
                                font.pixelSize: t.fsCaption
                                elide: Text.ElideRight
                            }
                        }
                        ChatPill {
                            visible: recipientRow.badge !== "" && recipientRow.width >= 330
                            text: recipientRow.badge
                            tone: t.success
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: clearAllDialog
        maxWidth: 400
        title: qsTr("Clear all messages?")
        destructiveText: qsTr("Clear all")
        standardButtons: Dialog.Cancel | Dialog.Discard
        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                text: qsTr("All DMR message history will be permanently removed from this device.")
                wrapMode: Text.WordWrap
                color: t.text
            }
        }
        onDiscarded: {
            if (page.smsManagerRef) page.smsManagerRef.clearAllHistory()
            page.selectedThreadKey = ""
            page.selectedPeerId = 0
            page.selectedPeerLabel = ""
            page.selectedPeerName = ""
            page.mobileThreadOpen = false
            page._syncModelInPlace(messageModel, [])
            close()
            if (page.app) page.app.notify(qsTr("Message history cleared"), "", null, "success")
        }
    }

    AppDialog {
        id: fastRepliesDialog
        maxWidth: 380
        title: qsTr("Faster replies")
        acceptText: qsTr("Set 1 s")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: page.enableFastReplies()
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: t.text
                text: qsTr("BrandMeister waits 2.5 s before replying. Cut it to 1 s?")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: t.textMuted
                font.pixelSize: t.fsSmall
                text: qsTr("Also applies to radios on your DMR ID. Undo with CONFIG DELAY 3.")
            }
        }
    }

    AppDialog {
        id: deleteThreadDialog
        maxWidth: 400
        title: qsTr("Delete conversation?")
        destructiveText: qsTr("Delete")
        standardButtons: Dialog.Cancel | Dialog.Discard
        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                text: qsTr("Messages with %1 will be removed from this device.").arg(page.pendingDeleteLabel)
                wrapMode: Text.WordWrap
                color: t.text
            }
        }
        onDiscarded: {
            var deletingOpen = page.pendingDeleteThreadKey === page.selectedThreadKey
            if (page.smsManagerRef && page.pendingDeleteThreadKey !== "")
                page.smsManagerRef.deleteThread(page.pendingDeleteThreadKey)
            if (deletingOpen) {
                page.selectedThreadKey = ""
                page.selectedPeerId = 0
                page.selectedPeerLabel = ""
                page.selectedPeerName = ""
                page.mobileThreadOpen = false
                page._syncModelInPlace(messageModel, [])
            }
            close()
        }
    }

    // SMALL BUILDING BLOCKS
    // LAYOUT
    RowLayout {
        id: body
        anchors.fill: parent
        anchors.margins: page.splitView ? 10 : 0
        anchors.bottomMargin: (page.splitView ? 10 : 0) + page.kbAnimated
        spacing: 10

        // ── Inbox ───────────────────────────────────────────
        Rectangle {
            id: inboxPane
            Layout.fillHeight: true
            Layout.fillWidth: !page.splitView
            Layout.preferredWidth: page.splitView ? Math.max(320, Math.min(400, page.width * 0.38)) : -1
            visible: page.splitView || !page.mobileThreadOpen
            // Icon-only buttons are decided by the pane, not the page: the
            // wide even on a large iPad
            readonly property bool compact: width > 0 && width < 390
            radius: page.splitView ? t.rMd : 0
            color: page.splitView ? t.surface2 : "transparent"
            border.color: page.splitView ? t.stroke : "transparent"
            border.width: page.splitView ? 1 : 0
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                ListView {
                    id: threadList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: threadModel
                    clip: true
                    spacing: 2
                    cacheBuffer: 600
                    boundsBehavior: Flickable.DragOverBounds
                    ScrollIndicator.vertical: ScrollIndicator {}

                    header: Column {
                        width: threadList.width
                        spacing: 8
                        bottomPadding: 8
                        onHeightChanged: Qt.callLater(function() {
                            if (!threadList.moving && threadList.contentY <= threadList.originY + height)
                                threadList.positionViewAtBeginning()
                        })

                        // Connection strip
                        Rectangle {
                            id: connectionCard
                            width: parent.width
                            implicitHeight: connCol.implicitHeight + 20
                            radius: t.rSm
                            color: t.surface
                            border.color: page.connectedDmr ? t.tint(t.success, 0.45) : t.stroke
                            border.width: 1

                            ColumnLayout {
                                id: connCol
                                x: 10
                                y: 10
                                width: parent.width - 20
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Rectangle {
                                        Layout.preferredWidth: 10
                                        Layout.preferredHeight: 10
                                        radius: 5
                                        color: page.connectedDmr ? t.success : (page.connecting ? t.warning : t.textMuted)
                                        SequentialAnimation on opacity {
                                            running: page.connecting && page.visible && Qt.application.state === Qt.ApplicationActive
                                            loops: Animation.Infinite
                                            alwaysRunToEnd: true
                                            NumberAnimation { to: 0.3; duration: 450 }
                                            NumberAnimation { to: 1.0; duration: 450 }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: page.connectedDmr ? (page.appState.selectedHost || page.appState.host || qsTr("DMR server"))
                                              : (page.connecting ? qsTr("Connecting…") : qsTr("Messaging offline"))
                                        color: t.text
                                        font.bold: true
                                        font.pixelSize: t.fsBody
                                        elide: Text.ElideRight
                                    }
                                    AppButton {
                                        implicitHeight: 38
                                        tone: page.connectedDmr ? "danger" : (page.connecting ? "warning" : "primary")
                                        busy: page.connecting
                                        glyph: page.connectedDmr ? "\uf011" : "\uf1e6"
                                        text: inboxPane.compact && page.connectedDmr ? ""
                                              : (page.connecting ? qsTr("Cancel")
                                                 : (page.connectedDmr ? qsTr("Disconnect") : qsTr("Connect")))
                                        enabled: page.busy || page.identityReady
                                        onClicked: page.connectionButtonClicked()
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    Layout.topMargin: -4
                                    text: {
                                        if (!page.connectedDmr)
                                            return qsTr("Connects in SMS standby. No voice talkgroup is linked.")
                                        var transport = page.appState.odtpActive ? qsTr("ODTP instant") : qsTr("HomeBrew")
                                        if (page.voiceLinked)
                                            return transport + " • " + qsTr("voice on TG %1").arg(page.appState.dmrtgid)
                                        return transport + " • " + qsTr("SMS standby, voice quiet")
                                    }
                                    color: page.connectedDmr ? t.success : t.textMuted
                                    font.pixelSize: t.fsCaption
                                    wrapMode: Text.WordWrap
                                }

                                SearchableComboSheet {
                                    Layout.fillWidth: true
                                    visible: !page.busy
                                    model: page.appState ? page.appState.hostsModel : []
                                    currentText: page.appState ? page.appState.selectedHost : ""
                                    sheetTitle: qsTr("Select DMR Server")
                                    placeholderText: qsTr("Choose BrandMeister / DMR host")
                                    itemIconColor: t.accent
                                    itemIconFn: function(hostName) {
                                        if (!(page.appState && page.appState.bmOdtp)) return ""
                                        if (page.droidstarRef && typeof page.droidstarRef.is_odtp_host === "function")
                                            return page.droidstarRef.is_odtp_host(hostName) ? "\uf0e7" : ""
                                        return ""
                                    }
                                    onActivated: function(index, text) { if (page.app) page.app.setHost(text) }
                                }

                                // Voice routing while connected: quiet standby or a linked TG
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: page.connectedDmr && (page.voiceLinked || page.restoreTg !== "")
                                    spacing: 8
                                    Label {
                                        text: page.voiceLinked ? "\uf028" : "\uf026"
                                        font.family: "FontAwesome"
                                        color: page.voiceLinked ? t.accent : t.textMuted
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: page.voiceLinked ? qsTr("Hearing TG %1 voice").arg(page.appState.dmrtgid)
                                                               : qsTr("Home TG %1 is paused").arg(page.restoreTg)
                                        color: t.textMuted
                                        font.pixelSize: t.fsCaption
                                        elide: Text.ElideRight
                                    }
                                    AppButton {
                                        implicitHeight: 32
                                        leftPadding: 10
                                        rightPadding: 10
                                        font.pixelSize: t.fsSmall
                                        glyph: page.voiceLinked ? "\uf0e0" : "\uf028"
                                        text: page.voiceLinked ? qsTr("SMS only") : qsTr("Link TG %1").arg(page.restoreTg)
                                        onClicked: {
                                            if (!page.app) return
                                            if (page.voiceLinked) page.app.setDmrSmsStandby()
                                            else page.app.leaveSmsStandby("")
                                        }
                                    }
                                }

                                AppButton {
                                    Layout.fillWidth: true
                                    visible: !page.identityReady
                                    tone: "warning"
                                    glyph: "\uf013"
                                    text: qsTr("Set Callsign & DMR ID")
                                    onClicked: if (page.app) page.app.navigate("settings")
                                }

                                // Station on air right now
                                Rectangle {
                                    Layout.fillWidth: true
                                    visible: !!(page.connectedDmr && page.appState.data1 !== "" &&
                                                parseInt(page.appState.data2 || "0") > 0 &&
                                                parseInt(page.appState.data2 || "0") !== parseInt(page.appState.dmrid || "0"))
                                    implicitHeight: 44
                                    radius: 10
                                    color: t.tint(t.success, 0.10)
                                    border.color: t.tint(t.success, 0.45)
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 4
                                        spacing: 8
                                        Label { text: "\uf130"; font.family: "FontAwesome"; color: t.success }
                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("On air: %1").arg(page.appState.data1) +
                                                  (page.appState.fetchedFirstName !== "" ? " • " + page.appState.fetchedFirstName : "")
                                            color: t.text
                                            font.pixelSize: t.fsSmall
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }
                                        RoundIconButton {
                                            glyph: "\uf0e0"
                                            tone: t.success
                                            implicitWidth: 36
                                            implicitHeight: 36
                                            accessibleName: qsTr("Message %1").arg(page.appState.data1)
                                            onClicked: page.openConversation(parseInt(page.appState.data2 || "0"), false, "")
                                        }
                                    }
                                }
                            }
                        }

                        // Search
                        AppTextField {
                            id: threadSearchField
                            width: parent.width
                            placeholderText: qsTr("Search or type a callsign / ID")
                            inputMethodHints: Qt.ImhNoPredictiveText
                            leftPadding: 36
                            rightPadding: searchClear.visible ? 40 : 12
                            onTextChanged: page.threadSearch = text
                            onAccepted: if (text.trim() !== "") page.openNewMessage(text, false, "")
                            Label {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: "\uf002"
                                font.family: "FontAwesome"
                                color: t.textMuted
                            }
                            ToolButton {
                                id: searchClear
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                visible: threadSearchField.text !== ""
                                text: "\uf057"
                                font.family: "FontAwesome"
                                opacity: 0.7
                                Accessible.name: qsTr("Clear search")
                                onClicked: threadSearchField.clear()
                            }
                        }

                        // Filters
                        Flickable {
                            width: parent.width
                            height: 34
                            contentWidth: filterRow.implicitWidth
                            contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            Row {
                                id: filterRow
                                height: parent.height
                                spacing: 6
                                Repeater {
                                    model: page.filterOptions
                                    delegate: Rectangle {
                                        id: filterChip
                                        required property var modelData
                                        readonly property bool active: page.threadFilter === modelData.value
                                        height: 32
                                        width: filterLabel.implicitWidth + 24
                                        radius: 16
                                        color: active ? t.tint(t.accent, 0.22) : t.surface
                                        border.color: active ? t.accent : t.stroke
                                        Label {
                                            id: filterLabel
                                            anchors.centerIn: parent
                                            text: filterChip.modelData.label
                                            color: filterChip.active ? t.text : t.textMuted
                                            font.pixelSize: t.fsSmall
                                            font.bold: filterChip.active
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: page.threadFilter = filterChip.modelData.value
                                        }
                                    }
                                }
                            }
                        }

                        // Direct dial when the search text isn't an existing conversation
                        Rectangle {
                            width: parent.width
                            visible: threadSearchField.text.trim() !== ""
                            height: visible ? 48 : 0
                            radius: 10
                            color: t.tint(t.accent, 0.10)
                            border.color: t.tint(t.accent, 0.4)
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 6
                                spacing: 6
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Message “%1”").arg(threadSearchField.text.trim())
                                    color: t.text
                                    elide: Text.ElideRight
                                }
                                AppButton {
                                    implicitHeight: 34
                                    leftPadding: 10
                                    rightPadding: 10
                                    glyph: "\uf007"
                                    text: inboxPane.compact ? "" : qsTr("Private")
                                    onClicked: page.openNewMessage(threadSearchField.text, false, "")
                                }
                                AppButton {
                                    implicitHeight: 34
                                    leftPadding: 10
                                    rightPadding: 10
                                    glyph: "\uf0c0"
                                    text: inboxPane.compact ? "" : qsTr("TG")
                                    onClicked: page.openNewMessage(threadSearchField.text, true, "")
                                }
                            }
                        }

                        Label {
                            visible: threadSearchField.text.trim() === ""
                            text: qsTr("Quick services")
                            color: t.textMuted
                            font.pixelSize: t.fsCaption
                            font.bold: true
                        }

                        Flickable {
                            width: parent.width
                            visible: threadSearchField.text.trim() === ""
                            height: visible ? 52 : 0
                            contentWidth: quickServiceRow.implicitWidth
                            contentHeight: height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.HorizontalFlick
                            Row {
                                id: quickServiceRow
                                height: parent.height
                                spacing: 6
                                Repeater {
                                    model: page.serviceActions
                                    delegate: Rectangle {
                                        id: servicePill
                                        required property var modelData
                                        width: Math.max(84, servicePillRow.implicitWidth + 22)
                                        height: 48
                                        radius: 12
                                        color: serviceMouse.pressed ? t.surface3 : t.surface
                                        border.color: t.stroke
                                        Row {
                                            id: servicePillRow
                                            anchors.centerIn: parent
                                            spacing: 8
                                            Label {
                                                text: servicePill.modelData.glyph
                                                font.family: "FontAwesome"
                                                font.pixelSize: 15
                                                color: t.success
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Column {
                                                anchors.verticalCenter: parent.verticalCenter
                                                Label {
                                                    text: servicePill.modelData.title
                                                    color: t.text
                                                    font.bold: true
                                                    font.pixelSize: 11
                                                }
                                                Label {
                                                    text: servicePill.modelData.sub
                                                    color: t.textMuted
                                                    font.pixelSize: 9
                                                }
                                            }
                                        }
                                        MouseArea {
                                            id: serviceMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: page.openConversation(servicePill.modelData.id, false,
                                                                             servicePill.modelData.draft)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    delegate: SwipeDelegate {
                        id: threadRow
                        required property string threadKey
                        required property int peerId
                        required property string peerCallsign
                        required property string peerName
                        required property bool isGroup
                        required property bool isService
                        required property string lastText
                        required property string lastDirection
                        required property string displayTime
                        required property int lastStatus
                        required property int unreadCount
                        readonly property bool current: page.selectedThreadKey === threadKey &&
                                                        (page.splitView || page.mobileThreadOpen)
                        readonly property string titleText: peerCallsign !== "" ? peerCallsign
                                                            : (isGroup ? qsTr("TG %1").arg(peerId) : qsTr("ID %1").arg(peerId))
                        width: ListView.view.width
                        implicitHeight: 68
                        leftPadding: 8
                        rightPadding: 8
                        clip: true
                        onClicked: {
                            if (swipe.position !== 0) swipe.close()
                            else page.selectThread(threadKey, peerId, peerCallsign, peerName, isGroup)
                        }
                        onPressAndHold: page.openThreadActions(threadKey, peerId, titleText, peerName,
                                                               isGroup, isService, unreadCount)
                        // Opaque so the swipe actions behind the row never show through
                        background: Rectangle {
                            radius: 12
                            color: threadRow.current ? Qt.tint(page.inboxBase, t.tint(t.accent, 0.14))
                                 : (threadRow.down ? t.surface3 : page.inboxBase)
                        }

                        // Swipe left: quick actions revealed on the trailing edge
                        swipe.right: Row {
                            // Positioned from the delegate's own geometry: swipe.* items are parented after
                            x: threadRow.width - width
                            height: threadRow.height
                            visible: threadRow.swipe.position < 0
                            spacing: 0

                            Rectangle {
                                visible: threadRow.unreadCount > 0
                                width: 76
                                height: parent.height
                                color: t.accentStrong
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "\uf00c"; font.family: "FontAwesome"; font.pixelSize: 16; color: "white" }
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Read"); font.pixelSize: t.fsCaption; font.bold: true; color: "white" }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (page.smsManagerRef) page.smsManagerRef.markThreadRead(threadRow.threadKey)
                                        threadRow.swipe.close()
                                    }
                                }
                            }
                            Rectangle {
                                width: 76
                                height: parent.height
                                color: t.dangerStrong
                                radius: 0
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: "\uf1f8"; font.family: "FontAwesome"; font.pixelSize: 16; color: "white" }
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Delete"); font.pixelSize: t.fsCaption; font.bold: true; color: "white" }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        threadRow.swipe.close()
                                        page.confirmDeleteThread(threadRow.threadKey, threadRow.titleText)
                                    }
                                }
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            ChatAvatar {
                                glyph: page._peerGlyph(threadRow.isService, threadRow.isGroup)
                                tone: page._peerTone(threadRow.isService, threadRow.isGroup)
                                size: 44
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Label {
                                        Layout.fillWidth: true
                                        text: threadRow.titleText
                                        maximumLineCount: 1
                                        color: t.text
                                        font.bold: true
                                        font.pixelSize: t.fsBody + 1
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        text: threadRow.displayTime
                                        color: threadRow.unreadCount > 0 ? t.accent : t.textMuted
                                        font.pixelSize: t.fsCaption
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Label {
                                        visible: threadRow.lastDirection === "out"
                                        text: threadRow.lastStatus === 2 ? "\uf058"
                                              : (threadRow.lastStatus === 3 ? "\uf071"
                                                 : (threadRow.lastStatus === 0 ? "\uf017" : "\uf00c"))
                                        font.family: "FontAwesome"
                                        font.pixelSize: 10
                                        color: threadRow.lastStatus === 2 ? t.success
                                             : (threadRow.lastStatus === 3 ? t.danger : t.textMuted)
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        // Line breaks in a message must not grow the fixed-height row
                                        text: (threadRow.lastText !== "" ? threadRow.lastText : threadRow.peerName)
                                              .replace(/\s+/g, " ")
                                        maximumLineCount: 1
                                        textFormat: Text.PlainText
                                        color: threadRow.unreadCount > 0 ? t.text : t.textMuted
                                        font.pixelSize: t.fsSmall
                                        font.bold: threadRow.unreadCount > 0
                                        elide: Text.ElideRight
                                    }
                                    Rectangle {
                                        visible: threadRow.unreadCount > 0
                                        implicitWidth: Math.max(20, unreadLabel.implicitWidth + 10)
                                        implicitHeight: 20
                                        radius: 10
                                        color: t.accent
                                        Label {
                                            id: unreadLabel
                                            anchors.centerIn: parent
                                            text: threadRow.unreadCount
                                            color: t.bg
                                            font.bold: true
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Empty state lives in the footer so it flows below the connection
                    footer: Item {
                        width: threadList.width
                        height: emptyInboxCol.visible ? emptyInboxCol.height + 36 : 0
                        Column {
                            id: emptyInboxCol
                            width: Math.min(threadList.width - 28, 300)
                            x: (threadList.width - width) / 2
                            y: 20
                            visible: threadModel.count === 0
                            spacing: 10
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "\uf086"
                                font.family: "FontAwesome"
                                font.pixelSize: 34
                                color: t.tint(t.accent, 0.6)
                            }
                            Label {
                                width: parent.width
                                text: page.threadSearch !== ""
                                      ? qsTr("No conversations match “%1”").arg(page.threadSearch)
                                      : qsTr("No conversations yet. Start one with New, a quick service, or the message button next to any station on Home and QSO.")
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                color: t.textMuted
                                font.pixelSize: t.fsSmall
                            }
                        }
                    }
                }
            }
        }

        // ── Conversation ────────────────────────────────────
        Rectangle {
            id: chatPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.splitView || page.mobileThreadOpen
            radius: page.splitView ? t.rMd : 0
            color: page.splitView ? t.surface2 : t.bg
            border.color: page.splitView ? t.stroke : "transparent"
            border.width: page.splitView ? 1 : 0
            clip: true

            // Nothing selected (split view only)
            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 340)
                visible: page.selectedPeerId <= 0
                spacing: 12
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "\uf0e0"
                    font.family: "FontAwesome"
                    font.pixelSize: 44
                    color: t.tint(t.accent, 0.7)
                }
                Label {
                    width: parent.width
                    text: qsTr("DMR radio messaging")
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 18
                    font.bold: true
                    color: t.text
                }
                Label {
                    width: parent.width
                    text: qsTr("Text physical radios through BrandMeister ODTP or HomeBrew. Pick a conversation or start a new one.")
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: t.textMuted
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("New message")
                    glyph: "\uf044"
                    tone: "primary"
                    onClicked: page.openNewMessage("", false, "")
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: page.selectedPeerId > 0

                // Chat header (hidden while a landscape keyboard leaves no room; tapping the timeline
                // dismisses the keyboard and brings it back)
                Rectangle {
                    Layout.fillWidth: true
                    visible: !page.cramped
                    Layout.preferredHeight: visible ? 60 : 0
                    color: page.splitView ? "transparent" : t.surface2
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: t.stroke
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: page.splitView ? 12 : 4
                        anchors.rightMargin: 4
                        spacing: 8

                        RoundIconButton {
                            visible: !page.splitView
                            glyph: "\uf053"
                            accessibleName: qsTr("Back to conversations")
                            onClicked: page.closeThread()
                        }
                        ChatAvatar {
                            glyph: page._peerGlyph(page.servicePeer, page.selectedIsGroup)
                            tone: page._peerTone(page.servicePeer, page.selectedIsGroup)
                            size: 38
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: page.selectedPeerLabel || qsTr("ID %1").arg(page.selectedPeerId)
                                color: t.text
                                font.bold: true
                                font.pixelSize: t.fsTitle
                                elide: Text.ElideRight
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Rectangle {
                                    Layout.preferredWidth: 7
                                    Layout.preferredHeight: 7
                                    radius: 3.5
                                    color: page.connectedDmr ? t.success : (page.connecting ? t.warning : t.textMuted)
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        var idPart = page.selectedIsGroup ? qsTr("TG %1").arg(page.selectedPeerId)
                                                                          : qsTr("ID %1").arg(page.selectedPeerId)
                                        var who = page.selectedPeerName !== "" ? page.selectedPeerName + " • " : ""
                                        var net = page.connectedDmr ? (page.appState.odtpActive ? "ODTP" : "HomeBrew")
                                                                    : (page.connecting ? qsTr("connecting") : qsTr("offline"))
                                        return who + idPart + " • " + net
                                    }
                                    color: t.textMuted
                                    font.pixelSize: t.fsCaption
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        RoundIconButton {
                            id: chatMenuButton
                            glyph: "\uf142"
                            accessibleName: qsTr("Conversation options")
                            onClicked: chatMenu.popup(chatMenuButton, chatMenuButton.width - chatMenu.width,
                                                      chatMenuButton.height)
                        }
                    }
                }

                // Offline banner
                Rectangle {
                    Layout.fillWidth: true
                    visible: !page.connectedDmr && !page.cramped
                    Layout.preferredHeight: visible ? 46 : 0
                    color: t.tint(t.warning, 0.10)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8
                        Label { text: "\uf071"; font.family: "FontAwesome"; color: t.warning }
                        Label {
                            Layout.fillWidth: true
                            text: page.connecting ? qsTr("Connecting to %1…").arg(page.appState.selectedHost)
                                  : (page.selectedIsGroup ? qsTr("Offline. Connecting links this talkgroup only.")
                                                          : qsTr("Offline. Connect to send and receive."))
                            color: t.text
                            font.pixelSize: t.fsSmall
                            elide: Text.ElideRight
                        }
                        AppButton {
                            implicitHeight: 32
                            leftPadding: 10
                            rightPadding: 10
                            font.pixelSize: t.fsSmall
                            tone: page.connecting ? "warning" : "primary"
                            busy: page.connecting
                            text: page.connecting ? qsTr("Cancel") : qsTr("Connect")
                            enabled: page.connecting || page.identityReady
                            onClicked: page.connectionButtonClicked()
                        }
                    }
                }

                // Parrot (9990/31000) is a voice echo; it never answers text
                Rectangle {
                    Layout.fillWidth: true
                    visible: page.parrotPeer && !page.cramped
                    Layout.preferredHeight: visible ? parrotRow.implicitHeight + 20 : 0
                    color: t.tint(t.accent, 0.10)
                    RowLayout {
                        id: parrotRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10
                        Label { text: "\uf05a"; font.family: "FontAwesome"; color: t.accent }
                        Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: qsTr("Parrot %1 only echoes voice and never replies to text. To test messaging, send ECHO to the BrandMeister service.").arg(page.selectedPeerId)
                            color: t.text
                            font.pixelSize: t.fsSmall
                            wrapMode: Text.WordWrap
                        }
                        AppButton {
                            implicitHeight: 34
                            leftPadding: 12
                            rightPadding: 12
                            font.pixelSize: t.fsSmall
                            tone: "primary"
                            glyph: "\uf01e"
                            text: qsTr("Echo test")
                            onClicked: page.openConversation(262993, false, "ECHO")
                        }
                    }
                }

                // Timeline
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TapHandler {
                        onTapped: function(eventPoint, button) {
                            if (composer.activeFocus) {
                                composer.focus = false
                                Qt.inputMethod.hide()
                            }
                        }
                    }

                    ListView {
                        id: messageList
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        // Short threads sit at the bottom, next to the composer. Updated imperatively
                        height: fitHeight
                        property real fitHeight: 0
                        function refit() {
                            fitHeight = Math.min(parent.height, contentHeight + topMargin + bottomMargin)
                        }
                        onContentHeightChanged: {
                            Qt.callLater(refit)
                            // Row heights are estimated until delegates exist; stay pinned while they settle
                            if (stickToEnd && !moving) Qt.callLater(jumpToEnd)
                        }
                        Component.onCompleted: refit()
                        Connections {
                            target: messageList.parent
                            function onHeightChanged() { messageList.refit() }
                        }
                        model: messageModel
                        clip: true
                        spacing: 0
                        cacheBuffer: 800
                        topMargin: 10
                        bottomMargin: 10
                        // New messages settle in rather than pop
                        add: Transition {
                            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
                            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: 220; easing.type: Easing.OutCubic }
                        }
                        displaced: Transition {
                            NumberAnimation { property: "y"; duration: 200; easing.type: Easing.OutCubic }
                        }
                        boundsBehavior: Flickable.DragOverBounds
                        ScrollIndicator.vertical: ScrollIndicator {}

                        property bool stickToEnd: true
                        readonly property real distanceFromEnd: (originY + contentHeight + bottomMargin) - (contentY + height)
                        function jumpToEnd() {
                            if (count === 0) return
                            positionViewAtEnd()
                            var target = originY + contentHeight + bottomMargin - height
                            if (target > contentY) contentY = target
                        }
                        onMovementEnded: stickToEnd = distanceFromEnd < 48
                        onHeightChanged: if (stickToEnd) Qt.callLater(jumpToEnd)

                        // Tapping the timeline dismisses the keyboard
                        TapHandler {
                            onTapped: function(eventPoint, button) {
                                if (composer.activeFocus) {
                                    composer.focus = false
                                    Qt.inputMethod.hide()
                                }
                            }
                        }

                        delegate: Item {
                            id: bubbleRow
                            required property int msgId
                            required property string direction
                            required property string body
                            required property string displayTime
                            required property int status
                            required property string formatName
                            required property int senderId
                            required property string senderCallsign
                            required property int replyMs
                            required property bool groupStart
                            required property bool groupEnd

                            readonly property bool outgoing: direction === "out"
                            readonly property real topGap: groupStart ? 12 : 3
                            readonly property real maxBubbleW: Math.min(width * 0.78, 560)
                            readonly property bool showSender: !outgoing && page.selectedIsGroup &&
                                                               groupStart && senderCallsign !== ""
                            readonly property bool showFailedHint: outgoing && status === 3 && groupEnd

                            // Horizontal swipe offset: right = reply, left = reveal details
                            property real swipeX: 0
                            readonly property real replyThreshold: 64
                            readonly property real replyProgress: Math.max(0, Math.min(1, swipeX / replyThreshold))
                            readonly property real detailsProgress: Math.max(0, Math.min(1, -swipeX / 90))

                            width: ListView.view.width
                            height: topGap + bubble.height + (showFailedHint ? 16 : 0)

                            function openActions() {
                                page.openMessageActions(bubble, msgId, body, status, outgoing, displayTime, formatName, replyMs)
                            }

                            NumberAnimation {
                                id: swipeBack
                                target: bubbleRow
                                property: "swipeX"
                                to: 0
                                duration: 260
                                easing.type: Easing.OutBack
                                easing.overshoot: 1.2
                            }

                            DragHandler {
                                id: swipeDrag
                                target: null
                                xAxis.enabled: true
                                yAxis.enabled: false
                                dragThreshold: 14
                                grabPermissions: PointerHandler.CanTakeOverFromItems
                                                 | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                                onActiveChanged: {
                                    if (active) {
                                        swipeBack.stop()
                                        return
                                    }
                                    if (bubbleRow.swipeX >= bubbleRow.replyThreshold)
                                        page.startReply(bubbleRow.body, bubbleRow.outgoing)
                                    swipeBack.start()
                                }
                                // Rubber-band resistance; reply side caps just past the trigger point
                                onTranslationChanged: {
                                    if (!active) return
                                    var dx = translation.x
                                    bubbleRow.swipeX = dx > 0 ? 96 * (1 - Math.exp(-dx / 110))
                                                              : -120 * (1 - Math.exp(dx / 140))
                                }
                            }

                            // Reply cue behind the leading edge
                            Rectangle {
                                x: 6 + bubbleRow.replyProgress * 6
                                y: bubble.y + (bubble.height - height) / 2
                                width: 32
                                height: 32
                                radius: 16
                                visible: bubbleRow.swipeX > 2
                                color: bubbleRow.replyProgress >= 1 ? t.accentStrong : t.surface3
                                opacity: bubbleRow.replyProgress
                                scale: 0.6 + 0.4 * bubbleRow.replyProgress
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Label {
                                    anchors.centerIn: parent
                                    text: "\uf112"
                                    font.family: "FontAwesome"
                                    font.pixelSize: 14
                                    color: bubbleRow.replyProgress >= 1 ? "white" : t.textMuted
                                }
                            }

                            // Details revealed on the trailing edge
                            Column {
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                y: bubble.y + (bubble.height - height) / 2
                                visible: bubbleRow.swipeX < -2
                                opacity: bubbleRow.detailsProgress
                                spacing: 1
                                Text {
                                    anchors.right: parent.right
                                    text: bubbleRow.displayTime
                                    color: t.text
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Text {
                                    anchors.right: parent.right
                                    text: bubbleRow.outgoing ? page._statusText(bubbleRow.status) : qsTr("Received")
                                    color: bubbleRow.status === 3 ? t.danger : t.textMuted
                                    font.pixelSize: 10
                                }
                                Text {
                                    anchors.right: parent.right
                                    visible: bubbleRow.formatName !== ""
                                    text: bubbleRow.formatName
                                    color: t.textMuted
                                    font.pixelSize: 10
                                }
                                Text {
                                    anchors.right: parent.right
                                    visible: bubbleRow.replyMs >= 0
                                    text: page._replyTimeText(bubbleRow.replyMs)
                                    color: t.success
                                    font.pixelSize: 10
                                }
                            }

                            Item {
                                id: swipeLayer
                                width: parent.width
                                height: parent.height
                                x: bubbleRow.swipeX

                                Rectangle {
                                    id: bubble
                                    y: bubbleRow.topGap
                                    x: bubbleRow.outgoing ? bubbleRow.width - width : 0
                                    width: Math.min(bubbleRow.maxBubbleW,
                                                    Math.max(bodyText.implicitWidth,
                                                             senderText.visible ? senderText.implicitWidth : 0,
                                                             metaRow.implicitWidth) + 24)
                                    height: bubbleCol.implicitHeight + 16
                                    radius: 18
                                    color: bubbleRow.outgoing ? (bubbleRow.status === 3 ? "#7F1D1D" : t.accentStrong)
                                                              : t.surface3
                                    scale: pressTap.pressed && !swipeDrag.active ? 0.97 : 1
                                    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                                    // Tighten the sender-side corners where consecutive bubbles join
                                    Rectangle {
                                        visible: !bubbleRow.groupStart
                                        width: 18
                                        height: Math.min(18, bubble.height / 2)
                                        color: bubble.color
                                        x: bubbleRow.outgoing ? bubble.width - width : 0
                                        y: 0
                                        radius: 4
                                    }
                                    Rectangle {
                                        visible: !bubbleRow.groupEnd
                                        width: 18
                                        height: Math.min(18, bubble.height / 2)
                                        color: bubble.color
                                        x: bubbleRow.outgoing ? bubble.width - width : 0
                                        y: bubble.height - height
                                        radius: 4
                                    }

                                    Column {
                                        id: bubbleCol
                                        x: 12
                                        y: 8
                                        width: bubble.width - 24
                                        spacing: 3

                                        Text {
                                            id: senderText
                                            visible: bubbleRow.showSender
                                            width: parent.width
                                            text: bubbleRow.senderCallsign
                                            color: t.warning
                                            font.bold: true
                                            font.pixelSize: t.fsCaption
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            id: bodyText
                                            width: parent.width
                                            text: bubbleRow.body
                                            color: bubbleRow.outgoing ? "white" : t.text
                                            font.pixelSize: t.fsBody + 1
                                            wrapMode: Text.Wrap
                                            textFormat: Text.PlainText
                                        }
                                        Row {
                                            id: metaRow
                                            anchors.right: parent.right
                                            spacing: 5
                                            Text {
                                                visible: bubbleRow.formatName !== "" && page.roomy
                                                text: bubbleRow.formatName
                                                color: bubbleRow.outgoing ? Qt.rgba(1, 1, 1, 0.65) : t.textMuted
                                                font.pixelSize: 10
                                            }
                                            Text {
                                                text: bubbleRow.displayTime
                                                color: bubbleRow.outgoing ? Qt.rgba(1, 1, 1, 0.75) : t.textMuted
                                                font.pixelSize: 10
                                            }
                                            Text {
                                                visible: bubbleRow.outgoing
                                                text: bubbleRow.status === 0 ? "\uf017"
                                                      : (bubbleRow.status === 2 ? "\uf058"
                                                         : (bubbleRow.status === 3 ? "\uf071" : "\uf00c"))
                                                font.family: "FontAwesome"
                                                font.pixelSize: 10
                                                color: bubbleRow.status === 2 ? "#A7F3D0"
                                                     : (bubbleRow.status === 3 ? "#FECACA" : Qt.rgba(1, 1, 1, 0.75))
                                            }
                                        }
                                    }

                                    TapHandler {
                                        id: pressTap
                                        acceptedButtons: Qt.LeftButton
                                        longPressThreshold: 0.42
                                        onLongPressed: bubbleRow.openActions()
                                        onTapped: function(eventPoint, button) {
                                            if (bubbleRow.status === 3 && page.smsManagerRef)
                                                page.smsManagerRef.retryMessage(bubbleRow.msgId)
                                        }
                                    }
                                    TapHandler {
                                        acceptedButtons: Qt.RightButton
                                        onTapped: function(eventPoint, button) { bubbleRow.openActions() }
                                    }
                                }

                                Text {
                                    visible: bubbleRow.showFailedHint
                                    anchors.right: bubble.right
                                    anchors.top: bubble.bottom
                                    anchors.topMargin: 2
                                    text: qsTr("Not delivered. Tap to retry")
                                    color: t.danger
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 40, 300)
                        visible: messageModel.count === 0
                            spacing: 8
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: page.servicePeer ? "\uf0ac" : "\uf075"
                                font.family: "FontAwesome"
                                font.pixelSize: 30
                                color: t.tint(t.accent, 0.55)
                            }
                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: page.servicePeer ? page.selectedPeerName
                                                       : qsTr("No messages yet. Pick a quick reply below or type your own.")
                                color: t.textMuted
                                font.pixelSize: t.fsSmall
                            }
                    }

                    // Jump to latest
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 12
                        width: 40
                        height: 40
                        radius: 20
                        readonly property bool shown: messageList.count > 0 && messageList.distanceFromEnd > 48
                        visible: opacity > 0.01
                        opacity: shown ? 1 : 0
                        scale: shown ? 1 : 0.6
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
                        color: t.surface3
                        border.color: t.stroke
                        Label {
                            anchors.centerIn: parent
                            text: "\uf078"
                            font.family: "FontAwesome"
                            color: t.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                messageList.stickToEnd = true
                                messageList.jumpToEnd()
                            }
                        }
                    }
                }

                // Quick replies
                Flickable {
                    Layout.fillWidth: true
                    visible: page.showPresets
                    Layout.preferredHeight: visible ? 44 : 0
                    contentWidth: presetRow.implicitWidth + 20
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: presetRow
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Repeater {
                            model: page.quickPresets
                            delegate: Rectangle {
                                id: presetChip
                                required property var modelData
                                height: 34
                                width: presetLabel.implicitWidth + 24
                                radius: 17
                                color: presetMouse.pressed ? t.surface3 : t.surface
                                border.color: t.tint(t.accent, 0.4)
                                Label {
                                    id: presetLabel
                                    anchors.centerIn: parent
                                    text: (presetChip.modelData.action ? "\u26A1 " : "") + presetChip.modelData.label
                                    color: t.text
                                    font.pixelSize: t.fsSmall
                                }
                                MouseArea {
                                    id: presetMouse
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.applyPreset(presetChip.modelData)
                                }
                            }
                        }
                    }
                }

                // Reply context
                Rectangle {
                    Layout.fillWidth: true
                    visible: page.replySnippet !== ""
                    Layout.preferredHeight: visible ? 44 : 0
                    color: page.splitView ? "transparent" : t.surface2
                    Rectangle {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 28
                        radius: 1.5
                        color: t.accent
                    }
                    Column {
                        x: 26
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 26 - 52
                        spacing: 1
                        Label {
                            text: page.replyToOutgoing ? qsTr("Replying to yourself") : qsTr("Replying to %1").arg(page.selectedPeerLabel)
                            color: t.accent
                            font.pixelSize: t.fsCaption
                            font.bold: true
                            width: parent.width
                            elide: Text.ElideRight
                        }
                        Label {
                            text: page.replySnippet
                            color: t.textMuted
                            font.pixelSize: t.fsSmall
                            width: parent.width
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                    RoundIconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 36
                        implicitHeight: 36
                        glyph: "\uf00d"
                        glyphSize: 14
                        tone: t.textMuted
                        accessibleName: qsTr("Cancel reply")
                        onClicked: page.cancelReply()
                    }
                }

                // Composer
                Rectangle {
                    id: composerBar
                    Layout.fillWidth: true
                    implicitHeight: composerRow.implicitHeight + 16
                    color: page.splitView ? "transparent" : t.surface2
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: t.stroke
                    }

                    RowLayout {
                        id: composerRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 6
                        anchors.rightMargin: 8
                        spacing: 6

                        RoundIconButton {
                            Layout.alignment: Qt.AlignBottom
                            glyph: "\uf0e7"
                            tone: page.presetsOpen ? t.accent : t.textMuted
                            accessibleName: qsTr("Quick replies")
                            onClicked: page.presetsOpen = !page.presetsOpen
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.alignment: Qt.AlignBottom
                            implicitHeight: Math.max(40, composerScroll.height + 4)
                            Behavior on implicitHeight { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                            radius: 20
                            color: t.surface
                            border.color: composer.activeFocus ? t.accent : t.stroke
                            border.width: composer.activeFocus ? 1.5 : 1

                            ScrollView {
                                id: composerScroll
                                anchors.left: parent.left
                                anchors.right: counter.visible ? counter.left : parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                // Grows with the draft; stays single-row-ish when a landscape keyboard leaves ~100pt
                                height: Math.min(composer.implicitHeight, page.cramped ? 60 : 112)
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                                T.TextArea {
                                    id: composer
                                    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding, 60)
                                    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding, 38)
                                    placeholderText: page.connectedDmr ? qsTr("Message") : qsTr("Message (offline)")
                                    color: t.text
                                    font.pixelSize: t.fsBody + 1
                                    wrapMode: TextEdit.Wrap
                                    topPadding: 9
                                    bottomPadding: 9
                                    leftPadding: 10
                                    rightPadding: 6
                                    selectionColor: t.tint(t.accent, 0.45)
                                    selectedTextColor: t.text

                                    Text {
                                        x: composer.leftPadding
                                        y: composer.topPadding
                                        width: composer.width - composer.leftPadding - composer.rightPadding
                                        text: composer.placeholderText
                                        font: composer.font
                                        color: t.textMuted
                                        elide: Text.ElideRight
                                        visible: composer.length === 0 && composer.preeditText.length === 0
                                    }
                                    inputMethodHints: Qt.ImhNoAutoUppercase
                                    EnterKey.type: Qt.EnterKeySend
                                    Keys.onReturnPressed: function(event) {
                                        if (event.modifiers & Qt.ShiftModifier) {
                                            event.accepted = false
                                            return
                                        }
                                        page.sendCurrent()
                                    }
                                    Keys.onEnterPressed: page.sendCurrent()
                                    onTextChanged: {
                                        if (length > 500) remove(500, length)
                                    }
                                }
                            }

                            Label {
                                id: counter
                                visible: composer.length > 400
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 11
                                text: 500 - composer.length
                                color: composer.length > 480 ? t.warning : t.textMuted
                                font.pixelSize: 10
                            }
                        }

                        // Send: a plain item so tapping it never steals focus from the keyboard
                        RoundIconButton {
                            Layout.alignment: Qt.AlignBottom
                            glyph: page.connectedDmr ? "\uf1d8" : "\uf1e6"
                            filled: true
                            tone: page.connectedDmr ? t.accentStrong : "#B45309"
                            enabled: page.connectedDmr ? composer.text.trim() !== ""
                                                       : (page.identityReady && !page.connecting)
                            accessibleName: page.connectedDmr ? qsTr("Send message") : qsTr("Connect DMR")
                            onClicked: {
                                if (page.connectedDmr) page.sendCurrent()
                                else page.connectionButtonClicked()
                            }
                        }
                    }
                }
            }
        }
    }
}
