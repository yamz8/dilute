import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A number and its unit. Left empty, it shows what was worked out for it,
// in its unit, in the accent color.
RowLayout {
    id: field
    property alias label: input.placeholderText
    property alias text: input.text
    property var units: []
    property string unit: units.length ? units[0] : ""
    property string computed: ""
    property alias input: input
    signal edited()

    function value() { return { text: input.text, unit: field.unit } }
    function clear() { input.text = "" }

    spacing: 8

    TextField {
        id: input
        Layout.fillWidth: true
        Material.roundedScale: theme.radius
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        onTextEdited: field.edited()

        Label {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text === "" && field.computed !== ""
            text: "= " + field.computed
            color: theme.accent
            font.bold: true
        }
    }

    ComboBox {
        id: combo
        Layout.preferredWidth: 112
        Material.roundedScale: theme.radius
        model: field.units
        currentIndex: Math.max(0, field.units.indexOf(field.unit))
        onActivated: {
            field.unit = currentText
            field.edited()
        }
    }
}
