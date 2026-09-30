import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: routeSettings

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    // Every entry maps onto a key the engine actually reads,
    // so what is picked here changes the config the core builds on the next connect.
    readonly property var regions: [
        { id: "other", label: qsTr("Other"), desc: qsTr("No country-specific direct rules") },
        { id: "ir", label: qsTr("Iran"), desc: qsTr("Keep Iranian sites direct") },
        { id: "cn", label: qsTr("China"), desc: qsTr("Keep Chinese sites direct") },
        { id: "ru", label: qsTr("Russia"), desc: qsTr("Keep Russian sites direct") },
        { id: "af", label: qsTr("Afghanistan"), desc: qsTr("Keep Afghan sites direct") },
        { id: "id", label: qsTr("Indonesia"), desc: qsTr("Keep Indonesian sites direct") },
        { id: "tr", label: qsTr("Turkey"), desc: qsTr("Keep Turkish sites direct") },
        { id: "br", label: qsTr("Brazil"), desc: qsTr("Keep Brazilian sites direct") }
    ]

    readonly property var balancers: [
        { id: "round-robin", label: qsTr("Round Robin") },
        { id: "consistent-hashing", label: qsTr("Consistent Hashing") },
        { id: "sticky-sessions", label: qsTr("Sticky Sessions") }
    ]

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
                text: qsTr("Route Settings")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: "#E9D5FF"
            }
        }

        ScrollView {
            id: routeScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: routeScroll.width
                spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: routingColumn.implicitHeight + 40
                    radius: 12
                    color: "#12102B"
                    border.width: 1
                    border.color: "#1A1640"

                    ColumnLayout {
                        id: routingColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 20
                        spacing: 12

                        Text {
                            text: qsTr("Routing")
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: "#A78BFA"
                        }

                        RouteToggle {
                            label: qsTr("Bypass LAN")
                            description: qsTr("Send private and local-network addresses direct")
                            checked: SettingsModel.bypassLan
                            onToggled: function (v) { SettingsModel.bypassLan = v }
                        }

                        RouteToggle {
                            label: qsTr("Block Ads")
                            description: qsTr("Drop known ad and malware domains")
                            checked: SettingsModel.blockAds
                            onToggled: function (v) { SettingsModel.blockAds = v }
                        }

                        RouteToggle {
                            label: qsTr("Resolve Destination")
                            description: qsTr("Resolve domains locally before routing them")
                            checked: SettingsModel.resolveDestination
                            onToggled: function (v) { SettingsModel.resolveDestination = v }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: regionColumn.implicitHeight + 40
                    radius: 12
                    color: "#12102B"
                    border.width: 1
                    border.color: "#1A1640"

                    ColumnLayout {
                        id: regionColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 20
                        spacing: 8

                        Text {
                            text: qsTr("Region")
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: "#A78BFA"
                        }

                        Repeater {
                            model: routeSettings.regions

                            delegate: OptionRow {
                                optionId: modelData.id
                                label: modelData.label
                                description: modelData.desc
                                selected: SettingsModel.region === modelData.id
                                onPicked: function (id) { SettingsModel.region = id }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: balancerColumn.implicitHeight + 40
                    radius: 12
                    color: "#12102B"
                    border.width: 1
                    border.color: "#1A1640"

                    ColumnLayout {
                        id: balancerColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 20
                        spacing: 8

                        Text {
                            text: qsTr("Balancer Strategy")
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: "#A78BFA"
                        }

                        Repeater {
                            model: routeSettings.balancers

                            delegate: OptionRow {
                                optionId: modelData.id
                                label: modelData.label
                                selected: SettingsModel.balancerStrategy === modelData.id
                                onPicked: function (id) { SettingsModel.balancerStrategy = id }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Routing changes are written into the core's configuration, so they "
                          + "take effect the next time you connect.")
                    font.pixelSize: 11
                    color: "#6B7280"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    component OptionRow: Rectangle {
        property string optionId: ""
        property string label: ""
        property string description: ""
        property bool selected: false

        signal picked(string id)

        Layout.fillWidth: true
        Layout.preferredHeight: description === "" ? 40 : 52
        radius: 8
        color: selected ? "#2D1B69" : optionHover.containsMouse ? "#1A1640" : "transparent"
        border.width: selected ? 1 : 0
        border.color: "#7C3AED"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12

            Column {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: label
                    font.pixelSize: 13
                    color: selected ? "#E9D5FF" : "#D1D5DB"
                }

                Text {
                    visible: description !== ""
                    text: description
                    font.pixelSize: 11
                    color: "#6B7280"
                }
            }

            Text {
                visible: selected
                text: "✓"
                font.pixelSize: 16
                color: "#A78BFA"
            }
        }

        MouseArea {
            id: optionHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: picked(optionId)
        }
    }

    component RouteToggle: RowLayout {
        property string label: ""
        property string description: ""
        property bool checked: false

        signal toggled(bool value)

        Layout.fillWidth: true

        Column {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: label
                font.pixelSize: 14
                color: "#D1D5DB"
            }

            Text {
                visible: description !== ""
                text: description
                font.pixelSize: 11
                color: "#6B7280"
            }
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
}
