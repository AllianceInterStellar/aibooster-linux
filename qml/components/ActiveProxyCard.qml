import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card
    height: 64
    radius: 12
    color: "#1A1640"
    border.width: 1
    border.color: "#2D2660"

    property string proxyName: ""
    property string proxyType: ""
    property string countryCode: ""
    property int delay: -1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Text {
            text: card.countryCode
            font.pixelSize: 24
        }

        Column {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: card.proxyName
                font.pixelSize: 13
                font.weight: Font.Medium
                color: "#E9D5FF"
            }

            Text {
                text: card.proxyType
                font.pixelSize: 11
                color: "#9CA3AF"
            }
        }

        Rectangle {
            width: delayText.implicitWidth + 16
            height: 24
            radius: 12
            color: {
                if (card.delay < 0) return "#374151"
                if (card.delay < 800) return "#16A34A30"
                if (card.delay < 1500) return "#CA8A0430"
                return "#DC262630"
            }

            Text {
                id: delayText
                anchors.centerIn: parent
                text: card.delay < 0 ? "—" : card.delay + " ms"
                font.pixelSize: 11
                font.weight: Font.Medium
                color: {
                    if (card.delay < 0) return "#9CA3AF"
                    if (card.delay < 800) return "#4ADE80"
                    if (card.delay < 1500) return "#FDE047"
                    return "#F87171"
                }
            }
        }
    }
}
