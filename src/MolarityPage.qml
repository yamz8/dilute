import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Mass, moles, concentration and volume, tied by the molecular weight.
Item {
    id: page
    property int tabIndex: 1
    signal copyRequested()
    signal cardRequested()

    readonly property var labels: ({
        mass: qsTr("Mass"), moles: qsTr("Moles"), concentration: qsTr("Concentration"),
        volume: qsTr("Volume"), mw: qsTr("Molecular weight (g/mol)")
    })
    readonly property string reagentName: reagent.currentIndex > 0 ? reagent.currentText : ""

    readonly property var result: {
        backend.significantFigures; backend.minimumPipette; translator.language
        return backend.molarity(page.inputs())
    }
    readonly property string solvedKey: {
        if (!result.ok) return ""
        for (const key of result.solved)
            if (key !== "moles" && key !== "mw") return key
        return ""
    }

    function inputs() {
        return { reagent: page.reagentName, mw: mw.text,
                 mass: mass.value(), moles: moles.value(),
                 concentration: concentration.value(), volume: volume.value() }
    }
    function restore(values) {
        const index = reagent.model.indexOf(values.reagent || "")
        reagent.currentIndex = Math.max(0, index)
        mw.text = values.mw || ""
        for (const [key, field] of [["mass", mass], ["moles", moles], ["concentration", concentration], ["volume", volume]]) {
            field.text = values[key] ? values[key].text : ""
            if (values[key] && values[key].unit) field.unit = values[key].unit
        }
    }
    function clear() {
        reagent.currentIndex = 0
        mw.text = ""
        mass.clear(); moles.clear(); concentration.clear(); volume.clear()
        mw.forceActiveFocus()
    }
    function computed(key) { return result.ok && result.solved.indexOf(key) >= 0 ? result.values[key].number : "" }
    // A solution is a recipe of one solid dissolved to volume.
    function recipeJson() {
        if (!result.ok) return null
        const v = result.values
        const component = { name: page.reagentName || qsTr("Solute"), final: v.concentration.serialized }
        if (v.mw) component.mw = v.mw.value
        return { format: "dilute", version: 1, name: page.reagentName || qsTr("Solution"), notes: "",
                 volume: v.volume.serialized, components: [component] }
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

            Label { text: qsTr("Molarity"); font.pixelSize: 22; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: qsTr("Fill in all but one of mass, concentration and volume. The molecular weight links grams and moles.")
                color: "#a1a1aa"
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    id: reagent
                    Layout.fillWidth: true
                    Material.roundedScale: theme.radius
                    model: [qsTr("Custom reagent")].concat(backend.reagents.map(r => r.name))
                    onActivated: (index) => {
                        if (index > 0) mw.text = backend.reagents[index - 1].mwText
                    }
                }
                Button {
                    text: qsTr("Edit list")
                    flat: true
                    Material.roundedScale: theme.radius
                    onClicked: reagentEditor.open()
                }
            }
            TextField {
                id: mw
                Layout.fillWidth: true
                Layout.rightMargin: 120
                placeholderText: page.labels.mw
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Material.roundedScale: theme.radius
                onTextEdited: if (reagent.currentIndex > 0 && text !== backend.reagents[reagent.currentIndex - 1].mwText) reagent.currentIndex = 0

                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: mw.text === "" && page.computed("mw") !== ""
                    text: "= " + page.computed("mw")
                    color: theme.accent
                    font.bold: true
                }
            }
            QuantityField { id: mass; Layout.fillWidth: true; label: page.labels.mass; units: backend.units("mass"); unit: "g"; computed: page.computed("mass") }
            QuantityField { id: concentration; Layout.fillWidth: true; label: page.labels.concentration; units: backend.units("solution"); unit: "mM"; computed: page.computed("concentration") }
            QuantityField { id: volume; Layout.fillWidth: true; label: page.labels.volume; units: backend.units("volume"); unit: "mL"; computed: page.computed("volume") }
            QuantityField { id: moles; Layout.fillWidth: true; label: page.labels.moles; units: backend.units("amount"); unit: "mmol"; computed: page.computed("moles") }
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
            caption: page.solvedKey !== "" ? page.labels[page.solvedKey] : qsTr("Mass")
            headline: page.result.ok ? page.result.main : ""
            onCopyRequested: page.copyRequested()
            onCardRequested: page.cardRequested()
        }
    }

    ReagentEditor { id: reagentEditor }
}
