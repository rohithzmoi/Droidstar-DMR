/*
    Copyright (C) 2026 Rohith Namboothiri

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
    title: qsTr("Nets")
    padding: 0

    property var app: null
    required property var netReminderManagerRef
    required property var appState
    required property var droidstarRef

    readonly property bool roomy: width >= 560

    Tokens { id: t }

    background: Rectangle { color: t.bg }

    property Component headerActions: Component {
        Row {
            spacing: 0
            HeaderAction {
                glyph: "\uf067"
                label: qsTr("Add Net")
                showLabel: page.roomy
                tone: t.accent
                onClicked: page.openNewNetEditor()
            }
        }
    }

    readonly property var modesModel: ["DMR", "YSF", "FCS", "REF", "DCS", "XRF", "P25", "NXDN", "M17", "IAX"]
    readonly property var modulesModel: ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                         "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"]
    readonly property var hoursModel: (function() {
        var h = []
        for (var i = 0; i < 24; i++) h.push(i.toString().padStart(2, "0"))
        return h
    })()
    readonly property var minutesModel: (function() {
        var m = []
        for (var i = 0; i < 60; i++) m.push(i.toString().padStart(2, "0"))
        return m
    })()
    readonly property var daysOfMonthModel: (function() {
        var d = []
        for (var i = 1; i <= 31; i++) d.push(String(i))
        return d
    })()
    readonly property var ordinalsModel: [qsTr("1st"), qsTr("2nd"), qsTr("3rd"), qsTr("4th"), qsTr("Last")]
    readonly property var weekdaysLongModel: [
        qsTr("Monday"), qsTr("Tuesday"), qsTr("Wednesday"),
        qsTr("Thursday"), qsTr("Friday"), qsTr("Saturday"), qsTr("Sunday")
    ]
    readonly property var weekdayChipsModel: [
        { dow: 1, label: qsTr("Mon") },
        { dow: 2, label: qsTr("Tue") },
        { dow: 3, label: qsTr("Wed") },
        { dow: 4, label: qsTr("Thu") },
        { dow: 5, label: qsTr("Fri") },
        { dow: 6, label: qsTr("Sat") },
        { dow: 7, label: qsTr("Sun") }
    ]
    readonly property var reminderPresets: [
        { mins: 0,  label: qsTr("At start") },
        { mins: 5,  label: qsTr("5m") },
        { mins: 10, label: qsTr("10m") },
        { mins: 15, label: qsTr("15m") },
        { mins: 30, label: qsTr("30m") },
        { mins: 60, label: qsTr("1h") }
    ]

    // ─────────────────────────────────────────────────────────
    // EDITOR STATE
    // ─────────────────────────────────────────────────────────
    property string editId: ""
    property string editName: ""
    property bool editEnabled: true
    property string editMode: "DMR"
    property string editHost: ""
    property string editModule: "A"
    property string editTgid: ""
    property string editHour: "19"
    property string editMinute: "00"
    property bool editUseUtc: false
    property string editRecurrence: "weekly"
    property var editDaysOfWeek: [7]
    property int editDayOfMonth: 1
    property int editMonthOrdinal: 1
    property int editMonthWeekday: 7
    property var editReminders: [30, 5]
    property string editNotes: ""

    function _hasDay(dow) {
        return editDaysOfWeek && editDaysOfWeek.indexOf(dow) >= 0
    }

    function _toggleDay(dow) {
        var arr = editDaysOfWeek ? editDaysOfWeek.slice() : []
        var idx = arr.indexOf(dow)
        if (idx >= 0) {
            if (arr.length > 1) arr.splice(idx, 1)
        } else {
            arr.push(dow)
        }
        arr.sort(function(a, b) { return a - b })
        editDaysOfWeek = arr
    }

    function _hasReminder(mins) {
        return editReminders && editReminders.indexOf(mins) >= 0
    }

    function _toggleReminder(mins) {
        var m = parseInt(mins)
        if (isNaN(m) || m < 0) return
        var arr = editReminders ? editReminders.slice() : []
        var idx = arr.indexOf(m)
        if (idx >= 0) {
            if (arr.length > 1) arr.splice(idx, 1)
        } else {
            if (arr.length >= 3) {
                if (page.app) page.app.notify(qsTr("Up to 3 reminders per net. Tap an active chip to remove it first."), "", null, "warning")
                return
            }
            arr.push(m)
        }
        arr.sort(function(a, b) { return b - a })
        editReminders = arr
    }

    function _addCustomReminder(textVal) {
        var m = parseInt((textVal || "").trim())
        if (isNaN(m) || m < 0 || m > 10080) {
            if (page.app) page.app.notify(qsTr("Enter minutes between 0 and 10080."), "", null, "warning")
            return false
        }
        if (_hasReminder(m)) return true
        if (editReminders.length >= 3) {
            if (page.app) page.app.notify(qsTr("Up to 3 reminders per net. Remove one first."), "", null, "warning")
            return false
        }
        var arr = editReminders.slice()
        arr.push(m)
        arr.sort(function(a, b) { return b - a })
        editReminders = arr
        return true
    }

    function _formatRemChip(mins) {
        if (mins === 0) return qsTr("At start")
        if (mins >= 60 && mins % 60 === 0) return qsTr("%1h before").arg(mins / 60)
        return qsTr("%1m before").arg(mins)
    }

    function openNewNetEditor() {
        var now = new Date()
        editId = ""
        editName = ""
        editEnabled = true
        editMode = appState ? appState.mode : "DMR"
        editHost = appState ? appState.selectedHost : ""
        editModule = appState ? appState.module : "A"
        editTgid = appState ? appState.dmrtgid : ""
        editHour = "19"
        editMinute = "00"
        editUseUtc = false
        editRecurrence = "weekly"
        var jsDay = now.getDay() // 0=Sun..6=Sat
        var isoDay = (jsDay === 0) ? 7 : jsDay
        editDaysOfWeek = [isoDay]
        editDayOfMonth = now.getDate()
        editMonthOrdinal = 1
        editMonthWeekday = isoDay
        editReminders = [30, 5]
        editNotes = ""
        customRemField.text = ""
        editorFlick.contentY = 0
        editorSheet.open()
    }

    function openEditNetEditor(net) {
        if (!net) return
        editId = net.id || ""
        editName = net.name || ""
        editEnabled = net.enabled !== undefined ? !!net.enabled : true
        editMode = net.mode || "DMR"
        editHost = net.host || ""
        editModule = net.module || "A"
        editTgid = net.tgid !== undefined ? String(net.tgid) : ""
        var tParts = String(net.time || "19:00").split(":")
        editHour = (tParts.length > 0 ? tParts[0] : "19").padStart(2, "0")
        editMinute = (tParts.length > 1 ? tParts[1] : "00").padStart(2, "0")
        editUseUtc = !!net.useUtc
        editRecurrence = net.recurrence || "weekly"

        var dArr = []
        if (net.daysOfWeek && net.daysOfWeek.length !== undefined) {
            for (var i = 0; i < net.daysOfWeek.length; i++) dArr.push(parseInt(net.daysOfWeek[i]))
        }
        editDaysOfWeek = dArr.length > 0 ? dArr : [7]

        editDayOfMonth = parseInt(net.dayOfMonth || 1)
        editMonthOrdinal = parseInt(net.monthWeekOrdinal || 1)
        editMonthWeekday = parseInt(net.monthWeekday || 7)

        var rArr = []
        if (net.reminders && net.reminders.length !== undefined) {
            for (var j = 0; j < net.reminders.length && rArr.length < 3; j++) rArr.push(parseInt(net.reminders[j]))
        }
        editReminders = rArr.length > 0 ? rArr : [15]
        editNotes = net.notes || ""
        customRemField.text = ""
        editorFlick.contentY = 0
        editorSheet.open()
    }

    function saveCurrentEdit() {
        var cleanName = editName.trim()
        if (cleanName === "") {
            if (page.app) page.app.notify(qsTr("Enter a name for the net."), "", null, "warning")
            return
        }
        if (!page.netReminderManagerRef) return

        var payload = {
            id: editId,
            name: cleanName,
            enabled: editEnabled,
            mode: editMode,
            host: editHost.trim(),
            module: editModule,
            tgid: editTgid.trim(),
            recurrence: editRecurrence,
            time: editHour.padStart(2, "0") + ":" + editMinute.padStart(2, "0"),
            useUtc: editUseUtc,
            daysOfWeek: editDaysOfWeek,
            dayOfMonth: editDayOfMonth,
            monthWeekOrdinal: editMonthOrdinal,
            monthWeekday: editMonthWeekday,
            reminders: editReminders,
            notes: editNotes.trim()
        }
        page.netReminderManagerRef.saveNet(payload)
        editorSheet.close()
        if (page.app) page.app.notify(qsTr("Saved net reminder \"%1\"").arg(cleanName), "", null, "success")
    }

    // ─────────────────────────────────────────────────────────
    // DELETE CONFIRMATION DIALOG
    // ─────────────────────────────────────────────────────────
    AppDialog {
        id: deleteConfirmDialog
        maxWidth: 400
        title: qsTr("Delete Net Reminder?")
        destructiveText: qsTr("Delete")
        standardButtons: Dialog.Cancel | Dialog.Discard

        property string targetId: ""
        property string targetName: ""

        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                text: qsTr("Remove \"%1\" and its scheduled notifications?").arg(deleteConfirmDialog.targetName)
                wrapMode: Text.WordWrap
                color: t.text
            }
        }

        onDiscarded: {
            if (page.netReminderManagerRef && targetId !== "") {
                page.netReminderManagerRef.deleteNet(targetId)
            }
            close()
            if (page.app) page.app.notify(qsTr("Net reminder removed"), "", null, "info")
        }
    }

    // ─────────────────────────────────────────────────────────
    // ADD / EDIT NET MODAL SHEET
    // ─────────────────────────────────────────────────────────
    Popup {
        id: editorSheet
        parent: Overlay.overlay
        modal: true
        dim: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        readonly property var win: page.Window.window
        readonly property real ow: parent ? parent.width : 400
        readonly property real oh: parent ? parent.height : 760
        readonly property bool wideSheet: ow >= 620
        readonly property real safeTop: (win && win.safeArea) ? win.safeArea.top : 0
        readonly property real safeBottom: (win && win.safeArea) ? win.safeArea.bottom : 0
        readonly property real safeLeft: (win && win.safeArea) ? win.safeArea.left : 0
        readonly property real safeRight: (win && win.safeArea) ? win.safeArea.right : 0
        readonly property real kb: Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height : 0

        width: wideSheet ? Math.min(560, ow - 40) : ow
        readonly property real centeredRoom: oh - safeTop - Math.max(kb, safeBottom) - 32
        height: Math.min(wideSheet ? 680 : oh * 0.88, wideSheet ? centeredRoom : oh - kb - safeTop - 16)
        x: (ow - width) / 2
        y: wideSheet ? safeTop + 16 + Math.max(0, (centeredRoom - height) / 2) : oh - kb - height

        Overlay.modal: Rectangle { color: "#99000000" }
        background: Rectangle {
            color: t.surface
            radius: 18
            border.color: t.stroke
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16 + (editorSheet.wideSheet ? 0 : editorSheet.safeLeft)
            anchors.rightMargin: 16 + (editorSheet.wideSheet ? 0 : editorSheet.safeRight)
            anchors.topMargin: 12
            anchors.bottomMargin: 12 + (editorSheet.wideSheet || editorSheet.kb > 0 ? 0 : editorSheet.safeBottom)
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: page.editId === "" ? qsTr("New Ham Net Reminder") : qsTr("Edit Ham Net Reminder")
                    font.pixelSize: t.fsTitle
                    font.bold: true
                    color: t.text
                    elide: Text.ElideRight
                }
                ToolButton {
                    text: "\uf00d"
                    font.family: "FontAwesome"
                    font.pixelSize: 15
                    onClicked: editorSheet.close()
                }
            }

            Flickable {
                id: editorFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: editorFormCol.implicitHeight + 16
                clip: true
                boundsBehavior: Flickable.DragOverBounds
                ScrollIndicator.vertical: ScrollIndicator {}

                ColumnLayout {
                    id: editorFormCol
                    width: editorFlick.width
                    spacing: 12

                    // Net Name
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("Net Name"); font.pixelSize: t.fsCaption; color: t.textMuted }
                        AppTextField {
                            Layout.fillWidth: true
                            text: page.editName
                            placeholderText: qsTr("e.g. Worldwide Amateur Radio Net")
                            onTextEdited: page.editName = text
                        }
                    }

                    // Schedule Time & Timezone
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("Net Start Time & Timezone"); font.pixelSize: t.fsCaption; color: t.textMuted }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            SearchableComboSheet {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 82
                                Layout.minimumWidth: 68
                                Layout.maximumWidth: 82
                                model: page.hoursModel
                                currentText: page.editHour
                                sheetTitle: qsTr("Hour (24h)")
                                searchable: false
                                onActivated: function(idx, txt) { page.editHour = txt }
                            }
                            Label { text: ":"; font.pixelSize: 18; font.bold: true; color: t.text }
                            SearchableComboSheet {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 82
                                Layout.minimumWidth: 68
                                Layout.maximumWidth: 82
                                model: page.minutesModel
                                currentText: page.editMinute
                                sheetTitle: qsTr("Minute")
                                searchable: true
                                onActivated: function(idx, txt) { page.editMinute = txt }
                            }

                            SegmentedControl {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 100
                                options: [
                                    { label: qsTr("Local"), value: false },
                                    { label: qsTr("UTC"),   value: true }
                                ]
                                currentValue: page.editUseUtc
                                onSelected: function(val) { page.editUseUtc = val }
                            }
                        }
                    }

                    // Recurrence Type
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: qsTr("Recurrence"); font.pixelSize: t.fsCaption; color: t.textMuted }

                        SegmentedControl {
                            Layout.fillWidth: true
                            readonly property bool compactLabels: editorFlick.width < 400
                            options: [
                                { label: qsTr("Daily"), value: "daily" },
                                { label: qsTr("Weekly"), value: "weekly" },
                                { label: compactLabels ? qsTr("Date") : qsTr("Month Date"), value: "monthly_date" },
                                { label: compactLabels ? qsTr("Nth Day") : qsTr("Month Day"), value: "monthly_weekday" }
                            ]
                            currentValue: page.editRecurrence
                            onSelected: function(val) { page.editRecurrence = val }
                        }

                        // Weekly multi-day picker (7 columns on roomy sheets, 4+3 grid on compact phones)
                        GridLayout {
                            Layout.fillWidth: true
                            visible: page.editRecurrence === "weekly"
                            columns: editorFlick.width >= 420 ? 7 : 4
                            columnSpacing: 6
                            rowSpacing: 6

                            Repeater {
                                model: page.weekdayChipsModel
                                delegate: ChipToggle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    checked: page.editDaysOfWeek.indexOf(modelData.dow) >= 0
                                    onClicked: page._toggleDay(modelData.dow)
                                }
                            }
                        }

                        // Monthly by Day of Month (1..31)
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.editRecurrence === "monthly_date"
                            spacing: 10
                            Label { text: qsTr("Day of month:"); font.pixelSize: t.fsBody; color: t.text }
                            SearchableComboSheet {
                                Layout.fillWidth: true
                                model: page.daysOfMonthModel
                                currentText: String(page.editDayOfMonth)
                                sheetTitle: qsTr("Select Day of Month")
                                searchable: false
                                onActivated: function(idx, txt) { page.editDayOfMonth = parseInt(txt) }
                            }
                        }

                        // Monthly by Nth Weekday (e.g. 2nd Tuesday)
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.editRecurrence === "monthly_weekday"
                            spacing: 8
                            SearchableComboSheet {
                                Layout.fillWidth: true
                                model: page.ordinalsModel
                                currentText: page.ordinalsModel[Math.max(0, Math.min(4, page.editMonthOrdinal - 1))]
                                sheetTitle: qsTr("Week of Month")
                                searchable: false
                                onActivated: function(idx) { page.editMonthOrdinal = idx + 1 }
                            }
                            SearchableComboSheet {
                                Layout.fillWidth: true
                                model: page.weekdaysLongModel
                                currentText: page.weekdaysLongModel[Math.max(0, Math.min(6, page.editMonthWeekday - 1))]
                                sheetTitle: qsTr("Day of Week")
                                searchable: false
                                onActivated: function(idx) { page.editMonthWeekday = idx + 1 }
                            }
                        }
                    }

                    // Up to 3 Reminders Configuration
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Reminders (up to 3 per net)")
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }
                            Label {
                                text: qsTr("%1 / 3 selected").arg(page.editReminders ? page.editReminders.length : 0)
                                font.pixelSize: t.fsCaption
                                color: t.accent
                            }
                        }

                        // Selected reminders badges (tap to remove)
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: page.editReminders
                                delegate: Rectangle {
                                    required property var modelData
                                    radius: 8
                                    color: t.tint(t.accent, 0.22)
                                    border.color: t.accent
                                    border.width: 1
                                    implicitHeight: 28
                                    implicitWidth: selRemRow.implicitWidth + 16
                                    Row {
                                        id: selRemRow
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Label {
                                            text: "\uf0f3"
                                            font.family: "FontAwesome"
                                            font.pixelSize: t.fsCaption
                                            color: t.accent
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            text: page._formatRemChip(modelData)
                                            font.pixelSize: t.fsSmall
                                            color: t.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Label {
                                            visible: page.editReminders.length > 1
                                            text: "\uf00d"
                                            font.family: "FontAwesome"
                                            font.pixelSize: 11
                                            color: t.textMuted
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page._toggleReminder(modelData)
                                    }
                                }
                            }
                        }

                        // Quick presets (6 columns on roomy sheets, 3x2 grid on compact phones)
                        GridLayout {
                            Layout.fillWidth: true
                            columns: editorFlick.width >= 460 ? 6 : 3
                            columnSpacing: 6
                            rowSpacing: 6

                            Repeater {
                                model: page.reminderPresets
                                delegate: ChipToggle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    checked: page.editReminders.indexOf(modelData.mins) >= 0
                                    onClicked: page._toggleReminder(modelData.mins)
                                }
                            }
                        }

                        // Custom minutes input
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            AppTextField {
                                id: customRemField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Custom minutes before (e.g. 45)")
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 0; top: 10080 }
                                onAccepted: if (page._addCustomReminder(text)) text = ""
                            }
                            AppButton {
                                text: qsTr("Add")
                                glyph: "\uf067"
                                enabled: customRemField.text.trim() !== "" && page.editReminders.length < 3
                                onClicked: if (page._addCustomReminder(customRemField.text)) customRemField.text = ""
                            }
                        }
                    }

                    // Radio Tuning Info (for 1-tap tuning)
                    Rectangle {
                        Layout.fillWidth: true
                        radius: 12
                        clip: true
                        color: t.surface2
                        border.color: t.stroke
                        border.width: 1
                        implicitHeight: radioTuneCol.implicitHeight + 20

                        ColumnLayout {
                            id: radioTuneCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: qsTr("One-Tap Tune Target (Optional)")
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: t.textMuted
                                    elide: Text.ElideRight
                                }
                                Button {
                                    text: qsTr("Use Current Radio")
                                    flat: true
                                    font.pixelSize: t.fsCaption
                                    onClicked: {
                                        if (!page.appState) return
                                        page.editMode = page.appState.mode || "DMR"
                                        page.editHost = page.appState.selectedHost || ""
                                        page.editModule = page.appState.module || "A"
                                        page.editTgid = page.appState.dmrtgid || ""
                                    }
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.columnSpan: (moduleFieldCol.visible || tgFieldCol.visible) ? 1 : 2
                                    spacing: 4
                                    Label { text: qsTr("Mode"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                    SearchableComboSheet {
                                        model: page.modesModel
                                        currentText: page.editMode
                                        sheetTitle: qsTr("Select Mode")
                                        searchable: false
                                        onActivated: function(idx, txt) { page.editMode = txt }
                                    }
                                }

                                ColumnLayout {
                                    id: moduleFieldCol
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    visible: page.editMode === "REF" || page.editMode === "XRF" || page.editMode === "DCS" || page.editMode === "M17"
                                    spacing: 4
                                    Label { text: qsTr("Module"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                    SearchableComboSheet {
                                        model: page.modulesModel
                                        currentText: page.editModule
                                        sheetTitle: qsTr("Select Module")
                                        searchable: false
                                        onActivated: function(idx, txt) { page.editModule = txt }
                                    }
                                }

                                ColumnLayout {
                                    id: tgFieldCol
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    visible: page.editMode === "DMR" || page.editMode === "P25" || page.editMode === "NXDN"
                                    spacing: 4
                                    Label { text: qsTr("Talkgroup ID"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                    AppTextField {
                                        Layout.fillWidth: true
                                        text: page.editTgid
                                        placeholderText: qsTr("e.g. 91")
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onTextEdited: page.editTgid = text
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.columnSpan: 2
                                    spacing: 4
                                    Label { text: qsTr("Host / Reflector"); font.pixelSize: t.fsCaption; color: t.textMuted }
                                    AppTextField {
                                        Layout.fillWidth: true
                                        text: page.editHost
                                        placeholderText: qsTr("e.g. BM_3102_United_States or REF001")
                                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                                        onTextEdited: page.editHost = text
                                    }
                                }
                            }
                        }
                    }

                    // Optional Notes
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: qsTr("Notes (Optional)"); font.pixelSize: t.fsCaption; color: t.textMuted }
                        AppTextField {
                            Layout.fillWidth: true
                            text: page.editNotes
                            placeholderText: qsTr("Net control, check-in topic, etc.")
                            onTextEdited: page.editNotes = text
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: editorSheet.close()
                }
                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Save Net")
                    tone: "primary"
                    glyph: "\uf00c"
                    onClicked: page.saveCurrentEdit()
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    // PAGE BODY
    // ─────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: Math.min(parent.width - 20, t.maxContentWidth)
        spacing: 10

        // Notification permission hint banner (if not yet authorized)
        Rectangle {
            Layout.fillWidth: true
            visible: !!(page.netReminderManagerRef && !page.netReminderManagerRef.notificationsAuthorized &&
                        page.netReminderManagerRef.nets && page.netReminderManagerRef.nets.length > 0)
            radius: 12
            color: t.tint(t.warning, 0.12)
            border.color: t.tint(t.warning, 0.5)
            border.width: 1
            implicitHeight: permRow.implicitHeight + 18

            RowLayout {
                id: permRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8
                Label { text: "\uf0f3"; font.family: "FontAwesome"; color: t.warning; font.pixelSize: 14 }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Allow notifications so DroidStar can alert you before nets start.")
                    font.pixelSize: t.fsSmall
                    color: t.text
                    wrapMode: Text.WordWrap
                }
                Button {
                    text: qsTr("Enable")
                    flat: true
                    onClicked: if (page.netReminderManagerRef) page.netReminderManagerRef.requestPermission()
                }
            }
        }

        // Top action bar
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                readonly property int totalNets: (page.netReminderManagerRef && page.netReminderManagerRef.nets)
                                                 ? page.netReminderManagerRef.nets.length : 0
                readonly property int activeCount: page.netReminderManagerRef ? page.netReminderManagerRef.activeNetsCount : 0
                text: totalNets === 0
                      ? qsTr("No net reminders configured")
                      : qsTr("%1 nets (%2 active)").arg(totalNets).arg(activeCount)
                font.pixelSize: t.fsSmall
                color: t.textMuted
                elide: Text.ElideRight
            }

            AppButton {
                text: page.roomy ? qsTr("Add Net Reminder") : qsTr("Add Net")
                glyph: "\uf067"
                tone: "primary"
                onClicked: page.openNewNetEditor()
            }
        }

        // Nets list
        ListView {
            id: netsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: page.netReminderManagerRef ? page.netReminderManagerRef.nets : []
            clip: true
            spacing: 10
            boundsBehavior: Flickable.DragOverBounds
            ScrollIndicator.vertical: ScrollIndicator {}

            footer: Item { width: 1; height: 20 }

            delegate: Rectangle {
                id: netCard
                required property var modelData

                width: ListView.view.width
                height: cardCol.implicitHeight + 24
                radius: 14
                clip: true
                color: t.surface2
                border.color: modelData.enabled ? t.tint(t.accent, 0.45) : t.stroke
                border.width: 1
                opacity: modelData.enabled ? 1.0 : 0.72

                ColumnLayout {
                    id: cardCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    readonly property bool wideCard: width >= 460
                    readonly property bool hasMode: !!(modelData.mode && String(modelData.mode).trim() !== "")
                    readonly property bool hasHost: !!(modelData.host && String(modelData.host).trim() !== "")
                    readonly property bool hasTg: !!(modelData.tgid && String(modelData.tgid).trim() !== "")
                    readonly property bool hasMod: !!(modelData.module && String(modelData.module).trim() !== "" &&
                                                      (modelData.mode === "REF" || modelData.mode === "XRF" ||
                                                       modelData.mode === "DCS" || modelData.mode === "M17"))
                    readonly property bool hasRadioTarget: hasMode || hasHost || hasTg || hasMod

                    // Header row: Name + Countdown pill + Switch
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 40
                            Layout.preferredWidth: 1
                            text: modelData.name || qsTr("Ham Net")
                            font.pixelSize: 16
                            font.bold: true
                            color: t.text
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                            radius: 8
                            clip: true
                            color: modelData.enabled ? t.tint(t.success, 0.15) : t.surface3
                            border.color: modelData.enabled ? t.tint(t.success, 0.5) : t.stroke
                            border.width: 1
                            implicitHeight: 22
                            implicitWidth: Math.ceil(cdLabel.implicitWidth) + 12
                            Layout.fillWidth: true
                            Layout.preferredWidth: implicitWidth
                            Layout.minimumWidth: Math.min(implicitWidth, 76)
                            Layout.maximumWidth: Math.min(implicitWidth, Math.max(110, cardCol.width * 0.54))
                            Label {
                                id: cdLabel
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.countdownText || ""
                                font.pixelSize: t.fsCaption
                                font.bold: modelData.enabled
                                color: modelData.enabled ? t.success : t.textMuted
                                elide: Text.ElideRight
                            }
                        }

                        Switch {
                            id: netEnableSwitch
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: -10
                            Layout.preferredWidth: implicitWidth
                            Layout.minimumWidth: implicitWidth
                            Layout.maximumWidth: implicitWidth
                            checked: !!modelData.enabled
                            onToggled: if (page.netReminderManagerRef) page.netReminderManagerRef.setNetEnabled(modelData.id, checked)
                        }
                    }

                    // Schedule & Reminders summary (side-by-side when it fits, stacked when long or on narrow screens)
                    GridLayout {
                        id: schedRemGrid
                        Layout.fillWidth: true
                        readonly property real combinedIdealWidth: schedTextLabel.implicitWidth + remTextLabel.implicitWidth + 52
                        readonly property bool sideBySide: cardCol.width >= combinedIdealWidth
                        columns: sideBySide ? 2 : 1
                        columnSpacing: 10
                        rowSpacing: 4

                        RowLayout {
                            id: schedRow
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 6

                            Label {
                                text: "\uf017"
                                font.family: "FontAwesome"
                                font.pixelSize: t.fsCaption
                                color: t.textMuted
                            }

                            Label {
                                id: schedTextLabel
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: modelData.scheduleSummary || ""
                                font.pixelSize: t.fsSmall
                                color: t.text
                                elide: Text.ElideRight
                            }
                        }

                        RowLayout {
                            id: remRow
                            Layout.fillWidth: !schedRemGrid.sideBySide
                            Layout.minimumWidth: 0
                            Layout.alignment: schedRemGrid.sideBySide ? Qt.AlignRight : Qt.AlignLeft
                            spacing: 5

                            Label {
                                text: "\uf0f3"
                                font.family: "FontAwesome"
                                font.pixelSize: t.fsCaption - 1
                                color: t.accent
                            }

                            Label {
                                id: remTextLabel
                                Layout.fillWidth: !schedRemGrid.sideBySide
                                Layout.minimumWidth: 0
                                Layout.maximumWidth: schedRemGrid.sideBySide
                                                     ? Math.max(90, cardCol.width * 0.54)
                                                     : Math.max(90, cardCol.width - 22)
                                text: modelData.remindersSummary || ""
                                font.pixelSize: t.fsCaption
                                color: t.accent
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // Radio pills + Action buttons (responsive: wraps actions onto their own row when tight)
                    GridLayout {
                        id: radioActionsGrid
                        Layout.fillWidth: true
                        readonly property real badgesIdealW: (cardCol.hasMode ? (modeBadge.implicitWidth + 16) : 0) +
                                                             (cardCol.hasHost ? (Math.min(180, hostBadge.implicitWidth + 12) + 6) : 0) +
                                                             (cardCol.hasTg ? (tgBadge.implicitWidth + 16) : 0) +
                                                             (cardCol.hasMod ? (modBadge.implicitWidth + 16) : 0)
                        readonly property real actionsIdealW: (cardCol.hasMode ? (tuneNetBtn.implicitWidth + 4) : 0) + 72
                        readonly property bool singleRow: !cardCol.hasRadioTarget ||
                                                          ((badgesIdealW + actionsIdealW + 10) <= cardCol.width)
                        columns: singleRow ? 2 : 1
                        columnSpacing: 8
                        rowSpacing: 8

                        // Left/Top: Radio Target Badges
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            visible: cardCol.hasRadioTarget
                            spacing: 6

                            Rectangle {
                                id: modePill
                                visible: cardCol.hasMode
                                radius: 6
                                color: t.tint(t.accent, 0.16)
                                implicitHeight: 22
                                implicitWidth: modeBadge.implicitWidth + 10
                                Layout.preferredWidth: implicitWidth
                                Layout.minimumWidth: implicitWidth
                                Layout.maximumWidth: implicitWidth
                                Label {
                                    id: modeBadge
                                    anchors.centerIn: parent
                                    text: modelData.mode || ""
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: t.accent
                                }
                            }

                            Rectangle {
                                id: hostPill
                                visible: cardCol.hasHost
                                radius: 6
                                clip: true
                                color: t.surface3
                                implicitHeight: 22
                                implicitWidth: Math.ceil(hostBadge.implicitWidth) + 12
                                Layout.fillWidth: true
                                Layout.minimumWidth: 44
                                Layout.preferredWidth: implicitWidth
                                Layout.maximumWidth: Math.min(240, implicitWidth)
                                Label {
                                    id: hostBadge
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    verticalAlignment: Text.AlignVCenter
                                    text: modelData.host || ""
                                    font.pixelSize: t.fsCaption
                                    color: t.text
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                id: tgPill
                                visible: cardCol.hasTg
                                radius: 6
                                clip: true
                                color: t.tint(t.warning, 0.16)
                                implicitHeight: 22
                                implicitWidth: Math.ceil(tgBadge.implicitWidth) + 10
                                Layout.fillWidth: true
                                Layout.preferredWidth: implicitWidth
                                Layout.minimumWidth: Math.min(implicitWidth, 48)
                                Layout.maximumWidth: Math.min(120, implicitWidth)
                                Label {
                                    id: tgBadge
                                    anchors.fill: parent
                                    anchors.leftMargin: 5
                                    anchors.rightMargin: 5
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    text: "TG " + modelData.tgid
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: t.warning
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                id: modPill
                                visible: cardCol.hasMod
                                radius: 6
                                color: t.tint(t.warning, 0.16)
                                implicitHeight: 22
                                implicitWidth: modBadge.implicitWidth + 10
                                Layout.preferredWidth: implicitWidth
                                Layout.minimumWidth: implicitWidth
                                Layout.maximumWidth: implicitWidth
                                Label {
                                    id: modBadge
                                    anchors.centerIn: parent
                                    text: qsTr("Mod ") + modelData.module
                                    font.pixelSize: t.fsCaption
                                    font.bold: true
                                    color: t.warning
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                            }
                        }

                        // Right/Bottom: Tune + Edit + Delete Actions
                        RowLayout {
                            id: actionsRow
                            Layout.fillWidth: !radioActionsGrid.singleRow || !cardCol.hasRadioTarget
                            Layout.alignment: Qt.AlignRight
                            spacing: 4

                            Item {
                                visible: !radioActionsGrid.singleRow || !cardCol.hasRadioTarget
                                Layout.fillWidth: true
                            }

                            AppButton {
                                id: tuneNetBtn
                                visible: cardCol.hasMode
                                text: qsTr("Tune")
                                glyph: "\uf1e6"
                                tone: "primary"
                                implicitHeight: 34
                                leftPadding: 12
                                rightPadding: 12
                                onClicked: if (page.app) page.app.tuneToNet(modelData)
                            }

                            ToolButton {
                                implicitWidth: 34
                                implicitHeight: 34
                                padding: 0
                                text: "\uf040"
                                font.family: "FontAwesome"
                                font.pixelSize: 15
                                Accessible.name: qsTr("Edit %1").arg(modelData.name)
                                onClicked: page.openEditNetEditor(modelData)
                            }

                            ToolButton {
                                implicitWidth: 34
                                implicitHeight: 34
                                padding: 0
                                text: "\uf1f8"
                                font.family: "FontAwesome"
                                font.pixelSize: 15
                                Accessible.name: qsTr("Delete %1").arg(modelData.name)
                                onClicked: {
                                    deleteConfirmDialog.targetId = modelData.id
                                    deleteConfirmDialog.targetName = modelData.name
                                    deleteConfirmDialog.open()
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !!(modelData.notes && modelData.notes !== "")
                        text: modelData.notes || ""
                        font.pixelSize: t.fsCaption
                        color: t.textMuted
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 320)
                visible: netsList.count === 0
                spacing: 10
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "\uf0f3"
                    font.family: "FontAwesome"
                    font.pixelSize: 38
                    color: t.textMuted
                    opacity: 0.6
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Never miss a Ham Net")
                    font.pixelSize: t.fsTitle
                    font.bold: true
                    color: t.text
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Schedule daily, weekly, or monthly nets with up to 3 local push reminders per net and one-tap tuning.")
                    font.pixelSize: t.fsSmall
                    color: t.textMuted
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
