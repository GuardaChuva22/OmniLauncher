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
            model: [{ label: "Jogos", icon: "grid" }, { label: "Modpacks", icon: "packs" }]
            delegate: NavItem {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                text: modelData.label
                iconName: modelData.icon
                compact: root.compact
                reduced: root.reduced
                active: root.page === index
                onClicked: root.pageRequested(index)
            }
        }
    }

    ColumnLayout {
        x: root.inset
        width: root.width - root.inset * 2
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16
        spacing: 10

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
            ToolTip.text: root.hasProfile ? launcher.activeProfile.name + " · Offline" : "Escolher Perfil"
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
                        text: root.hasProfile ? "Perfil offline" : "Nenhum perfil ativo"
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
