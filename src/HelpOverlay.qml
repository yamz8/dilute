import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    color: "#cc0e0e10"
    Material.roundedScale: theme.radius

    signal close()

    readonly property var shortcuts: [
        ["1 – 4  /  Ctrl+1 – 4", qsTr("Switch calculator")],
        ["Tab  /  Shift+Tab", qsTr("Next or previous field")],
        ["Esc", qsTr("Leave the field, so single keys work")],
        ["Space", qsTr("Copy the main result")],
        ["N", qsTr("New calculation or recipe")],
        ["Ctrl+S", qsTr("Save as a .dilute recipe")],
        ["Ctrl+O", qsTr("Open a recipe")],
        ["Ctrl+E", qsTr("Export the recipe card as PDF or SVG")],
        ["Ctrl+Z  /  Ctrl+Shift+Z", qsTr("Undo and redo recipe edits")],
        ["H", qsTr("Show or hide the history")],
        ["?", qsTr("Show or hide these shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: overlay.close()
        onWheel: {}
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, grid.implicitWidth + 64)
        height: column.implicitHeight + 48
        radius: theme.radius
        color: "#18181b"
        border.color: Qt.rgba(1, 1, 1, 0.08)

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Label {
                text: qsTr("Keyboard shortcuts")
                font.pixelSize: 18
                font.bold: true
            }

            GridLayout {
                id: grid
                columns: 2
                columnSpacing: 28
                rowSpacing: 8

                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData[0]
                        font.family: "monospace"
                        color: theme.accent
                    }
                }
                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        text: modelData[1]
                    }
                }
            }
        }
    }
}
