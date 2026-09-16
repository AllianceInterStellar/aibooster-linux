import QtQuick
import QtQuick.Controls

Item {
    id: connBtn
    width: 148
    height: 148

    property int status: 0
    property bool isConnected: status === 2
    property bool isConnecting: status === 1
    property bool isDisconnecting: status === 3
    /// Set while the account's tier is still unknown — connecting now would pick the wrong
    /// config source. `enabled` is Item's own and propagates to the MouseArea below.
    property bool checkingSubscription: false

    opacity: enabled ? 1.0 : 0.55

    signal clicked()

    Rectangle {
        id: outerRing
        anchors.centerIn: parent
        width: 148
        height: 148
        radius: 74
        color: "transparent"
        border.width: 3
        border.color: isConnected ? "#22C55E" : isConnecting ? "#FACC15" : "#7C3AED"

        RotationAnimation on rotation {
            running: connBtn.isConnecting || connBtn.isDisconnecting
            from: 0
            to: 360
            duration: 2000
            loops: Animation.Infinite
        }
    }

    SequentialAnimation {
        id: pulseAnim
        running: !connBtn.isConnecting && !connBtn.isDisconnecting
        loops: Animation.Infinite
        NumberAnimation { target: outerRing; property: "scale"; to: 1.06; duration: 1500; easing.type: Easing.InOutSine }
        NumberAnimation { target: outerRing; property: "scale"; to: 1.0; duration: 1500; easing.type: Easing.InOutSine }
    }

    Rectangle {
        id: innerCircle
        anchors.centerIn: parent
        width: 120
        height: 120
        radius: 60
        color: "#0F0A1E"
        border.width: 2
        border.color: isConnected ? "#16A34A" : isConnecting ? "#EAB308" : "#5B21B6"

        Column {
            anchors.centerIn: parent
            spacing: 4

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: isConnected ? "../icons/shield-check.svg" : "../icons/shield.svg"
                sourceSize.width: 34
                sourceSize.height: 34
                width: 34
                height: 34
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    if (connBtn.checkingSubscription)
                        return "Checking subscription…"
                    switch (connBtn.status) {
                        case 0: return "Tap to Connect"
                        case 1: return "Connecting..."
                        case 2: return "Connected"
                        case 3: return "Disconnecting..."
                    }
                }
                font.pixelSize: 10
                font.weight: Font.Medium
                color: isConnected ? "#4ADE80" : isConnecting ? "#FDE047" : "#C4B5FD"
            }
        }
    }

    Rectangle {
        id: glowEffect
        anchors.centerIn: parent
        width: 160
        height: 160
        radius: 80
        color: "transparent"
        border.width: 1
        border.color: isConnected ? "#22C55E20" : "#7C3AED20"
        visible: !isConnecting && !isDisconnecting

        SequentialAnimation on opacity {
            running: true
            loops: Animation.Infinite
            NumberAnimation { to: 0.6; duration: 2000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.2; duration: 2000; easing.type: Easing.InOutSine }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: connBtn.clicked()
    }
}
