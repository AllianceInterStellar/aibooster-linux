import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: inboundSettings

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: backHover.containsMouse ? "#1A1640" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "‹"
                    font.pixelSize: 24
                    color: "#A78BFA"
                }

                MouseArea {
                    id: backHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settingsScreen.subPage = -1
                }
            }

            Text {
                text: "Inbound Settings"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: "#E9D5FF"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: "#12102B"
            border.width: 1
            border.color: "#1A1640"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                PortField {
                    label: "Mixed Port"
                    value: SettingsModel.mixedPort
                    onEdited: function(v) { SettingsModel.mixedPort = v }
                }

                PortField {
                    label: "Local DNS Port"
                    value: SettingsModel.localDnsPort
                    onEdited: function(v) { SettingsModel.localDnsPort = v }
                }

                Text {
                    text: "Connection Test URL"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#A78BFA"
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    radius: 8
                    color: "#1A1640"
                    border.width: 1
                    border.color: "#2D2660"

                    TextField {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        text: SettingsModel.connectionTestUrl
                        color: "#E5E7EB"
                        font.pixelSize: 13
                        background: null
                        onTextEdited: SettingsModel.connectionTestUrl = text
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    component PortField: ColumnLayout {
        property string label: ""
        property int value: 0
        signal edited(int value)

        Layout.fillWidth: true
        spacing: 6

        Text {
            text: label
            font.pixelSize: 13
            font.weight: Font.Medium
            color: "#A78BFA"
        }

        Rectangle {
            Layout.preferredWidth: 140
            height: 40
            radius: 8
            color: "#1A1640"
            border.width: 1
            border.color: "#2D2660"

            TextField {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                text: value.toString()
                color: "#E5E7EB"
                font.pixelSize: 13
                background: null
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                onTextEdited: edited(parseInt(text) || 0)
            }
        }
    }
}
