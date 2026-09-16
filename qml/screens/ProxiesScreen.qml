import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models
import "../components" as C

Item {
    id: proxiesScreen

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "Proxies"
                font.pixelSize: 22
                font.weight: Font.Bold
                color: "#E9D5FF"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: searchField.implicitWidth + 24
                height: 36
                radius: 18
                color: "#1A1640"
                border.width: 1
                border.color: "#2D2660"

                TextField {
                    id: searchField
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    placeholderText: "Search..."
                    placeholderTextColor: "#6B7280"
                    color: "#E5E7EB"
                    font.pixelSize: 13
                    background: null
                    onTextChanged: ProxyListModel.filterModel.filterText = text
                }
            }

            Rectangle {
                width: 100
                height: 36
                radius: 18
                color: "#7C3AED"

                Text {
                    anchors.centerIn: parent
                    text: "Test All"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ProxyListModel.testAllDelays()
                }
            }
        }

        // Switching node talks to the running core; if the core says no, the tile would still
        // look selected while the tunnel carried on through the old one.
        Rectangle {
            id: selectionNotice

            property string message: ""
            property bool isError: true

            Layout.fillWidth: true
            Layout.preferredHeight: noticeText.implicitHeight + 20
            visible: message !== ""
            radius: 8
            color: isError ? "#371520" : "#132B1E"
            border.width: 1
            border.color: isError ? "#7F1D1D" : "#166534"

            Text {
                id: noticeText
                anchors.left: parent.left
                anchors.right: dismissNotice.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                text: selectionNotice.message
                font.pixelSize: 12
                color: selectionNotice.isError ? "#FCA5A5" : "#86EFAC"
                wrapMode: Text.WordWrap
            }

            Text {
                id: dismissNotice
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                font.pixelSize: 13
                color: "#9CA3AF"

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: selectionNotice.message = ""
                }
            }
        }

        ListView {
            id: proxyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            clip: true
            model: ProxyListModel.filterModel

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: C.ProxyTile {
                width: proxyList.width
                proxyId: model.proxyId
                name: model.name
                type: model.type
                countryCode: model.countryCode
                delay: model.delay
                isPremium: model.isPremium
                isSelected: model.proxyId === ProxyListModel.selectedId

                onSelected: ProxyListModel.selectProxy(model.proxyId)
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: searchField.text === ""
                  ? proxyList.count + " proxies"
                  : proxyList.count + " of " + ProxyListModel.count + " proxies"
            font.pixelSize: 12
            color: "#6B7280"
        }
    }

    Connections {
        target: ProxyListModel

        function onSelectionFailed(error) {
            selectionNotice.isError = true
            selectionNotice.message = error
        }

        function onSelectionApplied(name) {
            selectionNotice.isError = false
            selectionNotice.message = "Traffic is now going through \"" + name + "\"."
        }
    }
}
