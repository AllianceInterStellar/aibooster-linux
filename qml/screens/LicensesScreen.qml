import QtQuick

// The licence notices, on a page of their own.
//
// These are obligations, not decoration: GPL-3.0 section 7 requires the engine's authors be
// credited, the licence text be reachable, and modification be stated. Keeping them here
// rather than on the About panel is the usual arrangement — reachable in one tap, without
// the product's own screen reading like someone else's.
Item {
    id: licenses

    signal back()

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1E1B4B" }
            GradientStop { position: 1.0; color: "#080620" }
        }
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.margins: 28
        contentWidth: width
        // Bound to the Flickable, not to `parent`: inside a Flickable `parent` is the
        // contentItem, whose width comes from contentWidth — which comes from this column.
        // That is a binding loop, and Qt reports it and then picks a width arbitrarily.
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // Column, not ColumnLayout: a Layout inside a Flickable tries to fill the
        // contentItem, whose height is this layout's implicit height — Qt reports the
        // binding loop and then picks a size arbitrarily, so the page renders wrong.
        Column {
            id: column
            width: flick.width
            spacing: 20

            Row {
                width: column.width
                spacing: 10

                Text {
                    text: "\u2039"
                    font.pixelSize: 26
                    color: backArea.containsMouse ? "#C4B5FD" : "#A78BFA"

                    MouseArea {
                        id: backArea
                        anchors.fill: parent
                        anchors.margins: -10
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: licenses.back()
                    }
                }

                Text {
                    text: "Open source licenses"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: "#E9D5FF"
                }
            }

            // Sections are written out rather than generated from a model. A Repeater
            // delegate inside an inline component is rejected by the Qt 6.2 this client
            // supports, and four fixed entries do not need a model in the first place.
            SectionTitle { text: "AI Booster \u2014 Linux client" }
            SectionBody {
                text: "This application. Licensed under the GNU General Public License, "
                      + "version 3 or later."
            }
            Flow {
                width: column.width
                spacing: 14
                LinkText { label: "Source code"; url: "https://github.com/AllianceInterStellar/aibooster-linux" }
                LinkText { label: "GPL-3.0"; url: "https://www.gnu.org/licenses/gpl-3.0.html" }
            }

            SectionTitle { text: "Tunnelling engine" }
            SectionBody {
                text: "The engine is a separate program, built from hiddify-core and sing-box "
                      + "and licensed under the GPL-3.0. We rebuild it from source and ship it "
                      + "under our own name; the engine's own behaviour is unmodified.\n\n"
                      + "It is not linked into this application: this client launches it as a "
                      + "child process and communicates with it over loopback sockets only."
            }
            Flow {
                width: column.width
                spacing: 14
                LinkText { label: "hiddify-core"; url: "https://github.com/hiddify/hiddify-core" }
                LinkText { label: "sing-box"; url: "https://github.com/SagerNet/sing-box" }
                LinkText { label: "GPL-3.0"; url: "https://www.gnu.org/licenses/gpl-3.0.html" }
            }

            SectionTitle { text: "Qt 6" }
            SectionBody {
                text: "The user interface toolkit, used under the LGPL-3.0. Qt is linked "
                      + "dynamically and is not redistributed in our source repository."
            }
            Flow {
                width: column.width
                spacing: 14
                LinkText { label: "qt.io"; url: "https://www.qt.io" }
                LinkText { label: "LGPL-3.0"; url: "https://www.gnu.org/licenses/lgpl-3.0.html" }
            }

            SectionTitle { text: "OpenSSL" }
            SectionBody {
                text: "Used for the subscription payload decryption, under the Apache-2.0 "
                      + "licence. Linked dynamically and not redistributed by us."
            }
            Flow {
                width: column.width
                spacing: 14
                LinkText { label: "openssl.org"; url: "https://www.openssl.org" }
                LinkText { label: "Apache-2.0"; url: "https://www.apache.org/licenses/LICENSE-2.0" }
            }

            Item { width: 1; height: 8 }
        }
    }

    component SectionTitle: Text {
        width: column.width
        topPadding: 6
        font.pixelSize: 14
        font.weight: Font.Medium
        color: "#C4B5FD"
    }

    component SectionBody: Text {
        width: column.width
        wrapMode: Text.WordWrap
        font.pixelSize: 12
        lineHeight: 1.35
        color: "#9CA3AF"
    }

    component LinkText: Text {
        id: link

        property string label: ""
        property string url: ""

        text: link.label
        font.pixelSize: 12
        font.weight: Font.Medium
        color: linkArea.containsMouse ? "#C4B5FD" : "#A78BFA"

        MouseArea {
            id: linkArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Qt.openUrlExternally(link.url)
        }
    }
}
