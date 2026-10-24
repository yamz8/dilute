import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The reagent list behind the Molarity picker: rename, change a weight, add
// your own, or go back to the built-in list.
Popup {
    id: editor
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 48)
    height: Math.min(620, parent.height - 48)
    modal: true
    padding: 20
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181b"; radius: theme.radius; border.color: Qt.rgba(1, 1, 1, 0.08) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label { text: qsTr("Reagents"); font.pixelSize: 18; font.bold: true }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: backend.reagents
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: list.width
                height: 48
                RowLayout {
                    anchors.fill: parent
                    anchors.rightMargin: 12
                    spacing: 8
                    TextField {
                        id: nameField
                        property bool acceptsSpace: true
                        Layout.fillWidth: true
                        text: row.modelData.name
                        selectByMouse: true
                        Material.roundedScale: theme.radius
                        onEditingFinished: if (text !== row.modelData.name) backend.setReagent(row.index, text, mwField.text)
                    }
                    TextField {
                        id: mwField
                        Layout.preferredWidth: 110
                        text: row.modelData.mwText
                        placeholderText: "g/mol"
                        selectByMouse: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        Material.roundedScale: theme.radius
                        onEditingFinished: if (text !== row.modelData.mwText) backend.setReagent(row.index, nameField.text, text)
                    }
                    ToolButton {
                        text: "✕"
                        onClicked: backend.removeReagent(row.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove")
                    }
                }
            }
        }

        RowLayout {
            spacing: 8
            Button {
                text: qsTr("Add reagent")
                Material.roundedScale: theme.radius
                onClicked: {
                    backend.addReagent()
                    list.positionViewAtEnd()
                }
            }
            Button {
                text: qsTr("Restore built-in list")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: backend.resetReagents()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Done")
                highlighted: true
                Material.roundedScale: theme.radius
                onClicked: editor.close()
            }
        }
    }
}
