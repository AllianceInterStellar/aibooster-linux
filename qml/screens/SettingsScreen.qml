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
                        text: qsTr("Settings")
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: "#E9D5FF"
                    }
                }

                SettingsSection {
                    title: qsTr("General")

                    SettingsToggle { label: qsTr("System Proxy"); checked: SettingsModel.systemProxy; onToggled: SettingsModel.systemProxy = checked }
                    SettingsToggle { label: qsTr("TUN Mode"); checked: SettingsModel.tunMode; onToggled: SettingsModel.tunMode = checked }
                    SettingsToggle { label: qsTr("Auto Connect"); checked: SettingsModel.autoConnect; onToggled: SettingsModel.autoConnect = checked }
                    SettingsToggle { label: qsTr("Auto Reconnect"); checked: SettingsModel.autoReconnect; onToggled: SettingsModel.autoReconnect = checked }
                    // Only where the desktop has a tray; elsewhere closing always quits.
                    SettingsToggle { visible: Tray.available; label: qsTr("Keep Running in Tray"); checked: SettingsModel.closeToTray; onToggled: SettingsModel.closeToTray = checked }
                    // No "Per-App Proxy" here: per-process routing would have to go through
                    // the engine's rule list, and that whole branch is commented out in this
                    // core build, so the switch could only ever have been decorative.
                    SettingsToggle { label: qsTr("Enable IPv6"); checked: SettingsModel.enableIPv6; onToggled: SettingsModel.enableIPv6 = checked }
                    // Language names are written in their own language, so each is findable
                    // by someone who cannot read the current one.
                    SettingsChoice {
                        label: qsTr("Language")
                        options: [
                            { value: "", text: qsTr("Follow System") },
                            { value: "en", text: "English" },
                            { value: "zh_CN", text: "简体中文" }
                        ]
                        current: SettingsModel.language
                        onPicked: function (value) { SettingsModel.language = value }
                    }
                }

                SettingsSection {
                    title: qsTr("Advanced")

                    SettingsNavItem { label: qsTr("Route Settings"); onOpen: settingsScreen.subPage = 0 }
                    SettingsNavItem { label: qsTr("DNS Settings"); onOpen: settingsScreen.subPage = 1 }
                    SettingsNavItem { label: qsTr("Inbound Settings"); onOpen: settingsScreen.subPage = 2 }
                    SettingsNavItem { label: qsTr("TLS Tricks"); onOpen: settingsScreen.subPage = 3 }
                    SettingsNavItem { label: qsTr("WARP Settings"); onOpen: settingsScreen.subPage = 4 }
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

    component SettingsChoice: RowLayout {
        id: choice
        property string label: ""
        property var options: []
        property string current: ""

        signal picked(string value)

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        height: 44

        Text {
            Layout.fillWidth: true
            text: choice.label
            font.pixelSize: 14
            color: "#D1D5DB"
        }

        ComboBox {
            id: combo
            Layout.preferredWidth: 180
            model: choice.options
            textRole: "text"
            currentIndex: {
                for (let i = 0; i < choice.options.length; ++i)
                    if (choice.options[i].value === choice.current)
                        return i
                return 0
            }
            onActivated: function (index) { choice.picked(choice.options[index].value) }

            contentItem: Text {
                leftPadding: 12
                text: combo.displayText
                font.pixelSize: 13
                color: "#E9D5FF"
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                implicitHeight: 32
                radius: 8
                color: "#1A1640"
                border.width: 1
                border.color: combo.activeFocus ? "#7C3AED" : "#2E2A5A"
            }
            // The Basic style's list is white on white-grey; keep it in the app's palette.
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: combo.width
                highlighted: combo.highlightedIndex === index
                contentItem: Text {
                    text: modelData.text
                    font.pixelSize: 13
                    color: index === combo.currentIndex ? "#C4B5FD" : "#E5E7EB"
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.highlighted ? "#2E2A5A" : "#12102B"
                }
            }
            popup.background: Rectangle {
                radius: 8
                color: "#12102B"
                border.width: 1
                border.color: "#2E2A5A"
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
