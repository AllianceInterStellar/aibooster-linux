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

        // Upstream attribution. GPL-3.0 section 7 requires the engine's authors be credited,
        // the licence be reachable, and modification be stated — so this is not decoration
        // and must not be trimmed to make the screen tidier. It replaced a bare
        // a one-line credit that named the upstream project but met none of those obligations.
        Item { Layout.preferredHeight: 8 }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Open source"
            font.pixelSize: 12
            font.weight: Font.Medium
            color: "#6B7280"
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 420
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "This client is GPL-3.0. Its tunnelling engine is built from hiddify-core "
                  + "and sing-box, rebuilt and renamed by us; the engine itself is unmodified, "
                  + "and it runs as a separate process rather than being linked into this app."
            font.pixelSize: 11
            lineHeight: 1.25
            color: "#4B5563"
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 14

            LegalLink {
                text: "GPL-3.0"
                url: "https://www.gnu.org/licenses/gpl-3.0.html"
                font.pixelSize: 11
            }
            LegalLink {
                text: "hiddify-core"
                url: "https://github.com/hiddify/hiddify-core"
                font.pixelSize: 11
            }
            LegalLink {
                text: "sing-box"
                url: "https://github.com/SagerNet/sing-box"
                font.pixelSize: 11
            }
            LegalLink {
                text: "This client"
                url: "https://github.com/AllianceInterStellar/aibooster-linux"
                font.pixelSize: 11
            }
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
