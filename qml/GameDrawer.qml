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
    signal notify(string message)

    readonly property string gameId: game ? game.id : ""
    readonly property bool hasProfile: launcher.activeProfileId !== ""
    readonly property bool vanilla: game ? game.loader === "Vanilla" : true
    readonly property real panelWidth: Math.max(380, Math.min(460, width * 0.4))
    readonly property int heroHeight: 220
    readonly property color tint: Theme.coverTint(game ? game.coverId : "")

    visible: scrim.opacity > 0.001 || panel.x < width
    onGameIdChanged: { tabs.currentIndex = 0; flick.contentY = 0 }
    onOpenChanged: if (open) playButton.forceActiveFocus()

    function slug(text) {
        return (text || "").toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "")
            .replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") || "jogo"
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

            // Hero: the cover art, fading into the glass.
            Item {
                id: hero
                width: panel.width; height: root.heroHeight
                Image {
                    id: heroArt
                    anchors.fill: parent
                    source: Theme.coverSource(root.game ? root.game.coverId : "")
                    sourceSize: Qt.size(920, 1380)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: false
                }
                Item {
                    id: heroMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: true
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusXl
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#FFFFFFFF" }
                            GradientStop { position: 0.45; color: "#E6FFFFFF" }
                            GradientStop { position: 1; color: "#00FFFFFF" }
                        }
                    }
                }
                MultiEffect {
                    anchors.fill: parent
                    source: heroArt
                    maskEnabled: true
                    maskSource: heroMask
                    maskThresholdMin: 0
                    maskSpreadAtMin: 1
                    brightness: -0.04
                    opacity: heroArt.status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
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
                    text: "Jogar"
                    iconName: "play"
                    primary: true
                    reduced: root.reduced
                    font.pixelSize: 16
                    onClicked: root.playRequested()
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    horizontalAlignment: Text.AlignHCenter
                    text: root.hasProfile ? "Como " + launcher.activeProfile.name : "Escolha um Perfil para jogar"
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
                        onClicked: root.notify("A pasta do jogo será criada quando a instalação for real.")
                    }
                }

                SegmentTabs {
                    id: tabs
                    Layout.fillWidth: true
                    Layout.topMargin: 26
                    model: ["Visão geral", "Mods", "Ajustes"]
                    reduced: root.reduced
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
                        InfoRow { label: "Tipo de instalação"; iconName: "box"; value: root.game ? root.game.loader : "" }
                        InfoRow { label: "Mods"; iconName: "puzzle"; value: root.vanilla ? "Não se aplica" : "Nenhum" }
                        InfoRow { label: "Última sessão"; iconName: "calendar"; value: "Nunca jogado" }
                        InfoRow { label: "Tempo de jogo"; iconName: "clock"; value: "—" }
                        InfoRow { label: "Pasta"; iconName: "folder"; value: "instances/" + root.slug(root.game ? root.game.title : ""); divider: false }
                        ActionButton {
                            Layout.topMargin: 18
                            text: "Remover jogo"; iconName: "trash"
                            danger: true; quiet: true; centered: false
                            reduced: root.reduced
                            onClicked: root.removeRequested()
                        }
                    }

                    // Mods
                    ColumnLayout {
                        spacing: 14
                        RowLayout {
                            visible: !root.vanilla
                            Layout.fillWidth: true
                            Layout.topMargin: 14
                            spacing: 8
                            InputField { Layout.fillWidth: true; iconName: "search"; placeholderText: "Buscar nos mods instalados"; clearable: true }
                            ActionButton {
                                text: "Adicionar"; iconName: "plus"; reduced: root.reduced
                                onClicked: root.notify("O catálogo de mods será conectado na próxima etapa.")
                            }
                        }
                        ColumnLayout {
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
                                text: root.vanilla ? "Mude o tipo de instalação para Fabric, Forge ou NeoForge."
                                                   : "Mods do Modrinth e CurseForge aparecem aqui."
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

                    // Ajustes (local to this session until per-game settings are persisted)
                    ColumnLayout {
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            Text { text: "Memória"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: memory.value + " GB"; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold }
                        }
                        ValueSlider { id: memory; Layout.fillWidth: true; from: 1; to: 16; stepSize: 1; value: 4; snapMode: Slider.SnapAlways; reduced: root.reduced }

                        Text { text: "Java"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 12 }
                        ChoiceField { Layout.fillWidth: true; model: ["Automático", "Java 21", "Java 17", "Java 8"] }

                        Text { text: "Resolução da janela"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 12 }
                        ChoiceField { Layout.fillWidth: true; model: ["Padrão do jogo", "1280 × 720", "1600 × 900", "1920 × 1080"] }

                        Text { text: "Argumentos da JVM"; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; Layout.topMargin: 12 }
                        InputField { Layout.fillWidth: true; placeholderText: "-XX:+UseG1GC"; font.family: "Cascadia Mono" }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            Text { text: "Fechar o launcher ao iniciar"; color: Theme.text; font.family: Theme.font; font.pixelSize: 13; Layout.fillWidth: true }
                            ToggleSwitch { reduced: root.reduced }
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
