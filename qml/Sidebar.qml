import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property int page: 0
    property bool compact: false
    property bool reduced: false
    property bool profileMenuOpen: false
    property alias profileButton: profileButton
    signal pageRequested(int index)
    property var activeDownloads: []
    property real downloadsProgress: -1
    signal profileClicked()

    readonly property bool hasProfile: launcher.activeProfileId !== ""
    readonly property int inset: compact ? 14 : 16

    Rectangle { anchors.fill: parent; color: Theme.sidebar }
    Rectangle { width: 1; height: parent.height; anchors.right: parent.right; color: Theme.line }

    // Brand
    // The signature is artwork (symbol + "omni" wordmark), never retyped in
    // Manrope. Compact mode keeps the symbol alone, at the 24px minimum.
    Image {
        id: brand
        readonly property real aspect: root.compact ? 155 / 120 : 378 / 120
        x: root.compact ? Math.round((root.width - width) / 2) : 24
        y: root.compact ? 26 : 23
        height: root.compact ? 24 : 28
        width: Math.round(height * aspect)
        source: Theme.brandSource(root.compact ? "symbol-rose" : "logo-primary")
        sourceSize.height: 120
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        Accessible.role: Accessible.StaticText
        Accessible.name: "OmniLauncher"
    }

    ColumnLayout {
        x: root.inset; y: 88
        width: root.width - root.inset * 2
        spacing: 4
        Repeater {
            model: [{ label: "Jogos", icon: "grid", page: 0 }, { label: "Modpacks", icon: "packs", page: 1 },
                    { label: "Capturas", icon: "image", page: 3 }]
            delegate: NavItem {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                text: modelData.label
                iconName: modelData.icon
                compact: root.compact
                reduced: root.reduced
                active: root.page === modelData.page
                onClicked: root.pageRequested(modelData.page)
            }
        }
    }

    ColumnLayout {
        x: root.inset
        width: root.width - root.inset * 2
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16
        spacing: 10

        NavItem {
            id: downloadsItem
            Layout.fillWidth: true
            text: "Downloads"
            iconName: "download"
            compact: root.compact
            reduced: root.reduced
            active: root.page === 4
            badge: root.activeDownloads.length === 0 ? ""
                : root.downloadsProgress >= 0 ? Math.round(root.downloadsProgress * 100) + "%"
                : String(root.activeDownloads.length)
            ToolTip.visible: false
            onClicked: root.pageRequested(4)

            // Hover: a compact list of what is downloading right now.
            Popup {
                x: downloadsItem.width + 14
                y: downloadsItem.height - height
                width: 300
                padding: 14
                visible: downloadsItem.hovered && root.activeDownloads.length > 0
                closePolicy: Popup.NoAutoClose
                background: Rectangle { color: Theme.popup; radius: 14; border.width: 1; border.color: Theme.lineStrong }
                contentItem: ColumnLayout {
                    spacing: 10
                    Text {
                        text: "Baixando agora"
                        color: Theme.muted
                        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: root.activeDownloads.slice(0, 4)
                        delegate: ColumnLayout {
                            id: summary
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: summary.modelData.title
                                    color: Theme.text
                                    font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: summary.modelData.progress >= 0
                                    text: Math.round(summary.modelData.progress * 100) + "%"
                                    color: Theme.accentText
                                    font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 3; radius: 1.5
                                color: "#1FFFFFFF"
                                Rectangle {
                                    width: parent.width * Math.max(0.08, summary.modelData.progress)
                                    height: parent.height; radius: 1.5
                                    color: Theme.accent
                                    opacity: summary.modelData.progress >= 0 ? 1 : 0.5
                                }
                            }
                        }
                    }
                }
            }
        }
        NavItem {
            Layout.fillWidth: true
            text: "Configurações"
            iconName: "settings"
            compact: root.compact
            reduced: root.reduced
            active: root.page === 2
            onClicked: root.pageRequested(2)
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.line }

        Button {
            id: profileButton
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            hoverEnabled: true
            padding: 0
            leftPadding: root.compact ? 0 : 8; rightPadding: root.compact ? 0 : 10
            Accessible.name: root.hasProfile ? "Perfis, " + launcher.activeProfile.name : "Perfis, escolher Perfil"
            ToolTip.visible: root.compact && hovered
            ToolTip.text: root.hasProfile ? launcher.activeProfile.name + (launcher.activeProfile.type === "microsoft" ? " · Microsoft" : " · Offline") : "Escolher Perfil"
            ToolTip.delay: 300
            onClicked: root.profileClicked()
            background: Rectangle {
                radius: Theme.radiusLg
                color: root.profileMenuOpen ? "#17FFFFFF" : profileButton.hovered ? "#0DFFFFFF" : "transparent"
                Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                Rectangle {
                    anchors.fill: parent; anchors.margins: -2
                    radius: parent.radius + 2; color: "transparent"
                    border.width: 2; border.color: Theme.focusRing
                    visible: profileButton.visualFocus
                }
            }
            contentItem: Item {
                ProfileAvatar {
                    id: avatar
                    x: root.compact ? (parent.width - width) / 2 : 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34; height: 34
                    empty: !root.hasProfile
                    skin: launcher.activeProfile.type === "microsoft" ? (auth.skinRevision, auth.skinSource(launcher.activeProfile.uuid)) : ""
                    Rectangle {
                        visible: root.hasProfile
                        width: 10; height: 10; radius: 5
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.margins: -1
                        color: Theme.success
                        border.width: 2; border.color: Theme.base
                    }
                }
                Column {
                    visible: !root.compact
                    anchors.left: avatar.right; anchors.leftMargin: 11
                    anchors.right: chevron.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        width: parent.width
                        text: root.hasProfile ? launcher.activeProfile.name : "Escolher Perfil"
                        color: Theme.text
                        font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: !root.hasProfile ? "Nenhum perfil ativo"
                            : launcher.activeProfile.type === "microsoft" ? "Conta Microsoft" : "Perfil offline"
                        color: Theme.muted
                        font.family: Theme.font; font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                UiIcon {
                    id: chevron
                    visible: !root.compact
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 15; height: 15
                    name: "updown"
                    color: Theme.muted
                }
            }
        }
    }
}
