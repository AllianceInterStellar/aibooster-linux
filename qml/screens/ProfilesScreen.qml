import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models
import "../components" as C

Item {
    id: profilesScreen

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

            Text {
                text: "Profiles"
                font.pixelSize: 22
                font.weight: Font.Bold
                color: "#E9D5FF"
            }

            Item { Layout.preferredWidth: 10 }

            // "AiBooster Premium" account state, visible where profiles are managed.
            Rectangle {
                visible: AccountManager.loggedIn && AccountManager.premium
                Layout.preferredWidth: premiumBadgeLabel.implicitWidth + 18
                Layout.preferredHeight: 22
                radius: 11
                color: "#7C3AED"

                Text {
                    id: premiumBadgeLabel
                    anchors.centerIn: parent
                    text: "★ Premium"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: "#FDE68A"
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 92
                height: 36
                radius: 18
                color: "transparent"
                border.color: "#4C1D95"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: ProfileListModel.loading ? "…" : "Refresh"
                    font.pixelSize: 13
                    color: "#A78BFA"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: !ProfileListModel.loading
                    onClicked: ProfileListModel.refreshProfiles()
                }
            }

            Item { width: 8 }

            Rectangle {
                width: 120
                height: 36
                radius: 18
                color: "#7C3AED"

                Text {
                    anchors.centerIn: parent
                    text: "＋ Add Profile"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        addDialog.restartFlow()
                        addDialog.open()
                    }
                }
            }
        }

        ListView {
            id: profileList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            clip: true
            model: ProfileListModel

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: C.ProfileTile {
                width: profileList.width
                profileName: model.name
                url: model.url
                isActive: model.isActive
                isPremium: model.isPremium
                trafficProgress: model.trafficProgress
                remainingDays: model.remainingDays
                usedTrafficStr: model.usedTraffic
                totalTrafficStr: model.totalTraffic

                onActivate: ProfileListModel.setActive(index)
                onRemove: ProfileListModel.deleteProfile(index)
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: ProfileListModel.count + " profiles"
            font.pixelSize: 12
            color: "#6B7280"
        }
    }

    Dialog {
        id: addDialog
        anchors.centerIn: parent
        width: Math.min(480, profilesScreen.width - 48)
        modal: true
        title: "Add Profile"
        standardButtons: Dialog.Ok | Dialog.Cancel

        // NOT named reset(): Dialog already has a reset() signal, and shadowing it makes QML log an
        // invalid-override warning on every load and permanently disables the built-in. Same reason
        // LoginDialog uses restartFlow().
        function restartFlow() {
            urlField.text = ""
            nameField.text = ""
            errorText.text = ""
        }

        background: Rectangle {
            color: "#12102B"
            border.color: "#4C1D95"
            radius: 12
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: "Paste a subscription URL, a config link (vmess/vless/ss/trojan…), or the raw config content."
                font.pixelSize: 12
                color: "#9CA3AF"
                wrapMode: Text.WordWrap
            }

            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "https://… or vmess://…"
                color: "#E9D5FF"
                background: Rectangle {
                    color: "#080620"
                    border.color: urlField.activeFocus ? "#7C3AED" : "#312E5F"
                    radius: 8
                }
                onAccepted: addDialog.accept()
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "Name (optional — taken from the subscription if blank)"
                color: "#E9D5FF"
                background: Rectangle {
                    color: "#080620"
                    border.color: nameField.activeFocus ? "#7C3AED" : "#312E5F"
                    radius: 8
                }
                onAccepted: addDialog.accept()
            }

            Text {
                id: errorText
                Layout.fillWidth: true
                font.pixelSize: 12
                color: "#F87171"
                wrapMode: Text.WordWrap
                visible: text !== ""
            }
        }

        onAccepted: {
            var input = urlField.text.trim()
            if (input === "") {
                errorText.text = "Enter a subscription URL or config link."
                open()          // keep the dialog up instead of silently doing nothing
                return
            }
            ProfileListModel.addProfile(input, nameField.text.trim())
        }
    }

    // The add is asynchronous (it downloads); report the outcome rather than leaving the
    // list looking unchanged.
    Connections {
        target: ProfileListModel
        function onProfileError(message) {
            errorText.text = message
            addDialog.open()
        }
    }
}
