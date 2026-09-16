import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiBooster.Models

// Two-step email sign-in: send a one-time code, then verify it.
Dialog {
    id: loginDialog

    // 0 = enter email, 1 = enter the emailed code
    property int step: 0
    property string pendingEmail: ""

    // Not named reset(): that would clash with Dialog's built-in reset() slot.
    function restartFlow() {
        step = 0
        pendingEmail = ""
        emailField.text = ""
        codeField.text = ""
    }

    function submit() {
        if (AccountManager.busy)
            return
        if (step === 0) {
            pendingEmail = emailField.text.trim()
            AccountManager.sendOtp(pendingEmail)
        } else {
            AccountManager.verifyOtp(pendingEmail, codeField.text.trim())
        }
    }

    width: Math.min(420, parent ? parent.width - 48 : 420)
    anchors.centerIn: parent
    modal: true
    title: "Sign in"
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: "#12102B"
        border.color: "#4C1D95"
        radius: 12
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: loginDialog.step === 0
                  ? "Enter your email and we'll send you a one-time sign-in code."
                  : "Enter the 6-digit code sent to " + loginDialog.pendingEmail + "."
            font.pixelSize: 12
            color: "#9CA3AF"
            wrapMode: Text.WordWrap
        }

        TextField {
            id: emailField
            Layout.fillWidth: true
            visible: loginDialog.step === 0
            enabled: !AccountManager.busy
            placeholderText: "you@example.com"
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            color: "#E9D5FF"
            background: Rectangle {
                color: "#080620"
                border.color: emailField.activeFocus ? "#7C3AED" : "#312E5F"
                radius: 8
            }
            onAccepted: loginDialog.submit()
        }

        TextField {
            id: codeField
            Layout.fillWidth: true
            visible: loginDialog.step === 1
            enabled: !AccountManager.busy
            placeholderText: "6-digit code"
            inputMethodHints: Qt.ImhDigitsOnly
            color: "#E9D5FF"
            background: Rectangle {
                color: "#080620"
                border.color: codeField.activeFocus ? "#7C3AED" : "#312E5F"
                radius: 8
            }
            onAccepted: loginDialog.submit()
        }

        Text {
            Layout.fillWidth: true
            text: AccountManager.lastError
            font.pixelSize: 12
            color: "#F87171"
            wrapMode: Text.WordWrap
            visible: text !== ""
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: loginDialog.step === 1 ? "Use a different email" : ""
                font.pixelSize: 12
                color: "#A78BFA"
                visible: loginDialog.step === 1

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        loginDialog.step = 0
                        codeField.text = ""
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 36
                radius: 18
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
                    onClicked: loginDialog.close()
                }
            }

            Rectangle {
                Layout.preferredWidth: 120
                Layout.preferredHeight: 36
                radius: 18
                color: AccountManager.busy ? "#4C1D95" : "#7C3AED"

                Text {
                    anchors.centerIn: parent
                    text: AccountManager.busy ? "…"
                          : loginDialog.step === 0 ? "Send code" : "Verify"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: !AccountManager.busy
                    onClicked: loginDialog.submit()
                }
            }
        }
    }

    Connections {
        target: AccountManager
        function onOtpSent() {
            loginDialog.step = 1
        }
        function onLoginSucceeded() {
            loginDialog.close()
        }
    }
}
