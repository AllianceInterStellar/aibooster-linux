import QtQuick
import QtQuick.Layouts
import AiBooster.Models
import "../components" as C

Item {
    id: homeScreen

    C.SpaceBackground {
        connected: ConnectionModel.status === 2
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 20

        // Everything the user has to be told about the session: a failed connect, a paying
        // customer routed over free servers, or a subscription that only landed after the
        // session started. All of them come with a one-click way out.
        Rectangle {
            id: noticeBanner

            // Held in ConnectionModel, not here: this screen is unloaded on every tab switch, and
            // a notice raised while the user was elsewhere used to vanish with it.
            readonly property string message: ConnectionModel.sessionNotice
            readonly property bool isError: ConnectionModel.sessionNoticeIsError

            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(560, homeScreen.width - 64)
            Layout.preferredHeight: bannerRow.implicitHeight + 24
            visible: message !== ""
            radius: 12
            color: isError ? "#371520" : "#2A1B4D"
            border.width: 1
            border.color: isError ? "#DC2626" : "#7C3AED"

            // Anchored left/right/top rather than filled: the wrapping message needs a width
            // that does not depend on the height it is about to determine.
            RowLayout {
                id: bannerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 10

                Text {
                    Layout.alignment: Qt.AlignTop
                    text: "⚠"
                    font.pixelSize: 15
                    color: noticeBanner.isError ? "#FCA5A5" : "#FDE68A"
                }

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: noticeBanner.message
                    font.pixelSize: 12
                    color: noticeBanner.isError ? "#FECACA" : "#E9D5FF"
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 74
                    Layout.preferredHeight: 28
                    radius: 14
                    color: retryArea.containsMouse ? "#7C3AED" : "transparent"
                    border.color: "#7C3AED"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Retry"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: retryArea.containsMouse ? "#FFFFFF" : "#C4B5FD"
                    }

                    MouseArea {
                        id: retryArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ConnectionModel.retryConnection()
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: "✕"
                    font.pixelSize: 14
                    color: dismissArea.containsMouse ? "#E9D5FF" : "#9CA3AF"

                    MouseArea {
                        id: dismissArea
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ConnectionModel.dismissSessionNotice()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        C.ConnectionButton {
            Layout.alignment: Qt.AlignHCenter
            status: ConnectionModel.status
            // The subscription answers 1-3 s after launch; connecting before it does would
            // pick the free pool for an account that is actually premium.
            checkingSubscription: AccountManager.busy && ConnectionModel.status === 0
            enabled: !checkingSubscription
            onClicked: ConnectionModel.toggleConnection()
        }

        Item { Layout.preferredHeight: 12 }

        C.StatusBadge {
            Layout.alignment: Qt.AlignHCenter
            status: {
                switch (ConnectionModel.status) {
                    case 0: return "disconnected"
                    case 1: return "connecting"
                    case 2: return "connected"
                    case 3: return "disconnecting"
                }
            }
        }

        Item { Layout.preferredHeight: 8 }

        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: 40
            visible: ConnectionModel.status === 2

            Column {
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "↑ Upload"
                    font.pixelSize: 11
                    color: "#9CA3AF"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ConnectionModel.uploadSpeed
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    color: "#C4B5FD"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ConnectionModel.uploadTotal
                    font.pixelSize: 10
                    color: "#6B7280"
                }
            }

            Column {
                spacing: 2
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "↓ Download"
                    font.pixelSize: 11
                    color: "#9CA3AF"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ConnectionModel.downloadSpeed
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    color: "#C4B5FD"
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ConnectionModel.downloadTotal
                    font.pixelSize: 10
                    color: "#6B7280"
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: ConnectionModel.status === 2
            text: "IP: " + ConnectionModel.ipAddress
            font.pixelSize: 12
            color: "#6B7280"
        }

        Item { Layout.preferredHeight: 8 }

        C.ActiveProxyCard {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(400, homeScreen.width - 64)
            proxyName: ConnectionModel.activeProxyName
            proxyType: ConnectionModel.activeProxyType
            countryCode: ConnectionModel.activeProxyCountry
            delay: -1
        }

        Item { Layout.fillHeight: true }
    }
}
