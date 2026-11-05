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
import QtQuick.Shapes

// Siri-style running red light around the window edge while transmitting, over a faint glass rim.
// Cost: ring geometry is built once per size; each frame only two shared gradient angles change
// (no re-triangulation, no offscreen layers or blur). The animation stops when hidden or backgrounded.
Item {
    id: root

    property bool active: false
    property bool animate: true
    property real radius: 0

    enabled: false
    opacity: active ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

    property real angle: 0
    NumberAnimation on angle {
        from: 0; to: 360
        duration: 2800
        loops: Animation.Infinite
        running: root.visible && root.animate
    }

    // Rounded-rect outline as SVG; two of them with odd-even fill make a ring.
    function _rr(x, y, w, h, r) {
        r = Math.max(0, Math.min(r, w / 2, h / 2))
        return "M" + (x + r) + " " + y + " H" + (x + w - r)
             + " A" + r + " " + r + " 0 0 1 " + (x + w) + " " + (y + r) + " V" + (y + h - r)
             + " A" + r + " " + r + " 0 0 1 " + (x + w - r) + " " + (y + h) + " H" + (x + r)
             + " A" + r + " " + r + " 0 0 1 " + x + " " + (y + h - r) + " V" + (y + r)
             + " A" + r + " " + r + " 0 0 1 " + (x + r) + " " + y + " Z "
    }
    function _ring(th) {
        if (width <= 2 * th || height <= 2 * th) return ""
        return _rr(0, 0, width, height, radius) + _rr(th, th, width - 2 * th, height - 2 * th, Math.max(0, radius - th))
    }

    // Core line: two bright sparks chase each other over deep red / coral / ember.
    ConicalGradient {
        id: coreGrad
        centerX: root.width / 2
        centerY: root.height / 2
        angle: root.angle
        GradientStop { position: 0.00; color: "#E0102F" }
        GradientStop { position: 0.10; color: "#FF4D6D" }
        GradientStop { position: 0.16; color: "#FFE3EA" }
        GradientStop { position: 0.22; color: "#FF4D6D" }
        GradientStop { position: 0.38; color: "#C40A2A" }
        GradientStop { position: 0.52; color: "#FF7A1A" }
        GradientStop { position: 0.60; color: "#FF2D55" }
        GradientStop { position: 0.66; color: "#FFD6C2" }
        GradientStop { position: 0.72; color: "#FF2D55" }
        GradientStop { position: 0.88; color: "#D0103A" }
        GradientStop { position: 1.00; color: "#E0102F" }
    }
    // Glow: same motion, fully saturated so the faint layers read red, not grey.
    ConicalGradient {
        id: glowGrad
        centerX: root.width / 2
        centerY: root.height / 2
        angle: root.angle
        GradientStop { position: 0.00; color: "#FF1238" }
        GradientStop { position: 0.16; color: "#FF4D7A" }
        GradientStop { position: 0.38; color: "#E00A30" }
        GradientStop { position: 0.52; color: "#FF6A1A" }
        GradientStop { position: 0.66; color: "#FF3D6E" }
        GradientStop { position: 0.88; color: "#F0103A" }
        GradientStop { position: 1.00; color: "#FF1238" }
    }

    component Band: Shape {
        id: band
        property real thickness: 4
        property ShapeGradient gradient: glowGrad
        anchors.fill: parent
        ShapePath {
            strokeWidth: -1
            fillRule: ShapePath.OddEvenFill
            fillGradient: band.gradient
            PathSvg { path: root._ring(band.thickness) }
        }
    }

    // Stacked bands fake a soft glow falloff without a blur pass.
    Band { thickness: 24; opacity: 0.045 }
    Band { thickness: 18; opacity: 0.065 }
    Band { thickness: 13; opacity: 0.09 }
    Band { thickness: 9;  opacity: 0.13 }
    Band { thickness: 6;  opacity: 0.20 }
    Band { thickness: 4;  opacity: 0.32 }
    Band { thickness: 2.5; opacity: 1.0; gradient: coreGrad }

    // Glass rim: a faint hairline highlight just inside the light.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2.5
        radius: Math.max(0, root.radius - 2.5)
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 0.9, 0.93, 0.18)
    }
}
