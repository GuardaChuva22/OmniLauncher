import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool reduced: false
    signal manageProfiles()
    signal notify(string message)
    readonly property real gutter: width < 900 ? 32 : 48
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
                    description: "Cada jogo pode substituir este valor."
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
