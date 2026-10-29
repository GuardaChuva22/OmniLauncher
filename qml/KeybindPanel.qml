import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Minecraft's controls as defaults for every new game: click a key, press the new
// key (or mouse button). Saved as options.txt lines (key_key.forward:key.keyboard.w)
// through runner.setGameDefault; a key left at Minecraft's own default isn't stored.
ColumnLayout {
    id: root
    property bool reduced: false
    spacing: 0

    // The binding waiting for a key, or "".
    property string listening: ""

    readonly property var categories: [
        { title: "Movimento", keys: [
            { id: "key.forward", label: "Andar para frente", def: "key.keyboard.w" },
            { id: "key.back", label: "Andar para trás", def: "key.keyboard.s" },
            { id: "key.left", label: "Andar para a esquerda", def: "key.keyboard.a" },
            { id: "key.right", label: "Andar para a direita", def: "key.keyboard.d" },
            { id: "key.jump", label: "Pular", def: "key.keyboard.space" },
            { id: "key.sneak", label: "Agachar", def: "key.keyboard.left.shift" },
            { id: "key.sprint", label: "Correr", def: "key.keyboard.left.control" } ] },
        { title: "Ações", keys: [
            { id: "key.attack", label: "Atacar / quebrar", def: "key.mouse.left" },
            { id: "key.use", label: "Usar item / colocar bloco", def: "key.mouse.right" },
            { id: "key.pickItem", label: "Escolher bloco", def: "key.mouse.middle" },
            { id: "key.drop", label: "Soltar item", def: "key.keyboard.q" },
            { id: "key.swapOffhand", label: "Trocar de mão", def: "key.keyboard.f" } ] },
        { title: "Inventário", keys: [
            { id: "key.inventory", label: "Abrir inventário", def: "key.keyboard.e" },
            { id: "key.hotbar.1", label: "Slot 1", def: "key.keyboard.1" },
            { id: "key.hotbar.2", label: "Slot 2", def: "key.keyboard.2" },
            { id: "key.hotbar.3", label: "Slot 3", def: "key.keyboard.3" },
            { id: "key.hotbar.4", label: "Slot 4", def: "key.keyboard.4" },
            { id: "key.hotbar.5", label: "Slot 5", def: "key.keyboard.5" },
            { id: "key.hotbar.6", label: "Slot 6", def: "key.keyboard.6" },
            { id: "key.hotbar.7", label: "Slot 7", def: "key.keyboard.7" },
            { id: "key.hotbar.8", label: "Slot 8", def: "key.keyboard.8" },
            { id: "key.hotbar.9", label: "Slot 9", def: "key.keyboard.9" },
            { id: "key.saveToolbarActivator", label: "Salvar barra (criativo)", def: "key.keyboard.c" },
            { id: "key.loadToolbarActivator", label: "Carregar barra (criativo)", def: "key.keyboard.x" } ] },
        { title: "Multijogador", keys: [
            { id: "key.chat", label: "Abrir o chat", def: "key.keyboard.t" },
            { id: "key.command", label: "Abrir comando", def: "key.keyboard.slash" },
            { id: "key.playerlist", label: "Lista de jogadores", def: "key.keyboard.tab" },
            { id: "key.socialInteractions", label: "Interações sociais", def: "key.keyboard.p" } ] },
        { title: "Diversos", keys: [
            { id: "key.screenshot", label: "Captura de tela", def: "key.keyboard.f2" },
            { id: "key.togglePerspective", label: "Mudar a câmera", def: "key.keyboard.f5" },
            { id: "key.fullscreen", label: "Tela cheia", def: "key.keyboard.f11" },
            { id: "key.advancements", label: "Progressos", def: "key.keyboard.l" },
            { id: "key.smoothCamera", label: "Câmera cinematográfica", def: "key.keyboard.unknown" },
            { id: "key.spectatorOutlines", label: "Destacar jogadores (espectador)", def: "key.keyboard.unknown" } ] }
    ]
    readonly property var allKeys: {
        let list = []
        for (const category of categories) list = list.concat(category.keys)
        return list
    }

    function current(entry) { return runner.gameDefaults["key_" + entry.id] || entry.def }
    function bind(entry, value) {
        runner.setGameDefault("key_" + entry.id, value === entry.def ? "" : value)
        listening = ""
        sfx.play("click")
    }
    function conflicts(entry) {
        const value = current(entry)
        if (value === "key.keyboard.unknown") return []
        return allKeys.filter(function(other) { return other.id !== entry.id && current(other) === value })
    }
    readonly property bool customized: Object.keys(runner.gameDefaults).some(function(key) { return key.indexOf("key_key.") === 0 })
    function resetAll() {
        for (const key of Object.keys(runner.gameDefaults))
            if (key.indexOf("key_key.") === 0) runner.setGameDefault(key, "")
    }

    // Windows scan codes (as Qt reports them, 0x100 = extended) → GLFW key names.
    // Physical positions, like Minecraft itself: the key under the US "Q" is "q" on any layout.
    readonly property var scanCodes: ({
        0x01: "escape", 0x0E: "backspace", 0x0F: "tab", 0x1C: "enter", 0x1D: "left.control", 0x2A: "left.shift",
        0x36: "right.shift", 0x38: "left.alt", 0x39: "space", 0x3A: "caps.lock", 0x0C: "minus", 0x0D: "equal",
        0x1A: "left.bracket", 0x1B: "right.bracket", 0x27: "semicolon", 0x28: "apostrophe", 0x29: "grave.accent",
        0x2B: "backslash", 0x33: "comma", 0x34: "period", 0x35: "slash", 0x56: "world.2",
        0x37: "keypad.multiply", 0x4A: "keypad.subtract", 0x4E: "keypad.add", 0x53: "keypad.decimal",
        0x47: "keypad.7", 0x48: "keypad.8", 0x49: "keypad.9", 0x4B: "keypad.4", 0x4C: "keypad.5", 0x4D: "keypad.6",
        0x4F: "keypad.1", 0x50: "keypad.2", 0x51: "keypad.3", 0x52: "keypad.0", 0x45: "pause", 0x46: "scroll.lock",
        0x57: "f11", 0x58: "f12", 0x64: "f13", 0x65: "f14", 0x66: "f15",
        0x11C: "keypad.enter", 0x11D: "right.control", 0x135: "keypad.divide", 0x137: "print.screen", 0x138: "right.alt",
        0x145: "num.lock", 0x147: "home", 0x148: "up", 0x149: "page.up", 0x14B: "left", 0x14D: "right", 0x14F: "end",
        0x150: "down", 0x151: "page.down", 0x152: "insert", 0x153: "delete", 0x15B: "left.win", 0x15C: "right.win", 0x15D: "menu"
    })
    function keyName(event) {
        const code = event.nativeScanCode
        const rows = { 0x02: "1", 0x03: "2", 0x04: "3", 0x05: "4", 0x06: "5", 0x07: "6", 0x08: "7", 0x09: "8", 0x0A: "9", 0x0B: "0" }
        const letters = "qwertyuiop" // 0x10..0x19
        const home = "asdfghjkl"     // 0x1E..0x26
        const bottom = "zxcvbnm"     // 0x2C..0x32
        let name = scanCodes[code] || rows[code] || ""
        if (!name && code >= 0x10 && code <= 0x19) name = letters[code - 0x10]
        if (!name && code >= 0x1E && code <= 0x26) name = home[code - 0x1E]
        if (!name && code >= 0x2C && code <= 0x32) name = bottom[code - 0x2C]
        if (!name && code >= 0x3B && code <= 0x44) name = "f" + (code - 0x3A)
        return name ? "key.keyboard." + name : ""
    }
    function mouseName(button) {
        return button === Qt.LeftButton ? "key.mouse.left" : button === Qt.RightButton ? "key.mouse.right"
             : button === Qt.MiddleButton ? "key.mouse.middle" : button === Qt.BackButton ? "key.mouse.4"
             : button === Qt.ForwardButton ? "key.mouse.5" : ""
    }
    // What the key reads as on a button.
    function label(value) {
        if (!value || value === "key.keyboard.unknown") return "Nenhuma"
        const names = {
            "key.mouse.left": "Botão esquerdo", "key.mouse.right": "Botão direito", "key.mouse.middle": "Botão do meio",
            "key.mouse.4": "Mouse 4", "key.mouse.5": "Mouse 5",
            "key.keyboard.space": "Espaço", "key.keyboard.left.shift": "Shift esq.", "key.keyboard.right.shift": "Shift dir.",
            "key.keyboard.left.control": "Ctrl esq.", "key.keyboard.right.control": "Ctrl dir.",
            "key.keyboard.left.alt": "Alt esq.", "key.keyboard.right.alt": "Alt dir.", "key.keyboard.tab": "Tab",
            "key.keyboard.enter": "Enter", "key.keyboard.backspace": "Backspace", "key.keyboard.caps.lock": "Caps Lock",
            "key.keyboard.up": "Seta ↑", "key.keyboard.down": "Seta ↓", "key.keyboard.left": "Seta ←", "key.keyboard.right": "Seta →",
            "key.keyboard.page.up": "Page Up", "key.keyboard.page.down": "Page Down", "key.keyboard.home": "Home",
            "key.keyboard.end": "End", "key.keyboard.insert": "Insert", "key.keyboard.delete": "Delete",
            "key.keyboard.grave.accent": "`", "key.keyboard.minus": "-", "key.keyboard.equal": "=",
            "key.keyboard.left.bracket": "[", "key.keyboard.right.bracket": "]", "key.keyboard.backslash": "\\",
            "key.keyboard.semicolon": ";", "key.keyboard.apostrophe": "'", "key.keyboard.comma": ",",
            "key.keyboard.period": ".", "key.keyboard.slash": "/", "key.keyboard.world.2": "\\ (ISO)",
            "key.keyboard.left.win": "Win esq.", "key.keyboard.right.win": "Win dir.", "key.keyboard.menu": "Menu",
            "key.keyboard.print.screen": "Print Screen", "key.keyboard.pause": "Pause", "key.keyboard.scroll.lock": "Scroll Lock",
            "key.keyboard.num.lock": "Num Lock", "key.keyboard.keypad.enter": "Enter (num.)"
        }
        if (names[value]) return names[value]
        let rest = value.replace("key.keyboard.", "").replace("key.mouse.", "Mouse ")
        if (rest.indexOf("keypad.") === 0) return rest.replace("keypad.", "Num ").replace("add", "+").replace("subtract", "-").replace("multiply", "*").replace("divide", "/").replace("decimal", ".")
        return rest.toUpperCase()
    }

    // Toggle or hold, Minecraft's own options.
    SectionTitleRow { text: "Jeito de jogar" }
    Group {
        SettingRow {
            title: "Agachar"
            ChoiceField {
                id: crouchMode
                width: 200
                model: ["Segurar a tecla", "Apertar para alternar"]
                Binding on currentIndex { when: !crouchMode.popup.visible; value: runner.gameDefaults["toggleCrouch"] === "true" ? 1 : 0 }
                onActivated: function(index) { runner.setGameDefault("toggleCrouch", index === 1 ? "true" : "") }
            }
        }
        SettingRow {
            title: "Correr"
            divider: false
            ChoiceField {
                id: sprintMode
                width: 200
                model: ["Segurar a tecla", "Apertar para alternar"]
                Binding on currentIndex { when: !sprintMode.popup.visible; value: runner.gameDefaults["toggleSprint"] === "true" ? 1 : 0 }
                onActivated: function(index) { runner.setGameDefault("toggleSprint", index === 1 ? "true" : "") }
            }
        }
    }

    Repeater {
        model: root.categories
        delegate: ColumnLayout {
            id: category
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: 0
            SectionTitleRow {
                text: category.modelData.title
                showReset: category.index === 0 && root.customized
            }
            Group {
                Repeater {
                    model: category.modelData.keys
                    delegate: SettingRow {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property string value: root.current(modelData)
                        readonly property var clash: root.conflicts(modelData)
                        title: modelData.label
                        description: clash.length > 0 ? "Mesma tecla de: " + clash.map(function(c) { return c.label }).join(", ") : ""
                        divider: index < category.modelData.keys.length - 1
                        implicitHeight: 56
                        RowLayout {
                            spacing: 6
                            ActionButton {
                                visible: row.value !== row.modelData.def
                                iconName: "restart"; quiet: true; round: true; width: 34; height: 34; iconSize: 15
                                reduced: root.reduced
                                sound: ""
                                Accessible.name: "Voltar ao padrão"
                                ToolTip.visible: hovered; ToolTip.text: "Voltar para " + root.label(row.modelData.def); ToolTip.delay: 400
                                onClicked: root.bind(row.modelData, row.modelData.def)
                            }
                            KeyCap {
                                entry: row.modelData
                                value: row.value
                                clashing: row.clash.length > 0
                            }
                        }
                    }
                }
            }
        }
    }

    component SectionTitleRow: RowLayout {
        property alias text: title.text
        property bool showReset: false
        Layout.fillWidth: true
        Layout.topMargin: 22
        Layout.bottomMargin: 8
        Text {
            id: title
            Layout.fillWidth: true
            Layout.leftMargin: 4
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
            font.letterSpacing: 0.6
            font.capitalization: Font.AllUppercase
        }
        ActionButton {
            visible: parent.showReset
            text: "Restaurar teclas"; quiet: true; height: 30; reduced: root.reduced
            onClicked: root.resetAll()
        }
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

    // The key on a button. Click (any mouse button), then press the new key or click
    // the new mouse button; Esc cancels, the × clears it.
    component KeyCap: Item {
        id: cap
        property var entry
        property string value
        property bool clashing: false
        readonly property bool active: root.listening === entry.id
        implicitWidth: Math.max(150, capLabel.implicitWidth + (active ? 70 : 36))
        implicitHeight: 36
        focus: active
        onActiveChanged: if (active) forceActiveFocus()

        SoftGlow {
            anchors.fill: parent
            visible: opacity > 0.01
            opacity: cap.active ? 0.55 : 0
            color: Theme.accent
            radius: Theme.radiusMd
            blurMax: 20
            spread: -2
            Behavior on opacity { enabled: !root.reduced; NumberAnimation { duration: Theme.normal } }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: cap.active ? Theme.accentSoft : capMouse.containsMouse ? Theme.fillHover : Theme.fill
            border.width: 1
            border.color: cap.active ? Theme.accentLine : cap.clashing ? "#99F0A19A" : Theme.lineStrong
            Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
            // Keycap depth: a slightly darker bottom edge.
            Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: parent.width - 2 * parent.radius; height: 1; color: "#33000000" }
        }
        Text {
            id: capLabel
            anchors.centerIn: parent
            text: cap.active ? "Aperte uma tecla…" : root.label(cap.value)
            color: cap.active ? Theme.accentText : cap.clashing ? Theme.danger : cap.value === "key.keyboard.unknown" ? Theme.faint : Theme.text
            font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
            SequentialAnimation on opacity {
                running: cap.active && !root.reduced; loops: Animation.Infinite
                NumberAnimation { to: 0.45; duration: 650; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 650; easing.type: Easing.InOutSine }
                onStopped: capLabel.opacity = 1
            }
        }
        MouseArea {
            id: capMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.AllButtons
            onPressed: function(mouse) {
                if (cap.active) {
                    const name = root.mouseName(mouse.button)
                    if (name) root.bind(cap.entry, name)
                } else if (mouse.button === Qt.LeftButton) {
                    root.listening = cap.entry.id
                    sfx.play("click")
                }
            }
        }
        // Clear (unbind), only while listening. Takes no focus, so listening survives the press.
        ActionButton {
            visible: cap.active
            anchors.right: parent.right; anchors.rightMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            focusPolicy: Qt.NoFocus
            iconName: "close"; quiet: true; round: true; width: 30; height: 30; iconSize: 13
            reduced: root.reduced
            Accessible.name: "Deixar sem tecla"
            ToolTip.visible: hovered; ToolTip.text: "Deixar sem tecla"; ToolTip.delay: 300
            onClicked: root.bind(cap.entry, "key.keyboard.unknown")
        }
        // While listening, every key belongs to the binding, shortcuts included.
        Keys.onShortcutOverride: function(event) { event.accepted = cap.active }
        Keys.onPressed: function(event) {
            if (!cap.active) return
            event.accepted = true
            if (event.key === Qt.Key_Escape) { root.listening = ""; return }
            const name = root.keyName(event)
            if (name) root.bind(cap.entry, name)
        }
        onActiveFocusChanged: if (!activeFocus && cap.active) root.listening = ""
    }
}
