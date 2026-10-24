import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Dilute

// The recipe card as it will print, with PDF and SVG to save it as.
Popup {
    id: sheet
    property var card: ({})
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 48)
    height: Math.min(820, parent.height - 48)
    modal: true
    padding: 20
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#18181b"; radius: theme.radius; border.color: Qt.rgba(1, 1, 1, 0.08) }

    function show(card) {
        sheet.card = card
        open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        Label { text: qsTr("Recipe card"); font.pixelSize: 18; font.bold: true }
        CardView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            card: sheet.card
        }
        RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                flat: true
                Material.roundedScale: theme.radius
                onClicked: sheet.close()
            }
            Button {
                text: qsTr("Save SVG")
                Material.roundedScale: theme.radius
                onClicked: { backend.exportCard(sheet.card, "svg"); sheet.close() }
            }
            Button {
                text: qsTr("Save PDF")
                highlighted: true
                Material.roundedScale: theme.radius
                onClicked: { backend.exportCard(sheet.card, "pdf"); sheet.close() }
            }
        }
    }
}
