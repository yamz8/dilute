import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// C1V1 = C2V2: fill three, the fourth is worked out.
Item {
    id: page
    property int tabIndex: 0
    signal copyRequested()
    signal cardRequested()

    readonly property var concentrationUnits: backend.units("concentration")
    readonly property var volumeUnits: backend.units("volume")
    readonly property var labels: ({
        c1: qsTr("Stock concentration (C1)"), v1: qsTr("Stock volume (V1)"),
        c2: qsTr("Final concentration (C2)"), v2: qsTr("Final volume (V2)")
    })

    readonly property var result: {
        backend.significantFigures; backend.minimumPipette; translator.language
        return backend.dilution(page.inputs())
    }
    readonly property string solvedKey: result.ok && result.solved.length ? result.solved[0] : ""
    readonly property string caption: solvedKey !== "" ? labels[solvedKey] : qsTr("Diluent to add")

    function inputs() {
        return { c1: { text: c1.text, unit: c1.unit }, v1: { text: v1.text, unit: v1.unit },
                 c2: { text: c2.text, unit: c2.unit }, v2: { text: v2.text, unit: v2.unit } }
    }
    function restore(values) {
        for (const [key, field] of [["c1", c1], ["v1", v1], ["c2", c2], ["v2", v2]]) {
            field.text = values[key] ? values[key].text : ""
            if (values[key] && values[key].unit) field.unit = values[key].unit
        }
    }
    function clear() { c1.clear(); v1.clear(); c2.clear(); v2.clear(); c1.input.forceActiveFocus() }
    function computed(key) { return result.ok && result.solved.indexOf(key) >= 0 ? result.values[key].number : "" }
    // A dilution is a recipe of one stock brought to volume.
    function recipeJson() {
        if (!result.ok) return null
        const v = result.values
        return { format: "dilute", version: 1, name: qsTr("Dilution"), notes: "",
                 volume: v.v2.serialized,
                 components: [{ name: qsTr("Stock"), final: v.c2.serialized, stock: v.c1.serialized }] }
    }

    onResultChanged: if (result.ok) rememberTimer.restart()
    Timer {
        id: rememberTimer
        interval: 1500
        onTriggered: if (page.result.ok) backend.remember(page.tabIndex, page.inputs(), page.result.summary)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        ColumnLayout {
            Layout.preferredWidth: 420
            Layout.maximumWidth: 460
            Layout.fillHeight: true
            spacing: 14

            Label { text: qsTr("Dilution"); font.pixelSize: 22; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: qsTr("C1 × V1 = C2 × V2. Fill in any three; the empty one is worked out.")
                color: "#a1a1aa"
                wrapMode: Text.Wrap
            }
            QuantityField { id: c1; Layout.fillWidth: true; label: page.labels.c1; units: page.concentrationUnits; unit: "mM"; computed: page.computed("c1") }
            QuantityField { id: v1; Layout.fillWidth: true; label: page.labels.v1; units: page.volumeUnits; unit: "mL"; computed: page.computed("v1") }
            QuantityField { id: c2; Layout.fillWidth: true; label: page.labels.c2; units: page.concentrationUnits; unit: "mM"; computed: page.computed("c2") }
            QuantityField { id: v2; Layout.fillWidth: true; label: page.labels.v2; units: page.volumeUnits; unit: "mL"; computed: page.computed("v2") }
            Button {
                text: qsTr("Clear") + "  (N)"
                flat: true
                Material.roundedScale: theme.radius
                onClicked: page.clear()
            }
            Item { Layout.fillHeight: true }
        }

        ResultPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            result: page.result
            caption: page.caption
            headline: page.result.ok ? page.result.main : ""
            onCopyRequested: page.copyRequested()
            onCardRequested: page.cardRequested()
        }
    }
}
