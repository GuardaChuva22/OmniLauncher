import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool reduced: false
    property int source: 0
    signal notify(string message)
    signal installRequested(var pack)
    signal versionsRequested(var pack, string gameId)
    // Game that came from this Modrinth pack, if any (one install per pack; duplicate the game for more).
    function installedGame(projectId) {
        const settings = launcher.gameSettings
        for (const game of launcher.games)
            if ((settings[game.id] || {}).packProjectId === projectId) return game.id
        return ""
    }
    readonly property real gutter: width < 900 ? 32 : 48

    function formatCount(value) {
        if (value >= 1e6) return (value / 1e6).toFixed(1).replace(".", ",") + " mi"
        if (value >= 1e3) return Math.round(value / 1e3) + " mil"
        return String(value)
    }
    onVisibleChanged: if (visible && source === 0 && mods.packResults.length === 0 && !mods.packSearching) mods.searchPacks("", 0)

    Timer { id: searchDelay; interval: 350; onTriggered: mods.searchPacks(query.text, 0) }

    RowLayout {
        id: header
        x: root.gutter; y: 50
        width: root.width - root.gutter * 2
        spacing: 10
        Text {
            text: "Modpacks"
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
            font.letterSpacing: -0.6
            Layout.fillWidth: true
        }
        InputField {
            id: query
            visible: root.source === 0
            Layout.preferredWidth: Math.min(320, header.width * 0.38)
            iconName: "search"; clearable: true
            placeholderText: "Buscar no Modrinth"
            onTextChanged: searchDelay.restart()
        }
    }

    SegmentTabs {
        id: sources
        x: root.gutter; y: header.y + header.height + 14
        width: root.width - root.gutter * 2
        model: ["Modrinth", "CurseForge"]
        currentIndex: root.source
        onCurrentIndexChanged: root.source = currentIndex
        reduced: root.reduced
    }

    ListView {
        id: list
        visible: root.source === 0
        x: root.gutter - 10
        y: sources.y + sources.height + 12
        width: root.width - root.gutter * 2 + 20
        height: root.height - y
        clip: true
        spacing: 4
        bottomMargin: 32
        model: mods.packResults
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThinScrollBar {}
        onAtYEndChanged: if (atYEnd && count > 0 && count < mods.packTotalHits && !mods.packSearching)
            mods.searchPacks(query.text, count)

        delegate: Item {
            id: pack
            required property var modelData
            readonly property bool installing: mods.packProjectId === modelData.id && mods.packState !== ""
            readonly property string installedId: root.installedGame(modelData.id)
            width: list.width
            height: 96

            HoverHandler { id: hover }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusLg
                color: hover.hovered ? "#0AFFFFFF" : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.fast } }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10; anchors.rightMargin: 14
                spacing: 16
                Rectangle {
                    Layout.preferredWidth: 68; Layout.preferredHeight: 68
                    radius: 14; color: Theme.fill; clip: true
                    UiIcon { anchors.centerIn: parent; visible: packIcon.status !== Image.Ready; width: 24; height: 24; name: "packs"; color: Theme.faint }
                    Image {
                        id: packIcon
                        anchors.fill: parent
                        source: pack.modelData.iconUrl
                        sourceSize: Qt.size(136, 136)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: pack.modelData.title
                            color: Theme.text
                            font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.maximumWidth: implicitWidth
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "por " + pack.modelData.author
                            color: Theme.faint
                            font.family: Theme.font; font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: pack.modelData.description
                        color: Theme.muted
                        font.family: Theme.font; font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        text: root.formatCount(pack.modelData.downloads) + " downloads"
                            + (pack.modelData.latestVersion ? "  ·  até Minecraft " + pack.modelData.latestVersion : "")
                        color: Theme.faint
                        font.family: Theme.font; font.pixelSize: 12
                    }
                }
                ActionButton {
                    Layout.preferredWidth: 40
                    iconName: "updown"
                    enabled: mods.packState === ""
                    reduced: root.reduced
                    Accessible.name: "Versões"
                    ToolTip.visible: hovered; ToolTip.text: pack.installedId ? "Mudar a versão instalada" : "Escolher versão"; ToolTip.delay: 400
                    onClicked: root.versionsRequested(pack.modelData, pack.installedId)
                }
                ActionButton {
                    Layout.preferredWidth: 150
                    visible: pack.installedId !== "" && !pack.installing
                    text: "Instalado"; iconName: "check"
                    quiet: true; enabled: false
                    reduced: root.reduced
                }
                ActionButton {
                    Layout.preferredWidth: 150
                    visible: pack.installedId === "" || pack.installing
                    text: !pack.installing ? "Instalar"
                        : mods.packState === "downloading" && mods.packProgress >= 0 ? "Instalando " + Math.round(mods.packProgress * 100) + "%"
                        : "Preparando…"
                    iconName: pack.installing ? "" : "download"
                    primary: !pack.installing
                    enabled: !pack.installing && mods.packState === ""
                    reduced: root.reduced
                    onClicked: root.installRequested(pack.modelData)
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            text: mods.packSearching ? "Buscando…" : mods.packSearchError !== "" ? mods.packSearchError : "Nenhum modpack encontrado"
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 14
        }
    }

    ColumnLayout {
        visible: root.source === 1
        anchors.horizontalCenter: parent.horizontalCenter
        y: sources.y + (root.height - sources.y) * 0.32
        width: Math.min(420, root.width - root.gutter * 2)
        spacing: 10
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 60; height: 60; radius: 30
            color: Theme.fill
            border.width: 1; border.color: Theme.line
            UiIcon { anchors.centerIn: parent; width: 26; height: 26; name: "packs"; color: Theme.muted }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            text: "CurseForge em breve"
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "O acesso ao CurseForge precisa de uma chave de API própria do OmniLauncher."
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
    }
}
