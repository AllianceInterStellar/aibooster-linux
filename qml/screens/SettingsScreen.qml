import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models
import "../components" as C

Item {
    id: settingsScreen

    Rectangle {
        anchors.fill: parent
        color: "#080620"
    }

    property int subPage: -1

    StackLayout {
        anchors.fill: parent
        currentIndex: settingsScreen.subPage < 0 ? 0 : 1

        ScrollView {
            id: mainSettings
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                width: mainSettings.width
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 24

                    Text {
                        text: "Settings"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: "#E9D5FF"
                    }
                }

                SettingsSection {
                    title: "Account"

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 44
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: AccountManager.loggedIn ? AccountManager.email : "Not signed in"
                            font.pixelSize: 14
                            color: "#D1D5DB"
                            elide: Text.ElideMiddle
                        }

                        // AiBooster Premium state: gold-on-purple badge once subscribed.
                        Rectangle {
                            visible: AccountManager.loggedIn
                            Layout.preferredWidth: tierLabel.implicitWidth + 18
                            Layout.preferredHeight: 22
                            radius: 11
                            color: AccountManager.premium ? "#7C3AED" : "transparent"
                            border.color: "#4C1D95"
                            border.width: 1

                            Text {
                                id: tierLabel
                                anchors.centerIn: parent
                                text: AccountManager.premium ? "★ Premium" : "Free"
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                color: AccountManager.premium ? "#FDE68A" : "#9CA3AF"
                            }
                        }

                        Rectangle {
                            visible: !AccountManager.loggedIn
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 32
                            radius: 16
                            color: "#7C3AED"

                            Text {
                                anchors.centerIn: parent
                                text: "Sign in"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: "#FFFFFF"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    // A leftover error from an unrelated operation would greet the
                                    // user inside a dialog it has nothing to do with.
                                    AccountManager.clearError()
                                    loginDialog.restartFlow()
                                    loginDialog.open()
                                }
                            }
                        }

                        Rectangle {
                            visible: AccountManager.loggedIn && !AccountManager.premium
                            Layout.preferredWidth: upgradeLabel.implicitWidth + 28
                            Layout.preferredHeight: 32
                            radius: 16
                            color: AccountManager.checkoutPending ? "#4C1D95" : "#7C3AED"

                            Text {
                                id: upgradeLabel
                                anchors.centerIn: parent
                                text: AccountManager.checkoutPending ? "Waiting for payment…" : "Upgrade"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                color: "#FFFFFF"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: !AccountManager.busy && !AccountManager.checkoutPending
                                onClicked: AccountManager.startCheckout()
                            }
                        }

                        // The way out of a checkout the user abandoned in the browser — otherwise
                        // the button above stays disabled for the whole 10-minute poll window.
                        Rectangle {
                            visible: AccountManager.loggedIn && AccountManager.checkoutPending
                            Layout.preferredWidth: 76
                            Layout.preferredHeight: 32
                            radius: 16
                            color: "transparent"
                            border.color: "#4C1D95"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Cancel"
                                font.pixelSize: 13
                                color: "#A78BFA"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: AccountManager.cancelCheckout()
                            }
                        }

                        Rectangle {
                            visible: AccountManager.loggedIn
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 32
                            radius: 16
                            color: "transparent"
                            border.color: "#4C1D95"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Sign out"
                                font.pixelSize: 13
                                color: "#A78BFA"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: AccountManager.logout()
                            }
                        }

                        // Store-parity with the mobile clients: an account created in-app must be
                        // deletable in-app. Confirmation below; the server refuses (409, shown via
                        // lastError) while a subscription is still active.
                        Rectangle {
                            visible: AccountManager.loggedIn
                            Layout.preferredWidth: deleteAccountLabel.implicitWidth + 28
                            Layout.preferredHeight: 32
                            radius: 16
                            color: "transparent"
                            border.color: "#7F1D1D"
                            border.width: 1

                            Text {
                                id: deleteAccountLabel
                                anchors.centerIn: parent
                                text: "Delete account"
                                font.pixelSize: 13
                                color: "#F87171"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: !AccountManager.busy
                                onClicked: {
                                    // A stale error inside a destructive confirm reads as if the
                                    // deletion had already been attempted and failed.
                                    AccountManager.clearError()
                                    deleteAccountConfirm.open()
                                }
                            }
                        }
                    }

                    // Recurring-charge disclosure. The amount itself is only known to the Stripe
                    // session, so at minimum the user must learn before clicking that Upgrade
                    // starts a subscription that renews on its own.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.bottomMargin: 6
                        spacing: 4
                        visible: AccountManager.loggedIn && !AccountManager.premium

                        Text {
                            Layout.fillWidth: true
                            text: "AiBooster Premium is a recurring subscription billed by Stripe. "
                                  + "The price and billing period are shown on the secure checkout "
                                  + "page before you pay, and it renews automatically until you "
                                  + "cancel."
                            font.pixelSize: 11
                            color: "#9CA3AF"
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            spacing: 16

                            LegalLink {
                                text: "Terms of Service"
                                url: "https://allianceinterstellar.com/en/legal/terms"
                            }

                            LegalLink {
                                text: "Privacy Policy"
                                url: "https://allianceinterstellar.com/en/legal/privacy"
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: AccountManager.lastError
                        font.pixelSize: 12
                        color: "#F87171"
                        wrapMode: Text.WordWrap
                        visible: text !== "" && !loginDialog.opened
                    }
                }

                SettingsSection {
                    title: "General"

                    SettingsToggle { label: "System Proxy"; checked: SettingsModel.systemProxy; onToggled: SettingsModel.systemProxy = checked }
                    SettingsToggle { label: "TUN Mode"; checked: SettingsModel.tunMode; onToggled: SettingsModel.tunMode = checked }
                    SettingsToggle { label: "Auto Connect"; checked: SettingsModel.autoConnect; onToggled: SettingsModel.autoConnect = checked }
                    // No "Per-App Proxy" here: per-process routing would have to go through
                    // HiddifyOptions.rules, and that whole branch is commented out in this
                    // core build, so the switch could only ever have been decorative.
                    SettingsToggle { label: "Enable IPv6"; checked: SettingsModel.enableIPv6; onToggled: SettingsModel.enableIPv6 = checked }
                }

                SettingsSection {
                    title: "Advanced"

                    SettingsNavItem { label: "Route Settings"; onOpen: settingsScreen.subPage = 0 }
                    SettingsNavItem { label: "DNS Settings"; onOpen: settingsScreen.subPage = 1 }
                    SettingsNavItem { label: "Inbound Settings"; onOpen: settingsScreen.subPage = 2 }
                    SettingsNavItem { label: "TLS Tricks"; onOpen: settingsScreen.subPage = 3 }
                    SettingsNavItem { label: "WARP Settings"; onOpen: settingsScreen.subPage = 4 }
                }
            }
        }

        Loader {
            id: subPageLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: {
                switch (settingsScreen.subPage) {
                    case 0: return "settings/RouteSettings.qml"
                    case 1: return "settings/DnsSettings.qml"
                    case 2: return "settings/InboundSettings.qml"
                    case 3: return "settings/TlsTricks.qml"
                    case 4: return "settings/WarpSettings.qml"
                    default: return ""
                }
            }
        }
    }

    C.LoginDialog {
        id: loginDialog
    }

    Dialog {
        id: deleteAccountConfirm

        width: Math.min(420, parent ? parent.width - 48 : 420)
        anchors.centerIn: parent
        modal: true
        title: "Delete account"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: "#12102B"
            border.color: "#7F1D1D"
            radius: 12
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "This permanently deletes your account, servers, and subscription records. "
                      + "It cannot be undone. Cancel any active subscription first, or deletion "
                      + "will be refused."
                font.pixelSize: 13
                color: "#C4B5FD"
                wrapMode: Text.WordWrap
            }

            // The section's own error label sits BEHIND this modal, so a refusal (e.g. the 409
            // "cancel the active subscription first") must be visible right here.
            Text {
                Layout.fillWidth: true
                text: AccountManager.lastError
                font.pixelSize: 12
                color: "#F87171"
                wrapMode: Text.WordWrap
                visible: text !== "" && deleteAccountConfirm.opened
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 32
                    radius: 16
                    color: "transparent"
                    border.color: "#4C1D95"
                    border.width: 1

                    Text { anchors.centerIn: parent; text: "Cancel"; font.pixelSize: 13; color: "#A78BFA" }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: deleteAccountConfirm.close()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 32
                    radius: 16
                    color: AccountManager.busy ? "#7F1D1D" : "#B91C1C"

                    Text {
                        anchors.centerIn: parent
                        text: AccountManager.busy ? "Deleting…" : "Delete"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: "#FFFFFF"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: !AccountManager.busy
                        // Success flips loggedIn false (the local teardown is logout()'s), which
                        // closes this dialog via the connection below; failure keeps it open with
                        // lastError shown under the account section.
                        onClicked: AccountManager.deleteAccount()
                    }
                }
            }
        }

        Connections {
            target: AccountManager
            function onChanged() {
                if (!AccountManager.loggedIn && deleteAccountConfirm.opened)
                    deleteAccountConfirm.close()
            }
        }
    }

    component LegalLink: Text {
        id: legalLink

        property string url: ""

        font.pixelSize: 11
        color: legalLinkArea.containsMouse ? "#C4B5FD" : "#A78BFA"

        MouseArea {
            id: legalLinkArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Qt.openUrlExternally(legalLink.url)
        }
    }

    component SettingsSection: ColumnLayout {
        property string title: ""

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        Layout.topMargin: 8
        spacing: 0

        Text {
            text: title
            font.pixelSize: 14
            font.weight: Font.Bold
            color: "#A78BFA"
            Layout.bottomMargin: 8
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#1A1640"
            Layout.bottomMargin: 4
        }
    }

    component SettingsToggle: RowLayout {
        property string label: ""
        property bool checked: false

        signal toggled(bool checked)

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        height: 44

        Text {
            Layout.fillWidth: true
            text: label
            font.pixelSize: 14
            color: "#D1D5DB"
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

    component SettingsNavItem: Rectangle {
        property string label: ""
        signal open()

        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        height: 44
        color: navHover.containsMouse ? "#1A1640" : "transparent"
        radius: 8

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            Text {
                Layout.fillWidth: true
                text: label
                font.pixelSize: 14
                color: "#D1D5DB"
            }

            Text {
                text: "›"
                font.pixelSize: 20
                color: "#6B7280"
            }
        }

        MouseArea {
            id: navHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: open()
        }
    }
}
