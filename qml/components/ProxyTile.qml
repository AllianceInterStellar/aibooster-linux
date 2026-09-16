import QtQuick
import QtQuick.Layouts

Rectangle {
    id: tile
    height: 60
    radius: 10
    color: isSelected ? "#2D1B69" : hoverArea.containsMouse ? "#1A1640" : "#12102B"
    border.width: isSelected ? 1 : 0
    border.color: "#7C3AED"

    property string proxyId: ""
    property string name: ""
    property string type: ""
    property string countryCode: ""
    property int delay: -1
    property bool isPremium: false
    property bool isSelected: false

    signal selected()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        Text {
            text: tile.countryCode
            font.pixelSize: 22
        }

        Column {
            Layout.fillWidth: true
            spacing: 2

            Row {
                spacing: 6
                Text {
                    text: tile.name
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#E5E7EB"
                }
                Text {
                    visible: tile.isPremium
                    text: "👑"
                    font.pixelSize: 12
                }
            }

            Text {
                text: tile.type
                font.pixelSize: 11
                color: "#6B7280"
            }
        }

        Rectangle {
            width: delayLabel.implicitWidth + 14
            height: 22
            radius: 11
            color: {
                if (tile.delay < 0) return "#374151"
                if (tile.delay < 800) return "#16A34A25"
                if (tile.delay < 1500) return "#CA8A0425"
                return "#DC262625"
            }

            Text {
                id: delayLabel
                anchors.centerIn: parent
                text: tile.delay < 0 ? "—" : tile.delay + " ms"
                font.pixelSize: 11
                color: {
                    if (tile.delay < 0) return "#9CA3AF"
                    if (tile.delay < 800) return "#4ADE80"
                    if (tile.delay < 1500) return "#FDE047"
                    return "#F87171"
                }
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.selected()
    }
}
