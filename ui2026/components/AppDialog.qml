/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

import "../theme"

// Modal dialog in the app's card style. Stays inside the safe area and above the keyboard.
Dialog {
    id: control

    property real maxWidth: 440

    Tokens { id: t }

    readonly property var _win: parent ? parent.Window.window : null
    readonly property var _safe: (_win && _win.safeArea) ? _win.safeArea : null
    readonly property real safeTop: _safe ? _safe.top : 0
    readonly property real safeBottom: _safe ? _safe.bottom : 0
    readonly property real safeLeft: _safe ? _safe.left : 0
    readonly property real safeRight: _safe ? _safe.right : 0
    readonly property real keyboardHeight: {
        if (Qt.platform.os === "ios" && _win && _win.smsManagerRef && _win.smsManagerRef.keyboardHeight !== undefined)
            return _win.smsManagerRef.keyboardHeight
        return Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle.height : 0
    }
    readonly property real parentWidth: parent ? parent.width : 400
    readonly property real parentHeight: parent ? parent.height : 700
    // Height left for the dialog between the top inset and the keyboard (or bottom inset)
    readonly property real availableDialogHeight: parentHeight - safeTop - Math.max(keyboardHeight, safeBottom) - 24

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Dialogs that set their own height turn this off
    property bool fitHeightToContent: true

    function fitHeight() {
        if (!fitHeightToContent) return
        var h = Math.max(120, Math.min(implicitHeight, availableDialogHeight))
        if (Math.abs(height - h) > 0.5) height = h
    }
    // Applied after changes settle; binding height to implicitHeight re-enters the popup's
    onImplicitHeightChanged: Qt.callLater(fitHeight)
    onAvailableDialogHeightChanged: Qt.callLater(fitHeight)

    // Width is applied the same way: the overlay parent attaches after creation,
    function fitWidth() {
        var w = parentWidth > 0 ? Math.max(260, Math.min(parentWidth - safeLeft - safeRight - 32, maxWidth))
                                : maxWidth
        if (Math.abs(width - w) > 0.5) width = w
    }
    onParentWidthChanged: Qt.callLater(fitWidth)
    onSafeLeftChanged: Qt.callLater(fitWidth)
    onSafeRightChanged: Qt.callLater(fitWidth)
    onMaxWidthChanged: Qt.callLater(fitWidth)
    onAboutToShow: {
        fitWidth()
        fitHeight()
    }

    width: maxWidth
    x: safeLeft + (parentWidth - safeLeft - safeRight - width) / 2
    y: Math.max(safeTop + 12, safeTop + (parentHeight - safeTop - Math.max(keyboardHeight, safeBottom) - height) / 2)

    leftPadding: 20
    rightPadding: 20
    topPadding: 6
    bottomPadding: 6

    Overlay.modal: Rectangle { color: "#99000000" }

    background: Rectangle {
        color: t.surface
        radius: 18
        border.color: t.stroke
        border.width: 1
    }

    header: Label {
        visible: control.title !== ""
        text: control.title
        leftPadding: 20
        rightPadding: 20
        topPadding: 18
        bottomPadding: 8
        font.pixelSize: 18
        font.bold: true
        color: t.text
        // Elided rather than wrapped: a wrapping header re-enters the popup's implicit height
        elide: Text.ElideRight
    }

    // Button captions for the standard buttons; set per dialog for clearer wording
    property string acceptText: (standardButtons & Dialog.Save) ? qsTr("Save") : qsTr("OK")
    property string rejectText: (standardButtons & Dialog.Close) ? qsTr("Close") : qsTr("Cancel")
    property string destructiveText: qsTr("Discard")

    readonly property var _buttonSpecs: {
        var specs = []
        var sb = standardButtons
        if (sb & (Dialog.Cancel | Dialog.Close | Dialog.No))
            specs.push({ text: rejectText, tone: "neutral", role: "reject" })
        if (sb & Dialog.Discard)
            specs.push({ text: destructiveText, tone: "danger", role: "discard" })
        if (sb & (Dialog.Ok | Dialog.Save | Dialog.Yes | Dialog.Apply))
            specs.push({ text: acceptText, tone: "primary", role: "accept" })
        return specs
    }

    function _trigger(role) {
        if (role === "accept") control.accept()
        else if (role === "discard") control.discarded()
        else control.reject()
    }

    // App-styled footer built from standardButtons (replaces the style's DialogButtonBox)
    footer: Item {
        visible: control._buttonSpecs.length > 0
        implicitHeight: visible ? footerRow.implicitHeight + 22 : 0
        implicitWidth: footerRow.implicitWidth + 32

        RowLayout {
            id: footerRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.top: parent.top
            anchors.topMargin: 8
            spacing: 8

            Item { Layout.fillWidth: true; visible: !control._stackButtons }

            Repeater {
                model: control._buttonSpecs
                delegate: AppButton {
                    required property var modelData
                    Layout.fillWidth: control._stackButtons
                    text: modelData.text
                    tone: modelData.tone
                    onClicked: control._trigger(modelData.role)
                }
            }
        }
    }
    // Narrow dialogs share the row equally instead of right-aligning
    readonly property bool _stackButtons: width < 340

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
    }
}
