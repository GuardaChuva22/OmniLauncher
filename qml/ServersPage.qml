import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Dedicated servers hosted on this PC: a list on the left, the selected server's
// dashboard (status, console, content, settings, players, backups) on the right.
Item {
    id: root
    property bool reduced: false
    property Item blurSource: null
    readonly property bool dialogOpen: creator.shown || deletion.shown || contentRemoval.shown
    signal notify(string message)
    // Join a Steam friend's shared server (Main connects through the tunnel and starts the game).
    signal joinRequested(string hostId)
    // Open the Modrinth catalog for this server (Main owns the one ModCatalog).
    signal contentRequested(var target, string kind, string loader)
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
    property string listFilter: ""
    readonly property var shownServers: {
        const query = listFilter.trim().toLowerCase()
        if (query === "") return servers.servers
        return servers.servers.filter(function(s) {
            return (s.name + " " + s.version + " " + s.loader).toLowerCase().indexOf(query) !== -1
        })
    }
    readonly property int onlineCount: {
        let count = 0
        for (let i = 0; i < servers.servers.length; i++)
            if (((servers.status[servers.servers[i].id] || {}).state || "") === "running") count++
        return count
    }
    // Dashboard tabs by name, so their order can change freely.
    readonly property var tabKeys: ["console", "content", "settings", "players", "connection", "backups"]
    readonly property string tab: tabKeys[tabs.currentIndex] || "console"

    function stateLabel(state) {
        return ({ stopped: qsTr("Parado"), installing: qsTr("Instalando"), starting: qsTr("Iniciando"), running: qsTr("Online"),
                  stopping: qsTr("Parando"), failed: qsTr("Erro") })[state] || qsTr("Parado")
    }
    // "há 5 min" / "há 3 dias" from an ISO date; "" when there is none.
    function ago(iso) {
        if (!iso) return ""
        const s = Math.max(0, Math.floor((now - new Date(iso).getTime()) / 1000))
        if (s < 60) return qsTr("agora há pouco")
        if (s < 3600) return qsTr("há %1 min").arg(Math.floor(s / 60))
        if (s < 86400) return qsTr("há %1 h").arg(Math.floor(s / 3600))
        const d = Math.floor(s / 86400)
        return d === 1 ? qsTr("ontem") : d < 30 ? qsTr("há %1 dias").arg(d) : new Date(iso).toLocaleDateString(Qt.locale(), qsTr("d 'de' MMM yyyy"))
    }
    // "5 min" / "2 h" / "3 dias" since an ISO date.
    function span(iso) {
        if (!iso) return ""
        const s = Math.max(60, Math.floor((now - new Date(iso).getTime()) / 1000))
        if (s < 3600) return qsTr("%1 min").arg(Math.floor(s / 60))
        if (s < 86400) return qsTr("%1 h").arg(Math.floor(s / 3600))
        const d = Math.floor(s / 86400)
        return d === 1 ? qsTr("1 dia") : qsTr("%1 dias").arg(d)
    }
    function statusOf(id) { return servers.status[id] || { state: "stopped" } }
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
    function copy(text, what) { servers.copyText(text); root.notify(what + qsTr(" copiado.")) }
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

    // Steam friends (SteamTunnel): who is sharing a server, and this server's sharing.
    readonly property var hostingFriends: steam.ready ? steam.friends.filter(function(f) { return f.hosting && f.hosting.server }) : []
    readonly property var joined: steam.joined
    property int shareRevision: 0
    readonly property bool steamSharing: { shareRevision; return server !== null && steam.sharing(server.id) }
    readonly property bool steamShared: server !== null && steam.hosting.serverId === server.id
    property var invited: ({})
    Connections { target: steam; function onSharingChanged() { root.shareRevision++ } }
    component SteamAvatar: Rectangle {
        property string source: ""
        property string name: ""
        width: 36; height: 36; radius: 8
        color: "#14FFFFFF"
        Image { anchors.fill: parent; visible: parent.source !== ""; source: parent.source; smooth: true; fillMode: Image.PreserveAspectCrop }
        Text {
            anchors.centerIn: parent; visible: parent.source === ""
            text: parent.name.charAt(0).toUpperCase(); color: Theme.secondary
            font.family: Theme.fontDisplay; font.pixelSize: 15; font.weight: Font.Bold
        }
    }
    property bool connector: false
    readonly property string tunnel: status.tunnel || ""
    readonly property bool connectorSupported: server !== null && server.loader !== "Vanilla"
    Timer { id: listsRefresh; interval: 1500; onTriggered: root.refreshLists() }
    Timer { running: root.visible; interval: root.online ? 1000 : 30000; repeat: true; onTriggered: root.now = Date.now() }

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
    component KindPill: AbstractButton {
        id: pill
        property bool chosen: false
        implicitHeight: 32
        implicitWidth: pillText.implicitWidth + 28
        hoverEnabled: true
        onClicked: if (!chosen) sfx.play("click")
        background: Rectangle {
            radius: 8
            color: pill.chosen ? Theme.fillActive : pill.hovered ? Theme.fillHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
        contentItem: Text {
            id: pillText
            text: pill.text
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: pill.chosen ? Theme.text : pill.hovered ? Theme.secondary : Theme.muted
            font.family: Theme.font; font.pixelSize: 13; font.weight: pill.chosen ? Font.DemiBold : Font.Medium
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
        selectedIndex: Math.max(0, choice.values.indexOf(String(root.prop(choice.key, choice.fallback))))
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
            onClicked: root.copy(addressRow.value, qsTr("Endereço"))
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
                text: qsTr("Servidores")
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
            Rectangle {
                anchors.verticalCenter: titleText.verticalCenter
                visible: root.onlineCount > 0
                width: onlineLabel.implicitWidth + 32; height: 26; radius: 13
                color: "#1FA9CFA0"
                border.width: 1; border.color: "#40A9CFA0"
                Rectangle {
                    id: onlineDot
                    x: 11; anchors.verticalCenter: parent.verticalCenter
                    width: 7; height: 7; radius: 3.5
                    color: Theme.success
                    SequentialAnimation on opacity {
                        running: !root.reduced && root.visible; loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 900 }
                        NumberAnimation { to: 1; duration: 900 }
                    }
                }
                Text {
                    id: onlineLabel
                    x: 24; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 online").arg(root.onlineCount)
                    color: Theme.success
                    font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                }
            }
        }
        ActionButton {
            visible: servers.servers.length > 0
            text: qsTr("Novo servidor"); iconName: "plus"; primary: true; reduced: root.reduced
            onClicked: creator.prepare()
        }
    }

    // ── Steam friends sharing a server ────────────────────────────────────
    Flickable {
        id: friendsBar
        readonly property bool shown: root.hostingFriends.length > 0 || (root.joined.hostId || "") !== ""
        visible: shown
        x: root.gutter; y: header.y + header.height + 20
        width: root.width - root.gutter * 2
        height: shown ? 72 : 0
        contentWidth: friendsRow.implicitWidth
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        clip: true
        Row {
            id: friendsRow
            spacing: 10
            Repeater {
                // The joined host stays listed even if its presence lags behind.
                model: {
                    const list = root.hostingFriends.slice()
                    const id = root.joined.hostId || ""
                    if (id !== "" && !list.some(function(f) { return f.id === id }))
                        list.unshift({ id: id, name: root.joined.hostName || "Steam", avatar: "",
                                       hosting: { server: root.joined.server || "", version: root.joined.version || "", loader: root.joined.loader || "" } })
                    return list
                }
                delegate: Rectangle {
                    id: friendCard
                    required property var modelData
                    readonly property bool mine: (root.joined.hostId || "") === modelData.id
                    readonly property string joinState: mine ? (root.joined.state || "") : ""
                    width: 440; height: 72
                    radius: Theme.radiusLg
                    color: mine ? Theme.accentSoft : Theme.fill
                    border.width: 1; border.color: mine ? Theme.accentLine : Theme.line
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 12
                        spacing: 12
                        SteamAvatar { source: friendCard.modelData.avatar || ""; name: friendCard.modelData.name }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: friendCard.modelData.name + qsTr(" está hospedando")
                                color: Theme.muted; font.family: Theme.font; font.pixelSize: 12
                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                            Text {
                                text: friendCard.modelData.hosting.server || "—"
                                color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                            Text {
                                text: friendCard.joinState === "ready"
                                    ? qsTr("Conectado · %1 ms · %2").arg(root.joined.ping || 0).arg(root.joined.address || "")
                                    : friendCard.joinState === "connecting" ? qsTr("Conectando pela Steam…")
                                    : (friendCard.modelData.hosting.version || "") + (friendCard.modelData.hosting.loader && friendCard.modelData.hosting.loader !== "Vanilla" ? " · " + friendCard.modelData.hosting.loader : "")
                                color: friendCard.joinState === "ready" ? Theme.accentText : Theme.faint
                                font.family: Theme.font; font.pixelSize: 11
                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                        }
                        ActionButton {
                            visible: !friendCard.mine || friendCard.joinState === "failed"
                            text: qsTr("Entrar"); iconName: "play"; primary: true; reduced: root.reduced
                            enabled: steam.ready && (root.joined.state || "") !== "connecting"
                            onClicked: root.joinRequested(friendCard.modelData.id)
                        }
                        // The address for Minecraft's server list or Direct Connect, while connected.
                        ActionButton {
                            visible: friendCard.joinState === "ready"
                            iconName: "copy"; quiet: true; round: true; width: 36; height: 36; reduced: root.reduced
                            Accessible.name: qsTr("Copiar endereço")
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Copiar endereço para o Minecraft"); ToolTip.delay: 500
                            onClicked: root.copy(root.joined.address, qsTr("Endereço"))
                        }
                        ActionButton {
                            visible: friendCard.mine && friendCard.joinState !== "failed"
                            text: qsTr("Sair"); iconName: "logout"; quiet: true; reduced: root.reduced
                            onClicked: steam.leave()
                        }
                    }
                }
            }
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
            text: qsTr("Hospede um servidor no seu PC")
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Escolha um jogo da biblioteca: o servidor usa a mesma versão, o mesmo loader e os mesmos mods.")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
        Item { width: 1; height: 8 }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Criar servidor"); iconName: "plus"; primary: true; reduced: root.reduced
            onClicked: creator.prepare()
        }
    }

    // ── Server list ───────────────────────────────────────────────────────
    component Chip: Rectangle {
        property alias text: chipLabel.text
        property color ink: Theme.muted
        implicitWidth: chipLabel.implicitWidth + 14; implicitHeight: 20
        radius: 6
        color: "#0FFFFFFF"
        border.width: 1; border.color: Theme.line
        Text {
            id: chipLabel
            anchors.centerIn: parent
            color: parent.ink
            font.family: Theme.font; font.pixelSize: 11; font.weight: Font.Medium
        }
    }
    component MenuEntry: MenuItem {
        id: menuItem
        property bool destructive: false
        implicitHeight: 36
        leftPadding: 14; rightPadding: 14
        contentItem: Text {
            text: menuItem.text
            color: !menuItem.enabled ? Theme.faint : menuItem.destructive ? Theme.danger : Theme.text
            font.family: Theme.font; font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            x: 6; width: parent.width - 12; height: parent.height
            radius: 8
            color: menuItem.highlighted ? Theme.fillActive : "transparent"
        }
    }
    // One menu for every card, aimed at the card that opened it.
    Menu {
        id: serverMenu
        property var target: ({})
        readonly property string targetState: root.statusOf(target.id || "").state || "stopped"
        readonly property bool targetAlive: targetState === "starting" || targetState === "running" || targetState === "stopping"
        function openFor(entry) { target = entry; popup() }
        topPadding: 6; bottomPadding: 6
        background: Rectangle {
            implicitWidth: 210
            color: Theme.popup; radius: 12
            border.width: 1; border.color: Theme.lineStrong
        }
        MenuEntry {
            text: serverMenu.targetAlive ? qsTr("Parar") : qsTr("Iniciar")
            enabled: serverMenu.targetState !== "installing" && serverMenu.targetState !== "stopping"
            onTriggered: serverMenu.targetAlive ? servers.stop(serverMenu.target.id) : servers.start(serverMenu.target.id)
        }
        MenuEntry {
            text: qsTr("Copiar endereço")
            onTriggered: {
                const tunnel = root.statusOf(serverMenu.target.id).tunnel || ""
                const port = Number(serverMenu.target.port || 25565)
                root.copy(tunnel !== "" ? tunnel : servers.lanAddress + (port === 25565 ? "" : ":" + port), qsTr("Endereço"))
            }
        }
        MenuEntry { text: qsTr("Abrir pasta"); onTriggered: servers.openFolder(serverMenu.target.id) }
        MenuEntry {
            text: qsTr("Mods e datapacks")
            onTriggered: { root.selectedId = serverMenu.target.id; tabs.currentIndex = root.tabKeys.indexOf("content") }
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.line }
            leftPadding: 12; rightPadding: 12; topPadding: 4; bottomPadding: 4
        }
        MenuEntry {
            text: qsTr("Excluir servidor…")
            destructive: true
            onTriggered: deletion.openFor(serverMenu.target.id)
        }
    }

    Item {
        id: listPanel
        visible: servers.servers.length > 0
        x: root.gutter; y: friendsBar.shown ? friendsBar.y + friendsBar.height + 20 : header.y + header.height + 24
        width: root.width < 1100 ? 248 : 296
        height: root.height - y - 24

        InputField {
            id: listSearch
            visible: servers.servers.length > 4
            width: parent.width; height: visible ? 38 : 0
            iconName: "search"; clearable: true
            placeholderText: qsTr("Buscar servidor")
            font.pixelSize: 13
            onTextChanged: root.listFilter = text
        }

        ListView {
            id: list
            y: listSearch.visible ? listSearch.height + 10 : 0
            width: parent.width
            height: parent.height - y
            spacing: 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.shownServers
            ScrollBar.vertical: ThinScrollBar {}
            footer: Item {
                width: list.width - 10; height: newCard.height + 8
                AbstractButton {
                    id: newCard
                    y: 8
                    width: parent.width; height: 52
                    hoverEnabled: true
                    onClicked: creator.prepare()
                    background: Rectangle {
                        radius: Theme.radiusLg
                        color: newCard.hovered ? Theme.fill : "transparent"
                        border.width: 1; border.color: newCard.hovered ? Theme.lineStrong : Theme.line
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            UiIcon { anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; name: "plus"; color: newCard.hovered ? Theme.text : Theme.muted }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Novo servidor")
                                color: newCard.hovered ? Theme.text : Theme.muted
                                font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
            delegate: AbstractButton {
                id: entry
                required property var modelData
                readonly property var game: root.gameOf(modelData)
                readonly property var entryStatus: root.statusOf(modelData.id)
                readonly property string entryState: entryStatus.state || "stopped"
                readonly property bool entryAlive: entryState === "starting" || entryState === "running" || entryState === "stopping"
                readonly property bool active: root.selectedId === modelData.id
                readonly property int playerCount: (entryStatus.players || []).length
                width: list.width - 10; height: 86
                hoverEnabled: true
                onHoveredChanged: if (hovered && !active) sfx.play("hover")
                onClicked: { if (!active) sfx.play("click"); root.selectedId = modelData.id }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: serverMenu.openFor(entry.modelData) }
                transform: Translate { x: entry.hovered && !entry.active && !root.reduced ? 3 : 0; Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } } }
                background: Rectangle {
                    radius: Theme.radiusLg
                    color: entry.active ? Theme.accentSoft : entry.hovered ? Theme.fillHover : Theme.fill
                    border.width: 1; border.color: entry.active ? Theme.accentLine : entry.hovered ? Theme.lineStrong : Theme.line
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                    // The state color as a thin bar on the left edge while the server is not stopped.
                    Rectangle {
                        x: 0; anchors.verticalCenter: parent.verticalCenter
                        width: 3; height: parent.height - 32; radius: 1.5
                        color: root.stateColor(entry.entryState)
                        opacity: entry.entryState === "stopped" ? 0 : 0.9
                        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                    }
                }
                leftPadding: 12; rightPadding: 8
                contentItem: RowLayout {
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 46; Layout.preferredHeight: 46
                        radius: 12
                        color: "#14FFFFFF"
                        border.width: 1; border.color: Theme.line
                        Image {
                            anchors.fill: parent; anchors.margins: 3
                            visible: source != ""
                            source: entry.game && entry.game.packIcon ? entry.game.packIcon : ""
                            fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: true
                        }
                        UiIcon { anchors.centerIn: parent; visible: !(entry.game && entry.game.packIcon); width: 20; height: 20; name: "server"; color: Theme.muted }
                        Rectangle {
                            id: entryDot
                            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: -3
                            width: 14; height: 14; radius: 7
                            color: root.stateColor(entry.entryState)
                            border.width: 2.5; border.color: Theme.base
                            SequentialAnimation on opacity {
                                running: entry.entryAlive && entry.entryState !== "running" && !root.reduced && root.visible
                                loops: Animation.Infinite
                                onRunningChanged: if (!running) entryDot.opacity = 1
                                NumberAnimation { to: 0.3; duration: 600 }
                                NumberAnimation { to: 1; duration: 600 }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: entry.modelData.name
                            color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                            Layout.fillWidth: true; elide: Text.ElideRight
                        }
                        Row {
                            spacing: 5
                            Layout.fillWidth: true
                            Layout.preferredHeight: 20
                            clip: true
                            Chip { text: entry.modelData.version }
                            Chip { visible: entry.modelData.loader !== "Vanilla"; text: entry.modelData.loader }
                            Chip {
                                visible: entry.entryState === "running"
                                text: entry.playerCount === 1 ? qsTr("1 jogador") : qsTr("%1 jogadores").arg(entry.playerCount)
                                ink: entry.playerCount > 0 ? Theme.success : Theme.muted
                            }
                        }
                        Text {
                            text: entry.entryState === "running" ? qsTr("Online há %1").arg(root.span(entry.modelData.lastStartedAt))
                                : entry.entryState !== "stopped" ? root.stateLabel(entry.entryState) + (entry.entryStatus.detail ? " · " + entry.entryStatus.detail : "")
                                : !entry.modelData.installed ? qsTr("Ainda não instalado")
                                : entry.modelData.lastStartedAt ? qsTr("Ligado ") + root.ago(entry.modelData.lastStartedAt)
                                : qsTr("Nunca iniciado")
                            color: entry.entryState === "stopped" ? Theme.faint : root.stateColor(entry.entryState)
                            font.family: Theme.font; font.pixelSize: 11
                            Layout.fillWidth: true; elide: Text.ElideRight
                        }
                    }
                    // Quick actions show on hover (and stay on the selected card).
                    Column {
                        spacing: 4
                        opacity: entry.hovered || entry.active ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                        ActionButton {
                            width: 30; height: 30; round: true; iconSize: 13
                            iconName: entry.entryAlive ? "stop" : "play"
                            primary: !entry.entryAlive; danger: entry.entryAlive
                            enabled: entry.entryState !== "installing" && entry.entryState !== "stopping"
                            reduced: root.reduced
                            Accessible.name: (entry.entryAlive ? qsTr("Parar ") : qsTr("Iniciar ")) + entry.modelData.name
                            ToolTip.visible: hovered; ToolTip.text: entry.entryAlive ? qsTr("Parar") : qsTr("Iniciar"); ToolTip.delay: 500
                            onClicked: entry.entryAlive ? servers.stop(entry.modelData.id) : servers.start(entry.modelData.id)
                        }
                        ActionButton {
                            width: 30; height: 30; round: true; iconSize: 13
                            iconName: "dots"; quiet: true; reduced: root.reduced
                            Accessible.name: qsTr("Mais opções de ") + entry.modelData.name
                            onClicked: serverMenu.openFor(entry.modelData)
                        }
                    }
                }
            }
            Text {
                visible: list.count === 0 && root.listFilter !== ""
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                text: qsTr("Nenhum servidor encontrado")
                color: Theme.faint; font.family: Theme.font; font.pixelSize: 13
            }
        }
    }

    // ── Dashboard ─────────────────────────────────────────────────────────
    Item {
        id: dashboard
        visible: root.server !== null
        x: listPanel.x + listPanel.width + 24
        y: listPanel.y
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
                            + (game ? qsTr("  ·  de ") + game.title : "") + detail
                    }
                    color: root.state === "failed" ? Theme.danger : Theme.muted
                    font.family: Theme.font; font.pixelSize: 13
                    Layout.fillWidth: true; elide: Text.ElideRight
                }
            }
            ActionButton {
                visible: !root.alive && root.state !== "installing"
                text: root.server && !root.server.installed ? qsTr("Instalar") : qsTr("Iniciar")
                iconName: "play"; primary: true; reduced: root.reduced
                onClicked: servers.start(root.server.id)
            }
            ActionButton {
                visible: root.online || root.state === "starting"
                text: qsTr("Reiniciar"); iconName: "restart"; reduced: root.reduced
                onClicked: servers.restart(root.server.id)
            }
            ActionButton {
                visible: root.online || root.state === "starting"
                text: qsTr("Parar"); iconName: "stop"; danger: true; reduced: root.reduced
                onClicked: servers.stop(root.server.id)
            }
            ActionButton {
                visible: root.state === "stopping"
                text: qsTr("Forçar"); iconName: "close"; danger: true; quiet: true; reduced: root.reduced
                onClicked: servers.kill(root.server.id)
            }
        }

        RowLayout {
            id: stats
            y: titleRow.height + 18
            width: parent.width
            spacing: 12
            StatCard {
                label: qsTr("Jogadores")
                value: root.online ? root.players.length + " / " + root.prop("max-players", "20") : "—"
                hint: root.players.length > 0 ? root.players.join(", ") : root.online ? qsTr("Ninguém online") : ""
            }
            StatCard { label: qsTr("Tempo ligado"); value: root.uptime() }
            StatCard {
                label: qsTr("Memória")
                value: root.server ? (root.server.memoryMb / 1024).toFixed(root.server.memoryMb % 1024 === 0 ? 0 : 1) + " GB" : "—"
                hint: qsTr("Máximo para o Java")
            }
            StatCard {
                readonly property string shown: root.tunnel !== "" ? root.tunnel : root.address(servers.lanAddress)
                label: root.tunnel !== "" ? qsTr("Endereço público") : qsTr("Endereço local")
                value: shown
                valueColor: root.tunnel !== "" ? Theme.accentText : Theme.text
                hint: root.tunnel !== "" ? qsTr("Mande para os amigos · clique para copiar") : qsTr("Clique para copiar")
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.copy(parent.shown, qsTr("Endereço")) }
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
                Text { text: qsTr("Algumas mudanças só valem depois de reiniciar o servidor."); color: "#F2DDA6"; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                ActionButton { text: qsTr("Reiniciar agora"); quiet: true; height: 32; reduced: root.reduced; onClicked: servers.restart(root.server.id) }
            }
        }

        SegmentTabs {
            id: tabs
            y: (restartBanner.visible ? restartBanner.y + restartBanner.height : stats.y + stats.height) + 18
            width: Math.min(parent.width, 640)
            model: [qsTr("Console"), qsTr("Conteúdo"), qsTr("Configurações"), qsTr("Jogadores"), qsTr("Conexão"), qsTr("Backups")]
            reduced: root.reduced
            onCurrentIndexChanged: {
                if (root.tab === "players" || root.tab === "backups") root.refreshLists()
                if (root.tab === "content") contentTab.reload()
            }
        }

        Item {
            id: tabArea
            y: tabs.y + tabs.height + 16
            width: parent.width
            height: parent.height - y - 24

            // Console
            Rectangle {
                visible: root.tab === "console"
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
                    text: qsTr("O console aparece aqui quando o servidor inicia.")
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
                        placeholderText: enabled ? qsTr("Digite um comando (ex.: time set day, gamemode creative Steve)") : qsTr("Inicie o servidor para usar comandos")
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
                        model: [{ label: qsTr("Dia"), cmd: "time set day" }, { label: qsTr("Sol"), cmd: "weather clear" }, { label: qsTr("Salvar"), cmd: "save-all" }, { label: qsTr("Lista"), cmd: "list" }]
                        delegate: ActionButton {
                            required property var modelData
                            visible: dashboard.width > 760
                            text: modelData.label; quiet: true; reduced: root.reduced
                            enabled: root.online
                            onClicked: root.command(modelData.cmd)
                        }
                    }
                    ActionButton {
                        iconName: "copy"; quiet: true; round: true; width: 40; height: 40; reduced: root.reduced
                        enabled: consoleModel.count > 0
                        Accessible.name: qsTr("Copiar console")
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Copiar console"); ToolTip.delay: 500
                        onClicked: {
                            const lines = []
                            for (let i = 0; i < consoleModel.count; i++) lines.push(consoleModel.get(i).line)
                            root.copy(lines.join("\n"), qsTr("Console"))
                        }
                    }
                    ActionButton {
                        iconName: "trash"; quiet: true; round: true; width: 40; height: 40; reduced: root.reduced
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Limpar console"); ToolTip.delay: 500
                        Accessible.name: "Limpar console"
                        onClicked: { servers.clearConsole(root.server.id); consoleModel.clear() }
                    }
                }
            }

            // Content: the server's mods and its world's datapacks, from Modrinth or by hand.
            Item {
                id: contentTab
                visible: root.tab === "content"
                anchors.fill: parent
                property string kind: "mod"
                property string filter: ""
                readonly property bool vanilla: root.server !== null && root.server.loader === "Vanilla"
                // Vanilla servers take datapacks only.
                readonly property string kindShown: vanilla ? "datapack" : kind
                readonly property string targetId: root.server ? "server:" + root.server.id : ""
                readonly property string loader: kindShown === "datapack" ? "datapack" : (root.server ? root.server.loader : "")
                readonly property string world: String(root.prop("level-name", "world")) || "world"
                readonly property bool loaded: targetId !== "" && mods.gameId === targetId && mods.kind === kindShown
                readonly property var items: loaded ? mods.installed : []
                readonly property var shown: {
                    const query = filter.trim().toLowerCase()
                    if (query === "") return items
                    return items.filter(function(m) { return (m.title + " " + m.file).toLowerCase().indexOf(query) !== -1 })
                }
                readonly property int disabledCount: items.filter(function(m) { return !m.enabled }).length
                property string checkedFor: ""
                function reload() {
                    if (targetId === "") return
                    mods.loadInstalled(targetId, kindShown)
                    checkUpdates(false)
                }
                function checkUpdates(force) {
                    if (!root.server || (!force && checkedFor === targetId + kindShown)) return
                    checkedFor = targetId + kindShown
                    mods.checkUpdates(targetId, root.server.version, loader)
                }
                function changed() { if (root.alive) root.restartNeeded = true }
                function openCatalog() {
                    if (!root.server) return
                    root.contentRequested({ id: targetId, title: root.server.name, version: root.server.version, loader: root.server.loader },
                                          kindShown, loader)
                }
                onKindShownChanged: if (visible) reload()
                onTargetIdChanged: { contentSearch.text = ""; if (visible) reload() }
                // The game drawer shares the mod list: take it back when the tab shows again.
                onVisibleChanged: if (visible) reload()
                Connections {
                    target: mods
                    function onInstallFinished(projectId, ok) { if (ok && contentTab.loaded) contentTab.changed() }
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            visible: !contentTab.vanilla
                            Layout.preferredWidth: pills.implicitWidth + 8
                            Layout.preferredHeight: 40
                            radius: Theme.radiusMd
                            color: Theme.fill
                            border.width: 1; border.color: Theme.line
                            Row {
                                id: pills
                                anchors.centerIn: parent
                                spacing: 2
                                KindPill { text: qsTr("Mods"); chosen: contentTab.kind === "mod"; onClicked: contentTab.kind = "mod" }
                                KindPill { text: qsTr("Datapacks"); chosen: contentTab.kind === "datapack"; onClicked: contentTab.kind = "datapack" }
                            }
                        }
                        InputField {
                            id: contentSearch
                            Layout.fillWidth: true
                            iconName: "search"; clearable: true
                            placeholderText: contentTab.kindShown === "datapack" ? qsTr("Buscar nos datapacks") : qsTr("Buscar nos mods")
                            onTextChanged: contentTab.filter = text
                        }
                        ActionButton {
                            text: qsTr("Adicionar"); iconName: "plus"; primary: true; reduced: root.reduced
                            onClicked: contentTab.openCatalog()
                        }
                        ActionButton {
                            Layout.preferredWidth: 40
                            iconName: "folder"; reduced: root.reduced
                            Accessible.name: qsTr("Abrir pasta")
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Abrir pasta"); ToolTip.delay: 500
                            onClicked: servers.openFolder(root.server.id, contentTab.kindShown === "datapack" ? contentTab.world + "/datapacks" : "mods")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        Layout.bottomMargin: 8
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            readonly property int count: contentTab.items.length
                            text: (contentTab.kindShown === "datapack"
                                   ? (count === 0 ? qsTr("Nenhum datapack") : count === 1 ? qsTr("1 datapack") : qsTr("%1 datapacks").arg(count))
                                   : (count === 0 ? qsTr("Nenhum mod") : count === 1 ? qsTr("1 mod") : qsTr("%1 mods").arg(count)))
                                + (contentTab.disabledCount > 0 ? "  ·  " + contentTab.disabledCount + (contentTab.disabledCount > 1 ? qsTr(" desativados") : qsTr(" desativado")) : "")
                                + (contentTab.kindShown === "datapack" ? qsTr("  ·  mundo \"%1\"").arg(contentTab.world) : "")
                            color: Theme.muted
                            font.family: Theme.font; font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: mods.checkingUpdates && contentTab.loaded
                            text: qsTr("Verificando atualizações…")
                            color: Theme.faint
                            font.family: Theme.font; font.pixelSize: 12
                        }
                        ActionButton {
                            visible: contentTab.loaded && !mods.checkingUpdates && mods.updateCount > 0
                            implicitHeight: 30
                            text: qsTr("Atualizar %1").arg(mods.updateCount)
                            iconName: "download"; iconSize: 14
                            primary: true; reduced: root.reduced
                            font.pixelSize: 12
                            onClicked: { mods.updateAll(contentTab.targetId, root.server.version, contentTab.loader); contentTab.changed() }
                        }
                        ActionButton {
                            visible: contentTab.loaded && !mods.checkingUpdates && mods.updateCount === 0 && contentTab.items.length > 0
                            implicitHeight: 30
                            text: qsTr("Verificar atualizações")
                            quiet: true; reduced: root.reduced
                            font.pixelSize: 12
                            onClicked: contentTab.checkUpdates(true)
                        }
                        ActionButton {
                            visible: contentTab.kindShown === "mod"
                            implicitHeight: 30
                            text: qsTr("Sincronizar com o jogo"); iconName: "restart"; iconSize: 13
                            quiet: true; reduced: root.reduced
                            font.pixelSize: 12
                            enabled: root.state === "stopped"
                            ToolTip.visible: hovered; ToolTip.delay: 500
                            ToolTip.text: root.state === "stopped" ? qsTr("Copia de novo os mods do jogo (menos os só de cliente).") : qsTr("Pare o servidor para sincronizar.")
                            onClicked: servers.syncContent(root.server.id)
                        }
                        ActionButton {
                            visible: contentTab.kindShown === "datapack"
                            implicitHeight: 30
                            text: qsTr("Recarregar"); iconName: "restart"; iconSize: 13
                            quiet: true; reduced: root.reduced
                            font.pixelSize: 12
                            enabled: root.online
                            ToolTip.visible: hovered; ToolTip.delay: 500
                            ToolTip.text: root.online ? qsTr("Aplica os datapacks sem reiniciar (/reload).") : qsTr("Os datapacks entram quando o servidor iniciar.")
                            onClicked: { root.command("reload"); root.notify(qsTr("Datapacks recarregados.")) }
                        }
                    }

                    ListView {
                        id: contentView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: contentTab.shown
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ThinScrollBar {}
                        delegate: Item {
                            id: contentRow
                            required property var modelData
                            readonly property bool hasUpdate: (modelData.updateVersionId || "") !== ""
                            readonly property bool busy: modelData.projectId !== "" && mods.installing.indexOf(modelData.projectId) !== -1
                            width: contentView.width - 12
                            height: 60
                            HoverHandler { id: contentHover }
                            Rectangle {
                                anchors.fill: parent
                                anchors.topMargin: 2; anchors.bottomMargin: 2
                                radius: 10
                                color: contentHover.hovered ? "#08FFFFFF" : "transparent"
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8; anchors.rightMargin: 4
                                spacing: 12
                                Rectangle {
                                    Layout.preferredWidth: 38; Layout.preferredHeight: 38
                                    radius: 9; color: Theme.fill; clip: true
                                    opacity: contentRow.modelData.enabled ? 1 : 0.4
                                    UiIcon {
                                        anchors.centerIn: parent
                                        visible: contentIcon.status !== Image.Ready
                                        width: 16; height: 16
                                        name: contentTab.kindShown === "datapack" ? "box" : "puzzle"; color: Theme.faint
                                    }
                                    Image {
                                        id: contentIcon
                                        anchors.fill: parent
                                        source: contentRow.modelData.iconUrl
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
                                        text: contentRow.modelData.title
                                        color: contentRow.modelData.enabled ? Theme.text : Theme.muted
                                        font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    RowLayout {
                                        spacing: 6
                                        Text {
                                            Layout.maximumWidth: 220
                                            text: contentRow.modelData.versionNumber || contentRow.modelData.file
                                            color: Theme.faint
                                            font.family: Theme.font; font.pixelSize: 11
                                            elide: Text.ElideMiddle
                                        }
                                        Rectangle {
                                            visible: contentRow.hasUpdate
                                            Layout.preferredHeight: 18
                                            Layout.preferredWidth: contentUpdate.implicitWidth + 12
                                            radius: 5
                                            color: Theme.accentSoft
                                            Text {
                                                id: contentUpdate
                                                anchors.centerIn: parent
                                                text: "→ " + (contentRow.modelData.updateVersionNumber || "")
                                                color: Theme.accentText
                                                font.family: Theme.font; font.pixelSize: 10; font.weight: Font.DemiBold
                                            }
                                        }
                                        Text {
                                            visible: !contentRow.modelData.enabled
                                            text: qsTr("desativado")
                                            color: Theme.faint
                                            font.family: Theme.font; font.pixelSize: 11
                                        }
                                    }
                                }
                                Text {
                                    visible: contentRow.busy
                                    text: qsTr("Instalando…")
                                    color: Theme.muted
                                    font.family: Theme.font; font.pixelSize: 11
                                }
                                ActionButton {
                                    visible: contentRow.hasUpdate && !contentRow.busy
                                    width: 30; height: 30; round: true
                                    iconName: "download"; iconSize: 14
                                    primary: true; reduced: root.reduced
                                    Accessible.name: qsTr("Atualizar ") + contentRow.modelData.title
                                    ToolTip.visible: hovered; ToolTip.text: qsTr("Atualizar para ") + contentRow.modelData.updateVersionNumber; ToolTip.delay: 400
                                    onClicked: {
                                        mods.installVersionOf(contentRow.modelData.projectId, contentRow.modelData.updateVersionId,
                                                              contentTab.targetId, root.server.version, contentTab.loader, contentTab.kindShown)
                                        contentTab.changed()
                                    }
                                }
                                ToggleSwitch {
                                    checked: contentRow.modelData.enabled
                                    reduced: root.reduced
                                    Accessible.name: qsTr("Ativar ") + contentRow.modelData.title
                                    onToggled: {
                                        if (mods.setEnabled(contentTab.targetId, contentRow.modelData.file, checked, contentTab.kindShown)) {
                                            contentTab.changed()
                                        } else {
                                            checked = Qt.binding(function() { return contentRow.modelData.enabled })
                                            root.notify(mods.lastError)
                                        }
                                    }
                                }
                                ActionButton {
                                    width: 30; height: 30; round: true
                                    quiet: true; danger: true; iconName: "trash"; iconSize: 14
                                    reduced: root.reduced
                                    opacity: contentHover.hovered ? 1 : 0.5
                                    Accessible.name: qsTr("Remover ") + contentRow.modelData.title
                                    ToolTip.visible: hovered; ToolTip.text: qsTr("Remover"); ToolTip.delay: 500
                                    onClicked: contentRemoval.openFor(contentRow.modelData)
                                }
                            }
                            Rectangle { anchors.bottom: parent.bottom; x: 8; width: parent.width - 8; height: 1; color: Theme.line }
                        }

                        Text {
                            visible: contentTab.items.length > 0 && contentTab.shown.length === 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 28
                            text: qsTr("Nada corresponde à busca")
                            color: Theme.muted
                            font.family: Theme.font; font.pixelSize: 13
                        }
                        Column {
                            visible: contentTab.loaded && contentTab.items.length === 0
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 40, 420)
                            spacing: 8
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 56; height: 56; radius: 28
                                color: Theme.fill
                                border.width: 1; border.color: Theme.line
                                UiIcon { anchors.centerIn: parent; width: 24; height: 24; name: contentTab.kindShown === "datapack" ? "box" : "puzzle"; color: Theme.muted }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                topPadding: 6
                                text: contentTab.kindShown === "datapack" ? qsTr("Nenhum datapack no mundo") : qsTr("Nenhum mod no servidor")
                                color: Theme.text
                                font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: contentTab.kindShown === "datapack"
                                    ? (contentTab.vanilla ? qsTr("Servidores Vanilla não usam mods, mas datapacks funcionam: receitas, mecânicas, estruturas e mais, sem instalar nada nos jogadores.")
                                                          : qsTr("Datapacks mudam receitas, mecânicas e a geração do mundo, e só o servidor precisa deles."))
                                    : qsTr("Busque no Modrinth (só aparecem mods que funcionam em servidor) ou sincronize com os mods do jogo.")
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 13
                            }
                            Item { width: 1; height: 6 }
                            ActionButton {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: contentTab.kindShown === "datapack" ? qsTr("Buscar datapacks") : qsTr("Buscar mods")
                                iconName: "search"; reduced: root.reduced
                                onClicked: contentTab.openCatalog()
                            }
                        }
                    }
                }
            }

            // Settings
            Flickable {
                id: settingsFlick
                visible: root.tab === "settings"
                anchors.fill: parent
                contentHeight: settingsColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: settingsColumn
                    width: settingsFlick.width - 14
                    spacing: 0
                    SectionTitle { text: qsTr("Geral"); Layout.topMargin: 0 }
                    Group {
                        SettingRow {
                            title: qsTr("Nome")
                            description: qsTr("Como o servidor aparece aqui no launcher.")
                            InputField {
                                id: nameField
                                width: 260
                                maximumLength: 48
                                Binding on text { when: !nameField.activeFocus; value: root.server ? root.server.name : "" }
                                onEditingFinished: if (root.server && text !== root.server.name && !servers.rename(root.server.id, text)) root.notify(qsTr("Use um nome de 1 a 48 letras."))
                            }
                        }
                        SettingRow { title: qsTr("Mensagem (MOTD)"); description: qsTr("Aparece na lista de servidores do Minecraft."); PropText { key: "motd"; maximumLength: 59 } }
                        SettingRow {
                            title: qsTr("Memória do servidor")
                            description: qsTr("Servidores com muitos mods precisam de 4 GB ou mais.")
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
                        SettingRow { title: qsTr("Máximo de jogadores"); PropNumber { key: "max-players"; from: 1; to: 100; fallback: 20 } }
                        SettingRow { title: qsTr("Porta"); description: qsTr("Padrão 25565. Mude para rodar dois servidores ao mesmo tempo."); divider: false; PropText { key: "server-port"; width: 120; validator: IntValidator { bottom: 1024; top: 65535 } } }
                    }

                    SectionTitle { text: qsTr("Jogo") }
                    Group {
                        SettingRow {
                            title: qsTr("Modo de jogo")
                            PropChoice { key: "gamemode"; values: ["survival", "creative", "adventure", "spectator"]; model: [qsTr("Sobrevivência"), qsTr("Criativo"), qsTr("Aventura"), qsTr("Espectador")] }
                        }
                        SettingRow {
                            title: qsTr("Dificuldade")
                            PropChoice { key: "difficulty"; values: ["peaceful", "easy", "normal", "hard"]; fallback: "easy"; model: [qsTr("Pacífico"), qsTr("Fácil"), qsTr("Normal"), qsTr("Difícil")] }
                        }
                        SettingRow { title: qsTr("Hardcore"); description: qsTr("Morreu, virou espectador."); PropToggle { key: "hardcore" } }
                        SettingRow { title: "PvP"; description: qsTr("Jogadores podem atacar uns aos outros."); PropToggle { key: "pvp"; fallback: true } }
                        SettingRow { title: qsTr("Permitir voo"); description: qsTr("Evita expulsões por voo com mods de jetpack ou asas."); PropToggle { key: "allow-flight" } }
                        SettingRow { title: qsTr("Blocos de comando"); PropToggle { key: "enable-command-block" } }
                        SettingRow { title: qsTr("Proteção do spawn"); description: qsTr("Raio em blocos onde só operadores constroem."); PropNumber { key: "spawn-protection"; from: 0; to: 64; fallback: 16; suffix: qsTr(" blocos") } }
                        SettingRow { title: qsTr("Distância de visão"); PropNumber { key: "view-distance"; from: 3; to: 32; fallback: 10; suffix: " chunks" } }
                        SettingRow { title: qsTr("Distância de simulação"); divider: false; PropNumber { key: "simulation-distance"; from: 3; to: 32; fallback: 10; suffix: " chunks" } }
                    }

                    SectionTitle { text: qsTr("Mundo") }
                    Group {
                        SettingRow { title: qsTr("Nome da pasta do mundo"); description: qsTr("Mude para começar um mundo novo (o antigo fica guardado)."); PropText { key: "level-name" } }
                        SettingRow { title: qsTr("Seed"); description: qsTr("Só vale para mundos novos. Vazio = aleatória."); PropText { key: "level-seed"; placeholderText: qsTr("Aleatória") } }
                        SettingRow {
                            title: qsTr("Tipo de mundo")
                            PropChoice {
                                key: "level-type"
                                values: ["minecraft:normal", "minecraft:flat", "minecraft:large_biomes", "minecraft:amplified"]
                                model: [qsTr("Normal"), qsTr("Plano"), qsTr("Biomas grandes"), qsTr("Amplificado")]
                            }
                        }
                        SettingRow { title: qsTr("Estruturas"); description: qsTr("Vilas, templos, fortalezas."); PropToggle { key: "generate-structures"; fallback: true } }
                        SettingRow { title: qsTr("Nether"); PropToggle { key: "allow-nether"; fallback: true } }
                        SettingRow { title: qsTr("Monstros"); divider: false; PropToggle { key: "spawn-monsters"; fallback: true } }
                    }

                    SectionTitle { text: qsTr("Acesso") }
                    Group {
                        SettingRow {
                            title: qsTr("Só contas originais")
                            description: qsTr("Ligado, só entra quem tem conta Microsoft. Deixe desligado para amigos com perfil offline.")
                            PropToggle { key: "online-mode"; fallback: true }
                        }
                        SettingRow { title: qsTr("Lista de permissões (whitelist)"); description: qsTr("Só entra quem estiver na lista (aba Jogadores)."); PropToggle { key: "white-list" } }
                        SettingRow { title: qsTr("Aparecer como online"); description: qsTr("Mostra jogadores e status na lista do Minecraft."); divider: false; PropToggle { key: "enable-status"; fallback: true } }
                    }

                    SectionTitle { text: qsTr("Arquivos") }
                    Group {
                        SettingRow {
                            title: qsTr("Mods e configurações")
                            description: qsTr("Copia de novo do jogo os mods (menos os só de cliente) e as configs. Use depois de atualizar o modpack.")
                            ActionButton {
                                text: qsTr("Sincronizar"); iconName: "download"; quiet: true; reduced: root.reduced
                                enabled: root.state === "stopped" && root.server && root.server.loader !== "Vanilla"
                                onClicked: servers.syncContent(root.server.id)
                            }
                        }
                        SettingRow {
                            title: qsTr("Pasta do servidor")
                            ActionButton { text: qsTr("Abrir"); iconName: "folder"; quiet: true; reduced: root.reduced; onClicked: servers.openFolder(root.server.id) }
                        }
                        SettingRow {
                            title: qsTr("Reinstalar")
                            description: qsTr("Baixa os arquivos do servidor de novo. O mundo e as configs ficam.")
                            ActionButton { text: qsTr("Reinstalar"); quiet: true; reduced: root.reduced; enabled: !root.busy; onClicked: servers.install(root.server.id) }
                        }
                        SettingRow {
                            title: qsTr("Excluir servidor")
                            description: qsTr("Vai para a Lixeira do Windows, com o mundo.")
                            divider: false
                            ActionButton { text: qsTr("Excluir"); iconName: "trash"; danger: true; quiet: true; reduced: root.reduced; onClicked: deletion.openFor(root.server.id) }
                        }
                    }
                }
            }

            // Players
            Flickable {
                id: playersFlick
                visible: root.tab === "players"
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
                        text: qsTr("Inicie o servidor para gerenciar jogadores.")
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 13
                    }
                    SectionTitle { text: qsTr("Online agora"); Layout.topMargin: 0 }
                    Group {
                        Text {
                            visible: root.players.length === 0
                            Layout.margins: 20
                            text: root.online ? qsTr("Ninguém online. Mande o endereço para seus amigos (aba Conexão).") : qsTr("Servidor parado.")
                            color: Theme.faint; font.family: Theme.font; font.pixelSize: 13
                        }
                        Repeater {
                            model: root.players
                            delegate: SettingRow {
                                id: onlineRow
                                required property string modelData
                                required property int index
                                title: modelData
                                description: root.ops.indexOf(modelData) !== -1 ? qsTr("Operador") : ""
                                divider: index < root.players.length - 1
                                Row {
                                    spacing: 6
                                    ActionButton {
                                        text: root.ops.indexOf(onlineRow.modelData) !== -1 ? qsTr("Tirar op") : qsTr("Dar op")
                                        quiet: true; height: 34; reduced: root.reduced
                                        onClicked: root.command((root.ops.indexOf(onlineRow.modelData) !== -1 ? "deop " : "op ") + onlineRow.modelData)
                                    }
                                    ActionButton { text: qsTr("Expulsar"); quiet: true; height: 34; reduced: root.reduced; onClicked: root.command("kick " + onlineRow.modelData) }
                                    ActionButton { text: qsTr("Banir"); danger: true; quiet: true; height: 34; reduced: root.reduced; onClicked: root.command("ban " + onlineRow.modelData) }
                                }
                            }
                        }
                    }

                    SectionTitle { text: qsTr("Adicionar à lista") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        InputField {
                            id: playerName
                            Layout.fillWidth: true
                            placeholderText: qsTr("Nome do jogador")
                            maximumLength: 16
                            validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9_]{0,16}/ }
                            enabled: root.online
                        }
                        ActionButton { text: qsTr("Permitir"); reduced: root.reduced; enabled: root.online && playerName.text !== ""; onClicked: { root.command("whitelist add " + playerName.text); playerName.text = "" } }
                        ActionButton { text: qsTr("Dar op"); quiet: true; reduced: root.reduced; enabled: root.online && playerName.text !== ""; onClicked: { root.command("op " + playerName.text); playerName.text = "" } }
                        ActionButton { text: qsTr("Banir"); danger: true; quiet: true; reduced: root.reduced; enabled: root.online && playerName.text !== ""; onClicked: { root.command("ban " + playerName.text); playerName.text = "" } }
                    }

                    Repeater {
                        model: [{ title: qsTr("Lista de permissões"), names: root.whitelist, remove: "whitelist remove ", empty: qsTr("Ninguém na lista.") },
                                { title: qsTr("Operadores"), names: root.ops, remove: "deop ", empty: qsTr("Nenhum operador.") },
                                { title: qsTr("Banidos"), names: root.banned, remove: "pardon ", empty: qsTr("Ninguém banido.") }]
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
                                            text: qsTr("Remover"); quiet: true; height: 34; reduced: root.reduced
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
                visible: root.tab === "connection"
                anchors.fill: parent
                contentHeight: connectionColumn.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.vertical: ThinScrollBar {}
                ColumnLayout {
                    id: connectionColumn
                    width: connectionFlick.width - 14
                    spacing: 0
                    // Only with Steamworks in the build and the connection turned on (developer mode in releases).
                    SectionTitle { visible: steam.available && steam.enabled; text: qsTr("Amigos da Steam"); Layout.topMargin: 0 }
                    Group {
                        visible: steam.available && steam.enabled
                        SettingRow {
                            title: qsTr("Compartilhar com amigos da Steam")
                            description: {
                                if (!steam.ready) return qsTr("Abra a Steam e entre na sua conta para usar.")
                                if (root.steamShared) {
                                    const players = steam.hosting.players || []
                                    return players.length > 0 ? qsTr("Conectados pela Steam agora: ") + players.join(", ")
                                        : qsTr("Seus amigos veem este servidor no OmniLauncher e entram com um clique, pelo relay da Valve: sem IP, porta ou mod.")
                                }
                                if (!root.steamSharing) return qsTr("Desligado: seus amigos não veem este servidor.")
                                return root.online ? qsTr("Outro servidor seu já está compartilhado.") : qsTr("Aparece para seus amigos enquanto o servidor estiver online.")
                            }
                            divider: root.steamShared
                            ToggleSwitch {
                                reduced: root.reduced
                                enabled: steam.ready && root.server !== null
                                checked: root.steamSharing
                                Accessible.name: qsTr("Compartilhar com amigos da Steam")
                                onToggled: steam.setSharing(root.server.id, checked)
                            }
                        }
                        Repeater {
                            model: root.steamShared ? steam.friends.filter(function(f) { return f.id !== "loopback" }).slice(0, 40) : []
                            delegate: RowLayout {
                                id: friendRow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.leftMargin: 20; Layout.rightMargin: 14
                                Layout.topMargin: 8; Layout.bottomMargin: 8
                                spacing: 12
                                SteamAvatar { source: friendRow.modelData.avatar || ""; name: friendRow.modelData.name; width: 30; height: 30 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: friendRow.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text {
                                        text: friendRow.modelData.inLauncher ? qsTr("Com o OmniLauncher aberto") : qsTr("Online na Steam")
                                        color: friendRow.modelData.inLauncher ? Theme.success : Theme.faint
                                        font.family: Theme.font; font.pixelSize: 11
                                    }
                                }
                                ActionButton {
                                    readonly property bool sent: root.invited[friendRow.modelData.id] === true
                                    text: sent ? qsTr("Convidado") : qsTr("Convidar"); iconName: sent ? "check" : "plus"; quiet: true; reduced: root.reduced
                                    enabled: !sent
                                    onClicked: {
                                        if (steam.invite(friendRow.modelData.id)) {
                                            const next = Object.assign({}, root.invited); next[friendRow.modelData.id] = true; root.invited = next
                                            root.notify(qsTr("Convite enviado para %1.").arg(friendRow.modelData.name))
                                        } else root.notify(qsTr("Não foi possível enviar o convite."))
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: steam.available && steam.enabled
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        Layout.leftMargin: 4
                        wrapMode: Text.WordWrap
                        text: qsTr("Quem entra precisa da Steam e do OmniLauncher abertos, com um jogo da mesma versão e dos mesmos mods. Versão de teste: aparece na Steam como Spacewar.")
                        color: Theme.faint; font.family: Theme.font; font.pixelSize: 12
                    }

                    SectionTitle { text: qsTr("Conector"); Layout.topMargin: steam.available && steam.enabled ? 24 : 0 }
                    Group {
                        SettingRow {
                            title: qsTr("Endereço público sem configurar o roteador")
                            description: root.connectorSupported
                                ? qsTr("Usa o e4mc: um túnel grátis que dá ao servidor um endereço *.e4mc.link, sem port forwarding, Hamachi ou Radmin. O endereço muda a cada início.")
                                : qsTr("Disponível em servidores com Fabric, Forge ou NeoForge (o conector é um mod).")
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
                            label: qsTr("Endereço para os amigos")
                            value: root.tunnel !== "" ? root.tunnel : "—"
                            hint: root.tunnel !== "" ? qsTr("Funciona de qualquer lugar, com o Minecraft normal.") : root.online ? qsTr("Reinicie o servidor para gerar o endereço.") : qsTr("Aparece aqui quando o servidor iniciar.")
                        }
                    }

                    SectionTitle { text: qsTr("Outros endereços") }
                    Group {
                        AddressRow {
                            label: qsTr("Neste PC")
                            value: root.address("localhost")
                        }
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.line }
                        AddressRow {
                            label: qsTr("Mesma rede (Wi-Fi ou cabo da sua casa)")
                            value: root.address(servers.lanAddress)
                        }
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.line }
                        AddressRow {
                            label: qsTr("Pela internet")
                            value: servers.publicAddress !== "" ? servers.publicAddress + ":" + (root.server ? root.server.port : 25565) : "—"
                            hint: qsTr("Funciona só se a porta ") + (root.server ? root.server.port : 25565) + qsTr(" (TCP) estiver aberta no roteador (port forwarding) e a operadora não usar CGNAT.")
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        Layout.leftMargin: 4
                        wrapMode: Text.WordWrap
                        text: qsTr("Amigos com perfil offline só entram com \"Só contas originais\" desligado (aba Configurações). Todos precisam da mesma versão e dos mesmos mods: o jeito mais fácil é usar o mesmo modpack.")
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 12
                    }
                }
            }

            // Backups
            ColumnLayout {
                visible: root.tab === "backups"
                anchors.fill: parent
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Cópias do mundo em .zip. Com o servidor ligado, o mundo é salvo antes.")
                        color: Theme.muted; font.family: Theme.font; font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    ActionButton { text: qsTr("Pasta"); iconName: "folder"; quiet: true; reduced: root.reduced; onClicked: servers.openFolder(root.server.id, "backups") }
                    ActionButton { text: qsTr("Fazer backup"); iconName: "download"; primary: true; reduced: root.reduced; enabled: root.state !== "installing"; onClicked: servers.backup(root.server.id) }
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
                                Text { text: new Date(modelData.createdAt).toLocaleString(Qt.locale(), qsTr("d 'de' MMMM, HH:mm")) + "  ·  " + root.bytes(modelData.sizeBytes); color: Theme.muted; font.family: Theme.font; font.pixelSize: 12 }
                            }
                            ActionButton { iconName: "external"; quiet: true; round: true; width: 36; height: 36; reduced: root.reduced; Accessible.name: "Mostrar"; onClicked: runner.openFile(modelData.path.substring(0, modelData.path.lastIndexOf("/"))) }
                            ActionButton { iconName: "trash"; quiet: true; round: true; width: 36; height: 36; danger: true; reduced: root.reduced; Accessible.name: "Mover para a Lixeira"; onClicked: { runner.trashFile(modelData.path); root.refreshLists() } }
                        }
                    }
                    Text {
                        visible: root.backupList.length === 0
                        anchors.centerIn: parent
                        text: qsTr("Nenhum backup ainda.")
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
        title: qsTr("Novo servidor")
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
            Text { text: qsTr("Jogo"); color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.bottomMargin: 8 }
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
                text: qsTr("Os mods e configs do jogo vão junto; mods só de cliente (shaders, minimapa, HUD…) ficam de fora.")
                color: Theme.faint; font.family: Theme.font; font.pixelSize: 12
            }
            Text { text: qsTr("Nome"); color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 18; Layout.bottomMargin: 8 }
            InputField { id: serverName; Layout.fillWidth: true; maximumLength: 48; placeholderText: qsTr("Meu servidor") }
            RowLayout {
                Layout.topMargin: 18
                Layout.fillWidth: true
                Text { text: qsTr("Memória"); color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
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
                    text: qsTr("Li e aceito o <a href=\"https://aka.ms/MinecraftEULA\">EULA do Minecraft</a>, exigido pela Mojang para rodar um servidor.")
                    linkColor: Theme.accentText
                    color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13
                    onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                    HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
            }
            Text {
                visible: creator.games.length === 0
                Layout.topMargin: 12
                text: qsTr("Crie um jogo na biblioteca primeiro.")
                color: Theme.danger; font.family: Theme.font; font.pixelSize: 13
            }
            RowLayout {
                Layout.topMargin: 26
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancelar"); quiet: true; reduced: root.reduced; onClicked: creator.close() }
                ActionButton {
                    text: qsTr("Criar servidor"); primary: true; reduced: root.reduced
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
        title: qsTr("Excluir servidor?")
        width: 460
        showClose: false
        blurSource: root.blurSource
        reduced: root.reduced
        property string targetId: ""
        property var info: ({})
        readonly property var target: {
            const list = servers.servers
            for (let i = 0; i < list.length; i++) if (list[i].id === targetId) return list[i]
            return null
        }
        readonly property string targetState: root.statusOf(targetId).state || "stopped"
        readonly property bool targetBusy: targetState !== "stopped" && targetState !== "failed"
        function openFor(id) {
            targetId = id
            info = servers.summary(id)
            open()
        }
        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 64
                radius: Theme.radiusLg
                color: Theme.fill
                border.width: 1; border.color: Theme.line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 38; Layout.preferredHeight: 38
                        radius: 10; color: "#1AD26B63"
                        UiIcon { anchors.centerIn: parent; width: 18; height: 18; name: "server"; color: Theme.danger }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: deletion.target ? deletion.target.name : ""
                            color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                            Layout.fillWidth: true; elide: Text.ElideRight
                        }
                        Text {
                            text: {
                                const parts = []
                                if (deletion.info.sizeBytes !== undefined) parts.push(root.bytes(deletion.info.sizeBytes))
                                if (deletion.info.worldExists) parts.push(qsTr("mundo \"%1\"").arg(deletion.info.world))
                                if (deletion.info.mods > 0) parts.push(deletion.info.mods === 1 ? qsTr("1 mod") : qsTr("%1 mods").arg(deletion.info.mods))
                                if (deletion.info.backups > 0) parts.push(deletion.info.backups === 1 ? qsTr("1 backup") : qsTr("%1 backups").arg(deletion.info.backups))
                                return parts.join("  ·  ")
                            }
                            color: Theme.muted; font.family: Theme.font; font.pixelSize: 12
                            Layout.fillWidth: true; elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 16
                wrapMode: Text.WordWrap
                text: qsTr("A pasta inteira do servidor, com o mundo e os backups, vai para a Lixeira do Windows: dá para recuperar de lá. O jogo da biblioteca não é afetado.")
                color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13
            }
            Rectangle {
                visible: deletion.targetBusy
                Layout.fillWidth: true
                Layout.topMargin: 14
                implicitHeight: busyRow.implicitHeight + 20
                radius: Theme.radiusMd
                color: "#1FE8C77A"
                border.width: 1; border.color: "#59E8C77A"
                RowLayout {
                    id: busyRow
                    anchors.fill: parent; anchors.margins: 10; anchors.leftMargin: 14
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: deletion.targetState === "installing" ? qsTr("O servidor está instalando. Espere terminar para excluir.")
                            : qsTr("O servidor está ligado. Pare-o antes de excluir.")
                        color: "#F2DDA6"; font.family: Theme.font; font.pixelSize: 12
                    }
                    ActionButton {
                        visible: deletion.targetState === "running" || deletion.targetState === "starting"
                        text: qsTr("Parar"); iconName: "stop"; quiet: true; height: 32; reduced: root.reduced
                        onClicked: servers.stop(deletion.targetId)
                    }
                }
            }
            RowLayout {
                Layout.topMargin: 24
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    text: qsTr("Fazer backup antes"); iconName: "download"; quiet: true; reduced: root.reduced
                    visible: deletion.info.worldExists === true
                    enabled: deletion.targetState !== "installing"
                    ToolTip.visible: hovered; ToolTip.delay: 500
                    ToolTip.text: qsTr("Guarda uma cópia do mundo em Backups (vai junto para a Lixeira).")
                    onClicked: servers.backup(deletion.targetId)
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancelar"); quiet: true; reduced: root.reduced; onClicked: deletion.close() }
                ActionButton {
                    text: qsTr("Excluir"); iconName: "trash"; danger: true; reduced: root.reduced
                    enabled: deletion.target !== null && !deletion.targetBusy
                    onClicked: {
                        const name = deletion.target.name
                        if (servers.remove(deletion.targetId)) root.notify(name + qsTr(" foi para a Lixeira."))
                        else root.notify(qsTr("Não foi possível excluir. Pare o servidor e feche arquivos abertos dele."))
                        deletion.close()
                    }
                }
            }
        }
    }

    GlassDialog {
        id: contentRemoval
        parent: Overlay.overlay
        title: contentTab.kindShown === "datapack" ? qsTr("Remover datapack?") : qsTr("Remover mod?")
        width: 420
        showClose: false
        blurSource: root.blurSource
        reduced: root.reduced
        property var entry: ({})
        property string targetId: ""
        property string kind: "mod"
        function openFor(item) {
            entry = { file: item.file, title: item.title }
            targetId = contentTab.targetId
            kind = contentTab.kindShown
            open()
        }
        contentItem: ColumnLayout {
            spacing: 0
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("%1 sai do servidor (o arquivo é apagado). Para só deixar de usar, desative em vez de remover.").arg(contentRemoval.entry.title || contentRemoval.entry.file || "")
                color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14
            }
            RowLayout {
                Layout.topMargin: 24
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancelar"); quiet: true; reduced: root.reduced; onClicked: contentRemoval.close() }
                ActionButton {
                    text: qsTr("Remover"); danger: true; reduced: root.reduced
                    onClicked: {
                        if (mods.remove(contentRemoval.targetId, contentRemoval.entry.file, contentRemoval.kind)) {
                            root.notify(qsTr("%1 removido.").arg(contentRemoval.entry.title || contentRemoval.entry.file))
                            contentTab.changed()
                        } else root.notify(mods.lastError)
                        contentRemoval.close()
                    }
                }
            }
        }
    }
}
