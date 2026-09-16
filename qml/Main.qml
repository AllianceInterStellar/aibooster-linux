import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    minimumWidth: 860
    minimumHeight: 580
    visible: true
    title: "AiBooster VPN"
    color: "#000000"

    property int currentIndex: 0

    readonly property var navItems: [
        { icon: "🏠", label: "Home" },
        { icon: "🌐", label: "Proxies" },
        { icon: "📋", label: "Profiles" },
        { icon: "⚙️", label: "Settings" },
        { icon: "📜", label: "Logs" },
        { icon: "ℹ️", label: "About" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 72
            color: "#0A0820"

            Rectangle {
                width: 1
                height: parent.height
                anchors.right: parent.right
                color: "#1A1640"
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 16
                anchors.bottomMargin: 16
                spacing: 4

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "✦"
                    font.pixelSize: 28
                    color: "#A78BFA"
                }

                Item { Layout.preferredHeight: 16 }

                Repeater {
                    model: root.navItems

                    delegate: Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 56
                        height: 56
                        radius: 12
                        color: root.currentIndex === index ? "#4C1D95" : hoverArea.containsMouse ? "#1A1640" : "transparent"

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 2

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.icon
                                font.pixelSize: 18
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.label
                                font.pixelSize: 9
                                font.weight: Font.Medium
                                color: root.currentIndex === index ? "#E9D5FF" : "#9CA3AF"
                            }
                        }

                        MouseArea {
                            id: hoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentIndex = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    // From QGuiApplication::applicationVersion(), which main.cpp sets from the
                    // build. Hardcoding it here shipped a 2.1.0 binary that called itself 2.0.24.
                    text: "v" + Application.version
                    font.pixelSize: 10
                    color: "#6B7280"
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentIndex

            Loader {
                source: "screens/HomeScreen.qml"
                active: root.currentIndex === 0
            }
            Loader {
                source: "screens/ProxiesScreen.qml"
                active: root.currentIndex === 1
            }
            Loader {
                source: "screens/ProfilesScreen.qml"
                active: root.currentIndex === 2
            }
            Loader {
                source: "screens/SettingsScreen.qml"
                active: root.currentIndex === 3
            }
            Loader {
                source: "screens/LogsScreen.qml"
                active: root.currentIndex === 4
            }
            Loader {
                source: "screens/AboutScreen.qml"
                active: root.currentIndex === 5
            }
        }
    }
}
