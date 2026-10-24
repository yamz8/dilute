import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// A buffer recipe: components at final concentrations, made up to a volume.
// Each one is weighed out, or pipetted from a stock when it has one.
Item {
    id: page
    property int tabIndex: 3
    signal copyRequested()
    signal cardRequested()
    signal presetRequested(string name)
    signal openRequested()

    readonly property var result: recipe.result
    readonly property var concentrationUnits: backend.units("concentration")
    readonly property var volumeUnits: backend.units("volume")

    function inputs() { return recipe.snapshot() }
    function recipeJson() { return null }

    onResultChanged: if (result.ok) rememberTimer.restart()
    Timer {
        id: rememberTimer
        interval: 2000
        onTriggered: if (page.result.ok) backend.remember(page.tabIndex, page.inputs(), page.result.summary)
    }

    // Column widths shared by the header and the rows.
    readonly property real nameWidth: Math.max(160, table.width - 120 - 230 - 230 - 130 - 40 - 5 * 8)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TextField {
                id: nameField
                property bool acceptsSpace: true
                Layout.preferredWidth: 280
                placeholderText: qsTr("Recipe name")
                text: recipe.name
                selectByMouse: true
                font.pixelSize: 18
                Material.roundedScale: theme.radius
                onTextEdited: recipe.name = text
            }
            QuantityField {
                id: volume
                Layout.preferredWidth: 280
                label: qsTr("Final volume")
                units: page.volumeUnits
                text: recipe.volumeText
                unit: recipe.volumeUnit
                onEdited: recipe.setVolume(volume.text, volume.unit)
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: presets
                Layout.preferredWidth: 220
                Material.roundedScale: theme.radius
                model: recipe.presets
                displayText: qsTr("Load preset")
                onActivated: page.presetRequested(currentText)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#18181b"
            radius: theme.radius
            border.color: Qt.rgba(1, 1, 1, 0.08)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    component HeaderLabel: Label {
                        color: "#a1a1aa"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    HeaderLabel { Layout.preferredWidth: page.nameWidth; text: qsTr("Component") }
                    HeaderLabel { Layout.preferredWidth: 120; text: qsTr("MW (g/mol)") }
                    HeaderLabel { Layout.preferredWidth: 230; text: qsTr("Final concentration") }
                    HeaderLabel { Layout.preferredWidth: 230; text: qsTr("Stock (optional)") }
                    HeaderLabel { Layout.preferredWidth: 130; text: qsTr("Add") }
                    Item { Layout.preferredWidth: 40 }
                }

                ListView {
                    id: table
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: recipe
                    ScrollBar.vertical: ScrollBar {}
                    footer: Button {
                        text: "+  " + qsTr("Add component")
                        flat: true
                        Material.roundedScale: theme.radius
                        onClicked: {
                            recipe.addComponent()
                            table.positionViewAtEnd()
                        }
                    }
                    delegate: Item {
                        id: row
                        required property int index
                        required property string name
                        required property string mw
                        required property string finalText
                        required property string finalUnit
                        required property string stockText
                        required property string stockUnit
                        required property string amount
                        required property bool isVolume
                        required property string error
                        required property string warning
                        width: table.width
                        height: 52

                        RowLayout {
                            anchors.fill: parent
                            spacing: 8
                            TextField {
                                property bool acceptsSpace: true
                                Layout.preferredWidth: page.nameWidth
                                text: row.name
                                selectByMouse: true
                                Material.roundedScale: theme.radius
                                onTextEdited: recipe.setField(row.index, "name", text)
                            }
                            TextField {
                                Layout.preferredWidth: 120
                                text: row.mw
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Material.roundedScale: theme.radius
                                onTextEdited: recipe.setField(row.index, "mw", text)
                            }
                            RowLayout {
                                Layout.preferredWidth: 230
                                spacing: 4
                                TextField {
                                    Layout.fillWidth: true
                                    text: row.finalText
                                    selectByMouse: true
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    Material.roundedScale: theme.radius
                                    onTextEdited: recipe.setField(row.index, "finalText", text)
                                }
                                ComboBox {
                                    Layout.preferredWidth: 108
                                    model: page.concentrationUnits
                                    currentIndex: Math.max(0, page.concentrationUnits.indexOf(row.finalUnit))
                                    Material.roundedScale: theme.radius
                                    onActivated: recipe.setField(row.index, "finalUnit", currentText)
                                }
                            }
                            RowLayout {
                                Layout.preferredWidth: 230
                                spacing: 4
                                TextField {
                                    Layout.fillWidth: true
                                    text: row.stockText
                                    placeholderText: "—"
                                    selectByMouse: true
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    Material.roundedScale: theme.radius
                                    onTextEdited: recipe.setField(row.index, "stockText", text)
                                }
                                ComboBox {
                                    Layout.preferredWidth: 108
                                    model: page.concentrationUnits
                                    currentIndex: Math.max(0, page.concentrationUnits.indexOf(row.stockUnit))
                                    Material.roundedScale: theme.radius
                                    onActivated: recipe.setField(row.index, "stockUnit", currentText)
                                }
                            }
                            ColumnLayout {
                                Layout.preferredWidth: 130
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    visible: row.amount !== ""
                                    text: row.amount
                                    color: row.warning !== "" ? "#f5a524" : theme.accent
                                    font.bold: true
                                    font.pixelSize: 15
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: row.amount !== ""
                                    text: row.isVolume ? qsTr("pipette") : qsTr("weigh")
                                    color: "#a1a1aa"
                                    font.pixelSize: 11
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: row.error !== ""
                                    text: row.error
                                    color: "#ff6b6b"
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                    HoverHandler { id: errorHover }
                                    ToolTip.visible: errorHover.hovered && truncated
                                    ToolTip.text: row.error
                                }
                            }
                            ToolButton {
                                Layout.preferredWidth: 40
                                text: "✕"
                                onClicked: recipe.removeComponent(row.index)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Remove component")
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            spacing: 14

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    property bool acceptsSpace: true
                    placeholderText: qsTr("Notes: pH, order of addition, storage")
                    text: recipe.notes
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    Material.roundedScale: theme.radius
                    onTextChanged: if (text !== recipe.notes) recipe.notes = text
                }
            }

            ResultPanel {
                Layout.preferredWidth: 460
                Layout.fillHeight: true
                result: page.result
                caption: page.result.ok ? qsTr("Water") : ""
                headline: page.result.ok ? qsTr("to %1").arg(volume.text + " " + volume.unit) : ""
                onCopyRequested: page.copyRequested()
                onCardRequested: page.cardRequested()

                RowLayout {
                    spacing: 4
                    Button { text: qsTr("Open"); flat: true; Material.roundedScale: theme.radius; onClicked: page.openRequested() }
                    Button { text: qsTr("Save"); flat: true; Material.roundedScale: theme.radius; onClicked: recipe.save() }
                    Button { text: qsTr("Save as preset"); flat: true; Material.roundedScale: theme.radius; onClicked: recipe.savePreset() }
                }
            }
        }
    }
}
