import QtQuick
import QtQuick.Layouts

Rectangle {
    id: tile
    height: 80
    radius: 12
    color: isActive ? "#1A1640" : "#0F0D20"
    border.width: isPremium ? 1 : 0
    border.color: "#C8A24C"

    property string profileName: ""
    property string url: ""
    property bool isActive: false
    property bool isPremium: false
    property real trafficProgress: 0.0
    property int remainingDays: -1
    property string usedTrafficStr: ""
    property string totalTrafficStr: ""

    signal activate()
    signal remove()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 12

        Rectangle {
            width: 40
            height: 40
            radius: 20
            color: tile.isActive ? "#7C3AED" : "#1F1B3D"

            Text {
                anchors.centerIn: parent
                text: tile.isPremium ? "👑" : "📋"
                font.pixelSize: 18
            }
        }

        Column {
            Layout.fillWidth: true
            spacing: 3

            Row {
                spacing: 6
                Text {
                    text: tile.profileName
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#E5E7EB"
                }
                Rectangle {
                    visible: tile.isActive
                    width: activeLabel.implicitWidth + 10
                    height: 18
                    radius: 9
                    color: "#16A34A30"
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: activeLabel
                        anchors.centerIn: parent
                        text: "Active"
                        font.pixelSize: 10
                        color: "#4ADE80"
                    }
                }
            }

            Row {
                spacing: 8
                visible: tile.remainingDays >= 0

                Text {
                    text: tile.remainingDays + " days left"
                    font.pixelSize: 11
                    color: "#9CA3AF"
                }

                Text {
                    text: tile.usedTrafficStr + " / " + tile.totalTrafficStr
                    font.pixelSize: 11
                    color: "#6B7280"
                }
            }

            Rectangle {
                visible: tile.trafficProgress > 0
                width: parent.width
                height: 4
                radius: 2
                color: "#1F1B3D"

                Rectangle {
                    width: parent.width * Math.min(tile.trafficProgress, 1.0)
                    height: parent.height
                    radius: 2
                    color: tile.trafficProgress > 0.8 ? "#EF4444" : "#7C3AED"
                }
            }
        }

        Column {
            spacing: 4

            Rectangle {
                width: 28
                height: 28
                radius: 14
                color: hoverSet.containsMouse ? "#2D2660" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    font.pixelSize: 14
                    color: tile.isActive ? "#4ADE80" : "#6B7280"
                }

                MouseArea {
                    id: hoverSet
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tile.activate()
                }
            }

            Rectangle {
                width: 28
                height: 28
                radius: 14
                color: hoverDel.containsMouse ? "#371520" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 14
                    color: "#F87171"
                }

                MouseArea {
                    id: hoverDel
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tile.remove()
                }
            }
        }
    }
}
