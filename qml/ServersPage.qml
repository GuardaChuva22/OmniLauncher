import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Dedicated servers hosted on this PC: a list on the left, the selected server's
// dashboard (status, console, settings, players, backups) on the right.
Item {
    id: root
    property bool reduced: false
    property Item blurSource: null
    readonly property bool dialogOpen: creator.shown || deletion.shown
    signal notify(string message)
    readonly property real gutter: width < 900 ? 32 : 48

    property string selectedId: ""
    readonly property var server: {
        const list = servers.servers
        for (let i = 0; i < list.length; i++) if (list[i].id === selectedId) return list[i]
        return null
    }
    readonly property var status: server ? (servers.status[server.id] || { state: "stopped" }) : ({ state: "stopped" })
    readonly property string state: status.state || "stopped"
    readonly property bool online: state === "running"
    readonly property bool alive: state === "starting" || state === "running" || state === "stopping"
    readonly property bool busy: alive || state === "installing"
    readonly property var players: status.players || []
    property var props: ({})
    property bool restartNeeded: false
    property real now: Date.now()

    function stateLabel(state) {
        return ({ stopped: "Parado", installing: "Instalando", starting: "Iniciando", running: "Online",
                  stopping: "Parando", failed: "Erro" })[state] || "Parado"
    }
    function stateColor(state) {
        if (state === "running") return Theme.success
        if (state === "failed") return Theme.danger
        if (state === "stopped") return Theme.faint
        return "#E8C77A"
    }
    function gameOf(entry) {
        return entry ? launcher.games.find(function(g) { return g.id === entry.gameId }) || null : null
    }
    function prop(key, fallback) { return props[key] === undefined ? fallback : props[key] }
    function setProp(key, value) {
        if (!server) return
        if (servers.setServerProperty(server.id, key, String(value))) {
            reloadProps()
            if (alive) restartNeeded = true
        }
    }
    function reloadProps() { props = server ? servers.properties(server.id) : ({}) }
    function uptime() {
        const started = Number(status.startedAt || 0)
        if (!online || started === 0) return "—"
        const s = Math.max(0, Math.floor((now - started) / 1000))
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60)
        return h > 0 ? h + " h " + m + " min" : m > 0 ? m + " min" : s + " s"
    }
    function address(host) { return host + (Number(server ? server.port : 25565) === 25565 ? "" : ":" + server.port) }
    function copy(text, what) { servers.copyText(text); root.notify(what + " copiado.") }
    function bytes(n) { return n > 1073741824 ? (n / 1073741824).toFixed(1) + " GB" : (n / 1048576).toFixed(1) + " MB" }
    function command(text) { if (server) servers.sendCommand(server.id, text); listsRefresh.restart() }

    function reloadConsole() {
        consoleModel.clear()
        if (!server) return
        const lines = servers.console(server.id)
        for (let i = 0; i < lines.length; i++) consoleModel.append({ line: lines[i] })
        consoleView.positionViewAtEnd()
    }
    onSelectedIdChanged: { reloadConsole(); reloadProps(); restartNeeded = false; refreshLists(); connector = server ? servers.hasConnector(server.id) : false }
    onVisibleChanged: if (visible) { servers.refreshPublicAddress(); if (!server && servers.servers.length > 0) selectedId = servers.servers[0].id }
    Component.onCompleted: if (servers.servers.length > 0) selectedId = servers.servers[0].id

    property var whitelist: []
    property var ops: []
    property var banned: []
    function refreshLists() {
        whitelist = server ? servers.playerList(server.id, "whitelist.json") : []
        ops = server ? servers.playerList(server.id, "ops.json") : []
        banned = server ? servers.playerList(server.id, "banned-players.json") : []
        backupList = server ? servers.backups(server.id) : []
    }
    property var backupList: []
    property bool connector: false
    readonly property string tunnel: status.tunnel || ""
    readonly property bool connectorSupported: server !== null && server.loader !== "Vanilla"
    Timer { id: listsRefresh; interval: 1500; onTriggered: root.refreshLists() }
    Timer { running: root.online && root.visible; interval: 1000; repeat: true; onTriggered: root.now = Date.now() }

    ListModel { id: consoleModel }
    Connections {
        target: servers
        function onConsoleLine(id, line) {
            if (!root.server || id !== root.server.id) return
            const atEnd = consoleView.atYEnd || consoleModel.count < 3
            consoleModel.append({ line: line })
            if (consoleModel.count > 2000) consoleModel.remove(0, consoleModel.count - 2000)
            if (atEnd) consoleView.positionViewAtEnd()
        }
        function onMessage(text) { root.notify(text) }
        function onPropertiesChanged(id) { if (root.server && id === root.server.id) root.reloadProps() }
        function onBackupsChanged(id) { if (root.server && id === root.server.id) root.refreshLists() }
        function onConnectorChanged(id) {
            if (!root.server || id !== root.server.id) return
            root.connector = servers.hasConnector(id)
            connectorSwitch.busy = false
            if (root.alive) root.restartNeeded = true
        }
        function onStatusChanged() {
            if (root.state === "stopped" || root.state === "failed") root.restartNeeded = false
            if (root.state === "running" || root.state === "stopped") { root.reloadProps(); root.refreshLists() }
        }
        function onServersChanged() {
            if (!root.server) root.selectedId = servers.servers.length > 0 ? servers.servers[0].id : ""
        }
    }

    component SectionTitle: Text {
        color: Theme.muted
        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
        Layout.topMargin: 24
        Layout.bottomMargin: 10
        Layout.leftMargin: 4
    }
    component Group: Rectangle {
        default property alias rows: column.data
        Layout.fillWidth: true
        implicitHeight: column.implicitHeight
        radius: Theme.radiusLg
        color: "#0AFFFFFF"
        border.width: 1; border.color: Theme.line
        ColumnLayout { id: column; width: parent.width; spacing: 0 }
    }
    component StatCard: Rectangle {
        id: stat
        property string label
        property string value
        property string hint: ""
        property color valueColor: Theme.text
        Layout.fillWidth: true
        implicitHeight: 84
        radius: Theme.radiusLg
        color: statHover.hovered ? "#12FFFFFF" : "#0AFFFFFF"
        border.width: 1; border.color: statHover.hovered ? Theme.lineStrong : Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        HoverHandler { id: statHover }
        transform: Translate { y: statHover.hovered && !root.reduced ? -2 : 0; Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } } }
        Column {
            x: 16; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 32
            spacing: 4
            Text { text: stat.label; color: Theme.muted; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.Medium }
            Text { text: stat.value; color: stat.valueColor; width: parent.width; elide: Text.ElideRight; font.family: Theme.fontDisplay; font.pixelSize: 20; font.weight: Font.DemiBold }
            Text { visible: stat.hint !== ""; text: stat.hint; color: Theme.faint; width: parent.width; elide: Text.ElideRight; font.family: Theme.font; font.pixelSize: 11 }
        }
    }
    component PropToggle: ToggleSwitch {
        property string key
        property bool fallback: false
        reduced: root.reduced
        checked: String(root.prop(key, fallback ? "true" : "false")) === "true"
        onToggled: root.setProp(key, checked ? "true" : "false")
    }
    component PropChoice: ChoiceField {
        id: choice
        property string key
        property var values: []
        property string fallback: values.length > 0 ? values[0] : ""
        width: 200
        Binding on currentIndex { when: !choice.popup.visible; value: Math.max(0, choice.values.indexOf(String(root.prop(choice.key, choice.fallback)))) }
        onActivated: function(index) { root.setProp(key, values[index]) }
    }
    component PropNumber: RowLayout {
        id: number
        property string key
        property int from: 0
        property int to: 100
        property int fallback: 0
        property string suffix: ""
        spacing: 12
        ValueSlider {
            id: slider
            Layout.preferredWidth: 180
            from: number.from; to: number.to; stepSize: 1
            snapMode: Slider.SnapAlways
            reduced: root.reduced
            Binding on value { when: !slider.pressed; value: Number(root.prop(number.key, number.fallback)) }
            onPressedChanged: if (!pressed) root.setProp(number.key, Math.round(value))
        }
        Text {
            Layout.preferredWidth: 64
            text: Math.round(slider.value) + number.suffix
            color: Theme.secondary
            font.family: Theme.font; font.pixelSize: 13
        }
    }
    component PropText: InputField {
        id: field
        property string key
        width: 260
        Binding on text { when: !field.activeFocus; value: String(root.prop(field.key, "")) }
        onEditingFinished: if (text !== String(root.prop(key, ""))) root.setProp(key, text)
    }
    component AddressRow: RowLayout {
        id: addressRow
        property string label
        property string value
        property string hint
        Layout.fillWidth: true
        Layout.leftMargin: 20; Layout.rightMargin: 14
        Layout.topMargin: 12; Layout.bottomMargin: 12
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: addressRow.label; color: Theme.muted; font.family: Theme.font; font.pixelSize: 12 }
            Text { text: addressRow.value; color: Theme.text; font.family: "Consolas"; font.pixelSize: 15; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { visible: addressRow.hint !== ""; text: addressRow.hint; color: Theme.faint; font.family: Theme.font; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        ActionButton {
            iconName: "copy"; quiet: true; round: true; width: 36; height: 36; reduced: root.reduced
            enabled: addressRow.value !== "" && addressRow.value !== "—"
            Accessible.name: "Copiar endereço"
            onClicked: root.copy(addressRow.value, "Endereço")
        }
    }

    // ── Header ────────────────────────────────────────────────────────────
    RowLayout {
        id: header
        x: root.gutter; y: 50
        width: root.width - root.gutter * 2
        spacing: 12
        Row {
            spacing: 12
            Layout.fillWidth: true
            Text {
                id: titleText
                text: "Servidores"
                color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
                font.letterSpacing: -0.6
            }
            Text {
                anchors.baseline: titleText.baseline
                text: servers.servers.length
                visible: servers.servers.length > 0
                color: Theme.faint
                font.family: Theme.fontDisplay; font.pixelSize: 20; font.weight: Font.Medium
            }
        }
        ActionButton {
            visible: servers.servers.length > 0
            text: "Novo servidor"; iconName: "plus"; primary: true; reduced: root.reduced
            onClicked: creator.prepare()
        }
    }

    // ── Empty state ───────────────────────────────────────────────────────
    Column {
        visible: servers.servers.length === 0
        anchors.centerIn: parent
        spacing: 10
        width: 420
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 60; height: 60; radius: 30
            color: Theme.fill; border.width: 1; border.color: Theme.line
            UiIcon { anchors.centerIn: parent; width: 26; height: 26; name: "server"; color: Theme.muted }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Hospede um servidor no seu PC"
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Escolha um jogo da biblioteca: o servidor usa a mesma versão, o mesmo loader e os mesmos mods."
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
        Item { width: 1; height: 8 }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Criar servidor"; iconName: "plus"; primary: true; reduced: root.reduced
            onClicked: creator.prepare()
        }
    }

    // ── Server list ───────────────────────────────────────────────────────
    ListView {
        id: list
        visible: servers.servers.length > 0
        x: root.gutter; y: header.y + header.height + 24
        width: root.width < 1100 ? 220 : 264
        height: root.height - y - 24
        spacing: 6
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: servers.servers
        delegate: AbstractButton {
            id: entry
            required property var modelData
            readonly property var game: root.gameOf(modelData)
            readonly property string entryState: (servers.status[modelData.id] || {}).state || "stopped"
            readonly property bool active: root.selectedId === modelData.id
            width: list.width; height: 64
            hoverEnabled: true
            onHoveredChanged: if (hovered && !active) sfx.play("hover")
            onClicked: { if (!active) sfx.play("click"); root.selectedId = modelData.id }
            transform: Translate { x: entry.hovered && !entry.active && !root.reduced ? 3 : 0; Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } } }
            background: Rectangle {
                radius: Theme.radiusLg
                color: entry.active ? Theme.accentSoft : entry.hovered ? Theme.fillHover : Theme.fill
                border.width: 1; border.color: entry.active ? Theme.accentLine : Theme.line
            }
            contentItem: RowLayout {
                spacing: 12
                Rectangle {
                    Layout.leftMargin: 4
                    Layout.preferredWidth: 36; Layout.preferredHeight: 36
                    radius: 10
                    color: "#14FFFFFF"
                    Image {
                        anchors.fill: parent; anchors.margins: 2
                        visible: source != ""
                        source: entry.game && entry.game.packIcon ? entry.game.packIcon : ""
                        fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: true
                    }
                    UiIcon { anchors.centerIn: parent; visible: !(entry.game && entry.game.packIcon); width: 18; height: 18; name: "server"; color: Theme.muted }
                    Rectangle {
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: -2
                        width: 12; height: 12; radius: 6
                        color: root.stateColor(entry.entryState)
                        border.width: 2; border.color: Theme.base
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: entry.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text {
                        text: root.stateLabel(entry.entryState) + "  ·  " + entry.modelData.version + (entry.modelData.loader !== "Vanilla" ? " " + entry.modelData.loader : "")
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ── Dashboard ─────────────────────────────────────────────────────────
    Item {
        id: dashboard
        visible: root.server !== null
        x: list.x + list.width + 24
        y: list.y
        width: root.width - x - root.gutter
        height: root.height - y

        RowLayout {
            id: titleRow
            width: parent.width
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    spacing: 10
                    Text {
                        text: root.server ? root.server.name : ""
                        color: Theme.text
                        font.family: Theme.fontDisplay; font.pixelSize: 22; font.weight: Font.DemiBold
                        Layout.maximumWidth: dashboard.width - 420
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        implicitWidth: pillRow.implicitWidth + 18; implicitHeight: 24; radius: 12
                        color: Qt.rgba(root.stateColor(root.state).r, root.stateColor(root.state).g, root.stateColor(root.state).b, 0.14)
                        Row {
                            id: pillRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 7; height: 7; radius: 3.5
                                color: root.stateColor(root.state)
                                SequentialAnimation on opacity {
                                    running: root.busy && !root.reduced && root.visible; loops: Animation.Infinite
                                    NumberAnimation { to: 0.3; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                }
                            }
                            Text { text: root.stateLabel(root.state); color: root.stateColor(root.state); font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold }
                        }
                    }
                }
                Text {
                    text: {
                        if (!root.server) return ""
                        const game = root.gameOf(root.server)
                        const detail = root.status.detail ? "  ·  " + root.status.detail : ""
                        return "Minecraft " + root.server.version + (root.server.loader !== "Vanilla" ? " · " + root.server.loader + " " + root.server.loaderVersion : "")
                            + (game ? "  ·  de " + game.title : "") + detail
                    }
                    color: root.state === "failed" ? Theme.danger : Theme.muted
                    font.family: Theme.font; font.pixelSize: 13
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            ActionButton {
                visible: !root.alive && root.state !== "installing"
                text: root.server && !root.server.installed ? "Instalar" : "Iniciar"
                iconName: "play"; primary: true; reduced: root.reduced
                onClicked: servers.start(root.server.id)
            }
            ActionButton {
                visible: root.online || root.state === "starting"
                text: "Reiniciar"; iconName: "restart"; reduced: root.reduced
                onClicked: servers.restart(root.server.id)
            }
            ActionButton {
                visible: root.online || root.state === "starting"
                text: "Parar"; iconName: "stop"; danger: true; reduced: root.reduced
                onClicked: servers.stop(root.server.id)
            }
            ActionButton {
                visible: root.state === "stopping"
                text: "Forçar"; iconName: "close"; danger: true; quiet: true; reduced: root.reduced
                onClicked: servers.kill(root.server.id)
            }
        }

        RowLayout {
            id: stats
            y: titleRow.height + 18
            width: parent.width
            spacing: 12
            StatCard {
                label: "Jogadores"
                value: root.online ? root.players.length + " / " + root.prop("max-players", "20") : "—"
                hint: root.players.length > 0 ? root.players.join(", ") : root.online ? "Ninguém online" : ""
            }
            StatCard { label: "Tempo ligado"; value: root.uptime() }
            StatCard {
                label: "Memória"
                value: root.server ? (root.server.memoryMb / 1024).toFixed(root.server.memoryMb % 1024 === 0 ? 0 : 1) + " GB" : "—"
                hint: "Máximo para o Java"
            }
            StatCard {
                readonly property string shown: root.tunnel !== "" ? root.tunnel : root.address(servers.lanAddress)
                label: root.tunnel !== "" ? "Endereço público" : "Endereço local"
                value: shown
                valueColor: root.tunnel !== "" ? Theme.accentText : Theme.text
                hint: root.tunnel !== "" ? "Mande para os amigos · clique para copiar" : "Clique para copiar"
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.copy(parent.shown, "Endereço") }
            }
        }

        Rectangle {
            id: restartBanner
            visible: root.restartNeeded
            y: stats.y + stats.height + 12
            width: parent.width; height: visible ? 44 : 0
            radius: Theme.radiusMd
            color: "#1FE8C77A"
            border.width: 1; border.color: "#59E8C77A"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 6
                Text { text: "Algumas mudanças só valem depois de reiniciar o servidor."; color: "#F2DDA6"; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                ActionButton { text: "Reiniciar agora"; quiet: true; height: 32; reduced: root.reduced; onClicked: servers.restart(root.server.id) }
            }
        }

        SegmentTabs {
            id: tabs
            y: (restartBanner.visible ? restartBanner.y + restartBanner.height : stats.y + stats.height) + 18
            width: Math.min(parent.width, 560)
            model: ["Console", "Configurações", "Jogadores", "Conexão", "Backups"]
            reduced: root.reduced
            onCurrentIndexChanged: if (currentIndex === 2 || currentIndex === 4) root.refreshLists()
        }

        Item {
            id: tabArea
            y: tabs.y + tabs.height + 16
            width: parent.width
            height: parent.height - y - 24

            // Console
            Rectangle {
                visible: tabs.currentIndex === 0
                anchors.fill: parent
                radius: Theme.radiusLg
                color: "#B30B090C"
                border.width: 1; border.color: Theme.line
                ListView {
                    id: consoleView
                    anchors.fill: parent
                    anchors.margins: 14
                    anchors.bottomMargin: commandBar.height + 22
                    clip: true
                    model: consoleModel
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ThinScrollBar {}
                    delegate: TextEdit {
                        required property string line
                        width: consoleView.width - 12
                        text: line
                        readOnly: true
                        selectByMouse: true
                        wrapMode: Text.WrapAnywhere
                        textFormat: TextEdit.PlainText
                        selectionColor: "#8CE68AA6"
                        font.family: "Consolas"; font.pixelSize: 12
                        color: line.indexOf("[OmniLauncher]") === 0 ? Theme.accentText
                             : line.indexOf("> ") === 0 ? Theme.text
                             : /ERROR|FATAL|Exception|\/ERROR\]/.test(line) ? Theme.danger
                             : /WARN/.test(line) ? "#E8C77A"
                             : Theme.secondary
                    }
                }
                Text {
                    visible: consoleModel.count === 0
                    anchors.centerIn: parent
                    text: "O console aparece aqui quando o servidor inicia."
                    color: Theme.faint; font.family: Theme.font; font.pixelSize: 13
                }
                RowLayout {
                    id: commandBar
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 10
                    spacing: 8
                    InputField {
                        id: commandField
                        Layout.fillWidth: true
                        iconName: "terminal"
                        enabled: root.online || root.state === "starting"
                        placeholderText: enabled ? "Digite um comando (ex.: time set day, gamemode creative Steve)" : "Inicie o servidor para usar comandos"
                        font.family: "Consolas"
                        property var history: []
                        property int historyIndex: -1
                        onAccepted: {
                            if (text.trim() === "") return
                            history.push(text); historyIndex = -1
                            root.command(text); text = ""
                        }
                        Keys.onUpPressed: {
                            if (history.length === 0) return
                            historyIndex = historyIndex < 0 ? history.length - 1 : Math.max(0, historyIndex - 1)
                            text = history[historyIndex]
                        }
                        Keys.onDownPressed: {
                            if (historyIndex < 0) return
                            historyIndex++
                            if (historyIndex >= history.length) { historyIndex = -1; text = "" } else text = history[historyIndex]
                        }
                    }
                    Repeater {
                        model: [{ label: "Dia", cmd: "time set day" }, { label: "Sol", cmd: "weather clear" }, { label: "Salvar", cmd: "save-all" }, { label: "Lista", cmd: "list" }]
                        delegate: ActionButton {
                            required property var modelData
                            visible: dashboard.width > 760
                            text: modelData.label; quiet: true; reduced: root.reduced
                            enabled: root.online
                            onClicked: root.command(modelData.cmd)
                        }
                    }
                    ActionButton {
                        iconName: "trash"; quiet: true; round: true; width: 40; height: 40; reduced: root.reduced
                        Accessible.name: "Limpar console"
                        onClicked: { servers.clearConsole(root.server.id); consoleModel.clear() }
                    }
                }
            }

            // Settings
            Flickable {
                id: settingsFlick
                visible: tabs.currentIndex === 1
                anchors.fill: parent
                contentHeight: settingsColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: settingsColumn
                    width: settingsFlick.width - 14
                    spacing: 0
                    SectionTitle { text: "Geral"; Layout.topMargin: 0 }
                    Group {
                        SettingRow {
                            title: "Nome"
                            description: "Como o servidor aparece aqui no launcher."
                            InputField {
                                id: nameField
                                width: 260
                                maximumLength: 48
                                Binding on text { when: !nameField.activeFocus; value: root.server ? root.server.name : "" }
                                onEditingFinished: if (root.server && text !== root.server.name && !servers.rename(root.server.id, text)) root.notify("Use um nome de 1 a 48 letras.")
                            }
                        }
                        SettingRow { title: "Mensagem (MOTD)"; description: "Aparece na lista de servidores do Minecraft."; PropText { key: "motd"; maximumLength: 59 } }
                        SettingRow {
                            title: "Memória do servidor"
                            description: "Servidores com muitos mods precisam de 4 GB ou mais."
                            RowLayout {
                                spacing: 12
                                ValueSlider {
                                    id: memorySlider
                                    Layout.preferredWidth: 180
                                    from: 1; to: Math.max(2, Math.floor(runner.systemMemoryMb / 1024) - 2); stepSize: 1
                                    snapMode: Slider.SnapAlways
                                    reduced: root.reduced
                                    Binding on value { when: !memorySlider.pressed; value: root.server ? Math.round(root.server.memoryMb / 1024) : 2 }
                                    onPressedChanged: if (!pressed && root.server) { servers.setMemory(root.server.id, Math.round(value) * 1024); if (root.alive) root.restartNeeded = true }
                                }
                                Text { Layout.preferredWidth: 64; text: Math.round(memorySlider.value) + " GB"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13 }
                            }
                        }
                        SettingRow { title: "Máximo de jogadores"; PropNumber { key: "max-players"; from: 1; to: 100; fallback: 20 } }
                        SettingRow { title: "Porta"; description: "Padrão 25565. Mude para rodar dois servidores ao mesmo tempo."; divider: false; PropText { key: "server-port"; width: 120; validator: IntValidator { bottom: 1024; top: 65535 } } }
                    }

                    SectionTitle { text: "Jogo" }
                    Group {
                        SettingRow {
                            title: "Modo de jogo"
                            PropChoice { key: "gamemode"; values: ["survival", "creative", "adventure", "spectator"]; model: ["Sobrevivência", "Criativo", "Aventura", "Espectador"] }
                        }
                        SettingRow {
                            title: "Dificuldade"
                            PropChoice { key: "difficulty"; values: ["peaceful", "easy", "normal", "hard"]; fallback: "easy"; model: ["Pacífico", "Fácil", "Normal", "Difícil"] }
                        }
                        SettingRow { title: "Hardcore"; description: "Morreu, virou espectador."; PropToggle { key: "hardcore" } }
                        SettingRow { title: "PvP"; description: "Jogadores podem atacar uns aos outros."; PropToggle { key: "pvp"; fallback: true } }
                        SettingRow { title: "Permitir voo"; description: "Evita expulsões por voo com mods de jetpack ou asas."; PropToggle { key: "allow-flight" } }
                        SettingRow { title: "Blocos de comando"; PropToggle { key: "enable-command-block" } }
                        SettingRow { title: "Proteção do spawn"; description: "Raio em blocos onde só operadores constroem."; PropNumber { key: "spawn-protection"; from: 0; to: 64; fallback: 16; suffix: " blocos" } }
                        SettingRow { title: "Distância de visão"; PropNumber { key: "view-distance"; from: 3; to: 32; fallback: 10; suffix: " chunks" } }
                        SettingRow { title: "Distância de simulação"; divider: false; PropNumber { key: "simulation-distance"; from: 3; to: 32; fallback: 10; suffix: " chunks" } }
                    }

                    SectionTitle { text: "Mundo" }
                    Group {
                        SettingRow { title: "Nome da pasta do mundo"; description: "Mude para começar um mundo novo (o antigo fica guardado)."; PropText { key: "level-name" } }
                        SettingRow { title: "Seed"; description: "Só vale para mundos novos. Vazio = aleatória."; PropText { key: "level-seed"; placeholderText: "Aleatória" } }
                        SettingRow {
                            title: "Tipo de mundo"
                            PropChoice {
                                key: "level-type"
                                values: ["minecraft:normal", "minecraft:flat", "minecraft:large_biomes", "minecraft:amplified"]
                                model: ["Normal", "Plano", "Biomas grandes", "Amplificado"]
                            }
                        }
                        SettingRow { title: "Estruturas"; description: "Vilas, templos, fortalezas."; PropToggle { key: "generate-structures"; fallback: true } }
                        SettingRow { title: "Nether"; PropToggle { key: "allow-nether"; fallback: true } }
                        SettingRow { title: "Monstros"; divider: false; PropToggle { key: "spawn-monsters"; fallback: true } }
                    }

                    SectionTitle { text: "Acesso" }
                    Group {
                        SettingRow {
                            title: "Só contas originais"
                            description: "Ligado, só entra quem tem conta Microsoft. Deixe desligado para amigos com perfil offline."
                            PropToggle { key: "online-mode"; fallback: true }
                        }
                        SettingRow { title: "Lista de permissões (whitelist)"; description: "Só entra quem estiver na lista (aba Jogadores)."; PropToggle { key: "white-list" } }
                        SettingRow { title: "Aparecer como online"; description: "Mostra jogadores e status na lista do Minecraft."; divider: false; PropToggle { key: "enable-status"; fallback: true } }
                    }

                    SectionTitle { text: "Arquivos" }
                    Group {
                        SettingRow {
                            title: "Mods e configurações"
                            description: "Copia de novo do jogo os mods (menos os só de cliente) e as configs. Use depois de atualizar o modpack."
                            ActionButton {
                                text: "Sincronizar"; iconName: "download"; quiet: true; reduced: root.reduced
                                enabled: root.state === "stopped" && root.server && root.server.loader !== "Vanilla"
                                onClicked: servers.syncContent(root.server.id)
                            }
                        }
                        SettingRow {
                            title: "Pasta do servidor"
                            ActionButton { text: "Abrir"; iconName: "folder"; quiet: true; reduced: root.reduced; onClicked: servers.openFolder(root.server.id) }
                        }
                        SettingRow {
                            title: "Reinstalar"
                            description: "Baixa os arquivos do servidor de novo. O mundo e as configs ficam."
                            ActionButton { text: "Reinstalar"; quiet: true; reduced: root.reduced; enabled: !root.busy; onClicked: servers.install(root.server.id) }
                        }
                        SettingRow {
                            title: "Excluir servidor"
                            description: "Vai para a Lixeira do Windows, com o mundo."
                            divider: false
                            ActionButton { text: "Excluir"; iconName: "trash"; danger: true; quiet: true; reduced: root.reduced; enabled: !root.busy; onClicked: deletion.open() }
                        }
                    }
                }
            }

            // Players
            Flickable {
                id: playersFlick
                visible: tabs.currentIndex === 2
                anchors.fill: parent
                contentHeight: playersColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: playersColumn
                    width: playersFlick.width - 14
                    spacing: 0
                    Text {
                        visible: !root.online
                        Layout.fillWidth: true
                        Layout.bottomMargin: 8
                        text: "Inicie o servidor para gerenciar jogadores."
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 13
                    }
                    SectionTitle { text: "Online agora"; Layout.topMargin: 0 }
                    Group {
                        Text {
                            visible: root.players.length === 0
                            Layout.margins: 20
                            text: root.online ? "Ninguém online. Mande o endereço para seus amigos (aba Conexão)." : "Servidor parado."
                            color: Theme.faint; font.family: Theme.font; font.pixelSize: 13
                        }
                        Repeater {
                            model: root.players
                            delegate: SettingRow {
                                id: onlineRow
                                required property string modelData
                                required property int index
                                title: modelData
                                description: root.ops.indexOf(modelData) !== -1 ? "Operador" : ""
                                divider: index < root.players.length - 1
                                Row {
                                    spacing: 6
                                    ActionButton {
                                        text: root.ops.indexOf(onlineRow.modelData) !== -1 ? "Tirar op" : "Dar op"
                                        quiet: true; height: 34; reduced: root.reduced
                                        onClicked: root.command((root.ops.indexOf(onlineRow.modelData) !== -1 ? "deop " : "op ") + onlineRow.modelData)
                                    }
                                    ActionButton { text: "Expulsar"; quiet: true; height: 34; reduced: root.reduced; onClicked: root.command("kick " + onlineRow.modelData) }
                                    ActionButton { text: "Banir"; danger: true; quiet: true; height: 34; reduced: root.reduced; onClicked: root.command("ban " + onlineRow.modelData) }
                                }
                            }
                        }
                    }

                    SectionTitle { text: "Adicionar à lista" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        InputField {
                            id: playerName
                            Layout.fillWidth: true
                            placeholderText: "Nome do jogador"
                            maximumLength: 16
                            validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9_]{0,16}/ }
                            enabled: root.online
                        }
                        ActionButton { text: "Permitir"; reduced: root.reduced; enabled: root.online && playerName.text !== ""; onClicked: { root.command("whitelist add " + playerName.text); playerName.text = "" } }
                        ActionButton { text: "Dar op"; quiet: true; reduced: root.reduced; enabled: root.online && playerName.text !== ""; onClicked: { root.command("op " + playerName.text); playerName.text = "" } }
                        ActionButton { text: "Banir"; danger: true; quiet: true; reduced: root.reduced; enabled: root.online && playerName.text !== ""; onClicked: { root.command("ban " + playerName.text); playerName.text = "" } }
                    }

                    Repeater {
                        model: [{ title: "Lista de permissões", names: root.whitelist, remove: "whitelist remove ", empty: "Ninguém na lista." },
                                { title: "Operadores", names: root.ops, remove: "deop ", empty: "Nenhum operador." },
                                { title: "Banidos", names: root.banned, remove: "pardon ", empty: "Ninguém banido." }]
                        delegate: ColumnLayout {
                            id: listBlock
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            SectionTitle { text: listBlock.modelData.title + (listBlock.modelData.names.length > 0 ? "  ·  " + listBlock.modelData.names.length : "") }
                            Group {
                                Text {
                                    visible: listBlock.modelData.names.length === 0
                                    Layout.margins: 20
                                    text: listBlock.modelData.empty
                                    color: Theme.faint; font.family: Theme.font; font.pixelSize: 13
                                }
                                Repeater {
                                    model: listBlock.modelData.names
                                    delegate: SettingRow {
                                        id: nameRow
                                        required property string modelData
                                        required property int index
                                        title: modelData
                                        divider: index < listBlock.modelData.names.length - 1
                                        ActionButton {
                                            text: "Remover"; quiet: true; height: 34; reduced: root.reduced
                                            enabled: root.online
                                            onClicked: root.command(listBlock.modelData.remove + nameRow.modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Connection
            Flickable {
                id: connectionFlick
                visible: tabs.currentIndex === 3
                anchors.fill: parent
                contentHeight: connectionColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: connectionColumn
                    width: connectionFlick.width - 14
                    spacing: 0
                    SectionTitle { text: "Conector"; Layout.topMargin: 0 }
                    Group {
                        SettingRow {
                            title: "Endereço público sem configurar o roteador"
                            description: root.connectorSupported
                                ? "Usa o e4mc: um túnel grátis que dá ao servidor um endereço *.e4mc.link, sem port forwarding, Hamachi ou Radmin. O endereço muda a cada início."
                                : "Disponível em servidores com Fabric, Forge ou NeoForge (o conector é um mod)."
                            divider: root.connector
                            ToggleSwitch {
                                id: connectorSwitch
                                property bool busy: false
                                reduced: root.reduced
                                enabled: root.connectorSupported && !busy
                                checked: root.connector
                                Accessible.name: "Conector"
                                onToggled: { busy = true; servers.setConnector(root.server.id, checked) }
                            }
                        }
                        AddressRow {
                            visible: root.connector
                            label: "Endereço para os amigos"
                            value: root.tunnel !== "" ? root.tunnel : "—"
                            hint: root.tunnel !== "" ? "Funciona de qualquer lugar, com o Minecraft normal." : root.online ? "Reinicie o servidor para gerar o endereço." : "Aparece aqui quando o servidor iniciar."
                        }
                    }

                    SectionTitle { text: "Outros endereços" }
                    Group {
                        AddressRow {
                            label: "Neste PC"
                            value: root.address("localhost")
                        }
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.line }
                        AddressRow {
                            label: "Mesma rede (Wi-Fi ou cabo da sua casa)"
                            value: root.address(servers.lanAddress)
                        }
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.line }
                        AddressRow {
                            label: "Pela internet"
                            value: servers.publicAddress !== "" ? servers.publicAddress + ":" + (root.server ? root.server.port : 25565) : "—"
                            hint: "Funciona só se a porta " + (root.server ? root.server.port : 25565) + " (TCP) estiver aberta no roteador (port forwarding) e a operadora não usar CGNAT."
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        Layout.leftMargin: 4
                        wrapMode: Text.WordWrap
                        text: "Amigos com perfil offline só entram com \"Só contas originais\" desligado (aba Configurações). Todos precisam da mesma versão e dos mesmos mods: o jeito mais fácil é usar o mesmo modpack."
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 12
                    }
                }
            }

            // Backups
            ColumnLayout {
                visible: tabs.currentIndex === 4
                anchors.fill: parent
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: "Cópias do mundo em .zip. Com o servidor ligado, o mundo é salvo antes."
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    ActionButton { text: "Pasta"; iconName: "folder"; quiet: true; reduced: root.reduced; onClicked: servers.openFolder(root.server.id, "backups") }
                    ActionButton { text: "Fazer backup"; iconName: "download"; primary: true; reduced: root.reduced; enabled: root.state !== "installing"; onClicked: servers.backup(root.server.id) }
                }
                ListView {
                    id: backupView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: root.backupList
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ThinScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        width: backupView.width - 12; height: 56
                        radius: Theme.radiusMd
                        color: Theme.fill
                        border.width: 1; border.color: Theme.line
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 8
                            spacing: 12
                            UiIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; name: "box"; color: Theme.muted }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { text: modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Text { text: new Date(modelData.createdAt).toLocaleString(Qt.locale(), "d 'de' MMMM, HH:mm") + "  ·  " + root.bytes(modelData.sizeBytes); color: Theme.muted; font.family: Theme.font; font.pixelSize: 12 }
                            }
                            ActionButton { iconName: "external"; quiet: true; round: true; width: 36; height: 36; reduced: root.reduced; Accessible.name: "Mostrar"; onClicked: runner.openFile(modelData.path.substring(0, modelData.path.lastIndexOf("/"))) }
                            ActionButton { iconName: "trash"; quiet: true; round: true; width: 36; height: 36; danger: true; reduced: root.reduced; Accessible.name: "Mover para a Lixeira"; onClicked: { runner.trashFile(modelData.path); root.refreshLists() } }
                        }
                    }
                    Text {
                        visible: root.backupList.length === 0
                        anchors.centerIn: parent
                        text: "Nenhum backup ainda."
                        color: Theme.faint; font.family: Theme.font; font.pixelSize: 13
                    }
                }
            }
        }
    }

    // ── Dialogs ───────────────────────────────────────────────────────────
    GlassDialog {
        id: creator
        parent: Overlay.overlay
        title: "Novo servidor"
        width: 460
        blurSource: root.blurSource
        reduced: root.reduced
        readonly property var games: launcher.games
        readonly property var game: gameChoice.currentIndex >= 0 ? games[gameChoice.currentIndex] : null
        function prepare() {
            gameChoice.currentIndex = games.length > 0 ? 0 : -1
            serverName.text = game ? "Servidor " + game.title : ""
            const recommended = game ? runner.recommendedMemory(game.id, game.loader) : 4096
            creatorMemory.value = Math.max(2, Math.min(creatorMemory.to, Math.round(recommended / 1024) + 1))
            eula.checked = false
            open()
        }
        contentItem: ColumnLayout {
            spacing: 0
            Text { text: "Jogo"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.bottomMargin: 8 }
            ChoiceField {
                id: gameChoice
                Layout.fillWidth: true
                model: creator.games.map(function(g) { return g.title + "  ·  " + g.version + (g.loader !== "Vanilla" ? " " + g.loader : "") })
                onActivated: {
                    const game = creator.games[currentIndex]
                    serverName.text = "Servidor " + game.title
                    creatorMemory.value = Math.max(2, Math.min(creatorMemory.to, Math.round(runner.recommendedMemory(game.id, game.loader) / 1024) + 1))
                }
            }
            Text {
                visible: creator.game !== null && creator.game.loader !== "Vanilla"
                Layout.fillWidth: true; Layout.topMargin: 6
                wrapMode: Text.WordWrap
                text: "Os mods e configs do jogo vão junto; mods só de cliente (shaders, minimapa, HUD…) ficam de fora."
                color: Theme.faint; font.family: Theme.font; font.pixelSize: 12
            }
            Text { text: "Nome"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 18; Layout.bottomMargin: 8 }
            InputField { id: serverName; Layout.fillWidth: true; maximumLength: 48; placeholderText: "Meu servidor" }
            RowLayout {
                Layout.topMargin: 18
                Layout.fillWidth: true
                Text { text: "Memória"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                Text { text: Math.round(creatorMemory.value) + " GB"; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold }
            }
            ValueSlider {
                id: creatorMemory
                Layout.fillWidth: true
                from: 1; to: Math.max(2, Math.floor(runner.systemMemoryMb / 1024) - 2); stepSize: 1
                snapMode: Slider.SnapAlways
                reduced: root.reduced
            }
            RowLayout {
                Layout.topMargin: 18
                Layout.fillWidth: true
                spacing: 12
                ToggleSwitch { id: eula; reduced: root.reduced; Accessible.name: "Aceito o EULA do Minecraft" }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    text: "Li e aceito o <a href=\"https://aka.ms/MinecraftEULA\">EULA do Minecraft</a>, exigido pela Mojang para rodar um servidor."
                    linkColor: Theme.accentText
                    color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13
                    onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                    HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
            }
            Text {
                visible: creator.games.length === 0
                Layout.topMargin: 12
                text: "Crie um jogo na biblioteca primeiro."
                color: Theme.danger; font.family: Theme.font; font.pixelSize: 13
            }
            RowLayout {
                Layout.topMargin: 26
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancelar"; quiet: true; reduced: root.reduced; onClicked: creator.close() }
                ActionButton {
                    text: "Criar servidor"; primary: true; reduced: root.reduced
                    enabled: creator.game !== null && eula.checked && serverName.text.trim() !== ""
                    onClicked: {
                        const id = servers.create(serverName.text, creator.game, Math.round(creatorMemory.value) * 1024, eula.checked)
                        if (id === "") { root.notify(servers.lastError); return }
                        root.selectedId = id
                        tabs.currentIndex = 0
                        creator.close()
                    }
                }
            }
        }
    }

    GlassDialog {
        id: deletion
        parent: Overlay.overlay
        title: "Excluir servidor?"
        width: 420
        showClose: false
        blurSource: root.blurSource
        reduced: root.reduced
        contentItem: ColumnLayout {
            spacing: 0
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: (root.server ? root.server.name : "") + " e o mundo dele vão para a Lixeira do Windows. O jogo da biblioteca não é afetado."
                color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14
            }
            RowLayout {
                Layout.topMargin: 24
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancelar"; quiet: true; reduced: root.reduced; onClicked: deletion.close() }
                ActionButton {
                    text: "Excluir"; danger: true; reduced: root.reduced
                    onClicked: {
                        const name = root.server.name
                        if (servers.remove(root.server.id)) root.notify(name + " foi para a Lixeira.")
                        else root.notify("Pare o servidor antes de excluir.")
                        deletion.close()
                    }
                }
            }
        }
    }
}
