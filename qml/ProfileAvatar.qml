import QtQuick

Rectangle {
    id: root
    property bool empty: false
    // Skin texture (file URL); the face and hat layers are cut from it, pixels kept sharp.
    property string skin: ""
    width: 36; height: 36
    radius: width / 2
    color: empty ? "#1AFFFFFF" : "#3D443D"
    UiIcon { anchors.centerIn: parent; visible: root.empty; name: "user"; color: Theme.muted; width: Math.round(root.width * 0.46); height: width }
    // Pixel-built local Steve placeholder; no network or personal data needed.
    Item {
        visible: !root.empty && root.skin !== ""
        width: Math.round(root.width * 0.62); height: width
        anchors.centerIn: parent
        Image { anchors.fill: parent; source: root.skin; sourceClipRect: Qt.rect(8, 8, 8, 8); smooth: false }
        Image { anchors.fill: parent; source: root.skin; sourceClipRect: Qt.rect(40, 8, 8, 8); smooth: false }
    }
    Item {
        id: face
        readonly property real cell: Math.round(root.width * 0.62) / 8
        visible: !root.empty && root.skin === ""
        width: cell * 8; height: width
        anchors.centerIn: parent
        Repeater {
            model: ["33302533", "32222223", "87777778", "77877877", "74277247", "77744777", "77666677", "77566577"]
            delegate: Row {
                required property string modelData
                required property int index
                y: index * face.cell
                height: face.cell
                Repeater {
                    model: modelData.split("")
                    delegate: Rectangle {
                        required property string modelData
                        width: face.cell + 0.25; height: width
                        antialiasing: false
                        color: ({"0":"#241C18","2":"#402B20","3":"#30271F","4":"#605390","5":"#774732","6":"#4E3024","7":"#BE8B69","8":"#9F7253"})[modelData] || "#BE8B69"
                    }
                }
            }
        }
    }
}
