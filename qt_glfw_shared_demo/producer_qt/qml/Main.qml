import QtQuick 2.15

Rectangle {
    id: root
    width: 800
    height: 600

    // Dark navy gradient background
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#0d1b2a" }
        GradientStop { position: 1.0; color: "#1b263b" }
    }

    // ── Title ────────────────────────────────────────────────────────────────
    Text {
        id: titleText
        anchors {
            top: parent.top
            horizontalCenter: parent.horizontalCenter
            topMargin: 24
        }
        text: "Qt QML Offscreen"
        color: "#e94560"
        font { pixelSize: 30; bold: true; family: "Sans" }
    }

    // ── Spinning border rectangle ─────────────────────────────────────────────
    Rectangle {
        id: spinner
        width: 130; height: 130
        radius: 10
        color: "transparent"
        border { color: "#e94560"; width: 4 }
        anchors {
            horizontalCenter: parent.horizontalCenter
            verticalCenter:   parent.verticalCenter
            verticalCenterOffset: -30
        }

        RotationAnimator on rotation {
            from: 0; to: 360
            duration: 2400
            loops: Animation.Infinite
            running: true
        }

        // Inner glowing dot
        Rectangle {
            anchors.centerIn: parent
            width: 22; height: 22; radius: 11
            color: "#e94560"
        }
    }

    // ── Bouncing ball ────────────────────────────────────────────────────────
    Rectangle {
        id: ball
        width: 28; height: 28; radius: 14
        color: "#00b4d8"

        SequentialAnimation on x {
            loops: Animation.Infinite
            NumberAnimation { from: 60;             to: root.width - 90; duration: 1400; easing.type: Easing.InOutSine }
            NumberAnimation { from: root.width - 90; to: 60;             duration: 1400; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on y {
            loops: Animation.Infinite
            NumberAnimation { from: root.height - 160; to: root.height - 90; duration: 600; easing.type: Easing.InBounce }
            NumberAnimation { from: root.height - 90;  to: root.height - 160; duration: 600; easing.type: Easing.OutBounce }
        }
    }

    // ── Frame counter / clock ────────────────────────────────────────────────
    Text {
        id: clockLabel
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 22
        }
        color: "#90e0ef"
        font { pixelSize: 20; family: "Monospace" }

        Timer {
            running: true
            repeat:  true
            interval: 100
            onTriggered: clockLabel.text = Qt.formatDateTime(new Date(), "hh:mm:ss.zzz")
        }
    }

    // ── Subtle frame-rate indicator ───────────────────────────────────────────
    Rectangle {
        anchors {
            left: parent.left
            bottom: parent.bottom
            leftMargin: 16
            bottomMargin: 22
        }
        width: 10; height: 10; radius: 5
        color: "#e94560"

        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.2; duration: 500 }
            NumberAnimation { from: 0.2; to: 1.0; duration: 500 }
        }
    }
}
