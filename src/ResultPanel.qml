import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The answer: what was worked out, in one big line, then the sentence to
// follow at the bench, the warnings, and what's wrong when nothing adds up.
Rectangle {
    id: panel
    property var result: ({})
    property string caption: ""
    property string headline: ""
    default property alias extra: extraColumn.data

    signal copyRequested()
    signal cardRequested()

    color: "#18181b"
    radius: theme.radius
    border.color: Qt.rgba(1, 1, 1, 0.08)

    readonly property bool ok: !!result.ok
    readonly property var warnings: result.warnings || []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Label {
            visible: panel.ok && panel.caption !== ""
            text: panel.caption
            color: "#a1a1aa"
            font.pixelSize: 13
        }
        Label {
            visible: panel.ok && panel.headline !== ""
            Layout.fillWidth: true
            text: panel.headline
            color: theme.accent
            font.pixelSize: 38
            font.bold: true
            elide: Text.ElideRight
        }
        Label {
            visible: panel.ok && !!panel.result.sentence
            Layout.fillWidth: true
            text: panel.result.sentence || ""
            wrapMode: Text.Wrap
            font.pixelSize: 15
        }
        Label {
            visible: panel.ok && !!panel.result.details
            Layout.fillWidth: true
            text: panel.result.details || ""
            wrapMode: Text.Wrap
            color: "#a1a1aa"
        }
        Repeater {
            model: panel.ok ? panel.warnings : []
            delegate: Label {
                required property string modelData
                Layout.fillWidth: true
                text: "⚠  " + modelData
                color: "#f5a524"
                wrapMode: Text.Wrap
            }
        }
        Label {
            visible: !panel.ok && !!panel.result.error
            Layout.fillWidth: true
            text: panel.result.error || ""
            color: panel.result.incomplete ? "#a1a1aa" : "#ff6b6b"
            wrapMode: Text.Wrap
            font.pixelSize: 15
        }

        ColumnLayout {
            id: extraColumn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
        }

        RowLayout {
            spacing: 8
            Button {
                text: qsTr("Copy") + "  (Space)"
                enabled: panel.ok
                Material.roundedScale: theme.radius
                onClicked: panel.copyRequested()
            }
            Button {
                text: qsTr("Recipe card") + "  (Ctrl+E)"
                enabled: !!panel.result.card
                Material.roundedScale: theme.radius
                onClicked: panel.cardRequested()
            }
        }
    }
}
