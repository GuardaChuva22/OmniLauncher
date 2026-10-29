import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool reduced: false
    property int source: 0
    property Item blurSource: null
    readonly property bool dialogOpen: publisher.shown
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
        model: ["Modrinth", "Customizado"]
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
            transform: Translate { y: hover.hovered && !root.reduced ? -2 : 0; Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } } }

            HoverHandler { id: hover; onHoveredChanged: if (hovered) sfx.play("hover") }
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

    // Customizado: modpacks made here and shared by code (OmniLauncher Cloud), and the
    // ones followed by code or manifest link. Followers get every new version as an update.
    readonly property var myPacks: {
        const settings = launcher.gameSettings
        return launcher.games.filter(function(game) { return !!(settings[game.id] || {}).cloudKey })
    }
    readonly property var followedPacks: root.customGames.filter(function(game) { return !(launcher.gameSettings[game.id] || {}).cloudKey })
    property var pendingEditor: ({})
    function nextVersion(previous) {
        if (!previous) return "1"
        if (/^\d+$/.test(previous)) return String(Number(previous) + 1)
        const match = previous.match(/^(.*?)(\d+)$/)
        return match ? match[1] + (Number(match[2]) + 1) : previous + ".1"
    }
    function join(text) {
        const value = text.trim()
        const code = cloud.codeFrom(value)
        const key = cloud.keyFrom(value)
        if ((value.indexOf("https://") === 0 || value.indexOf("http://") === 0) && value.indexOf(cloud.baseUrl + "/p/") !== 0) {
            mods.prepareCustom(value)
            return true
        }
        if (code === "") { root.notify("Cole o código de 6 letras do modpack (ou o link dele)."); return false }
        if (!cloud.configured) { root.notify("A nuvem do OmniLauncher ainda não está no ar nesta versão."); return false }
        // An editor link for a pack this PC already follows only adds the key.
        const following = launcher.games.find(function(g) { return (launcher.gameSettings[g.id] || {}).cloudCode === code })
        if (following && key) {
            launcher.setCloudPack(following.id, code, key)
            root.notify("Agora você também pode publicar versões de " + following.title + ".")
            return true
        }
        if (following) { root.notify("Você já segue esse modpack: " + following.title + "."); return false }
        root.pendingEditor = key ? { code: code, key: key } : {}
        mods.prepareCustom(cloud.packUrl(code))
        return true
    }
    Connections {
        target: mods
        function onPackFinished(gameId, ok, message, pack) {
            if (!ok || !pack || pack.kind !== "custom" || !cloud.configured || String(pack.url).indexOf(cloud.baseUrl + "/p/") !== 0) return
            const code = cloud.codeFrom(pack.url)
            const key = root.pendingEditor.code === code ? root.pendingEditor.key : ""
            launcher.setCloudPack(gameId, code, key)
            root.pendingEditor = {}
        }
    }
    Connections {
        target: cloud
        function onPublished(gameId, ok, message, result) {
            if (!ok) { root.notify(message); publisher.step = "form"; return }
            launcher.setCloudPack(gameId, result.code, result.editorKey)
            launcher.setCustomPack(gameId, result.url, result.version)
            publisher.outcome = result
            publisher.step = "done"
            sfx.play("success")
        }
    }

    component SectionLabel: Text {
        Layout.topMargin: 30; Layout.bottomMargin: 8
        color: Theme.muted
        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
    }
    component PackCover: Rectangle {
        property var game
        Layout.preferredWidth: 48; Layout.preferredHeight: 72
        radius: 8; color: Theme.fill; clip: true
        Image {
            anchors.fill: parent
            source: parent.game ? Theme.coverSource(parent.game.coverId) : ""
            sourceSize: Qt.size(96, 144)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
    }

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

            // Create
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: createRow.implicitHeight + 40
                radius: Theme.radiusLg
                color: "#0AFFFFFF"
                border.width: 1; border.color: Theme.line
                SoftGlow { z: -1; anchors.fill: parent; color: Theme.accent; radius: Theme.radiusLg; blurMax: 40; spread: -12; opacity: 0.18 }
                RowLayout {
                    id: createRow
                    x: 20; y: 20
                    width: parent.width - 40
                    spacing: 18
                    Rectangle {
                        Layout.preferredWidth: 52; Layout.preferredHeight: 52
                        radius: 14; color: Theme.accentSoft
                        UiIcon { anchors.centerIn: parent; width: 24; height: 24; name: "packs"; color: Theme.accent }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "Crie seu modpack e chame os amigos"; color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Escolha um jogo seu: os mods e as configs viram um modpack com um código. Quem entrar com o código recebe cada versão nova que você publicar."
                            color: Theme.muted; font.family: Theme.font; font.pixelSize: 13
                        }
                    }
                    ActionButton {
                        text: "Criar modpack"; iconName: "plus"; primary: true; reduced: root.reduced
                        enabled: cloud.configured && cloud.state === ""
                        onClicked: publisher.prepare(null)
                    }
                }
            }

            // Join
            SectionLabel { text: "Entrar em um modpack" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                InputField {
                    id: joinField
                    Layout.fillWidth: true
                    iconName: "external"; clearable: true
                    placeholderText: "Código do modpack (ex.: K7X2QM), link de editor ou link de manifesto"
                    onAccepted: joinButton.clicked()
                }
                ActionButton {
                    id: joinButton
                    text: "Entrar"; iconName: "download"; primary: true
                    enabled: joinField.text.trim() !== "" && mods.packState === ""
                    reduced: root.reduced
                    onClicked: if (root.join(joinField.text)) joinField.text = ""
                }
            }
            Text {
                visible: !cloud.configured
                Layout.topMargin: 8
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "A nuvem de modpacks do OmniLauncher ainda está sendo preparada. Por enquanto funcionam só links de manifesto."
                color: Theme.faint; font.family: Theme.font; font.pixelSize: 12
            }
            Text {
                readonly property bool active: mods.packState !== "" && mods.packProjectId.indexOf("custom:") === 0
                visible: active
                Layout.topMargin: 10
                text: mods.packState === "downloading"
                    ? "Instalando " + (mods.packProgress >= 0 ? Math.round(mods.packProgress * 100) + "%" : "…")
                    : mods.packState === "backing-up" ? "Fazendo backup dos mundos…" : "Lendo o modpack…"
                color: Theme.accentText
                font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
            }

            // Mine
            SectionLabel { visible: root.myPacks.length > 0; text: "Meus modpacks" }
            Repeater {
                model: root.myPacks
                delegate: Item {
                    id: mineRow
                    required property var modelData
                    readonly property var saved: launcher.gameSettings[modelData.id] || ({})
                    readonly property bool publishing: cloud.state !== "" && cloud.gameId === modelData.id
                    Layout.fillWidth: true
                    implicitHeight: 92
                    RowLayout {
                        anchors.fill: parent
                        spacing: 16
                        PackCover { game: mineRow.modelData }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { Layout.fillWidth: true; text: mineRow.modelData.title; color: Theme.text; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text {
                                text: "Versão " + (mineRow.saved.customPackVersion || "?") + "  ·  Minecraft " + mineRow.modelData.version + "  ·  " + mineRow.modelData.loader
                                color: Theme.muted; font.family: Theme.font; font.pixelSize: 12
                            }
                            Row {
                                spacing: 8
                                Text { text: mineRow.publishing ? cloud.detail : "Código"; color: mineRow.publishing ? Theme.accentText : Theme.faint; font.family: Theme.font; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                                Rectangle {
                                    visible: !mineRow.publishing
                                    implicitWidth: codeText.implicitWidth + 14; implicitHeight: 22; radius: 6
                                    color: Theme.accentSoft
                                    Text { id: codeText; anchors.centerIn: parent; text: mineRow.saved.cloudCode || ""; color: Theme.accentText; font.family: "Consolas"; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 1.5 }
                                }
                            }
                        }
                        ActionButton {
                            text: "Copiar código"; iconName: "copy"; quiet: true; reduced: root.reduced
                            onClicked: { servers.copyText(mineRow.saved.cloudCode); root.notify("Código " + mineRow.saved.cloudCode + " copiado. Mande pros seus amigos.") }
                        }
                        ActionButton {
                            text: "Publicar versão"; iconName: "download"; primary: true; reduced: root.reduced
                            enabled: cloud.state === ""
                            onClicked: publisher.prepare(mineRow.modelData)
                        }
                        ActionButton {
                            text: "Abrir"; quiet: true; reduced: root.reduced
                            onClicked: root.openGameRequested(mineRow.modelData.id)
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
            }

            // Followed
            SectionLabel { visible: root.followedPacks.length > 0; text: "Seguindo" }
            Repeater {
                model: root.followedPacks
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
                        PackCover { game: customRow.modelData }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { Layout.fillWidth: true; text: customRow.modelData.title; color: Theme.text; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text {
                                text: "Versão " + (customRow.saved.customPackVersion || "?") + "  ·  Minecraft " + customRow.modelData.version
                                    + "  ·  " + customRow.modelData.loader + (customRow.saved.cloudCode ? "  ·  código " + customRow.saved.cloudCode : "")
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: customRow.busy ? "Atualizando…"
                                    : customRow.hasUpdate ? "Nova versão: " + customRow.latest.version
                                      + (customRow.latest.minecraft !== customRow.modelData.version ? " (Minecraft " + customRow.latest.minecraft + ")" : "")
                                      + (customRow.latest.changelog ? "  ·  " + customRow.latest.changelog : "")
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

    // ── Create or publish a version ────────────────────────────────────────
    GlassDialog {
        id: publisher
        parent: Overlay.overlay
        width: 480
        blurSource: root.blurSource
        reduced: root.reduced
        showClose: step !== "busy"
        closePolicy: step === "busy" ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property var fixedGame: null
        property string step: "form"   // form | busy | done
        property var outcome: ({})
        readonly property var candidates: launcher.games.filter(function(g) {
            const s = launcher.gameSettings[g.id] || {}
            return g.loader !== "Vanilla" && !s.cloudCode && !s.customPackUrl
        })
        readonly property var game: fixedGame ? fixedGame : (gamePick.currentIndex >= 0 ? candidates[gamePick.currentIndex] : null)
        readonly property var saved: game ? (launcher.gameSettings[game.id] || {}) : ({})
        title: step === "done" ? "Modpack publicado" : fixedGame ? "Publicar nova versão" : "Criar modpack"
        function prepare(game) {
            fixedGame = game
            step = "form"
            outcome = {}
            gamePick.currentIndex = candidates.length > 0 ? 0 : -1
            packName.text = game ? game.title : (candidates.length ? candidates[0].title : "")
            packVersion.text = game ? root.nextVersion((launcher.gameSettings[game.id] || {}).customPackVersion) : "1"
            changelog.text = ""
            open()
        }
        Connections {
            target: cloud
            function onStateChanged() { if (cloud.state !== "" && publisher.opened) publisher.step = "busy" }
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Form
            ColumnLayout {
                visible: publisher.step === "form"
                Layout.fillWidth: true
                spacing: 0
                Text { visible: !publisher.fixedGame; text: "Jogo"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.bottomMargin: 8 }
                ChoiceField {
                    id: gamePick
                    visible: !publisher.fixedGame
                    Layout.fillWidth: true
                    model: publisher.candidates.map(function(g) { return g.title + "  ·  " + g.version + " " + g.loader })
                    onActivated: function(index) { packName.text = publisher.candidates[index].title }
                }
                Text {
                    visible: !publisher.fixedGame && publisher.candidates.length === 0
                    Layout.fillWidth: true; Layout.topMargin: 8
                    wrapMode: Text.WordWrap
                    text: "Nenhum jogo com Fabric, Forge ou NeoForge disponível. Crie um jogo com mods primeiro."
                    color: Theme.danger; font.family: Theme.font; font.pixelSize: 13
                }
                Text { text: "Nome"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: publisher.fixedGame ? 0 : 18; Layout.bottomMargin: 8 }
                InputField { id: packName; Layout.fillWidth: true; maximumLength: 80; placeholderText: "Nome do modpack" }
                Text { text: "Versão"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 18; Layout.bottomMargin: 8 }
                InputField { id: packVersion; Layout.fillWidth: true; maximumLength: 40; placeholderText: "1" }
                Text { text: "Novidades (opcional)"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 18; Layout.bottomMargin: 8 }
                TextArea {
                    id: changelog
                    Layout.fillWidth: true
                    Layout.preferredHeight: 84
                    wrapMode: TextEdit.Wrap
                    placeholderText: "Ex.: adicionei Create e deixei a noite mais escura."
                    placeholderTextColor: Theme.faint
                    color: Theme.text
                    font.family: Theme.font; font.pixelSize: 14
                    padding: 12
                    background: Rectangle { radius: Theme.radiusMd; color: Theme.fill; border.width: 1; border.color: changelog.activeFocus ? Theme.accentLine : Theme.line }
                }
                Text {
                    Layout.fillWidth: true; Layout.topMargin: 14
                    wrapMode: Text.WordWrap
                    text: "Mods do Modrinth vão só como referência. Configs e mods de fora do Modrinth são enviados para a nuvem (até 24 MB cada)."
                    color: Theme.faint; font.family: Theme.font; font.pixelSize: 12
                }
                RowLayout {
                    Layout.topMargin: 24
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Cancelar"; quiet: true; reduced: root.reduced; onClicked: publisher.close() }
                    ActionButton {
                        text: publisher.fixedGame ? "Publicar versão" : "Criar e publicar"; primary: true; reduced: root.reduced
                        enabled: publisher.game !== null && packName.text.trim() !== "" && packVersion.text.trim() !== "" && cloud.state === ""
                        onClicked: cloud.publish(publisher.game, publisher.saved, packName.text, packVersion.text, changelog.text)
                    }
                }
            }

            // Working
            ColumnLayout {
                visible: publisher.step === "busy"
                Layout.fillWidth: true
                spacing: 14
                Item { Layout.preferredHeight: 6 }
                Rectangle {
                    id: busyTrack
                    Layout.fillWidth: true; height: 4; radius: 2; color: "#24FFFFFF"; clip: true
                    Rectangle {
                        width: parent.width * 0.3; height: parent.height; radius: 2; color: Theme.accent
                        NumberAnimation on x { running: publisher.step === "busy" && !root.reduced; from: -busyTrack.width * 0.3; to: busyTrack.width; duration: 1100; loops: Animation.Infinite; easing.type: Easing.InOutCubic }
                    }
                }
                Text { Layout.fillWidth: true; text: cloud.detail; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14; elide: Text.ElideRight }
                Item { Layout.preferredHeight: 8 }
            }

            // Done
            ColumnLayout {
                visible: publisher.step === "done"
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Versão " + (publisher.outcome.version || "") + " no ar. Mande o código para os amigos: eles colam em Modpacks → Customizado → Entrar."
                    color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 20
                    implicitHeight: 92
                    radius: Theme.radiusLg
                    color: Theme.accentSoft
                    border.width: 1; border.color: Theme.accentLine
                    Text {
                        anchors.centerIn: parent
                        text: publisher.outcome.code || ""
                        color: Theme.text
                        font.family: "Consolas"; font.pixelSize: 44; font.weight: Font.Bold; font.letterSpacing: 8
                    }
                }
                RowLayout {
                    Layout.topMargin: 14
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Copiar código"; iconName: "copy"; primary: true; reduced: root.reduced
                        onClicked: { servers.copyText(publisher.outcome.code); root.notify("Código copiado.") }
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: "Copiar link de editor"; iconName: "edit"; quiet: true; reduced: root.reduced
                        onClicked: { servers.copyText(cloud.editorLink(publisher.outcome.code, publisher.outcome.editorKey)); root.notify("Link de editor copiado. Quem tiver ele pode publicar versões.") }
                    }
                }
                Text {
                    Layout.fillWidth: true; Layout.topMargin: 12
                    wrapMode: Text.WordWrap
                    text: "O link de editor é a chave do modpack: guarde e só passe para quem também vai publicar (ou para o seu outro PC)."
                    color: Theme.faint; font.family: Theme.font; font.pixelSize: 12
                }
                RowLayout {
                    Layout.topMargin: 22
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Pronto"; quiet: true; reduced: root.reduced; onClicked: publisher.close() }
                }
            }
        }
    }
}
