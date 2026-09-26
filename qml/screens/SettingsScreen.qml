import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: settingsScreen

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    property int subPage: -1

    StackLayout {
        anchors.fill: parent
        currentIndex: settingsScreen.subPage < 0 ? 0 : 1

        ScrollView {
            id: mainSettings
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                width: mainSettings.width
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 24

                    Text {
                        text: "Settings"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: "#E9D5FF"
                    }
                }

                SettingsSection {
                    title: "General"

                    SettingsToggle { label: "System Proxy"; checked: SettingsModel.systemProxy; onToggled: SettingsModel.systemProxy = checked }
                    SettingsToggle { label: "TUN Mode"; checked: SettingsModel.tunMode; onToggled: SettingsModel.tunMode = checked }
                    SettingsToggle { label: "Auto Connect"; checked: SettingsModel.autoConnect; onToggled: SettingsModel.autoConnect = checked }
                    // No "Per-App Proxy" here: per-process routing would have to go through
                    // the engine's rule list, and that whole branch is commented out in this
                    // core build, so the switch could only ever have been decorative.
                    SettingsToggle { label: "Enable IPv6"; checked: SettingsModel.enableIPv6; onToggled: SettingsModel.enableIPv6 = checked }
                }

                SettingsSection {
                    title: "Advanced"

                    SettingsNavItem { label: "Route Settings"; onOpen: settingsScreen.subPage = 0 }
                    SettingsNavItem { label: "DNS Settings"; onOpen: settingsScreen.subPage = 1 }
                    SettingsNavItem { label: "Inbound Settings"; onOpen: settingsScreen.subPage = 2 }
                    SettingsNavItem { label: "TLS Tricks"; onOpen: settingsScreen.subPage = 3 }
                    SettingsNavItem { label: "WARP Settings"; onOpen: settingsScreen.subPage = 4 }
                }
            }
        }

        Loader {
            id: subPageLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: {
                switch (settingsScreen.subPage) {
                    case 0: return "settings/RouteSettings.qml"
                    case 1: return "settings/DnsSettings.qml"
                    case 2: return "settings/InboundSettings.qml"
                    case 3: return "settings/TlsTricks.qml"
                    case 4: return "settings/WarpSettings.qml"
                    default: return ""
                }
            }
        }
    }

    component SettingsSection: ColumnLayout {
        property string title: ""

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        Layout.topMargin: 8
        spacing: 0

        Text {
            text: title
            font.pixelSize: 14
            font.weight: Font.Bold
            color: "#A78BFA"
            Layout.bottomMargin: 8
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#1A1640"
            Layout.bottomMargin: 4
        }
    }

    component SettingsToggle: RowLayout {
        property string label: ""
        property bool checked: false

        signal toggled(bool checked)

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        height: 44

        Text {
            Layout.fillWidth: true
            text: label
            font.pixelSize: 14
            color: "#D1D5DB"
        }

        Switch {
            checked: parent.checked
            onToggled: parent.toggled(checked)

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

    component SettingsNavItem: Rectangle {
        property string label: ""
        signal open()

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        height: 44
        color: navHover.containsMouse ? "#1A1640" : "transparent"
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            Text {
                Layout.fillWidth: true
                text: label
                font.pixelSize: 14
                color: "#D1D5DB"
            }

            Text {
                text: "›"
                font.pixelSize: 20
                color: "#6B7280"
            }
        }

        MouseArea {
            id: navHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: open()
        }
    }
}
