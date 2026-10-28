import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool reduced: false
    property int source: 0
    signal notify(string message)
    readonly property real gutter: width < 900 ? 32 : 48

    RowLayout {
        id: header
        x: root.gutter; y: 50
        width: root.width - root.gutter * 2
        spacing: 10
        Text {
            text: "Modpacks"
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
            font.letterSpacing: -0.6
            Layout.fillWidth: true
        }
        InputField {
            Layout.preferredWidth: Math.min(320, header.width * 0.38)
            iconName: "search"; clearable: true
            placeholderText: "Buscar no " + (root.source === 0 ? "Modrinth" : "CurseForge")
            onAccepted: root.notify("O catálogo será conectado na próxima etapa.")
        }
    }

    SegmentTabs {
        id: sources
        x: root.gutter; y: header.y + header.height + 14
        width: root.width - root.gutter * 2
        model: ["Modrinth", "CurseForge"]
        currentIndex: root.source
        onCurrentIndexChanged: root.source = currentIndex
        reduced: root.reduced
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        y: sources.y + (root.height - sources.y) * 0.32
        width: Math.min(420, root.width - root.gutter * 2)
        spacing: 10
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 60; height: 60; radius: 30
            color: Theme.fill
            border.width: 1; border.color: Theme.line
            UiIcon { anchors.centerIn: parent; width: 26; height: 26; name: "packs"; color: Theme.muted }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            text: "Catálogo ainda não conectado"
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Modpacks instalados daqui viram jogos na sua biblioteca."
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
    }
}
