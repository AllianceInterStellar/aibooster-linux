import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: warpSettings

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
                text: "WARP Settings"
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

                RowLayout {
                    Layout.fillWidth: true
                    height: 44

                    Column {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "Enable WARP"
                            font.pixelSize: 14
                            color: "#D1D5DB"
                        }

                        Text {
                            text: "Cloudflare WARP for enhanced privacy"
                            font.pixelSize: 11
                            color: "#6B7280"
                        }
                    }

                    Switch {
                        checked: SettingsModel.warpEnabled
                        onToggled: SettingsModel.warpEnabled = checked

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

                Column {
                    Layout.fillWidth: true
                    spacing: 12
                    opacity: SettingsModel.warpEnabled ? 1.0 : 0.4

                    Text {
                        text: "WARP Mode"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: "#A78BFA"
                    }

                    Repeater {
                        // config.WarpOptions.Mode — the only two values the core's builder
                        // tests for. Anything else is dropped on the floor by the core.
                        model: [
                            { id: "proxy_over_warp", label: "Proxy over WARP",
                              desc: "Reach the proxy through WARP" },
                            { id: "warp_over_proxy", label: "WARP over Proxy",
                              desc: "Reach WARP through the proxy" }
                        ]

                        delegate: Rectangle {
                            width: parent.width
                            height: 52
                            radius: 8
                            color: SettingsModel.warpMode === modelData.id ? "#2D1B69" : modeHover.containsMouse ? "#1A1640" : "transparent"
                            border.width: SettingsModel.warpMode === modelData.id ? 1 : 0
                            border.color: "#7C3AED"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12

                                Column {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: modelData.label
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        color: SettingsModel.warpMode === modelData.id ? "#E9D5FF" : "#D1D5DB"
                                    }

                                    Text {
                                        text: modelData.desc
                                        font.pixelSize: 11
                                        color: "#6B7280"
                                    }
                                }

                                Text {
                                    visible: SettingsModel.warpMode === modelData.id
                                    text: "✓"
                                    font.pixelSize: 16
                                    color: "#A78BFA"
                                }
                            }

                            MouseArea {
                                id: modeHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: SettingsModel.warpEnabled
                                onClicked: SettingsModel.warpMode = modelData.id
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
