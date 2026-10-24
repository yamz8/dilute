import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Recent calculations, newest first. Clicking one puts it back.
Rectangle {
    id: panel
    color: "#18181b"
    border.color: Qt.rgba(1, 1, 1, 0.08)

    signal picked(var entry)
    readonly property var tabNames: [qsTr("Dilution"), qsTr("Molarity"), qsTr("Serial dilution"), qsTr("Buffer recipe")]

    function when(iso) {
        translator.language
        const date = new Date(iso)
        return date.toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("History"); font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
            Button {
                text: qsTr("Clear")
                flat: true
                enabled: backend.history.length > 0
                Material.roundedScale: theme.radius
                onClicked: backend.clearHistory()
            }
        }
        Label {
            visible: backend.history.length === 0
            Layout.fillWidth: true
            text: qsTr("Calculations show up here as you make them.")
            color: "#a1a1aa"
            wrapMode: Text.Wrap
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: backend.history
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: item
                required property var modelData
                width: list.width
                Material.roundedScale: theme.radius
                onClicked: panel.picked(modelData)
                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: panel.tabNames[item.modelData.tab] + "  ·  " + panel.when(item.modelData.time)
                        color: theme.accent
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: item.modelData.summary
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
