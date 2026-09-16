import QtQuick
import QtQuick.Layouts

Item {
    id: aboutScreen

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1E1B4B" }
            GradientStop { position: 1.0; color: "#080620" }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "✦"
            font.pixelSize: 60
            color: "#A78BFA"
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "AiBooster VPN"
            font.pixelSize: 28
            font.weight: Font.Bold
            color: "#E9D5FF"
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Version " + Application.version
            font.pixelSize: 14
            color: "#9CA3AF"
        }

        Item { Layout.preferredHeight: 16 }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 320
            height: infoCol.implicitHeight + 32
            radius: 16
            color: "#12102B"
            border.width: 1
            border.color: "#1A1640"

            ColumnLayout {
                id: infoCol
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                InfoRow { label: "Platform"; value: Qt.platform.os }
                InfoRow { label: "Qt Version"; value: "6.x" }
                InfoRow { label: "Build"; value: "QML + C++" }
                InfoRow { label: "Engine"; value: "Sing-Box" }
                InfoRow { label: "License"; value: "GPLv3" }
            }
        }

        Item { Layout.preferredHeight: 16 }

        // The app takes recurring payments and collects an email address, so the policies
        // governing both have to be reachable from inside the app, not only from the website.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20

            LegalLink {
                text: "Privacy Policy"
                url: "https://allianceinterstellar.com/en/legal/privacy"
            }

            LegalLink {
                text: "Terms of Service"
                url: "https://allianceinterstellar.com/en/legal/terms"
            }
        }

        Item { Layout.preferredHeight: 4 }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Built with ❤️ for privacy"
            font.pixelSize: 13
            color: "#6B7280"
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Based on Hiddify"
            font.pixelSize: 12
            color: "#4B5563"
        }
    }

    component LegalLink: Text {
        id: legalLink

        property string url: ""

        font.pixelSize: 13
        font.weight: Font.Medium
        color: legalLinkArea.containsMouse ? "#C4B5FD" : "#A78BFA"

        MouseArea {
            id: legalLinkArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Qt.openUrlExternally(legalLink.url)
        }
    }

    component InfoRow: RowLayout {
        property string label: ""
        property string value: ""

        Layout.fillWidth: true

        Text {
            Layout.fillWidth: true
            text: label
            font.pixelSize: 13
            color: "#9CA3AF"
        }

        Text {
            text: value
            font.pixelSize: 13
            font.weight: Font.Medium
            color: "#E5E7EB"
        }
    }
}
