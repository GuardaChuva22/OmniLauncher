pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GlassDialog {
    id: root
    property var game: null
    property string kind: "mod"
    // A dedicated server ("server:<id>"): client-only mods stay out of the results.
    readonly property bool forServer: game !== null && String(game.id).indexOf("server:") === 0
    property string loader: ""
    property string category: ""
    property var selected: null
    property bool initializing: false
    readonly property var project: mods.projectDetails
    readonly property bool installed: selected !== null && mods.installedProjects.indexOf(selected.id) !== -1
    readonly property bool installing: selected !== null && mods.installing.indexOf(selected.id) !== -1
    readonly property bool canInstall: selected !== null && project.id === selected.id
        && project.compatibilityKnown === true && project.compatible === true && !mods.projectLoading
    readonly property bool wide: width >= 1080
    readonly property var sortValues: ["auto", "downloads", "updated", "newest", "follows"]

    title: kind === "shader" ? qsTr("Adicionar shaders") : kind === "resourcepack" ? qsTr("Adicionar resource packs")
         : kind === "datapack" ? qsTr("Adicionar datapacks") : qsTr("Adicionar mods")
    width: Math.min(1360, parent ? parent.width - 40 : 1360)
    height: Math.min(860, parent ? parent.height - 48 : 860)
    padding: 24
    topPadding: 0

    function openFor(target, contentKind, contentLoader) {
        if (!target) return
        initializing = true
        searchDelay.stop()
        game = target
        kind = contentKind || "mod"
        loader = contentLoader || target.loader
        query.text = ""
        category = ""
        selected = null
        sameVersion.checked = true
        sameLoader.checked = true
        sort.currentIndex = 0
        mods.clearProjectDetails()
        mods.loadInstalled(target.id, kind)
        mods.fetchCategories(kind)
        initializing = false
        runSearch(0)
        open()
        query.forceActiveFocus()
    }
    function scheduleSearch() {
        if (!initializing && visible && game) searchDelay.restart()
    }
    function runSearch(offset) {
        if (!game) return
        searchDelay.stop()
        mods.search(query.text, sameVersion.checked ? game.version : "",
                    sameLoader.checked ? loader : "", offset, kind, category, sortValues[sort.currentIndex],
                    forServer ? "server" : "")
        if (offset === 0) results.positionViewAtBeginning()
    }
    function selectProject(hit) {
        selected = hit
        detailsScroll.contentItem.contentY = 0
        mods.fetchProjectDetails(hit.id, game.version, loader, kind)
    }
    function resetFilters() {
        initializing = true
        category = ""
        sameVersion.checked = true
        sameLoader.checked = true
        sort.currentIndex = 0
        initializing = false
        runSearch(0)
    }
    function formatCount(value) {
        if (value >= 1e6) return Number(value / 1e6).toLocaleString(Qt.locale(), "f", 1) + qsTr(" mi")
        if (value >= 1e3) return Math.round(value / 1e3) + qsTr(" mil")
        return Number(value || 0).toLocaleString(Qt.locale(), "f", 0)
    }
    function categoryName(value) {
        const names = {
            "adventure": qsTr("Aventura"), "cursed": qsTr("Inusitados"), "decoration": qsTr("Decoração"),
            "economy": qsTr("Economia"), "equipment": qsTr("Equipamentos"), "food": qsTr("Comida"),
            "game-mechanics": qsTr("Mecânicas"), "library": qsTr("Bibliotecas"), "magic": qsTr("Magia"),
            "management": qsTr("Gerenciamento"), "minigame": qsTr("Minijogos"), "mobs": qsTr("Criaturas"),
            "optimization": qsTr("Otimização"), "social": qsTr("Social"), "storage": qsTr("Armazenamento"),
            "technology": qsTr("Tecnologia"), "transportation": qsTr("Transporte"), "utility": qsTr("Utilidades"),
            "worldgen": qsTr("Geração de mundo"), "realistic": qsTr("Realistas"), "simplistic": qsTr("Simples"),
            "themed": qsTr("Temáticos"), "vanilla-like": qsTr("Estilo vanilla"), "fantasy": qsTr("Fantasia"),
            "semi-realistic": qsTr("Semirrealistas"), "cartoon": qsTr("Cartoon"), "gui": qsTr("Interface"),
            "audio": qsTr("Áudio"), "models": qsTr("Modelos"), "combat": qsTr("Combate"),
            "core-shaders": qsTr("Core shaders"), "path-tracing": qsTr("Path tracing"), "pbr": "PBR",
            "potato": qsTr("Leves"), "low": qsTr("Baixo"), "medium": qsTr("Médio"), "high": qsTr("Alto"),
            "screenshot": qsTr("Capturas de tela")
        }
        return names[value] || value.replace(/-/g, " ").replace(/^./, function(c) { return c.toUpperCase() })
    }
    function openLink(url) {
        if (/^https?:\/\//i.test(String(url))) Qt.openUrlExternally(url)
    }
    function readableBody(body) {
        // Project readmes often contain full-width banners and badge walls. Keep
        // their text and links readable inside this narrow information panel.
        return String(body || "")
            .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
            .replace(/<img\b[^>]*>/gi, "")
            .replace(/<\/?(?:table|thead|tbody|tr)\b[^>]*>/gi, "\n")
            .replace(/<\/?(?:td|th)\b[^>]*>/gi, " ")
    }
    onClosed: searchDelay.stop()
    Timer { id: searchDelay; interval: 350; onTriggered: root.runSearch(0) }

    component Caption: Text {
        color: Theme.muted
        font.family: Theme.font
        font.pixelSize: 12
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    component SectionLabel: Text {
        color: Theme.secondary
        font.family: Theme.font
        font.pixelSize: 12
        font.weight: Font.DemiBold
        textFormat: Text.PlainText
    }
    component CategoryButton: AbstractButton {
        id: categoryButton
        required property string categoryId
        required property string label
        readonly property bool chosen: root.category === categoryId
        implicitHeight: 34
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: label
        Accessible.role: Accessible.RadioButton
        Accessible.checked: chosen
        onClicked: { root.category = categoryId; root.scheduleSearch() }
        background: Rectangle {
            radius: Theme.radiusSm
            color: categoryButton.chosen ? Theme.accentSoft : categoryButton.hovered ? Theme.fillHover : "transparent"
            border.width: categoryButton.visualFocus ? 1 : 0
            border.color: Theme.focusRing
            Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
        }
        contentItem: Text {
            leftPadding: 10; rightPadding: 10
            text: categoryButton.label
            textFormat: Text.PlainText
            color: categoryButton.chosen ? Theme.accentText : Theme.muted
            font.family: Theme.font; font.pixelSize: 12
            font.weight: categoryButton.chosen ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    contentItem: ColumnLayout {
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Caption {
                Layout.fillWidth: true
                text: root.game ? root.game.title + "  ·  Minecraft " + root.game.version + "  ·  "
                    + (root.kind === "mod" ? root.loader : root.kind === "shader" ? "Iris / OptiFine"
                       : root.kind === "datapack" ? qsTr("Datapacks") : qsTr("Resource packs")) : ""
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            Caption { text: qsTr("Catálogo Modrinth"); color: Theme.faint }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18
            ColumnLayout {
                Layout.preferredWidth: root.wide ? 182 : 158
                Layout.minimumWidth: Layout.preferredWidth
                Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    UiIcon { name: "sliders"; color: Theme.muted; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                    SectionLabel { text: qsTr("Filtros"); Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Limpar"); quiet: true; reduced: root.reduced
                        implicitHeight: 30; font.pixelSize: 11
                        onClicked: root.resetFilters()
                    }
                }
                SectionLabel { text: qsTr("Compatibilidade"); Layout.topMargin: 4 }
                RowLayout {
                    Layout.fillWidth: true
                    Caption { Layout.fillWidth: true; text: root.game ? "Minecraft " + root.game.version : "Minecraft" }
                    ToggleSwitch { id: sameVersion; checked: true; reduced: root.reduced; Accessible.name: qsTr("Filtrar pela versão do jogo"); onToggled: root.scheduleSearch() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.kind !== "resourcepack" && root.kind !== "datapack"
                    Caption { Layout.fillWidth: true; text: root.kind === "shader" ? "Iris / OptiFine" : root.loader }
                    ToggleSwitch { id: sameLoader; checked: true; reduced: root.reduced; Accessible.name: qsTr("Filtrar pelo loader do jogo"); onToggled: root.scheduleSearch() }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Desative para explorar outras versões. A instalação exige compatibilidade com este jogo.")
                    font.pixelSize: 11
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; Layout.topMargin: 4; Layout.bottomMargin: 4 }
                SectionLabel { text: qsTr("Categorias") }
                CategoryButton { Layout.fillWidth: true; categoryId: ""; label: qsTr("Todas as categorias") }
                ListView {
                    id: categories
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    model: mods.categories
                    ScrollBar.vertical: ThinScrollBar {}
                    delegate: CategoryButton {
                        required property var modelData
                        width: categories.width - 8
                        categoryId: modelData.id
                        label: root.categoryName(modelData.id)
                    }
                }
                Caption { visible: mods.categoriesLoading; text: qsTr("Carregando categorias…") }
                Caption { visible: mods.categoriesError !== ""; text: mods.categoriesError; Layout.fillWidth: true }
                ActionButton {
                    visible: mods.categoriesError !== ""
                    text: qsTr("Tentar de novo"); quiet: true; reduced: root.reduced
                    onClicked: mods.fetchCategories(root.kind)
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.line }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 240
                spacing: 12
                InputField {
                    id: query
                    Layout.fillWidth: true
                    iconName: "search"; clearable: true
                    Accessible.name: placeholderText
                    placeholderText: root.kind === "shader" ? qsTr("Buscar shaders") : root.kind === "resourcepack" ? qsTr("Buscar resource packs")
                        : root.kind === "datapack" ? qsTr("Buscar datapacks") : qsTr("Buscar mods")
                    onTextChanged: root.scheduleSearch()
                    onAccepted: root.runSearch(0)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Caption {
                        Layout.fillWidth: true
                        text: mods.searching && results.count === 0 ? qsTr("Buscando…") : qsTr("%1 resultados").arg(root.formatCount(mods.totalHits))
                    }
                    ChoiceField {
                        id: sort
                        Layout.preferredWidth: root.wide ? 174 : 146
                        implicitHeight: 34; font.pixelSize: 12
                        model: [qsTr("Recomendados"), qsTr("Mais baixados"), qsTr("Atualizados recentemente"), qsTr("Mais recentes"), qsTr("Mais seguidos")]
                        Accessible.name: qsTr("Ordenar resultados")
                        onActivated: root.scheduleSearch()
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ListView {
                        id: results
                        anchors.fill: parent
                        clip: true
                        spacing: 5
                        model: mods.results
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ThinScrollBar {}
                        Keys.onReturnPressed: if (currentIndex >= 0 && currentIndex < count) root.selectProject(mods.results[currentIndex])
                        delegate: AbstractButton {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool chosen: root.selected !== null && root.selected.id === modelData.id
                            readonly property bool added: mods.installedProjects.indexOf(modelData.id) !== -1
                            width: results.width - 10
                            height: 116
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: modelData.title + qsTr(" por ") + modelData.author
                            onClicked: { results.currentIndex = index; root.selectProject(modelData) }
                            background: Rectangle {
                                radius: Theme.radiusLg
                                color: row.chosen ? Theme.accentSoft : row.hovered ? Theme.fillHover : Theme.fill
                                border.width: 1
                                border.color: row.visualFocus ? Theme.focusRing : row.chosen ? Theme.accentLine : "transparent"
                                Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                                Rectangle { visible: row.chosen; width: 3; height: 28; radius: 1.5; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                anchors.fill: parent; anchors.margins: 14
                                Rectangle {
                                    Layout.preferredWidth: root.wide ? 52 : 42
                                    Layout.preferredHeight: width
                                    Layout.alignment: Qt.AlignTop
                                    radius: Theme.radiusMd; color: Theme.fill; clip: true
                                    UiIcon { anchors.centerIn: parent; visible: hitIcon.status !== Image.Ready; name: "puzzle"; color: Theme.faint }
                                    Image { id: hitIcon; anchors.fill: parent; source: row.modelData.iconUrl; sourceSize: Qt.size(104, 104); fillMode: Image.PreserveAspectFit; asynchronous: true }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: row.modelData.title; textFormat: Text.PlainText
                                        color: Theme.text; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Caption {
                                        Layout.fillWidth: true
                                        text: qsTr("por ") + row.modelData.author
                                        font.pixelSize: 11; wrapMode: Text.NoWrap; elide: Text.ElideRight
                                    }
                                    Caption {
                                        Layout.fillWidth: true
                                        text: row.modelData.description
                                        font.pixelSize: 12; maximumLineCount: 2; elide: Text.ElideRight
                                    }
                                    Caption {
                                        text: row.added ? qsTr("Instalado") : root.formatCount(row.modelData.downloads) + qsTr(" downloads")
                                        color: row.added ? Theme.success : Theme.faint
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                        footer: Column {
                            width: results.width - 10
                            spacing: 8
                            Caption { width: parent.width; text: mods.searchError; visible: mods.searchError !== "" && results.count > 0 }
                            ActionButton {
                                width: parent.width
                                visible: results.count > 0 && (mods.searchOffset < mods.totalHits || mods.searchError !== "")
                                text: mods.searching ? qsTr("Carregando…") : mods.searchError !== "" ? qsTr("Tentar de novo") : qsTr("Carregar mais")
                                enabled: !mods.searching && !searchDelay.running
                                reduced: root.reduced
                                onClicked: root.runSearch(mods.searchOffset)
                            }
                        }
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 24, 300)
                        visible: results.count === 0
                        spacing: 12
                        UiIcon { name: "search"; color: Theme.faint; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                        Text {
                            Layout.fillWidth: true
                            text: mods.searching ? qsTr("Buscando…") : mods.searchError !== "" ? qsTr("Não foi possível buscar") : qsTr("Nada encontrado")
                            color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        }
                        Caption {
                            Layout.fillWidth: true
                            visible: !mods.searching
                            text: mods.searchError !== "" ? mods.searchError : qsTr("Tente outro nome ou ajuste os filtros de versão e categoria.")
                            horizontalAlignment: Text.AlignHCenter
                        }
                        ActionButton {
                            Layout.alignment: Qt.AlignHCenter
                            visible: !mods.searching
                            text: mods.searchError !== "" ? qsTr("Tentar de novo") : qsTr("Limpar filtros")
                            reduced: root.reduced
                            onClicked: if (mods.searchError !== "") root.runSearch(0); else root.resetFilters()
                        }
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.line }
            ColumnLayout {
                Layout.preferredWidth: root.wide ? 340 : 280
                Layout.minimumWidth: Layout.preferredWidth
                Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true
                spacing: 16
                SectionLabel { text: qsTr("Detalhes do projeto") }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        visible: root.selected === null
                        anchors.centerIn: parent
                        width: parent.width - 36
                        spacing: 14
                        UiIcon { name: "puzzle"; color: Theme.faint; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 36; Layout.preferredHeight: 36 }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Conheça antes de instalar")
                            color: Theme.secondary; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        }
                        Caption { Layout.fillWidth: true; text: qsTr("Selecione um resultado para ver a descrição, as categorias e a compatibilidade com seu jogo."); horizontalAlignment: Text.AlignHCenter }
                    }
                    ScrollView {
                        id: detailsScroll
                        visible: root.selected !== null
                        anchors.fill: parent
                        contentWidth: availableWidth
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical: ThinScrollBar {}
                        ColumnLayout {
                            width: detailsScroll.availableWidth - 12
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 72; Layout.preferredHeight: 72
                                radius: Theme.radiusLg; color: Theme.fill; clip: true
                                UiIcon { visible: projectIcon.status !== Image.Ready; anchors.centerIn: parent; width: 28; height: 28; name: "puzzle"; color: Theme.faint }
                                Image { id: projectIcon; anchors.fill: parent; source: root.selected ? root.selected.iconUrl : ""; sourceSize: Qt.size(144, 144); fillMode: Image.PreserveAspectFit; asynchronous: true }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.selected ? root.selected.title : ""; textFormat: Text.PlainText
                                color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 22; font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }
                            Caption { Layout.fillWidth: true; Layout.topMargin: -10; text: root.selected ? qsTr("por ") + root.selected.author : "" }
                            Caption { Layout.fillWidth: true; text: root.selected ? root.selected.description : ""; color: Theme.secondary; font.pixelSize: 13 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 20
                                ColumnLayout {
                                    spacing: 2
                                    SectionLabel { text: root.formatCount(root.selected ? root.selected.downloads : 0); font.pixelSize: 16; color: Theme.text }
                                    Caption { text: qsTr("Downloads"); color: Theme.faint; font.pixelSize: 11 }
                                }
                                ColumnLayout {
                                    visible: root.project.followers !== undefined
                                    spacing: 2
                                    SectionLabel { text: root.formatCount(root.project.followers); font.pixelSize: 16; color: Theme.text }
                                    Caption { text: qsTr("Seguidores"); color: Theme.faint; font.pixelSize: 11 }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                            Caption {
                                Layout.fillWidth: true
                                visible: (root.project.gameVersions || []).length > 0
                                text: "Minecraft: " + (root.project.gameVersions || []).join(", ")
                                maximumLineCount: 3; elide: Text.ElideRight
                            }
                            Caption {
                                Layout.fillWidth: true
                                visible: (root.project.loaders || []).length > 0
                                text: qsTr("Loaders: %1").arg((root.project.loaders || []).join(", "))
                            }
                            Caption { visible: mods.projectLoading; text: qsTr("Carregando informações…") }
                            Caption { Layout.fillWidth: true; visible: mods.projectError !== ""; text: mods.projectError; color: Theme.danger }
                            ActionButton {
                                visible: mods.projectError !== ""
                                text: qsTr("Tentar de novo"); reduced: root.reduced
                                onClicked: root.selectProject(root.selected)
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: root.project.categories || []
                                    delegate: Rectangle {
                                        required property string modelData
                                        implicitWidth: categoryText.implicitWidth + 16
                                        implicitHeight: 26
                                        radius: 7; color: Theme.fill
                                        Text { id: categoryText; anchors.centerIn: parent; text: root.categoryName(modelData); color: Theme.muted; font.family: Theme.font; font.pixelSize: 11 }
                                    }
                                }
                            }
                            SectionLabel { visible: !!root.project.body; text: qsTr("Sobre o projeto") }
                            Text {
                                Layout.fillWidth: true
                                visible: !!root.project.body
                                text: root.readableBody(root.project.body)
                                textFormat: Text.MarkdownText
                                color: Theme.secondary; linkColor: Theme.accentText
                                font.family: Theme.font; font.pixelSize: 12
                                wrapMode: Text.Wrap
                                onLinkActivated: function(link) { root.openLink(link) }
                            }
                            Caption { Layout.fillWidth: true; visible: !!root.project.license; text: qsTr("Licença: %1").arg(root.project.license || "") }
                            ActionButton {
                                Layout.fillWidth: true
                                text: qsTr("Ver no Modrinth"); iconName: "external"; quiet: true; reduced: root.reduced
                                onClicked: root.openLink("https://modrinth.com/" + root.kind + "/" + (root.project.slug || root.selected.id))
                            }
                            ActionButton {
                                Layout.fillWidth: true
                                visible: !!root.project.sourceUrl
                                text: qsTr("Código-fonte"); iconName: "external"; quiet: true; reduced: root.reduced
                                onClicked: root.openLink(root.project.sourceUrl)
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; visible: root.selected !== null }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.selected !== null
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        UiIcon { name: root.canInstall ? "check" : "info"; color: root.canInstall ? Theme.success : Theme.muted; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                        Caption {
                            Layout.fillWidth: true
                            color: root.canInstall ? Theme.success : Theme.muted
                            text: mods.projectLoading ? qsTr("Verificando compatibilidade…")
                                : root.canInstall ? (root.forServer ? qsTr("Compatível com este servidor") : qsTr("Compatível com este jogo"))
                                : root.project.compatibilityKnown === true ? (root.forServer ? qsTr("Sem versão compatível com este servidor") : qsTr("Sem versão compatível com este jogo"))
                                : qsTr("Não foi possível verificar a compatibilidade")
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        visible: root.canInstall && !!root.project.versionNumber
                        text: qsTr("Versão: %1").arg(root.project.versionNumber || "")
                        font.pixelSize: 11
                        elide: Text.ElideRight; wrapMode: Text.NoWrap
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        text: root.installed ? qsTr("Instalado") : root.installing ? qsTr("Instalando…") : root.forServer ? qsTr("Instalar no servidor") : qsTr("Instalar no jogo")
                        iconName: root.installed ? "check" : root.installing ? "" : "download"
                        primary: !root.installed
                        enabled: root.canInstall && !root.installed && !root.installing
                        reduced: root.reduced
                        onClicked: mods.install(root.selected.id, root.game.id, root.game.version, root.loader, root.kind)
                    }
                }
            }
        }
    }
}
