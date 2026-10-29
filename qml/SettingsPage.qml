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
    readonly property var afterLaunchOptions: [{ label: "Minimizar", value: "minimize" }, { label: "Fechar", value: "close" },
                                               { label: "Manter aberto", value: "keep" }]
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
        text: "Configurações"
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

            SectionTitle { text: "Aparência" }
            Group {
                SettingRow {
                    title: "Reduzir movimento"
                    description: "Trocas diretas, sem animações nas capas e no fundo."
                    ToggleSwitch {
                        checked: launcher.reducedMotion
                        reduced: root.reduced
                        onToggled: launcher.setReducedMotion(checked)
                        Accessible.name: "Reduzir movimento"
                    }
                }
                SettingRow {
                    title: "Fundo da capa"
                    description: "O fundo acompanha a capa sob o cursor."
                    divider: false
                    ToggleSwitch { checked: true; reduced: root.reduced; enabled: false }
                }
            }

            SectionTitle { text: "Perfis" }
            Group {
                SettingRow {
                    title: launcher.activeProfileId ? launcher.activeProfile.name : "Nenhum perfil ativo"
                    description: launcher.profiles.length + (launcher.profiles.length === 1 ? " perfil salvo" : " perfis salvos")
                    divider: false
                    ActionButton { text: "Gerenciar"; iconName: "user"; reduced: root.reduced; onClicked: root.manageProfiles() }
                }
            }

            SectionTitle { text: "Java e memória" }
            Group {
                SettingRow {
                    title: "Memória padrão"
                    description: "Cada jogo pode substituir este valor. Este PC tem " + Math.round(runner.systemMemoryMb / 1024) + " GB."
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
                    description: "Baixado automaticamente na versão que cada jogo pede."
                    divider: false
                    Text { text: "Automático"; color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Medium }
                }
            }

            SectionTitle { text: "Ao iniciar um jogo" }
            Group {
                SettingRow {
                    title: "Comportamento do launcher"
                    description: "Ao fechar o jogo, o launcher volta para a frente."
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

            SectionTitle { text: "Padrões do Minecraft" }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 4; Layout.bottomMargin: 12
                text: "Aplicados a cada jogo novo antes da primeira abertura. Depois, o que você mudar dentro do jogo fica só nele."
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Group {
                SettingRow {
                    title: "Sensibilidade do mouse"
                    OptionSlider {
                        reduced: root.reduced
                        key: "mouseSensitivity"; from: 0; to: 200; step: 1; suffix: "%"
                        shown: Number(root.option(key, "0.5")) * 200
                        toStored: function(v) { return (v / 200).toFixed(3) }
                    }
                }
                SettingRow {
                    title: "Campo de visão (FOV)"
                    OptionSlider {
                        reduced: root.reduced
                        key: "fov"; from: 30; to: 110; step: 1
                        shown: 70 + Number(root.option(key, "0")) * 40
                        toStored: function(v) { return ((v - 70) / 40).toFixed(4) }
                        label: function(v) { return Math.round(v) === 70 ? "Normal" : Math.round(v) === 110 ? "Quake Pro" : String(Math.round(v)) }
                    }
                }
                SettingRow {
                    title: "Volume geral"
                    OptionSlider {
                        reduced: root.reduced
                        key: "soundCategory_master"; suffix: "%"
                        shown: Number(root.option(key, "1")) * 100
                        toStored: function(v) { return (v / 100).toFixed(2) }
                    }
                }
                SettingRow {
                    title: "Música"
                    OptionSlider {
                        reduced: root.reduced
                        key: "soundCategory_music"; suffix: "%"
                        shown: Number(root.option(key, "1")) * 100
                        toStored: function(v) { return (v / 100).toFixed(2) }
                    }
                }
                SettingRow {
                    title: "Distância de renderização"
                    OptionSlider {
                        reduced: root.reduced
                        key: "renderDistance"; from: 2; to: 32; suffix: " chunks"
                        shown: Number(root.option(key, "12"))
                    }
                }
                SettingRow {
                    title: "Distância de simulação"
                    OptionSlider {
                        reduced: root.reduced
                        key: "simulationDistance"; from: 5; to: 32; suffix: " chunks"
                        shown: Number(root.option(key, "12"))
                    }
                }
                SettingRow {
                    title: "FPS máximo"
                    OptionSlider {
                        reduced: root.reduced
                        key: "maxFps"; from: 30; to: 260; step: 10
                        shown: Number(root.option(key, "120"))
                        label: function(v) { return v >= 260 ? "Ilimitado" : String(Math.round(v)) }
                    }
                }
                SettingRow {
                    title: "Brilho"
                    OptionSlider {
                        reduced: root.reduced
                        key: "gamma"; suffix: "%"
                        shown: Number(root.option(key, "0.5")) * 100
                        toStored: function(v) { return (v / 100).toFixed(2) }
                        label: function(v) { return v <= 0 ? "Sombrio" : v >= 100 ? "Claro" : Math.round(v) + "%" }
                    }
                }
                SettingRow {
                    title: "Escala da interface"
                    ChoiceField {
                        id: guiScale
                        width: 200
                        model: ["Automática", "1×", "2×", "3×", "4×"]
                        Binding on currentIndex { when: !guiScale.popup.visible; value: Math.min(4, Number(root.option("guiScale", "0"))) }
                        onActivated: function(index) { root.setOption("guiScale", index) }
                    }
                }
                SettingRow {
                    title: "Idioma do jogo"
                    ChoiceField {
                        id: language
                        readonly property var codes: ["", "pt_br", "en_us", "es_es"]
                        width: 200
                        model: ["Padrão do jogo", "Português (Brasil)", "English (US)", "Español"]
                        Binding on currentIndex { when: !language.popup.visible; value: Math.max(0, language.codes.indexOf(root.option("lang", ""))) }
                        onActivated: function(index) { runner.setGameDefault("lang", language.codes[index]) }
                    }
                }
                SettingRow { title: "VSync"; OptionToggle { reduced: root.reduced; key: "enableVsync"; fallback: true } }
                SettingRow { title: "Legendas"; OptionToggle { reduced: root.reduced; key: "showSubtitles" } }
                SettingRow { title: "Pulo automático"; OptionToggle { reduced: root.reduced; key: "autoJump" } }
                SettingRow {
                    title: "Pular telas de primeira vez"
                    description: "Acessibilidade inicial, tutorial e aviso do multiplayer."
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
                    title: "Teclas e tudo mais de um jogo"
                    description: runner.defaultKeybindCount > 0
                        ? runner.defaultKeybindCount + " teclas salvas. Configure dentro de um jogo e copie de novo para atualizar."
                        : "Configure as teclas dentro de um jogo uma vez e copie daqui: teclas (inclusive de mods), sons e os valores acima."
                    RowLayout {
                        spacing: 8
                        ChoiceField {
                            id: sourceGame
                            width: 200
                            model: launcher.games.map(function(game) { return game.title })
                        }
                        ActionButton {
                            text: "Copiar"; reduced: root.reduced
                            enabled: sourceGame.currentIndex >= 0 && launcher.games.length > 0
                            onClicked: {
                                const count = runner.importGameDefaults(launcher.games[sourceGame.currentIndex].id)
                                root.notify(count < 0 ? "Esse jogo ainda não foi aberto, então não tem configurações para copiar."
                                                      : count + " configurações copiadas como padrão.")
                            }
                        }
                    }
                }
                SettingRow {
                    title: "Restaurar"
                    description: "Volta tudo aos padrões do próprio Minecraft."
                    divider: false
                    ActionButton {
                        text: "Restaurar"; quiet: true; reduced: root.reduced
                        enabled: Object.keys(runner.gameDefaults).length > 0
                        onClicked: runner.resetGameDefaults()
                    }
                }
            }

            SectionTitle { text: "Discord" }
            Group {
                SettingRow {
                    title: "Mostrar o que estou jogando"
                    description: discord.configured
                        ? (discord.connected ? "Conectado ao Discord." : "O Discord precisa estar aberto.") + " Mostra o jogo e o tempo de sessão no seu perfil."
                        : "Indisponível neste build (falta o ID do app do Discord)."
                    divider: false
                    ToggleSwitch {
                        enabled: discord.configured
                        checked: root.discordEnabled
                        reduced: root.reduced
                        onToggled: root.discordToggled(checked)
                    }
                }
            }

            SectionTitle { text: "Atualizações" }
            Group {
                SettingRow {
                    title: "OmniLauncher " + Qt.application.version + (updates.currentCommit ? "  ·  " + updates.currentCommit : "")
                    description: !updates.enabled ? "Versão de desenvolvimento: atualizações automáticas desligadas."
                        : updates.state === "checking" ? "Verificando…"
                        : updates.state === "available" ? "Nova versão disponível" + (updates.changes ? ": " + updates.changes : ".")
                        : updates.state === "downloading" ? "Baixando " + (updates.progress >= 0 ? Math.round(updates.progress * 100) + "%" : "…")
                        : updates.state === "installing" ? "Instalando e reiniciando…"
                        : updates.state === "error" ? updates.error
                        : updates.state === "current" ? "Você está na versão mais recente."
                        : "As atualizações são verificadas ao abrir."
                    divider: false
                    ActionButton {
                        visible: updates.enabled
                        text: updates.state === "available" || updates.state === "error" && updates.changes !== "" ? "Atualizar e reiniciar" : "Verificar"
                        primary: updates.state === "available"
                        iconName: updates.state === "available" ? "download" : ""
                        enabled: updates.state !== "checking" && updates.state !== "downloading" && updates.state !== "installing"
                        reduced: root.reduced
                        onClicked: updates.state === "available" || updates.state === "error" && updates.changes !== "" ? updates.install() : updates.check()
                    }
                }
            }

            SectionTitle { text: "Armazenamento" }
            Group {
                SettingRow {
                    title: "Pasta de dados"
                    description: "Jogos, versões baixadas, Java, capas importadas e cache."
                    divider: false
                    ActionButton { text: "Abrir"; iconName: "folder"; reduced: root.reduced; onClicked: runner.openDataFolder() }
                }
            }

            Text {
                Layout.topMargin: 32
                Layout.leftMargin: 4
                text: "OmniLauncher " + Qt.application.version + " · protótipo"
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 12
            }
        }
    }
}
