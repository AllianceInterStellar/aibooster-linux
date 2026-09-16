import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: dnsSettings

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
                text: "DNS Settings"
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

                DnsField { label: "Remote DNS"; value: SettingsModel.remoteDns; onEdited: function(v) { SettingsModel.remoteDns = v } }
                DnsField { label: "Direct DNS"; value: SettingsModel.directDns; onEdited: function(v) { SettingsModel.directDns = v } }

                RowLayout {
                    Layout.fillWidth: true
                    height: 44

                    Text {
                        Layout.fillWidth: true
                        text: "Enable Fake DNS"
                        font.pixelSize: 14
                        color: "#D1D5DB"
                    }

                    Switch {
                        checked: SettingsModel.enableFakeDns
                        onToggled: SettingsModel.enableFakeDns = checked

                        indicator: Rectangle {
                            width: 44
                            height: 24
                            radius: 12
                            color: parent.checked ? "#7C3AED" : "#374151"

                            Rectangle {
                                x: parent.parent.checked ? parent.width - width - 3 : 3
                                y: 3
                                width: 18
                                height: 18
                                radius: 9
                                color: "#FFFFFF"
                                Behavior on x { NumberAnimation { duration: 150 } }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    component DnsField: ColumnLayout {
        property string label: ""
        property string value: ""
        signal edited(string value)

        Layout.fillWidth: true
        spacing: 6

        Text {
            text: label
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
                text: value
                color: "#E5E7EB"
                font.pixelSize: 13
                background: null
                onTextEdited: edited(text)
            }
        }
    }
}
