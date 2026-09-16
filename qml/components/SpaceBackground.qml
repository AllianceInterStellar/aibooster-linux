import QtQuick

Item {
    id: bg
    anchors.fill: parent

    property bool connected: false

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#4C1D95" }
            GradientStop { position: 0.3; color: "#1E1B4B" }
            GradientStop { position: 0.7; color: "#0F0A1E" }
            GradientStop { position: 1.0; color: "#000000" }
        }
    }

    Canvas {
        id: starsCanvas
        anchors.fill: parent
        property var stars: []
        property real twinkleValue: 0

        Component.onCompleted: {
            for (var i = 0; i < 120; i++) {
                stars.push({
                    x: Math.random() * width,
                    y: Math.random() * height,
                    r: Math.random() * 1.5 + 0.3,
                    opacity: Math.random() * 0.7 + 0.3,
                    phase: Math.random() * Math.PI * 2
                })
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (var i = 0; i < stars.length; i++) {
                var s = stars[i]
                var twinkle = 0.5 + 0.5 * Math.sin(s.phase + starsCanvas.twinkleValue * Math.PI * 2)
                ctx.globalAlpha = s.opacity * twinkle
                ctx.fillStyle = "#FFFFFF"
                ctx.beginPath()
                ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2)
                ctx.fill()
            }
        }

        NumberAnimation {
            id: twinkleAnim
            target: starsCanvas
            property: "twinkleValue"
            from: 0
            to: 1
            duration: 4000
            loops: Animation.Infinite
            running: true
        }

        Timer {
            interval: 80
            running: true
            repeat: true
            onTriggered: starsCanvas.requestPaint()
        }
    }

    Canvas {
        id: particlesCanvas
        anchors.fill: parent
        visible: bg.connected
        property var particles: []

        Component.onCompleted: {
            for (var i = 0; i < 30; i++) {
                particles.push({
                    x: Math.random() * width,
                    y: Math.random() * height,
                    size: Math.random() * 3 + 1,
                    speedY: -(Math.random() * 0.8 + 0.2),
                    opacity: Math.random() * 0.5 + 0.2
                })
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            for (var i = 0; i < particles.length; i++) {
                var p = particles[i]
                p.y += p.speedY
                if (p.y < -10) {
                    p.y = height + 10
                    p.x = Math.random() * width
                }
                ctx.globalAlpha = p.opacity
                ctx.fillStyle = "#A78BFA"
                ctx.beginPath()
                ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2)
                ctx.fill()
            }
        }

        Timer {
            interval: 50
            running: bg.connected
            repeat: true
            onTriggered: particlesCanvas.requestPaint()
        }
    }
}
