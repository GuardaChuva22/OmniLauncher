import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool reduced: false
    signal manageProfiles()
    property bool discordEnabled: true
    signal discordToggled(bool enabled)
    signal notify(string message)
    readonly property real gutter: width < 900 ? 32 : 48
    // options.txt helpers: the stored value, or the game's own default when unset.
    function option(key, fallback) {
        const value = runner.gameDefaults[key]
        return value === undefined ? fallback : value
    }
    function setOption(key, value) { runner.setGameDefault(key, String(value)) }
    readonly property var afterLaunchOptions: [{ label: qsTr("Ir para a bandeja"), value: "minimize" }, { label: qsTr("Fechar"), value: "close" },
                                               { label: qsTr("Manter aberto"), value: "keep" }]
    function setPreference(key, value) {
        if (!launcher.setPreference(key, value)) root.notify(launcher.lastError)
    }

    component SectionTitle: Text {
        color: Theme.muted
        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
        font.letterSpacing: 0.6
        font.capitalization: Font.AllUppercase
        Layout.topMargin: 28
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

    component OptionSlider: RowLayout {
        id: optionSlider
        property string key
        property real from: 0
        property real to: 100
        property real step: 1
        property real shown: 0          // value in display units
        property string suffix: ""
        property var toStored: function(v) { return v }
        property var label: function(v) { return Math.round(v) + optionSlider.suffix }
        property bool reduced: false
        spacing: 14
        ValueSlider {
            id: control
            Layout.preferredWidth: 200
            from: optionSlider.from; to: optionSlider.to; stepSize: optionSlider.step
            snapMode: Slider.SnapAlways
            reduced: optionSlider.reduced
            Binding on value { when: !control.pressed; value: optionSlider.shown }
            onPressedChanged: if (!pressed) runner.setGameDefault(optionSlider.key, String(optionSlider.toStored(value)))
        }
        Text {
            Layout.preferredWidth: 70
            horizontalAlignment: Text.AlignRight
            text: optionSlider.label(control.value)
            color: Theme.text
            font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
        }
    }
    component OptionToggle: ToggleSwitch {
        property string key
        property bool fallback: false
        checked: (runner.gameDefaults[key] === undefined ? (fallback ? "true" : "false") : runner.gameDefaults[key]) === "true"
        onToggled: runner.setGameDefault(key, checked ? "true" : "false")
    }


    Text {
        id: title
        x: root.gutter; y: 50
        text: qsTr("Configurações")
        color: Theme.text
        font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
        font.letterSpacing: -0.6
    }

    Flickable {
        id: flick
        x: root.gutter; y: title.y + title.height + 10
        width: root.width - root.gutter * 2
        height: root.height - y
        contentHeight: content.implicitHeight + 48
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ThinScrollBar { parent: flick.parent; x: flick.x + flick.width + 8; y: flick.y; height: flick.height }

        ColumnLayout {
            id: content
            width: flick.width
            spacing: 0

            SectionTitle { text: qsTr("Aparência") }
            Group {
                SettingRow {
                    title: qsTr("Idioma")
                    description: i18n.language === "auto" ? qsTr("Segue o idioma do Windows.") : ""
                    ChoiceField {
                        id: languageChoice
                        readonly property var codes: ["auto", "pt", "en"]
                        width: 220
                        model: [qsTr("Automático (Windows)"), "Português (Brasil)", "English"]
                        Binding on currentIndex { when: !languageChoice.popup.visible; value: Math.max(0, languageChoice.codes.indexOf(i18n.language)) }
                        onActivated: function(index) { i18n.setLanguage(languageChoice.codes[index]) }
                    }
                }
                SettingRow {
                    title: qsTr("Reduzir movimento")
                    description: qsTr("Trocas diretas, sem animações nas capas e no fundo.")
                    ToggleSwitch {
                        checked: launcher.reducedMotion
                        reduced: root.reduced
                        onToggled: launcher.setReducedMotion(checked)
                        Accessible.name: "Reduzir movimento"
                    }
                }
                SettingRow {
                    title: qsTr("Fundo da capa")
                    description: qsTr("O fundo acompanha a capa sob o cursor.")
                    divider: false
                    ToggleSwitch { checked: true; reduced: root.reduced; enabled: false }
                }
            }

            SectionTitle { text: qsTr("Sons") }
            Group {
                SettingRow {
                    title: qsTr("Sons da interface")
                    description: qsTr("Cliques, switches, janelas e avisos, bem baixinho.")
                    ToggleSwitch {
                        checked: sfx.enabled
                        reduced: root.reduced
                        onToggled: { sfx.enabled = checked; if (checked) sfx.play("toggle-on") }
                        Accessible.name: "Sons da interface"
                    }
                }
                SettingRow {
                    title: qsTr("Volume")
                    RowLayout {
                        spacing: 12
                        ValueSlider {
                            id: soundVolume
                            Layout.preferredWidth: 180
                            from: 0; to: 100; stepSize: 5
                            snapMode: Slider.SnapAlways
                            enabled: sfx.enabled
                            reduced: root.reduced
                            Binding on value { when: !soundVolume.pressed; value: Math.round(sfx.volume * 100) }
                            onPressedChanged: if (!pressed) { sfx.volume = value / 100; sfx.play("click") }
                        }
                        Text {
                            Layout.preferredWidth: 48
                            text: Math.round(soundVolume.value) + "%"
                            color: Theme.secondary
                            font.family: Theme.font; font.pixelSize: 13
                        }
                    }
                }
                SettingRow {
                    title: qsTr("Som ao passar o mouse")
                    description: qsTr("Um toque leve sobre capas, menus e listas.")
                    divider: false
                    ToggleSwitch {
                        checked: sfx.hoverEnabled
                        enabled: sfx.enabled
                        reduced: root.reduced
                        onToggled: sfx.hoverEnabled = checked
                        Accessible.name: "Som ao passar o mouse"
                    }
                }
            }

            SectionTitle { text: qsTr("Perfis") }
            Group {
                SettingRow {
                    title: launcher.activeProfileId ? launcher.activeProfile.name : qsTr("Nenhum perfil ativo")
                    description: launcher.profiles.length + (launcher.profiles.length === 1 ? qsTr(" perfil salvo") : qsTr(" perfis salvos"))
                    divider: false
                    ActionButton { text: qsTr("Gerenciar"); iconName: "user"; reduced: root.reduced; onClicked: root.manageProfiles() }
                }
            }

            SectionTitle { text: qsTr("Java e memória") }
            Group {
                SettingRow {
                    title: qsTr("Memória padrão")
                    description: qsTr("Cada jogo pode substituir este valor. Este PC tem ") + Math.round(runner.systemMemoryMb / 1024) + " GB."
                    RowLayout {
                        spacing: 14
                        ValueSlider {
                            id: memory
                            Layout.preferredWidth: 180
                            from: 1; to: 16; stepSize: 1
                            snapMode: Slider.SnapAlways
                            reduced: root.reduced
                            Binding on value { when: !memory.pressed; value: Math.round(launcher.preferences.defaultMemoryMb / 1024) }
                            onPressedChanged: if (!pressed) root.setPreference("defaultMemoryMb", value * 1024)
                            Keys.onReleased: root.setPreference("defaultMemoryMb", value * 1024)
                        }
                        Text { text: memory.value + " GB"; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.preferredWidth: 44; horizontalAlignment: Text.AlignRight }
                    }
                }
                SettingRow {
                    title: "Java"
                    description: qsTr("Baixado automaticamente na versão que cada jogo pede.")
                    divider: false
                    Text { text: qsTr("Automático"); color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Medium }
                }
            }

            SectionTitle { text: qsTr("Ao iniciar um jogo") }
            Group {
                SettingRow {
                    title: qsTr("Comportamento do launcher")
                    description: qsTr("Ao fechar o jogo, o launcher volta para a frente.")
                    divider: false
                    ChoiceField {
                        id: afterLaunch
                        width: 200
                        model: root.afterLaunchOptions.map(function(option) { return option.label })
                        Binding on currentIndex {
                            when: !afterLaunch.popup.visible
                            value: Math.max(0, root.afterLaunchOptions.findIndex(function(option) { return option.value === launcher.preferences.afterLaunch }))
                        }
                        onActivated: function(index) { root.setPreference("afterLaunch", root.afterLaunchOptions[index].value) }
                    }
                }
            }

            SectionTitle { text: qsTr("Padrões do Minecraft") }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4; Layout.bottomMargin: 12
                text: qsTr("Aplicados a cada jogo novo antes da primeira abertura. Depois, o que você mudar dentro do jogo fica só nele.")
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Group {
                SettingRow {
                    title: qsTr("Sensibilidade do mouse")
                    OptionSlider {
                        reduced: root.reduced
                        key: "mouseSensitivity"; from: 0; to: 200; step: 1; suffix: "%"
                        shown: Number(root.option(key, "0.5")) * 200
                        toStored: function(v) { return (v / 200).toFixed(3) }
                    }
                }
                SettingRow {
                    title: qsTr("Campo de visão (FOV)")
                    OptionSlider {
                        reduced: root.reduced
                        key: "fov"; from: 30; to: 110; step: 1
                        shown: 70 + Number(root.option(key, "0")) * 40
                        toStored: function(v) { return ((v - 70) / 40).toFixed(4) }
                        label: function(v) { return Math.round(v) === 70 ? qsTr("Normal") : Math.round(v) === 110 ? qsTr("Quake Pro") : String(Math.round(v)) }
                    }
                }
                SettingRow {
                    title: qsTr("Volume geral")
                    OptionSlider {
                        reduced: root.reduced
                        key: "soundCategory_master"; suffix: "%"
                        shown: Number(root.option(key, "1")) * 100
                        toStored: function(v) { return (v / 100).toFixed(2) }
                    }
                }
                SettingRow {
                    title: qsTr("Música")
                    OptionSlider {
                        reduced: root.reduced
                        key: "soundCategory_music"; suffix: "%"
                        shown: Number(root.option(key, "1")) * 100
                        toStored: function(v) { return (v / 100).toFixed(2) }
                    }
                }
                SettingRow {
                    title: qsTr("Distância de renderização")
                    OptionSlider {
                        reduced: root.reduced
                        key: "renderDistance"; from: 2; to: 32; suffix: " chunks"
                        shown: Number(root.option(key, "12"))
                    }
                }
                SettingRow {
                    title: qsTr("Distância de simulação")
                    OptionSlider {
                        reduced: root.reduced
                        key: "simulationDistance"; from: 5; to: 32; suffix: " chunks"
                        shown: Number(root.option(key, "12"))
                    }
                }
                SettingRow {
                    title: qsTr("FPS máximo")
                    OptionSlider {
                        reduced: root.reduced
                        key: "maxFps"; from: 30; to: 260; step: 10
                        shown: Number(root.option(key, "120"))
                        label: function(v) { return v >= 260 ? qsTr("Ilimitado") : String(Math.round(v)) }
                    }
                }
                SettingRow {
                    title: qsTr("Brilho")
                    OptionSlider {
                        reduced: root.reduced
                        key: "gamma"; suffix: "%"
                        shown: Number(root.option(key, "0.5")) * 100
                        toStored: function(v) { return (v / 100).toFixed(2) }
                        label: function(v) { return v <= 0 ? qsTr("Sombrio") : v >= 100 ? qsTr("Claro") : Math.round(v) + "%" }
                    }
                }
                SettingRow {
                    title: qsTr("Escala da interface")
                    ChoiceField {
                        id: guiScale
                        width: 200
                        model: [qsTr("Automática"), qsTr("1×"), qsTr("2×"), qsTr("3×"), qsTr("4×")]
                        Binding on currentIndex { when: !guiScale.popup.visible; value: Math.min(4, Number(root.option("guiScale", "0"))) }
                        onActivated: function(index) { root.setOption("guiScale", index) }
                    }
                }
                SettingRow {
                    title: qsTr("Idioma do jogo")
                    ChoiceField {
                        id: language
                        readonly property var codes: ["", "pt_br", "en_us", "es_es"]
                        width: 200
                        model: [qsTr("Padrão do jogo"), qsTr("Português (Brasil)"), qsTr("English (US)"), qsTr("Español")]
                        Binding on currentIndex { when: !language.popup.visible; value: Math.max(0, language.codes.indexOf(root.option("lang", ""))) }
                        onActivated: function(index) { runner.setGameDefault("lang", language.codes[index]) }
                    }
                }
                SettingRow { title: "VSync"; OptionToggle { reduced: root.reduced; key: "enableVsync"; fallback: true } }
                SettingRow { title: qsTr("Legendas"); OptionToggle { reduced: root.reduced; key: "showSubtitles" } }
                SettingRow { title: qsTr("Pulo automático"); OptionToggle { reduced: root.reduced; key: "autoJump" } }
                SettingRow {
                    title: qsTr("Pular telas de primeira vez")
                    description: qsTr("Acessibilidade inicial, tutorial e aviso do multiplayer.")
                    ToggleSwitch {
                        checked: root.option("onboardAccessibility", "true") === "false"
                        reduced: root.reduced
                        onToggled: {
                            runner.setGameDefault("onboardAccessibility", checked ? "false" : "")
                            runner.setGameDefault("tutorialStep", checked ? "none" : "")
                            runner.setGameDefault("skipMultiplayerWarning", checked ? "true" : "")
                            runner.setGameDefault("joinedFirstServer", checked ? "true" : "")
                        }
                    }
                }
                SettingRow {
                    title: qsTr("Restaurar")
                    description: qsTr("Volta tudo aos padrões do próprio Minecraft.")
                    divider: false
                    ActionButton {
                        text: qsTr("Restaurar"); quiet: true; reduced: root.reduced
                        enabled: Object.keys(runner.gameDefaults).length > 0
                        onClicked: runner.resetGameDefaults()
                    }
                }
            }

            SectionTitle { text: qsTr("Controles") }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                wrapMode: Text.WordWrap
                text: qsTr("Clique numa tecla e aperte a nova (vale botão do mouse também). Entram em todo jogo novo; num jogo que já existe, use Aplicar padrões nos ajustes dele.")
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 12
            }
            KeybindPanel { Layout.fillWidth: true; reduced: root.reduced }

            SectionTitle { text: "Discord" }
            Group {
                SettingRow {
                    title: qsTr("Mostrar o que estou jogando")
                    description: discord.configured
                        ? (discord.connected ? qsTr("Conectado ao Discord.") : qsTr("O Discord precisa estar aberto.")) + qsTr(" Mostra o jogo e o tempo de sessão no seu perfil.")
                        : qsTr("Indisponível neste build (falta o ID do app do Discord).")
                    divider: false
                    ToggleSwitch {
                        enabled: discord.configured
                        checked: root.discordEnabled
                        reduced: root.reduced
                        onToggled: root.discordToggled(checked)
                    }
                }
            }

            SectionTitle { text: qsTr("Atualizações") }
            Group {
                SettingRow {
                    title: "OmniLauncher " + Qt.application.version + (updates.currentCommit ? "  ·  " + updates.currentCommit : "")
                    description: !updates.enabled ? qsTr("Versão de desenvolvimento: atualizações automáticas desligadas.")
                        : updates.state === "checking" ? qsTr("Verificando…")
                        : updates.state === "available" ? qsTr("Nova versão disponível") + (updates.changes ? ": " + updates.changes : ".")
                        : updates.state === "downloading" ? "Baixando " + (updates.progress >= 0 ? Math.round(updates.progress * 100) + "%" : "…")
                        : updates.state === "installing" ? qsTr("Instalando e reiniciando…")
                        : updates.state === "error" ? updates.error
                        : updates.state === "current" ? qsTr("Você está na versão mais recente.")
                        : qsTr("As atualizações são verificadas ao abrir.")
                    divider: false
                    ActionButton {
                        visible: updates.enabled
                        text: updates.state === "available" || updates.state === "error" && updates.changes !== "" ? qsTr("Atualizar e reiniciar") : qsTr("Verificar")
                        primary: updates.state === "available"
                        iconName: updates.state === "available" ? "download" : ""
                        enabled: updates.state !== "checking" && updates.state !== "downloading" && updates.state !== "installing"
                        reduced: root.reduced
                        onClicked: updates.state === "available" || updates.state === "error" && updates.changes !== "" ? updates.install() : updates.check()
                    }
                }
            }

            SectionTitle { text: qsTr("Armazenamento") }
            Group {
                SettingRow {
                    title: qsTr("Pasta de dados")
                    description: qsTr("Jogos, versões baixadas, Java, capas importadas e cache.")
                    divider: false
                    ActionButton { text: qsTr("Abrir"); iconName: "folder"; reduced: root.reduced; onClicked: runner.openDataFolder() }
                }
            }

            Text {
                Layout.topMargin: 32
                Layout.leftMargin: 4
                text: "OmniLauncher " + Qt.application.version + qsTr(" · protótipo")
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 12
            }
        }
    }
}
