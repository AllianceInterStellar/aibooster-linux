import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: tlsTricks

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    function rangeLow(text, fallback) {
        var n = parseInt(String(text).split("-")[0], 10)
        return isNaN(n) ? fallback : n
    }

    function rangeHigh(text, fallback) {
        var parts = String(text).split("-")
        var n = parseInt(parts[parts.length - 1], 10)
        return isNaN(n) ? fallback : n
    }

    function rangeText(slider) {
        return Math.round(slider.first.value) + "-" + Math.round(slider.second.value)
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
                text: "TLS Tricks"
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

                    Text {
                        Layout.fillWidth: true
                        text: "Enable TLS Fragment"
                        font.pixelSize: 14
                        color: "#D1D5DB"
                    }

                    Switch {
                        checked: SettingsModel.enableTlsFragment
                        onToggled: SettingsModel.enableTlsFragment = checked

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
                    spacing: 8
                    opacity: SettingsModel.enableTlsFragment ? 1.0 : 0.4

                    // Both settings are RANGES ("10-100"), not single numbers: config.TLSTricks
                    // takes a min-max pair and picks inside it per packet. A plain Slider could
                    // neither show the stored pair nor write one back.
                    Text {
                        text: "Fragment Size: " + SettingsModel.tlsFragmentSize + " bytes"
                        font.pixelSize: 13
                        color: "#A78BFA"
                    }

                    RangeSlider {
                        id: sizeRange
                        width: parent.width
                        from: 1
                        to: 500
                        stepSize: 1
                        snapMode: RangeSlider.SnapAlways
                        enabled: SettingsModel.enableTlsFragment
                        first.value: tlsTricks.rangeLow(SettingsModel.tlsFragmentSize, 10)
                        second.value: tlsTricks.rangeHigh(SettingsModel.tlsFragmentSize, 100)
                        first.onMoved: SettingsModel.tlsFragmentSize = tlsTricks.rangeText(sizeRange)
                        second.onMoved: SettingsModel.tlsFragmentSize = tlsTricks.rangeText(sizeRange)

                        background: RangeTrack { slider: sizeRange }
                        first.handle: RangeHandle { slider: sizeRange; position: sizeRange.first.visualPosition }
                        second.handle: RangeHandle { slider: sizeRange; position: sizeRange.second.visualPosition }
                    }

                    Text {
                        text: "Fragment Sleep: " + SettingsModel.tlsFragmentSleep + " ms"
                        font.pixelSize: 13
                        color: "#A78BFA"
                    }

                    RangeSlider {
                        id: sleepRange
                        width: parent.width
                        from: 0
                        to: 500
                        stepSize: 1
                        snapMode: RangeSlider.SnapAlways
                        enabled: SettingsModel.enableTlsFragment
                        first.value: tlsTricks.rangeLow(SettingsModel.tlsFragmentSleep, 50)
                        second.value: tlsTricks.rangeHigh(SettingsModel.tlsFragmentSleep, 200)
                        first.onMoved: SettingsModel.tlsFragmentSleep = tlsTricks.rangeText(sleepRange)
                        second.onMoved: SettingsModel.tlsFragmentSleep = tlsTricks.rangeText(sleepRange)

                        background: RangeTrack { slider: sleepRange }
                        first.handle: RangeHandle { slider: sleepRange; position: sleepRange.first.visualPosition }
                        second.handle: RangeHandle { slider: sleepRange; position: sleepRange.second.visualPosition }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    height: 44

                    Text {
                        Layout.fillWidth: true
                        text: "Enable Mux"
                        font.pixelSize: 14
                        color: "#D1D5DB"
                    }

                    Switch {
                        checked: SettingsModel.enableMux
                        onToggled: SettingsModel.enableMux = checked

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
                    spacing: 8
                    opacity: SettingsModel.enableMux ? 1.0 : 0.4

                    Text {
                        text: "Mux Protocol: " + SettingsModel.muxProtocol
                        font.pixelSize: 13
                        color: "#A78BFA"
                    }

                    Text {
                        text: "Max Connections: " + SettingsModel.muxMaxConnections
                        font.pixelSize: 13
                        color: "#A78BFA"
                    }

                    Slider {
                        width: parent.width
                        from: 1
                        to: 16
                        stepSize: 1
                        value: SettingsModel.muxMaxConnections
                        enabled: SettingsModel.enableMux
                        onMoved: SettingsModel.muxMaxConnections = value

                        background: Rectangle {
                            y: parent.height / 2 - 2
                            width: parent.width
                            height: 4
                            radius: 2
                            color: "#1A1640"

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: "#7C3AED"
                            }
                        }

                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            width: 18
                            height: 18
                            radius: 9
                            color: "#A78BFA"
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }

    component RangeTrack: Rectangle {
        required property RangeSlider slider

        y: slider.height / 2 - 2
        width: slider.width
        height: 4
        radius: 2
        color: "#1A1640"

        Rectangle {
            x: slider.first.visualPosition * parent.width
            width: (slider.second.visualPosition - slider.first.visualPosition) * parent.width
            height: parent.height
            radius: 2
            color: "#7C3AED"
        }
    }

    component RangeHandle: Rectangle {
        required property RangeSlider slider
        required property real position

        x: slider.leftPadding + position * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: "#A78BFA"
    }
}
