import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A serial dilution: every tube gets the transfer volume from the one before
// and enough diluent to make it 1:factor.
Item {
    id: page
    property int tabIndex: 2
    signal copyRequested()
    signal cardRequested()

    readonly property var result: {
        backend.significantFigures; backend.minimumPipette; translator.language
        return backend.serial(page.inputs())
    }

    function inputs() {
        return { start: start.value(), factor: factor.text, steps: steps.value, transfer: transfer.value() }
    }
    function restore(values) {
        start.text = values.start ? values.start.text : ""
        if (values.start && values.start.unit) start.unit = values.start.unit
        factor.text = values.factor || ""
        steps.value = values.steps || 6
        transfer.text = values.transfer ? values.transfer.text : ""
        if (values.transfer && values.transfer.unit) transfer.unit = values.transfer.unit
    }
    function clear() {
        start.clear(); transfer.clear()
        factor.text = "10"
        steps.value = 6
        start.input.forceActiveFocus()
    }
    function recipeJson() { return null }

    onResultChanged: if (result.ok) rememberTimer.restart()
    Timer {
        id: rememberTimer
        interval: 1500
        onTriggered: if (page.result.ok) backend.remember(page.tabIndex, page.inputs(), page.result.summary)
    }

    readonly property var columns: [
        { key: "tube", title: qsTr("Tube"), width: 0.08 },
        { key: "concentration", title: qsTr("Concentration"), width: 0.22 },
        { key: "fold", title: qsTr("Dilution"), width: 0.16 },
        { key: "diluent", title: qsTr("Diluent"), width: 0.18 },
        { key: "transfer", title: qsTr("Transfer"), width: 0.18 },
        { key: "finalVolume", title: qsTr("Final volume"), width: 0.18 }
    ]

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        ColumnLayout {
            Layout.preferredWidth: 380
            Layout.maximumWidth: 420
            Layout.fillHeight: true
            spacing: 14

            Label { text: qsTr("Serial dilution"); font.pixelSize: 22; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: qsTr("Each tube is filled with diluent and gets the transfer volume from the tube before it.")
                color: "#a1a1aa"
                wrapMode: Text.Wrap
            }
            QuantityField { id: start; Layout.fillWidth: true; label: qsTr("Start concentration"); units: backend.units("concentration"); unit: "mM" }
            TextField {
                id: factor
                Layout.fillWidth: true
                Layout.rightMargin: 120
                placeholderText: qsTr("Dilution factor per step")
                text: "10"
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Material.roundedScale: theme.radius
            }
            RowLayout {
                spacing: 12
                Label { text: qsTr("Tubes") }
                SpinBox {
                    id: steps
                    from: 1
                    to: 96
                    value: 6
                    editable: true
                    Material.roundedScale: theme.radius
                }
            }
            QuantityField { id: transfer; Layout.fillWidth: true; label: qsTr("Transfer volume"); units: backend.units("volume"); unit: "µL" }
            Button {
                text: qsTr("Clear") + "  (N)"
                flat: true
                Material.roundedScale: theme.radius
                onClicked: page.clear()
            }
            Item { Layout.fillHeight: true }
        }

        ResultPanel {
            id: panel
            Layout.fillWidth: true
            Layout.fillHeight: true
            result: page.result
            caption: qsTr("Diluent per tube")
            headline: page.result.ok && page.result.rows.length ? page.result.rows[0].diluent : ""
            onCopyRequested: page.copyRequested()
            onCardRequested: page.cardRequested()

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: page.result.ok
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.08)
                radius: Math.min(4, theme.radius)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    Row {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        Repeater {
                            model: page.columns
                            delegate: Label {
                                required property var modelData
                                width: (panel.width - 50) * modelData.width
                                height: 32
                                leftPadding: 10
                                verticalAlignment: Text.AlignVCenter
                                text: modelData.title
                                color: "#a1a1aa"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }
                    ListView {
                        id: table
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: page.result.ok ? page.result.rows : []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: tableRow
                            required property var modelData
                            required property int index
                            width: table.width
                            height: 30
                            color: index % 2 ? "transparent" : Qt.rgba(1, 1, 1, 0.03)
                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: page.columns
                                    delegate: Label {
                                        required property var modelData
                                        width: (panel.width - 50) * modelData.width
                                        height: 30
                                        leftPadding: 10
                                        verticalAlignment: Text.AlignVCenter
                                        text: tableRow.modelData[modelData.key]
                                        color: modelData.key === "concentration" ? theme.accent : "#e4e4e7"
                                        font.bold: modelData.key === "concentration"
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
