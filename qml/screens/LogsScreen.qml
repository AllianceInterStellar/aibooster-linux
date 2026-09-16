import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

Item {
    id: logsScreen

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    property var levelNames: ["All", "Debug", "Info", "Warning", "Error", "Fatal"]
    property int selectedLevel: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "Logs"
                font.pixelSize: 22
                font.weight: Font.Bold
                color: "#E9D5FF"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: filterField.implicitWidth + 24
                height: 36
                radius: 18
                color: "#1A1640"
                border.width: 1
                border.color: "#2D2660"

                TextField {
                    id: filterField
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    placeholderText: "Filter logs..."
                    placeholderTextColor: "#6B7280"
                    color: "#E5E7EB"
                    font.pixelSize: 13
                    background: null
                    onTextChanged: LogsModel.filterModel.filterText = text
                }
            }

            Rectangle {
                width: 80
                height: 36
                radius: 18
                color: "#371520"

                Text {
                    anchors.centerIn: parent
                    text: "Clear"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#F87171"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: LogsModel.clearLogs()
                }
            }
        }

        Row {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: logsScreen.levelNames

                delegate: Rectangle {
                    width: lvlText.implicitWidth + 16
                    height: 28
                    radius: 14
                    color: logsScreen.selectedLevel === index ? "#7C3AED" : "#1A1640"

                    Text {
                        id: lvlText
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 12
                        color: logsScreen.selectedLevel === index ? "#FFFFFF" : "#9CA3AF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            logsScreen.selectedLevel = index
                            LogsModel.filterModel.selectedLevel = index
                        }
                    }
                }
            }
        }

        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 2
            clip: true
            model: LogsModel.filterModel

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                width: logList.width
                height: logRow.implicitHeight + 8
                radius: 4
                color: index % 2 === 0 ? "#0C0A20" : "#0F0D24"

                RowLayout {
                    id: logRow
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    spacing: 8

                    Text {
                        text: model.time
                        font.pixelSize: 11
                        font.family: "monospace"
                        color: "#6B7280"
                    }

                    Rectangle {
                        width: levelLabel.implicitWidth + 8
                        height: 18
                        radius: 3
                        color: {
                            switch (model.levelName) {
                                case "DEBG": return "#1E3A5F"
                                case "INFO": return "#1A3D2E"
                                case "WARN": return "#3D3A1A"
                                case "ERRO": return "#3D1A1A"
                                case "FATL": return "#5F1A1A"
                                default: return "#1A1640"
                            }
                        }

                        Text {
                            id: levelLabel
                            anchors.centerIn: parent
                            text: model.levelName
                            font.pixelSize: 10
                            font.family: "monospace"
                            font.weight: Font.Bold
                            color: {
                                switch (model.levelName) {
                                    case "DEBG": return "#93C5FD"
                                    case "INFO": return "#86EFAC"
                                    case "WARN": return "#FDE047"
                                    case "ERRO": return "#FCA5A5"
                                    case "FATL": return "#F87171"
                                    default: return "#D1D5DB"
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: model.message
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: "#D1D5DB"
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
