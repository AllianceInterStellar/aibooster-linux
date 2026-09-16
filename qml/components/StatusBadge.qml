import QtQuick

Rectangle {
    id: badge
    width: label.implicitWidth + 16
    height: 22
    radius: 11

    property string status: "disconnected"

    color: {
        switch (badge.status) {
            case "connected":     return "#16A34A30"
            case "connecting":    return "#CA8A0430"
            case "disconnecting": return "#CA8A0430"
            default:              return "#37415130"
        }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: {
            switch (badge.status) {
                case "connected":     return "Connected"
                case "connecting":    return "Connecting"
                case "disconnecting": return "Disconnecting"
                default:              return "Disconnected"
            }
        }
        font.pixelSize: 11
        font.weight: Font.Medium
        color: {
            switch (badge.status) {
                case "connected":     return "#4ADE80"
                case "connecting":    return "#FDE047"
                case "disconnecting": return "#FDE047"
                default:              return "#9CA3AF"
            }
        }
    }
}
