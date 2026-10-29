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
    signal openGameRequested(string gameId)
    readonly property var customGames: {
        const settings = launcher.gameSettings
        return launcher.games.filter(function(game) { return !!(settings[game.id] || {}).customPackUrl })
    }
    function checkCustomPacks() {
        for (const game of customGames) mods.checkCustom(game.id, launcher.gameSettings[game.id].customPackUrl)
    }
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
        model: ["Modrinth", "Customizado", "CurseForge"]
        currentIndex: root.source
        onCurrentIndexChanged: {
            root.source = currentIndex
            if (currentIndex === 1) root.checkCustomPacks()
        }
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

    // Customizado: packs published by link (yours or a friend's); new versions show as updates.
    Flickable {
        id: custom
        visible: root.source === 1
        x: root.gutter
        y: sources.y + sources.height + 20
        width: root.width - root.gutter * 2
        height: root.height - y
        contentHeight: customColumn.implicitHeight + 40
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ThinScrollBar {}

        ColumnLayout {
            id: customColumn
            width: custom.width
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: "Modpacks publicados por você ou por amigos. Cole o link do manifesto (.json) para instalar; quando uma nova versão for publicada, ela aparece aqui como atualização."
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 14
                spacing: 8
                InputField {
                    id: customLink
                    Layout.fillWidth: true
                    iconName: "external"; clearable: true
                    placeholderText: "https://… manifest.json"
                    onAccepted: addCustom.clicked()
                }
                ActionButton {
                    id: addCustom
                    text: "Adicionar"; iconName: "plus"; primary: true
                    enabled: customLink.text.trim() !== "" && mods.packState === ""
                    reduced: root.reduced
                    onClicked: mods.prepareCustom(customLink.text)
                }
            }
            Text {
                readonly property bool active: mods.packState !== "" && mods.packProjectId.indexOf("custom:") === 0
                visible: active
                Layout.topMargin: 10
                text: mods.packState === "downloading"
                    ? "Instalando " + (mods.packProgress >= 0 ? Math.round(mods.packProgress * 100) + "%" : "…")
                    : mods.packState === "backing-up" ? "Fazendo backup dos mundos…" : "Lendo o manifesto…"
                color: Theme.accentText
                font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
            }
            Text {
                visible: root.customGames.length > 0
                Layout.topMargin: 28; Layout.bottomMargin: 8
                text: "ASSINADOS"
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                font.letterSpacing: 0.6
            }
            Repeater {
                model: root.customGames
                delegate: Item {
                    id: customRow
                    required property var modelData
                    readonly property var saved: launcher.gameSettings[modelData.id] || ({})
                    readonly property var latest: mods.customLatest[modelData.id]
                    readonly property bool hasUpdate: !!latest && latest.version !== saved.customPackVersion
                    readonly property bool busy: mods.packState !== "" && mods.packProjectId === "custom:" + saved.customPackUrl
                    Layout.fillWidth: true
                    implicitHeight: 86
                    RowLayout {
                        anchors.fill: parent
                        spacing: 16
                        Rectangle {
                            Layout.preferredWidth: 48; Layout.preferredHeight: 72
                            radius: 8; color: Theme.fill; clip: true
                            Image {
                                anchors.fill: parent
                                source: Theme.coverSource(customRow.modelData.coverId)
                                sourceSize: Qt.size(96, 144)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: customRow.modelData.title
                                color: Theme.text
                                font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                text: "Versão " + (customRow.saved.customPackVersion || "?") + "  ·  Minecraft " + customRow.modelData.version
                                    + "  ·  " + customRow.modelData.loader
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 12
                            }
                            Text {
                                text: customRow.busy ? "Atualizando…"
                                    : customRow.hasUpdate ? "Nova versão: " + customRow.latest.version
                                      + (customRow.latest.minecraft !== customRow.modelData.version ? " (Minecraft " + customRow.latest.minecraft + ")" : "")
                                    : customRow.latest ? "Atualizado" : "Verificando…"
                                color: customRow.hasUpdate || customRow.busy ? Theme.accentText : Theme.faint
                                font.family: Theme.font; font.pixelSize: 12
                                font.weight: customRow.hasUpdate ? Font.DemiBold : Font.Normal
                            }
                        }
                        ActionButton {
                            visible: customRow.hasUpdate
                            text: "Atualizar"; iconName: "download"; primary: true
                            enabled: mods.packState === ""
                            reduced: root.reduced
                            onClicked: mods.prepareCustom(customRow.saved.customPackUrl, customRow.modelData.id)
                        }
                        ActionButton {
                            text: "Abrir"; quiet: true
                            reduced: root.reduced
                            onClicked: root.openGameRequested(customRow.modelData.id)
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
            }
        }
    }

    ColumnLayout {
        visible: root.source === 2
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
