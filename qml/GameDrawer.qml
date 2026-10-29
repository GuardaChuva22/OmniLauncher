import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Right-side details drawer. Clicking outside, Escape or the close button dismisses it.
FocusScope {
    id: root
    property var game: null
    property bool open: false
    property bool reduced: false
    property Item blurSource: null
    signal closeRequested()
    signal playRequested()
    signal editRequested()
    signal removeRequested()
    signal duplicateRequested()
    signal profileRequested()
    signal modsRequested()
    signal modVersionRequested(var mod)
    signal packVersionsRequested()
    property string updatesCheckedFor: ""
    function checkModUpdates(force) {
        if (!game || vanilla || (!force && updatesCheckedFor === gameId)) return
        updatesCheckedFor = gameId
        mods.checkUpdates(gameId, game.version, game.loader)
    }
    signal notify(string message)

    readonly property string gameId: game ? game.id : ""
    readonly property bool hasProfile: launcher.activeProfileId !== ""
    readonly property bool vanilla: game ? game.loader === "Vanilla" : true
    readonly property real panelWidth: Math.max(420, Math.min(540, width * 0.44))
    readonly property int heroHeight: 220
    readonly property color tint: Theme.coverTint(game ? game.coverId : "")
    // Saved values for this game (stats included) and the effective launch settings.
    readonly property var saved: game ? (launcher.gameSettings[game.id] || ({})) : ({})
    readonly property var effective: (launcher.gameSettings, launcher.preferences, game ? launcher.launchSettings(game.id) : ({}))
    readonly property bool thisRunning: runner.running && runner.gameId === gameId
    readonly property bool otherRunning: runner.running && runner.gameId !== gameId
    readonly property var resolutions: [{ label: "Padrão do jogo", value: "" }, { label: "1280 × 720", value: "1280x720" },
                                        { label: "1600 × 900", value: "1600x900" }, { label: "1920 × 1080", value: "1920x1080" },
                                        { label: "2560 × 1440", value: "2560x1440" }]

    function lastPlayedText() {
        if (!saved.lastPlayed) return "Nunca jogado"
        const date = new Date(saved.lastPlayed)
        const days = Math.floor((Date.now() - date.getTime()) / 86400000)
        if (days < 1) return "Hoje, " + date.toLocaleTimeString(Qt.locale(), "HH:mm")
        if (days < 2) return "Ontem"
        if (days < 7) return days + " dias atrás"
        return date.toLocaleDateString(Qt.locale(), "d 'de' MMM yyyy")
    }
    function playTimeText() {
        const seconds = saved.playSeconds || 0
        if (seconds < 60) return seconds > 0 ? "Menos de 1 min" : "—"
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        return hours > 0 ? hours + " h " + minutes + " min" : minutes + " min"
    }
    function save(key, value) {
        if (game && !launcher.setGameSetting(game.id, key, value)) notify(launcher.lastError)
    }

    visible: scrim.opacity > 0.001 || panel.x < width
    onGameIdChanged: { tabs.currentIndex = 0; flick.contentY = 0; if (gameId) mods.loadInstalled(gameId) }
    onOpenChanged: if (open) playButton.forceActiveFocus()


    Item {
        id: backdrop
        anchors.fill: parent
        opacity: root.open ? 1 : 0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.slow; easing.type: Easing.OutCubic } }
        ShaderEffectSource {
            id: backdropCapture
            anchors.fill: parent
            sourceItem: backdrop.visible ? root.blurSource : null
            textureSize: Qt.size(Math.ceil(width / 2), Math.ceil(height / 2))
            live: backdrop.visible
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: backdropCapture
            autoPaddingEnabled: false
            blurEnabled: true; blur: 1; blurMax: 40
            saturation: -0.1
        }
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Theme.scrim
        opacity: root.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.slow; easing.type: Easing.OutCubic } }
        MouseArea {
            anchors.fill: parent
            enabled: root.open
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onPressed: root.closeRequested()
            onWheel: function(wheel) { wheel.accepted = true }
        }
    }

    GlassPanel {
        id: panel
        width: root.panelWidth
        y: 48
        height: root.height - y - 12
        x: root.open ? root.width - width - 12 : root.width + 40
        source: root.blurSource
        sourceX: x; sourceY: y
        tint: Theme.glassDense
        radius: Theme.radiusXl
        Behavior on x { NumberAnimation { duration: root.reduced ? 0 : 380; easing.type: Easing.OutQuint } }

        // Swallow clicks so they never reach the scrim.
        MouseArea { anchors.fill: parent; hoverEnabled: true; onWheel: function(wheel) { wheel.accepted = false } }

        Flickable {
            id: flick
            anchors.fill: parent
            contentHeight: body.y + body.implicitHeight + 28
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ThinScrollBar {}

            // Hero: the cover art, clipped to the panel's rounded top and fading into the glass.
            Item {
                id: hero
                width: panel.width; height: root.heroHeight
                Image {
                    id: heroArt
                    anchors.fill: parent
                    source: Theme.coverSource(root.game ? root.game.coverId : "")
                    sourceSize: Qt.size(1080, 1620)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: false
                }
                // Rounded on top only: the mask runs past the bottom edge.
                Rectangle {
                    id: heroMask
                    width: parent.width; height: parent.height + Theme.radiusXl
                    radius: Theme.radiusXl
                    visible: false
                    layer.enabled: true
                }
                MultiEffect {
                    anchors.fill: parent
                    source: heroArt
                    maskEnabled: true
                    maskSource: heroMask
                    brightness: -0.04
                    opacity: heroArt.status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
                }
                // Long fade into the panel's own tone, so no edge line shows.
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height * 0.75
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#00171418" }
                        GradientStop { position: 0.55; color: "#99171418" }
                        GradientStop { position: 1; color: "#F2171418" }
                    }
                }
            }

            ColumnLayout {
                id: body
                x: 24; y: root.heroHeight - 84
                width: panel.width - 48
                spacing: 0

                // Identity
                RowLayout {
                    spacing: 18
                    Layout.fillWidth: true
                    Item {
                        Layout.preferredWidth: 92; Layout.preferredHeight: 138
                        Layout.alignment: Qt.AlignBottom
                        SoftGlow { anchors.fill: parent; radius: 10; color: "black"; blurMax: 24; spread: -4; offsetY: 10; opacity: 0.7 }
                        Image {
                            id: thumbArt
                            anchors.fill: parent
                            source: heroArt.source
                            sourceSize: Qt.size(184, 276)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: false
                        }
                        Rectangle { id: thumbMask; anchors.fill: parent; radius: 10; visible: false; layer.enabled: true }
                        MultiEffect { anchors.fill: parent; source: thumbArt; maskEnabled: true; maskSource: thumbMask }
                        Rectangle { anchors.fill: parent; radius: 10; color: "transparent"; border.width: 1; border.color: "#26FFFFFF" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        Layout.bottomMargin: 2
                        spacing: 6
                        Rectangle {
                            Layout.preferredHeight: 22
                            Layout.preferredWidth: loaderLabel.implicitWidth + 18
                            radius: 6
                            color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.16)
                            border.width: 1
                            border.color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.32)
                            Text {
                                id: loaderLabel
                                anchors.centerIn: parent
                                text: root.game ? root.game.loader : ""
                                color: Qt.lighter(root.tint, 1.25)
                                font.family: Theme.font; font.pixelSize: 11; font.weight: Font.DemiBold
                                font.letterSpacing: 0.3
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.game ? root.game.title : ""
                            color: Theme.text
                            font.family: Theme.fontDisplay; font.pixelSize: 24; font.weight: Font.DemiBold
                            font.letterSpacing: -0.4
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            lineHeight: 0.95
                        }
                        Text {
                            text: root.game ? "Minecraft " + root.game.version : ""
                            color: Theme.muted
                            font.family: Theme.font; font.pixelSize: 13
                        }
                    }
                }

                ActionButton {
                    id: playButton
                    objectName: "playButton"
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    Layout.preferredHeight: 54
                    text: root.thisRunning ? "Fechar o jogo" : "Jogar"
                    iconName: root.thisRunning ? "close" : "play"
                    primary: !root.thisRunning
                    enabled: !root.otherRunning && !runner.busy
                    reduced: root.reduced
                    font.pixelSize: 16
                    onClicked: root.thisRunning ? runner.stopGame() : root.playRequested()
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    horizontalAlignment: Text.AlignHCenter
                    text: root.thisRunning ? "Em execução como " + launcher.activeProfile.name
                        : root.otherRunning ? "Outro jogo está em execução"
                        : root.hasProfile ? "Como " + launcher.activeProfile.name : "Escolha um Perfil para jogar"
                    color: Theme.muted
                    font.family: Theme.font; font.pixelSize: 12
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    spacing: 8
                    ActionButton { Layout.fillWidth: true; text: "Editar"; iconName: "edit"; reduced: root.reduced; onClicked: root.editRequested() }
                    ActionButton { Layout.fillWidth: true; text: "Duplicar"; iconName: "copy"; reduced: root.reduced; onClicked: root.duplicateRequested() }
                    ActionButton {
                        Layout.preferredWidth: 40
                        iconName: "folder"; reduced: root.reduced
                        Accessible.name: "Abrir pasta"
                        ToolTip.visible: hovered; ToolTip.text: "Abrir pasta"; ToolTip.delay: 500
                        onClicked: runner.openGameFolder(root.gameId)
                    }
                }

                SegmentTabs {
                    id: tabs
                    Layout.fillWidth: true
                    Layout.topMargin: 26
                    model: ["Visão geral", "Mods", "Ajustes"]
                    reduced: root.reduced
                    onCurrentIndexChanged: if (currentIndex === 1) root.checkModUpdates(false)
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    currentIndex: tabs.currentIndex
                    Layout.preferredHeight: children[currentIndex] ? children[currentIndex].implicitHeight : 0

                    // Visão geral
                    ColumnLayout {
                        spacing: 0
                        InfoRow {
                            label: "Perfil"
                            iconName: "user"
                            value: root.hasProfile ? launcher.activeProfile.name : ""
                            ActionButton {
                                text: root.hasProfile ? "Trocar" : "Escolher"
                                quiet: true; height: 30; reduced: root.reduced
                                font.pixelSize: 12
                                onClicked: root.profileRequested()
                            }
                        }
                        InfoRow { label: "Versão do Minecraft"; iconName: "cube"; value: root.game ? root.game.version : "" }
                        InfoRow {
                            visible: !!root.saved.packProjectId
                            label: "Modpack"; iconName: "packs"
                            value: root.saved.packVersionNumber || ""
                            ActionButton {
                                text: "Versões"; quiet: true; height: 30; reduced: root.reduced
                                font.pixelSize: 12
                                enabled: mods.packState === ""
                                onClicked: root.packVersionsRequested()
                            }
                        }
                        InfoRow { label: "Tipo de instalação"; iconName: "box"; value: root.game ? root.game.loader + (root.game.loaderVersion ? " " + root.game.loaderVersion : "") : "" }
                        InfoRow {
                            label: "Mods"; iconName: "puzzle"
                            value: root.vanilla ? "Não se aplica"
                                : mods.gameId !== root.gameId || mods.installed.length === 0 ? "Nenhum"
                                : mods.installed.length === 1 ? "1 mod" : mods.installed.length + " mods"
                        }
                        InfoRow { label: "Última sessão"; iconName: "calendar"; value: root.lastPlayedText() }
                        InfoRow { label: "Tempo de jogo"; iconName: "clock"; value: root.playTimeText() }
                        InfoRow {
                            label: "Pasta"; iconName: "folder"; divider: false
                            ActionButton {
                                text: "Abrir"; quiet: true; height: 30; reduced: root.reduced
                                font.pixelSize: 12
                                onClicked: runner.openGameFolder(root.gameId)
                            }
                        }
                        ActionButton {
                            Layout.topMargin: 18
                            text: "Remover jogo"; iconName: "trash"
                            danger: true; quiet: true; centered: false
                            reduced: root.reduced
                            onClicked: root.removeRequested()
                        }
                    }

                    // Mods: search, update check, per-mod update/version/enable/remove.
                    ColumnLayout {
                        id: modsTab
                        spacing: 0
                        property string filter: ""
                        readonly property var list: root.gameId !== "" && mods.gameId === root.gameId ? mods.installed : []
                        readonly property var shown: list.filter(function(mod) {
                            const query = filter.trim().toLowerCase()
                            return query === "" || mod.title.toLowerCase().indexOf(query) !== -1 || mod.file.toLowerCase().indexOf(query) !== -1
                        })
                        readonly property int disabledCount: list.filter(function(mod) { return !mod.enabled }).length

                        RowLayout {
                            visible: !root.vanilla
                            Layout.fillWidth: true
                            Layout.topMargin: 14
                            spacing: 8
                            InputField {
                                Layout.fillWidth: true
                                iconName: "search"; clearable: true
                                placeholderText: "Buscar nos mods"
                                onTextChanged: modsTab.filter = text
                            }
                            ActionButton {
                                text: "Adicionar"; iconName: "plus"; reduced: root.reduced
                                onClicked: root.modsRequested()
                            }
                            ActionButton {
                                Layout.preferredWidth: 40
                                iconName: "folder"; reduced: root.reduced
                                Accessible.name: "Pasta de mods"
                                ToolTip.visible: hovered; ToolTip.text: "Pasta de mods"; ToolTip.delay: 500
                                onClicked: runner.openModsFolder(root.gameId)
                            }
                        }

                        RowLayout {
                            visible: !root.vanilla && modsTab.list.length > 0
                            Layout.fillWidth: true
                            Layout.topMargin: 14
                            Layout.bottomMargin: 6
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: (modsTab.list.length === 1 ? "1 mod" : modsTab.list.length + " mods")
                                    + (modsTab.disabledCount > 0 ? "  ·  " + modsTab.disabledCount + " desativado" + (modsTab.disabledCount > 1 ? "s" : "") : "")
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 12
                            }
                            Text {
                                visible: mods.checkingUpdates
                                text: "Verificando atualizações…"
                                color: Theme.faint
                                font.family: Theme.font; font.pixelSize: 12
                            }
                            ActionButton {
                                visible: !mods.checkingUpdates && mods.updateCount > 0
                                implicitHeight: 30
                                text: "Atualizar " + (mods.updateCount === 1 ? "1 mod" : mods.updateCount + " mods")
                                iconName: "download"; iconSize: 14
                                primary: true; reduced: root.reduced
                                font.pixelSize: 12
                                onClicked: mods.updateAll(root.gameId, root.game.version, root.game.loader)
                            }
                            ActionButton {
                                visible: !mods.checkingUpdates && mods.updateCount === 0
                                implicitHeight: 30
                                text: "Verificar atualizações"
                                quiet: true; reduced: root.reduced
                                font.pixelSize: 12
                                onClicked: root.checkModUpdates(true)
                            }
                        }

                        Repeater {
                            model: modsTab.shown
                            delegate: Item {
                                id: modRow
                                required property var modelData
                                readonly property bool hasUpdate: (modelData.updateVersionId || "") !== ""
                                readonly property bool busy: modelData.projectId !== "" && mods.installing.indexOf(modelData.projectId) !== -1
                                Layout.fillWidth: true
                                implicitHeight: 60

                                HoverHandler { id: rowHover }
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.leftMargin: -8; anchors.rightMargin: -8
                                    anchors.topMargin: 2; anchors.bottomMargin: 2
                                    radius: 10
                                    color: rowHover.hovered ? "#08FFFFFF" : "transparent"
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 12
                                    Rectangle {
                                        Layout.preferredWidth: 38; Layout.preferredHeight: 38
                                        radius: 9; color: Theme.fill; clip: true
                                        opacity: modRow.modelData.enabled ? 1 : 0.4
                                        UiIcon { anchors.centerIn: parent; visible: modIcon.status !== Image.Ready; width: 16; height: 16; name: "puzzle"; color: Theme.faint }
                                        Image {
                                            id: modIcon
                                            anchors.fill: parent
                                            source: modRow.modelData.iconUrl
                                            sourceSize: Qt.size(76, 76)
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            Layout.fillWidth: true
                                            text: modRow.modelData.title
                                            color: modRow.modelData.enabled ? Theme.text : Theme.muted
                                            font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        RowLayout {
                                            spacing: 6
                                            Text {
                                                Layout.maximumWidth: 150
                                                text: modRow.modelData.versionNumber || modRow.modelData.file
                                                color: Theme.faint
                                                font.family: Theme.font; font.pixelSize: 11
                                                elide: Text.ElideMiddle
                                            }
                                            Rectangle {
                                                visible: modRow.hasUpdate
                                                Layout.preferredHeight: 18
                                                Layout.preferredWidth: updateLabel.implicitWidth + 12
                                                radius: 5
                                                color: Theme.accentSoft
                                                Text {
                                                    id: updateLabel
                                                    anchors.centerIn: parent
                                                    text: "→ " + (modRow.modelData.updateVersionNumber || "")
                                                    color: Theme.accentText
                                                    font.family: Theme.font; font.pixelSize: 10; font.weight: Font.DemiBold
                                                }
                                            }
                                            Text {
                                                visible: !modRow.modelData.enabled
                                                text: "desativado"
                                                color: Theme.faint
                                                font.family: Theme.font; font.pixelSize: 11
                                            }
                                        }
                                    }
                                    Text {
                                        visible: modRow.busy
                                        text: "Instalando…"
                                        color: Theme.muted
                                        font.family: Theme.font; font.pixelSize: 11
                                    }
                                    ActionButton {
                                        visible: modRow.hasUpdate && !modRow.busy
                                        width: 30; height: 30; round: true
                                        iconName: "download"; iconSize: 14
                                        primary: true; reduced: root.reduced
                                        Accessible.name: "Atualizar " + modRow.modelData.title
                                        ToolTip.visible: hovered; ToolTip.text: "Atualizar para " + modRow.modelData.updateVersionNumber; ToolTip.delay: 400
                                        onClicked: mods.installVersionOf(modRow.modelData.projectId, modRow.modelData.updateVersionId,
                                                                         root.gameId, root.game.version, root.game.loader)
                                    }
                                    ToggleSwitch {
                                        checked: modRow.modelData.enabled
                                        reduced: root.reduced
                                        Accessible.name: "Ativar " + modRow.modelData.title
                                        onToggled: mods.setEnabled(root.gameId, modRow.modelData.file, checked)
                                    }
                                    ActionButton {
                                        width: 30; height: 30; round: true
                                        quiet: true; iconName: "dots"; iconSize: 14
                                        reduced: root.reduced
                                        Accessible.name: "Mais opções de " + modRow.modelData.title
                                        onClicked: modMenu.popup()
                                    }
                                }
                                Menu {
                                    id: modMenu
                                    topPadding: 6; bottomPadding: 6
                                    background: Rectangle {
                                        implicitWidth: 200
                                        color: Theme.popup; radius: 12
                                        border.width: 1; border.color: Theme.lineStrong
                                    }
                                    delegate: MenuItem {
                                        id: menuItem
                                        implicitHeight: 36
                                        leftPadding: 14; rightPadding: 14
                                        contentItem: Text {
                                            text: menuItem.text
                                            color: !menuItem.enabled ? Theme.faint : menuItem.text === "Remover" ? Theme.danger : Theme.text
                                            font.family: Theme.font; font.pixelSize: 13
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            x: 6; width: parent.width - 12; height: parent.height
                                            radius: 8
                                            color: menuItem.highlighted ? Theme.fillActive : "transparent"
                                        }
                                    }
                                    Action {
                                        text: "Mudar versão…"
                                        enabled: modRow.modelData.projectId !== ""
                                        onTriggered: root.modVersionRequested(modRow.modelData)
                                    }
                                    Action {
                                        text: "Remover"
                                        onTriggered: mods.remove(root.gameId, modRow.modelData.file)
                                    }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                            }
                        }

                        Text {
                            visible: modsTab.list.length > 0 && modsTab.shown.length === 0
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 24
                            text: "Nenhum mod corresponde à busca"
                            color: Theme.muted
                            font.family: Theme.font; font.pixelSize: 13
                        }
                        ColumnLayout {
                            visible: root.vanilla || modsTab.list.length === 0
                            Layout.fillWidth: true
                            Layout.topMargin: 28
                            spacing: 8
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 52; height: 52; radius: 26
                                color: Theme.fill
                                border.width: 1; border.color: Theme.line
                                UiIcon { anchors.centerIn: parent; width: 22; height: 22; name: "puzzle"; color: Theme.muted }
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: 6
                                text: root.vanilla ? "Vanilla não usa mods" : "Nenhum mod instalado"
                                color: Theme.text
                                font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: root.vanilla ? "Mude o tipo de instalação para Fabric, Forge ou NeoForge para adicionar mods."
                                    : "Busque no Modrinth ou coloque arquivos .jar na pasta de mods."
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 13
                            }
                            ActionButton {
                                visible: root.vanilla
                                Layout.alignment: Qt.AlignHCenter
                                Layout.topMargin: 8
                                text: "Mudar tipo de instalação"; reduced: root.reduced
                                onClicked: root.editRequested()
                            }
                        }
                    }

                    // Ajustes, saved per game. Unset values follow the global defaults.
                    ColumnLayout {
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            Text { text: "Memória"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: memory.value + " GB"; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold }
                        }
                        ValueSlider {
                            id: memory
                            Layout.fillWidth: true
                            from: 1; to: 16; stepSize: 1
                            snapMode: Slider.SnapAlways
                            reduced: root.reduced
                            // Follows the saved value whenever it is not being dragged.
                            Binding on value { when: !memory.pressed; value: Math.round((root.effective.memoryMb || 4096) / 1024) }
                            onPressedChanged: if (!pressed) root.save("memoryMb", value * 1024)
                            Keys.onReleased: root.save("memoryMb", value * 1024)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 12
                            Text { text: "Java"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: "Automático"; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "O OmniLauncher baixa a versão do Java que esta versão do Minecraft pede."
                            color: Theme.faint
                            font.family: Theme.font; font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        Text { text: "Resolução da janela"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 12 }
                        ChoiceField {
                            id: resolutionChoice
                            Layout.fillWidth: true
                            model: root.resolutions.map(function(option) { return option.label })
                            Binding on currentIndex {
                                when: !resolutionChoice.popup.visible
                                value: Math.max(0, root.resolutions.findIndex(function(option) { return option.value === (root.effective.resolution || "") }))
                            }
                            onActivated: function(index) { root.save("resolution", root.resolutions[index].value) }
                        }

                        Text { text: "Argumentos da JVM"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 12 }
                        InputField {
                            id: jvmArgs
                            Layout.fillWidth: true
                            placeholderText: "-XX:+UseG1GC"
                            font.family: "Cascadia Mono"
                            Binding on text { when: !jvmArgs.activeFocus; value: root.effective.jvmArgs || "" }
                            onEditingFinished: if (text.trim() !== (root.effective.jvmArgs || "")) root.save("jvmArgs", text)
                        }
                    }
                }
            }
        }

        // Fixed controls over the hero
        Row {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14
            spacing: 8
            Repeater {
                model: [{ icon: "edit", label: "Editar jogo" }, { icon: "close", label: "Fechar" }]
                delegate: AbstractButton {
                    id: heroButton
                    required property var modelData
                    required property int index
                    width: 34; height: 34
                    hoverEnabled: true
                    Accessible.name: modelData.label
                    ToolTip.visible: hovered; ToolTip.text: modelData.label; ToolTip.delay: 500
                    onClicked: index === 0 ? root.editRequested() : root.closeRequested()
                    background: Rectangle {
                        radius: 17
                        color: heroButton.down ? "#99000000" : heroButton.hovered ? "#80000000" : "#59000000"
                        border.width: heroButton.visualFocus ? 2 : 1
                        border.color: heroButton.visualFocus ? Theme.focusRing : "#1FFFFFFF"
                        Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                    }
                    contentItem: Item {
                        UiIcon { anchors.centerIn: parent; width: 15; height: 15; name: heroButton.modelData.icon; color: Theme.text }
                    }
                }
            }
        }
    }
}
