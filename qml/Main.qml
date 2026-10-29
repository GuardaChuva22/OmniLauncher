import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    title: "OmniLauncher"
    width: 1440; height: 880
    minimumWidth: 1000; minimumHeight: 680
    visible: true
    color: Theme.base
    flags: Qt.Window | Qt.FramelessWindowHint
    font.family: Theme.font
    font.pixelSize: 14

    property int page: 0
    property string selectedGameId: preferences.selectedGameId
    property string previewGameId: ""
    property string drawerGameId: ""
    property bool drawerOpen: false
    property string launchedTitle: ""
    // A Microsoft profile needs a fresh session first; the game waits here meanwhile.
    property string pendingLaunchGameId: ""
    // What the version picker is choosing for: a pack (new install or version change) or a mod.
    property var pickerTarget: ({})
    property string packChangeGameId: ""
    property string launchedCover: "meadow"
    // The launch screen covers the app while files are prepared, and stays on a
    // failure until it is dismissed. A running game does not block the launcher.
    readonly property bool launching: runner.busy || runner.state === "failed"
    readonly property bool reduced: launcher.reducedMotion
    readonly property bool compactSidebar: width < 1180
    readonly property real sidebarWidth: compactSidebar ? 76 : 248
    readonly property var selectedGame: gameById(selectedGameId) || (launcher.games.length ? launcher.games[0] : null)
    readonly property var drawerGame: gameById(drawerGameId)
    readonly property var previewGame: gameById(previewGameId) || selectedGame
    readonly property string atmosphericCover: launching ? launchedCover
        : drawerOpen && drawerGame ? drawerGame.coverId
        : previewGame ? previewGame.coverId : "meadow"
    readonly property var filteredGames: launcher.games.filter(function(game) {
        const query = home.searchText.trim().toLocaleLowerCase()
        return (home.loaderFilter === "Todos" || game.loader === home.loaderFilter)
            && game.title.toLocaleLowerCase().indexOf(query) !== -1
    })
    readonly property bool modalOpen: gameEditor.shown || profileEditor.shown || removal.shown || modCatalog.shown
        || versionPicker.shown
    readonly property var atmosphericSources: {
        const ids = Theme.covers.slice()
        for (let game of launcher.games) if (ids.indexOf(game.coverId) === -1) ids.push(game.coverId)
        return ids
    }

    Settings { id: preferences; category: "Home"; property string selectedGameId: "" }
    onSelectedGameIdChanged: preferences.selectedGameId = selectedGameId
    onPageChanged: { previewGameId = ""; closeDrawer() }

    function gameById(id) {
        if (!id) return null
        for (let game of launcher.games) if (game.id === id) return game
        return null
    }
    function notify(message) { toastText.text = message; toast.open(); toastTimer.restart() }
    function openDrawer(game) {
        if (!game) return
        selectedGameId = game.id
        drawerGameId = game.id
        previewGameId = ""
        drawerOpen = true
    }
    function closeDrawer() {
        if (!drawerOpen) return
        drawerOpen = false
        if (page === 0) home.forceActiveFocus()
    }
    function editGame(game) { gameEditor.prepare(game) }
    function confirmRemoval(id, title, isProfile) {
        removal.targetId = id
        removal.targetTitle = title
        removal.isProfile = isProfile
        removal.open()
    }
    function duplicateGame(game) {
        if (!game) return
        const id = launcher.addGame(game.title.slice(0, 64) + " (cópia)", game.version, game.loader, game.coverId, game.loaderVersion || "")
        if (id) { selectedGameId = id; notify("Jogo duplicado.") }
        else notify(launcher.lastError)
    }
    function beginLaunch(game) {
        if (!game) return
        selectedGameId = game.id
        if (runner.running) {
            notify(runner.gameId === game.id ? "Esse jogo já está aberto." : "Feche o jogo em execução antes de iniciar outro.")
            return
        }
        if (!launcher.activeProfileId) {
            profileMenu.open()
            notify("Escolha um Perfil para iniciar.")
            return
        }
        launchedTitle = game.title
        launchedCover = game.coverId
        previewGameId = ""
        drawerOpen = false
        if (launcher.activeProfile.type === "microsoft") {
            pendingLaunchGameId = game.id
            auth.requestSession(launcher.activeProfile.uuid)
            return
        }
        runner.launch(game, launcher.activeProfile.name, launcher.launchSettings(game.id))
    }
    Connections {
        target: auth
        function onSignedIn(profile) {
            window.bringToFront()
            if (launcher.addMicrosoftProfile(profile.uuid, profile.name)) window.notify("Conta conectada: " + profile.name)
            else window.notify(launcher.lastError)
            profileMenu.open()
        }
        function onSignInFailed(message) {
            window.bringToFront()
            profileMenu.open()
        }
        function onSessionReady(uuid, account) {
            const game = window.gameById(window.pendingLaunchGameId)
            window.pendingLaunchGameId = ""
            if (game && launcher.activeProfile.uuid === uuid)
                runner.launch(game, account.name, launcher.launchSettings(game.id), account)
        }
        function onSessionFailed(uuid, message) {
            window.pendingLaunchGameId = ""
            window.notify(message)
            profileMenu.open()
        }
    }
    function leaveLaunch() {
        if (runner.busy) runner.cancel()
        else runner.dismiss()
        home.forceActiveFocus()
    }
    function pickPackVersion(pack, gameId) {
        pickerTarget = { kind: "pack", projectId: pack.id, imageUrl: pack.imageUrl || "", gameId: gameId || "" }
        const saved = gameId ? (launcher.gameSettings[gameId] || {}) : {}
        versionPicker.title = "Versões de " + pack.title
        versionPicker.currentId = saved.packVersionId || ""
        versionPicker.actionText = gameId ? "Mudar para esta versão" : "Instalar esta versão"
        mods.fetchProjectVersions(pack.id)
        versionPicker.open()
    }
    function pickModVersion(game, mod) {
        pickerTarget = { kind: "mod", projectId: mod.projectId, game: game }
        versionPicker.title = "Versões de " + mod.title
        versionPicker.currentId = mod.versionId || ""
        versionPicker.actionText = "Usar esta versão"
        mods.fetchProjectVersions(mod.projectId, game.version, game.loader)
        versionPicker.open()
    }
    function bringToFront() {
        if (window.visibility === Window.Minimized) window.showNormal()
        window.raise()
        window.requestActivate()
    }
    function versionsFor(version) {
        const list = runner.versions.length ? runner.versions.slice() : Theme.versions.slice()
        if (version && list.indexOf(version) === -1) list.unshift(version)
        return list
    }
    Connections {
        target: runner
        function onGameStarted(detached) {
            const after = launcher.preferences.afterLaunch
            if (detached && after === "close") Qt.quit()
            else if (after === "minimize") window.showMinimized()
        }
        function onGameExited(gameId, seconds, exitCode) {
            launcher.recordSession(gameId, seconds)
            if (window.visibility === Window.Minimized) window.showNormal()
            window.raise()
            window.requestActivate()
        }
    }
    Connections {
        target: launcher
        function onGamesChanged() {
            Qt.callLater(function() {
                if (!gameById(selectedGameId)) selectedGameId = launcher.games.length ? launcher.games[0].id : ""
                if (!gameById(drawerGameId)) window.drawerOpen = false
            })
        }
    }
    Component.onCompleted: {
        if (!gameById(selectedGameId) && launcher.games.length) selectedGameId = launcher.games[0].id
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !window.modalOpen && !window.launching; onActivated: gameEditor.prepare(null) }
    Shortcut {
        sequence: "Ctrl+F"; enabled: !window.modalOpen && !window.launching
        onActivated: { window.page = 0; window.closeDrawer(); home.focusSearch() }
    }
    Shortcut {
        sequence: "Escape"
        enabled: window.launching || (window.drawerOpen && !window.modalOpen && !profileMenu.visible)
        onActivated: window.launching ? window.leaveLaunch() : window.closeDrawer()
    }
    Shortcut { sequence: "F11"; onActivated: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized() }

    // Everything inside appRoot can be blurred by the modal backdrop and popups.
    Item {
        id: appRoot
        anchors.fill: parent

        // Drawer glass samples this scene: background, navigation and pages.
        Item {
            id: scene
            anchors.fill: parent
            clip: true

            // Opaque base: blur captures of the scene must not be see-through.
            Rectangle { anchors.fill: parent; color: Theme.base }

            // Each atmosphere is blurred once at a small resolution, then scaled up.
            // Magnifying that small blur shows a faint block grid, so the whole layer
            // gets a second, light blur at window resolution. Nothing animates at rest.
            Item {
                id: atmosphereLayer
                anchors.fill: parent
                // Applied as the layer's own effect: hiding the layer and blurring it from
                // outside froze the crossfades inside it.
                layer.enabled: true
                layer.effect: MultiEffect {
                    autoPaddingEnabled: false
                    blurEnabled: true
                    blur: 1
                    blurMax: 24
                }
                Repeater {
                    model: window.atmosphericSources
                    delegate: Item {
                        id: atmosphere
                        required property string modelData
                        width: 640
                        height: Math.ceil(640 * scene.height / scene.width)
                        scale: scene.width / 640 * (window.launching ? 1.1 : 1.04)
                        transformOrigin: Item.TopLeft
                        opacity: window.atmosphericCover === modelData ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: window.reduced ? 0 : 520; easing.type: Easing.OutCubic } }
                        Behavior on scale { NumberAnimation { duration: window.reduced ? 0 : 900; easing.type: Easing.OutCubic } }
                        Image {
                            id: backgroundArt
                            anchors.fill: parent
                            source: Theme.coverSource(atmosphere.modelData)
                            sourceSize: Qt.size(640, 960)
                            fillMode: Image.PreserveAspectCrop
                            visible: false
                        }
                        MultiEffect {
                            anchors.fill: parent; anchors.margins: -40
                            source: backgroundArt
                            blurEnabled: true; blur: 1; blurMax: 56
                            saturation: -0.05
                            brightness: -0.2
                            opacity: 0.7
                        }
                    }
                }
            }
            // Legibility: darker toward the bottom and the left edge, gentle vignette.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: "#33151216" }
                    GradientStop { position: 0.45; color: "#8C151216" }
                    GradientStop { position: 1; color: "#F2151216" }
                }
            }
            Rectangle {
                width: scene.height; height: scene.width
                anchors.centerIn: parent
                rotation: -90
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00151216" }
                    GradientStop { position: 0.7; color: "#33151216" }
                    GradientStop { position: 1; color: "#99151216" }
                }
            }

            // Fine grain over the dark gradients prevents visible banding.
            Image {
                anchors.fill: parent
                source: Theme.brandSource("noise")
                fillMode: Image.Tile
                smooth: false
                opacity: 0.035
            }

            // Window drag strip, below all interactive content.
            MouseArea {
                width: parent.width; height: 44
                onPressed: window.startSystemMove()
                onDoubleClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
            }

            Sidebar {
                id: sidebar
                width: window.sidebarWidth
                height: parent.height
                page: window.page
                compact: window.compactSidebar
                reduced: window.reduced
                profileMenuOpen: profileMenu.visible
                opacity: window.launching ? 0 : 1
                enabled: !window.launching
                Behavior on opacity { NumberAnimation { duration: window.reduced ? 0 : Theme.normal } }
                Behavior on width { NumberAnimation { duration: window.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
                onPageRequested: function(index) { window.page = index }
                onProfileClicked: profileMenu.visible ? profileMenu.close() : profileMenu.open()
            }

            Item {
                id: pageContent
                x: sidebar.width
                width: parent.width - x; height: parent.height
                opacity: window.launching ? 0 : 1
                enabled: !window.launching
                Behavior on opacity { NumberAnimation { duration: window.reduced ? 0 : 250 } }

                HomePage {
                    id: home
                    anchors.fill: parent
                    visible: window.page === 0
                    focus: visible
                    games: window.filteredGames
                    allGames: launcher.games
                    selectedId: window.drawerOpen ? window.drawerGameId : window.selectedGameId
                    reduced: window.reduced
                    onOpenRequested: function(game) { window.openDrawer(game) }
                    onPlayRequested: function(game) { window.beginLaunch(game) }
                    onEditRequested: function(game) { window.editGame(game) }
                    onCreateRequested: gameEditor.prepare(null)
                    onPreviewChanged: function(id, active) {
                        if (active) window.previewGameId = id
                        else if (window.previewGameId === id) window.previewGameId = ""
                    }
                    onSearchTextChanged: window.previewGameId = ""
                }
                ModpacksPage {
                    anchors.fill: parent
                    visible: window.page === 1
                    reduced: window.reduced
                    onNotify: function(message) { window.notify(message) }
                    onInstallRequested: function(pack) { mods.preparePack(pack.id, pack.imageUrl) }
                    onVersionsRequested: function(pack, gameId) { window.pickPackVersion(pack, gameId) }
                }
                SettingsPage {
                    anchors.fill: parent
                    visible: window.page === 2
                    reduced: window.reduced
                    onManageProfiles: profileMenu.open()
                    onNotify: function(message) { window.notify(message) }
                }
            }
        }

        GameDrawer {
            id: drawer
            anchors.fill: parent
            game: window.drawerGame
            open: window.drawerOpen && window.drawerGame !== null
            reduced: window.reduced
            blurSource: scene
            onCloseRequested: window.closeDrawer()
            onPlayRequested: window.beginLaunch(window.drawerGame)
            onEditRequested: window.editGame(window.drawerGame)
            onDuplicateRequested: window.duplicateGame(window.drawerGame)
            onRemoveRequested: window.confirmRemoval(window.drawerGameId, window.drawerGame ? window.drawerGame.title : "", false)
            onProfileRequested: profileMenu.open()
            onModsRequested: modCatalog.openFor(window.drawerGame)
            onModVersionRequested: function(mod) { window.pickModVersion(window.drawerGame, mod) }
            onPackVersionsRequested: {
                const saved = launcher.gameSettings[window.drawerGameId] || {}
                window.pickPackVersion({ id: saved.packProjectId, title: window.drawerGame.title }, window.drawerGameId)
            }
            onNotify: function(message) { window.notify(message) }
        }

        Item {
            id: launchPresentation
            anchors.fill: parent
            visible: opacity > 0
            enabled: window.launching
            opacity: window.launching ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: window.reduced ? 0 : 420; easing.type: Easing.OutCubic } }
            MouseArea { anchors.fill: parent; enabled: window.launching }

            Column {
                anchors.centerIn: parent
                width: Math.min(520, parent.width * 0.6)
                spacing: 0
                Item {
                    id: launchCover
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 168; height: 252
                    scale: window.launching ? 1 : 0.92
                    Behavior on scale { NumberAnimation { duration: window.reduced ? 0 : 600; easing.type: Easing.OutQuint } }
                    SoftGlow { anchors.fill: parent; color: "black"; blurMax: 48; spread: -8; offsetY: 24; opacity: 0.8 }
                    Image {
                        id: launchArt
                        anchors.fill: parent
                        source: Theme.coverSource(window.launchedCover)
                        sourceSize: Qt.size(336, 504)
                        fillMode: Image.PreserveAspectCrop
                        visible: false
                    }
                    Rectangle { id: launchMask; anchors.fill: parent; radius: Theme.radiusLg; visible: false; layer.enabled: true }
                    MultiEffect { anchors.fill: parent; source: launchArt; maskEnabled: true; maskSource: launchMask }
                }
                Text {
                    width: parent.width
                    topPadding: 34
                    horizontalAlignment: Text.AlignHCenter
                    text: window.launchedTitle
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontDisplay; font.pixelSize: 34; font.weight: Font.DemiBold
                    font.letterSpacing: -0.6
                }
                readonly property bool failed: runner.state === "failed"
                Text {
                    width: parent.width
                    topPadding: 10
                    horizontalAlignment: Text.AlignHCenter
                    text: parent.failed ? "Não foi possível iniciar" : runner.stage
                    color: parent.failed ? Theme.danger : Theme.secondary
                    font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Medium
                }
                Item { width: 1; height: 18; visible: !parent.failed }
                Rectangle {
                    id: launchTrack
                    visible: !parent.failed
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 300; height: 4; radius: 2
                    color: "#24FFFFFF"
                    clip: true
                    Rectangle {
                        visible: runner.progress >= 0
                        width: launchTrack.width * Math.max(0, runner.progress)
                        height: parent.height; radius: 2; color: Theme.accent
                        Behavior on width { NumberAnimation { duration: window.reduced ? 0 : 200 } }
                    }
                    // Unknown duration: a short segment sweeps across the track.
                    Rectangle {
                        id: launchSweep
                        visible: runner.progress < 0
                        width: launchTrack.width * 0.3
                        height: parent.height; radius: 2; color: Theme.accent
                        x: window.reduced ? (launchTrack.width - width) / 2 : 0
                        NumberAnimation on x {
                            running: launchSweep.visible && window.launching && !window.reduced
                            from: -launchSweep.width; to: launchTrack.width
                            duration: 1100; loops: Animation.Infinite; easing.type: Easing.InOutCubic
                        }
                    }
                }
                Text {
                    width: parent.width
                    topPadding: 12
                    horizontalAlignment: Text.AlignHCenter
                    visible: text !== ""
                    text: parent.failed ? runner.error : runner.detail
                    wrapMode: Text.WordWrap
                    maximumLineCount: 10
                    elide: Text.ElideRight
                    color: Theme.muted
                    font.family: Theme.font; font.pixelSize: parent.failed ? 13 : 12
                    lineHeight: 1.1
                }
                Item { width: 1; height: 26 }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    ActionButton {
                        visible: runner.busy
                        text: "Cancelar"; quiet: true; reduced: window.reduced
                        onClicked: window.leaveLaunch()
                    }
                    ActionButton {
                        visible: runner.state === "failed"
                        text: "Ver log"; iconName: "terminal"; quiet: true; reduced: window.reduced
                        onClicked: if (!runner.openLog()) window.notify("Nenhum log disponível para esta tentativa.")
                    }
                    ActionButton {
                        visible: runner.state === "failed"
                        text: "Voltar"; quiet: true; reduced: window.reduced
                        onClicked: window.leaveLaunch()
                    }
                    ActionButton {
                        visible: runner.state === "failed"
                        text: "Tentar de novo"; iconName: "play"; primary: true; reduced: window.reduced
                        onClicked: {
                            const game = window.gameById(runner.gameId)
                            runner.dismiss()
                            window.beginLaunch(game)
                        }
                    }
                }
            }
        }
    }

    // Modal backdrop: the whole app, blurred and dimmed, under every dialog.
    Item {
        id: modalBackdrop
        anchors.fill: parent
        opacity: window.modalOpen ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: window.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
        ShaderEffectSource {
            id: backdropCapture
            anchors.fill: parent
            sourceItem: appRoot
            textureSize: Qt.size(Math.ceil(width / 2), Math.ceil(height / 2))
            live: modalBackdrop.visible
            visible: false
        }
        Connections {
            target: Qt.application
            function onAboutToQuit() { backdropCapture.sourceItem = null }
        }
        MultiEffect {
            anchors.fill: parent
            source: backdropCapture
            autoPaddingEnabled: false
            blurEnabled: true; blur: 1; blurMax: 48
            saturation: -0.1
        }
        Rectangle { anchors.fill: parent; color: Theme.backdropTint }
    }

    // Window chrome stays crisp above the backdrop.
    Row {
        anchors.right: parent.right; anchors.top: parent.top
        Repeater {
            model: ["minimize", window.visibility === Window.Maximized ? "restore" : "maximize", "close"]
            delegate: Button {
                id: captionButton
                required property string modelData
                required property int index
                width: 46; height: 36
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: ["Minimizar", "Maximizar ou restaurar", "Fechar"][index]
                background: Rectangle {
                    color: captionButton.hovered ? (captionButton.index === 2 ? "#C42B1C" : "#1AFFFFFF") : "transparent"
                    Behavior on color { ColorAnimation { duration: 90 } }
                }
                contentItem: Item {
                    UiIcon {
                        name: captionButton.modelData; anchors.centerIn: parent
                        width: 14; height: 14; stroke: 1.2
                        color: captionButton.hovered ? "#FFFFFF" : "#B3B8BA"
                    }
                }
                onClicked: {
                    if (index === 0) window.showMinimized()
                    else if (index === 1) window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                    else window.close()
                }
            }
        }
    }
    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge|Qt.LeftEdge, Qt.TopEdge|Qt.RightEdge, Qt.BottomEdge|Qt.LeftEdge, Qt.BottomEdge|Qt.RightEdge]
        delegate: MouseArea {
            required property int modelData
            readonly property bool resizeLeft: (modelData & Qt.LeftEdge) !== 0
            readonly property bool resizeRight: (modelData & Qt.RightEdge) !== 0
            readonly property bool resizeTop: (modelData & Qt.TopEdge) !== 0
            readonly property bool resizeBottom: (modelData & Qt.BottomEdge) !== 0
            readonly property bool corner: (resizeLeft || resizeRight) && (resizeTop || resizeBottom)
            width: corner ? 9 : resizeLeft || resizeRight ? 5 : window.width - 18
            height: corner ? 9 : resizeTop || resizeBottom ? 5 : window.height - 18
            x: resizeLeft ? 0 : resizeRight ? window.width - width : 9
            y: resizeTop ? 0 : resizeBottom ? window.height - height : 9
            enabled: window.visibility !== Window.Maximized
            cursorShape: corner ? (resizeLeft && resizeTop || resizeRight && resizeBottom ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : resizeLeft || resizeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: window.startSystemResize(modelData)
        }
    }

    // ── Perfis ────────────────────────────────────────────────────────────
    Popup {
        id: profileMenu
        objectName: "profileMenu"
        x: 12
        y: window.height - height - 76
        width: 320
        padding: 14
        topPadding: 18
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: {
            if (profileEditor.visible) profileName.forceActiveFocus()
            else if (!window.modalOpen && !window.drawerOpen) sidebar.profileButton.forceActiveFocus()
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: window.reduced ? 0 : 160; easing.type: Easing.OutCubic }
            NumberAnimation { property: "y"; from: window.height - profileMenu.height - 66; to: window.height - profileMenu.height - 76; duration: window.reduced ? 0 : 260; easing.type: Easing.OutQuint }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: window.reduced ? 0 : 110 } }
        background: GlassPanel {
            source: appRoot
            sourceX: profileMenu.x; sourceY: profileMenu.y
            tint: Theme.menu
            radius: 18
        }
        contentItem: ColumnLayout {
            spacing: 4
            Text {
                text: "Perfis"
                color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: 16; font.weight: Font.DemiBold
                Layout.leftMargin: 8; Layout.bottomMargin: 6
            }
            Text {
                visible: !launcher.profiles.length
                text: "Escolha como você vai jogar."
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 13
                Layout.leftMargin: 8; Layout.bottomMargin: 8
            }
            ListView {
                id: profileList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(launcher.profiles.length * 54, 270)
                visible: launcher.profiles.length > 0
                clip: true
                model: launcher.profiles
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}
                delegate: Item {
                    id: profileRow
                    required property var modelData
                    readonly property bool active: launcher.activeProfileId === modelData.id
                    width: profileList.width; height: 54
                    Button {
                        id: chooseProfile
                        anchors.fill: parent
                        hoverEnabled: true
                        leftPadding: 8; rightPadding: 44
                        onClicked: {
                            if (launcher.selectProfile(profileRow.modelData.id)) profileMenu.close()
                            else window.notify(launcher.lastError)
                        }
                        Accessible.name: "Selecionar Perfil " + profileRow.modelData.name
                        background: Rectangle {
                            radius: 12
                            color: profileRow.active ? Theme.accentSoft : chooseProfile.hovered || chooseProfile.visualFocus ? "#0DFFFFFF" : "transparent"
                            Behavior on color { ColorAnimation { duration: window.reduced ? 0 : Theme.fast } }
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            ProfileAvatar {
                                Layout.preferredWidth: 34; Layout.preferredHeight: 34
                                skin: profileRow.modelData.type === "microsoft" ? (auth.skinRevision, auth.skinSource(profileRow.modelData.uuid)) : ""
                            }
                            ColumnLayout {
                                spacing: 1; Layout.fillWidth: true
                                Text { text: profileRow.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    text: (profileRow.modelData.type === "microsoft" ? "Microsoft" : "Offline") + (profileRow.active ? " · ativo" : "")
                                    color: Theme.muted; font.family: Theme.font; font.pixelSize: 11
                                }
                            }
                            UiIcon { visible: profileRow.active && !removeProfile.visible; name: "check"; color: Theme.accent; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                        }
                    }
                    ActionButton {
                        id: removeProfile
                        anchors.right: parent.right; anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 30; round: true
                        quiet: true; iconName: "trash"; iconSize: 14
                        visible: chooseProfile.hovered || hovered || visualFocus
                        Accessible.name: "Remover Perfil " + profileRow.modelData.name
                        ToolTip.visible: hovered; ToolTip.text: "Remover"; ToolTip.delay: 500
                        onClicked: { profileMenu.close(); window.confirmRemoval(profileRow.modelData.id, profileRow.modelData.name, true) }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 6; Layout.bottomMargin: 6; color: Theme.line }
            Text {
                visible: auth.lastError !== "" && !auth.busy
                Layout.fillWidth: true
                Layout.leftMargin: 8; Layout.rightMargin: 8; Layout.bottomMargin: 6
                text: auth.lastError
                color: Theme.danger
                font.family: Theme.font; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            ActionButton {
                Layout.fillWidth: true; centered: false; quiet: true
                text: auth.busy ? auth.stage + "…" : "Conectar Microsoft"; iconName: "external"
                enabled: !auth.busy
                font.weight: Font.Medium
                onClicked: { profileMenu.close(); auth.signIn() }
            }
            ActionButton {
                Layout.fillWidth: true; centered: false; quiet: true
                text: "Criar perfil offline"; iconName: "plus"
                font.weight: Font.Medium
                onClicked: { profileMenu.close(); launcher.clearError(); profileName.text = ""; profileEditor.open(); profileName.forceActiveFocus() }
            }
        }
    }

    VersionPicker {
        id: versionPicker
        blurSource: appRoot
        reduced: window.reduced
        onPicked: function(versionId) {
            const target = window.pickerTarget
            if (target.kind === "pack") mods.preparePack(target.projectId, target.imageUrl, versionId, target.gameId)
            else if (target.kind === "mod") mods.installVersionOf(target.projectId, versionId, target.game.id, target.game.version, target.game.loader)
        }
    }
    ModCatalog {
        id: modCatalog
        blurSource: appRoot
        reduced: window.reduced
    }
    Connections {
        target: mods
        function onInstallFinished(projectId, ok, message) { window.notify(message) }
        // A prepared pack becomes a game (its cover imported when possible), then its files install.
        function onPackPrepared(info) {
            if (info.gameId) {
                // Version change of an installed pack: same game, new Minecraft/loader as the pack asks.
                const game = window.gameById(info.gameId)
                window.packChangeGameId = info.gameId
                if (game && launcher.updateGame(game.id, game.title, info.version, info.loader, game.coverId, info.loaderVersion))
                    mods.installPackFiles(game.id)
                else { mods.cancelPack(); window.notify(launcher.lastError || "Esse jogo não está mais na biblioteca.") }
                return
            }
            window.packChangeGameId = ""
            let cover = info.coverPath ? launcher.importCover(info.coverPath) : ""
            if (!cover) cover = "dusk"
            const id = launcher.addGame(info.title.slice(0, 72) || "Modpack", info.version, info.loader, cover, info.loaderVersion)
            if (id) mods.installPackFiles(id)
            else { mods.cancelPack(); window.notify(launcher.lastError) }
        }
        function onPackFinished(gameId, ok, message, pack) {
            const changed = window.packChangeGameId !== "" && window.packChangeGameId === gameId
            window.packChangeGameId = ""
            if (ok && pack.projectId) launcher.setPackSource(gameId, pack.projectId, pack.versionId, pack.versionNumber)
            if (!ok && gameId && !changed) launcher.removeGame(gameId)
            window.notify(ok && !changed ? "Modpack instalado. Ele já está em Jogos." : message)
            if (ok && !changed) {
                window.selectedGameId = gameId
                window.page = 0
            }
            if (ok && changed && gameId === mods.gameId) mods.loadInstalled(gameId)
        }
    }

    GlassDialog {
        id: profileEditor
        objectName: "profileEditor"
        title: "Criar perfil offline"
        width: 420
        blurSource: appRoot
        reduced: window.reduced
        onClosed: sidebar.profileButton.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: 0
            ProfileAvatar { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 72; Layout.preferredHeight: 72; Layout.topMargin: 4 }
            Text { text: "Nome do jogador"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 24; Layout.bottomMargin: 8 }
            InputField { id: profileName; objectName: "profileName"; Layout.fillWidth: true; placeholderText: "Seu nome no jogo"; maximumLength: 16; onAccepted: profileCreate.clicked() }
            Text {
                Layout.fillWidth: true; Layout.topMargin: 8
                text: launcher.lastError !== "" ? launcher.lastError : "3 a 16 letras, números ou sublinhado."
                color: launcher.lastError !== "" ? Theme.danger : Theme.faint
                font.family: Theme.font; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 28
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancelar"; quiet: true; reduced: window.reduced; onClicked: profileEditor.close() }
                ActionButton {
                    id: profileCreate
                    text: "Criar perfil"; primary: true; reduced: window.reduced
                    onClicked: if (launcher.addOfflineProfile(profileName.text)) { profileEditor.close(); window.notify("Perfil criado.") }
                }
            }
        }
    }

    GlassDialog {
        id: gameEditor
        objectName: "gameEditor"
        property string editingId: ""
        property string chosenCover: "meadow"
        property string chosenLoader: "Vanilla"
        property string chosenLoaderVersion: ""
        // The loader build list follows the chosen Minecraft version.
        readonly property bool loaderListReady: runner.loaderVersionsFor === versionChoice.currentText
            && runner.loaderVersionsLoader === chosenLoader && runner.loaderVersions.length > 0
        readonly property string loaderVersionToSave: chosenLoader !== "Vanilla"
            ? (chosenLoaderVersion !== "" && (!loaderListReady || runner.loaderVersions.indexOf(chosenLoaderVersion) !== -1)
               ? chosenLoaderVersion : runner.recommendedLoader) : ""
        readonly property color tint: Theme.coverTint(chosenCover)
        title: editingId ? "Editar jogo" : "Criar jogo"
        width: Math.min(760, window.width - 80)
        blurSource: appRoot
        reduced: window.reduced
        function prepare(game) {
            launcher.clearError()
            editingId = game ? game.id : ""
            chosenCover = game ? game.coverId : "meadow"
            chosenLoader = game ? game.loader : "Vanilla"
            chosenLoaderVersion = game && game.loaderVersion ? game.loaderVersion : ""
            gameName.text = game ? game.title : ""
            const version = game ? game.version : (runner.latestRelease || window.versionsFor("")[0])
            versionChoice.model = window.versionsFor(version)
            versionChoice.currentIndex = Math.max(0, versionChoice.model.indexOf(version))
            runner.fetchLoaderVersions(version, chosenLoader)
            open()
            gameName.forceActiveFocus()
        }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 30
                // Cover
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 180
                    spacing: 14
                    Item {
                        Layout.preferredWidth: 180; Layout.preferredHeight: 270
                        SoftGlow { anchors.fill: parent; color: "black"; blurMax: 32; spread: -6; offsetY: 14; opacity: 0.6 }
                        Image {
                            id: coverPreview
                            anchors.fill: parent
                            source: Theme.coverSource(gameEditor.chosenCover)
                            fillMode: Image.PreserveAspectCrop
                            sourceSize: Qt.size(360, 540)
                            visible: false
                        }
                        Rectangle { id: previewMask; anchors.fill: parent; radius: Theme.radiusLg; visible: false; layer.enabled: true }
                        MultiEffect { anchors.fill: parent; source: coverPreview; maskEnabled: true; maskSource: previewMask }
                        Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: "transparent"; border.width: 1; border.color: "#24FFFFFF" }
                    }
                    GridLayout {
                        columns: 5
                        columnSpacing: 6
                        Layout.fillWidth: true
                        Repeater {
                            model: Theme.covers
                            delegate: AbstractButton {
                                id: coverChoice
                                required property string modelData
                                readonly property bool chosen: gameEditor.chosenCover === modelData
                                Layout.preferredWidth: 30; Layout.preferredHeight: 45
                                hoverEnabled: true
                                Accessible.name: "Escolher capa " + modelData
                                onClicked: gameEditor.chosenCover = modelData
                                background: Item {
                                    Image {
                                        id: thumb
                                        anchors.fill: parent
                                        source: Theme.coverSource(coverChoice.modelData)
                                        fillMode: Image.PreserveAspectCrop
                                        sourceSize: Qt.size(60, 90)
                                        visible: false
                                    }
                                    Rectangle { id: thumbMask; anchors.fill: parent; radius: 6; visible: false; layer.enabled: true }
                                    MultiEffect {
                                        anchors.fill: parent; source: thumb; maskEnabled: true; maskSource: thumbMask
                                        brightness: coverChoice.chosen || coverChoice.hovered ? 0 : -0.25
                                    }
                                    Rectangle {
                                        anchors.fill: parent; anchors.margins: -3
                                        radius: 8; color: "transparent"
                                        border.width: 2
                                        border.color: coverChoice.visualFocus ? Theme.focusRing : Theme.accent
                                        visible: coverChoice.chosen || coverChoice.visualFocus
                                    }
                                }
                            }
                        }
                        ActionButton {
                            Layout.preferredWidth: 30; Layout.preferredHeight: 45
                            implicitHeight: 45
                            iconName: "image"; iconSize: 15
                            reduced: window.reduced
                            Accessible.name: "Escolher imagem"
                            ToolTip.visible: hovered; ToolTip.text: "Escolher imagem do computador"; ToolTip.delay: 400
                            onClicked: coverFile.open()
                        }
                    }
                }
                // Fields
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 0
                    Text { text: "Nome"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.bottomMargin: 8 }
                    InputField { id: gameName; objectName: "gameName"; Layout.fillWidth: true; placeholderText: "Como vai chamar seu jogo?"; maximumLength: 72 }
                    Text { text: "Versão do Minecraft"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 20; Layout.bottomMargin: 8 }
                    ChoiceField {
                        id: versionChoice
                        Layout.fillWidth: true
                        model: Theme.versions
                        onActivated: { gameEditor.chosenLoaderVersion = ""; runner.fetchLoaderVersions(currentText, gameEditor.chosenLoader) }
                    }
                    Text { text: "Tipo de instalação"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 20; Layout.bottomMargin: 8 }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 8; columnSpacing: 8
                        Repeater {
                            model: [
                                { name: "Vanilla", hint: "Jogo original, sem mods" },
                                { name: "Fabric", hint: "Leve, mods atualizados" },
                                { name: "Forge", hint: "Catálogo clássico" },
                                { name: "NeoForge", hint: "Sucessor do Forge" }
                            ]
                            delegate: AbstractButton {
                                id: loaderOption
                                required property var modelData
                                readonly property bool chosen: gameEditor.chosenLoader === modelData.name
                                Layout.fillWidth: true
                                Layout.preferredHeight: 60
                                hoverEnabled: true
                                Accessible.name: modelData.name
                                Accessible.role: Accessible.RadioButton
                                Accessible.checked: chosen
                                onClicked: {
                                    gameEditor.chosenLoader = modelData.name
                                    gameEditor.chosenLoaderVersion = ""
                                    runner.fetchLoaderVersions(versionChoice.currentText, modelData.name)
                                }
                                background: Rectangle {
                                    radius: 12
                                    color: loaderOption.chosen ? Theme.accentSoft : loaderOption.hovered ? "#0FFFFFFF" : Theme.fill
                                    border.width: loaderOption.chosen || loaderOption.visualFocus ? 1.5 : 1
                                    border.color: loaderOption.visualFocus ? Theme.focusRing : loaderOption.chosen ? Theme.accent : Theme.line
                                    Behavior on color { ColorAnimation { duration: window.reduced ? 0 : Theme.fast } }
                                }
                                contentItem: Item {
                                    Column {
                                        x: 14; anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 44
                                        spacing: 2
                                        Text { text: loaderOption.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold }
                                        Text { width: parent.width; text: loaderOption.modelData.hint; color: Theme.muted; font.family: Theme.font; font.pixelSize: 12; elide: Text.ElideRight }
                                    }
                                    Rectangle {
                                        anchors.right: parent.right; anchors.rightMargin: 14
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 18; height: 18; radius: 9
                                        color: loaderOption.chosen ? Theme.accent : "transparent"
                                        border.width: loaderOption.chosen ? 0 : 1.5
                                        border.color: Theme.lineStrong
                                        UiIcon { anchors.centerIn: parent; visible: loaderOption.chosen; width: 12; height: 12; stroke: 2; name: "check"; color: Theme.accentInk }
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: gameEditor.chosenLoader !== "Vanilla" && gameEditor.loaderListReady
                        text: "Versão do " + gameEditor.chosenLoader
                        color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13
                        Layout.topMargin: 20; Layout.bottomMargin: 8
                    }
                    ChoiceField {
                        id: loaderVersionChoice
                        visible: gameEditor.chosenLoader !== "Vanilla" && gameEditor.loaderListReady
                        Layout.fillWidth: true
                        model: runner.loaderVersions.map(function(version) {
                            return version === runner.recommendedLoader ? version + "  ·  recomendada" : version
                        })
                        Binding on currentIndex {
                            when: !loaderVersionChoice.popup.visible
                            value: Math.max(0, runner.loaderVersions.indexOf(gameEditor.loaderVersionToSave))
                        }
                        onActivated: function(index) { gameEditor.chosenLoaderVersion = runner.loaderVersions[index] }
                    }
                }
            }
            Text {
                visible: launcher.lastError !== ""
                text: launcher.lastError
                color: Theme.danger
                font.family: Theme.font; font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true; Layout.topMargin: 18
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 28
                spacing: 8
                ActionButton {
                    visible: gameEditor.editingId !== ""
                    text: "Remover"; quiet: true; danger: true; iconName: "trash"
                    reduced: window.reduced
                    onClicked: window.confirmRemoval(gameEditor.editingId, gameName.text, false)
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancelar"; quiet: true; reduced: window.reduced; onClicked: gameEditor.close() }
                ActionButton {
                    objectName: "saveGameButton"
                    text: gameEditor.editingId ? "Salvar" : "Criar jogo"
                    primary: true
                    iconName: gameEditor.editingId ? "check" : "plus"
                    reduced: window.reduced
                    onClicked: {
                        if (gameEditor.editingId) {
                            if (launcher.updateGame(gameEditor.editingId, gameName.text, versionChoice.currentText, gameEditor.chosenLoader, gameEditor.chosenCover, gameEditor.loaderVersionToSave))
                                gameEditor.close()
                        } else {
                            const id = launcher.addGame(gameName.text, versionChoice.currentText, gameEditor.chosenLoader, gameEditor.chosenCover, gameEditor.loaderVersionToSave)
                            if (id) {
                                window.page = 0
                                home.clearFilters()
                                window.selectedGameId = id
                                gameEditor.close()
                            }
                        }
                    }
                }
            }
        }
    }
    FileDialog {
        id: coverFile
        title: "Escolher capa"
        nameFilters: ["Imagens (*.png *.jpg *.jpeg *.webp)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const imported = launcher.importCover(selectedFile)
            if (imported) gameEditor.chosenCover = imported
        }
    }

    GlassDialog {
        id: removal
        property string targetId: ""
        property string targetTitle: ""
        property bool isProfile: false
        title: isProfile ? "Remover perfil?" : "Remover jogo?"
        width: 420
        showClose: false
        blurSource: appRoot
        reduced: window.reduced
        contentItem: ColumnLayout {
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: removal.isProfile
                    ? "“" + removal.targetTitle + "” deixará de aparecer em Perfis."
                    : "“" + removal.targetTitle + "” sairá da sua biblioteca. Esta ação não pode ser desfeita."
                color: Theme.secondary
                font.family: Theme.font; font.pixelSize: 14
                wrapMode: Text.WordWrap
                lineHeight: 1.15
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 28
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancelar"; quiet: true; reduced: window.reduced; onClicked: removal.close() }
                ActionButton {
                    text: "Remover"; danger: true; iconName: "trash"
                    reduced: window.reduced
                    onClicked: {
                        const profile = removal.isProfile ? launcher.profiles.find(function(p) { return p.id === removal.targetId }) : null
                        const ok = removal.isProfile ? launcher.removeProfile(removal.targetId) : launcher.removeGame(removal.targetId)
                        if (ok && profile && profile.type === "microsoft") auth.forget(profile.uuid)
                        if (ok) {
                            if (!removal.isProfile) { gameEditor.close(); window.drawerOpen = false }
                            removal.close()
                        } else window.notify(launcher.lastError)
                    }
                }
            }
        }
    }

    Popup {
        id: toast
        x: sidebar.width + (window.width - sidebar.width - width) / 2
        y: window.height - height - 28
        width: Math.min(implicitWidth, window.width - 120)
        padding: 0
        closePolicy: Popup.NoAutoClose
        background: GlassPanel {
            source: appRoot
            sourceX: toast.x; sourceY: toast.y
            tint: Theme.menu
            radius: 14
            shadow: false
        }
        contentItem: RowLayout {
            spacing: 10
            UiIcon { name: "info"; width: 16; height: 16; color: Theme.secondary; Layout.leftMargin: 16 }
            Text {
                id: toastText
                color: Theme.text
                font.family: Theme.font; font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 520
                Layout.topMargin: 13; Layout.bottomMargin: 13; Layout.rightMargin: 18
            }
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: window.reduced ? 0 : 160 }
            NumberAnimation { property: "y"; from: window.height - toast.height - 16; to: window.height - toast.height - 28; duration: window.reduced ? 0 : 260; easing.type: Easing.OutQuint }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: window.reduced ? 0 : 160 } }
    }
    Timer { id: toastTimer; interval: 3600; onTriggered: toast.close() }
}
