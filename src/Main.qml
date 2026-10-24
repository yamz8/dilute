import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1240
    height: 800
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: (recipe.dirty ? "● " : "") + (recipe.fileName !== "" ? recipe.fileName + " — Dilute" : "Dilute")
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property int currentTab: 0
    property bool helpVisible: false
    property bool historyVisible: false
    property bool closing: false
    property var pendingAction: null
    readonly property var pages: [dilutionPage, molarityPage, serialPage, bufferPage]
    readonly property var page: pages[currentTab]

    // Anything that would drop an unsaved recipe asks first.
    function guard(action) {
        if (recipe.dirty) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }
    function quit() { guard(function() { win.closing = true; Qt.quit() }) }
    function copyMain() { if (page.result && page.result.ok) backend.copy(page.result.main) }
    function showCard() {
        if (page.result && page.result.card) exportSheet.show(page.result.card)
        else backend.notify(qsTr("Nothing to put on a card yet"), true)
    }
    function newCalculation() {
        if (currentTab === 3) guard(recipe.newRecipe)
        else page.clear()
    }
    function save() {
        if (currentTab === 3) {
            recipe.save()
            return
        }
        const json = page.recipeJson()
        if (json) backend.saveRecipe(json)
        else backend.notify(currentTab === 2 ? qsTr("A serial dilution isn't a recipe; export its card with Ctrl+E")
                                             : qsTr("Finish the calculation first"), true)
    }
    function openRecipe() { guard(function() { win.currentTab = 3; recipe.open() }) }
    function restoreHistory(entry) {
        if (entry.tab === 3) {
            guard(function() { recipe.restore(entry.inputs); win.currentTab = 3 })
        } else {
            pages[entry.tab].restore(entry.inputs)
            currentTab = entry.tab
        }
    }
    // Leaves a text field, so single-key shortcuts reach the window again.
    function leaveField() { content.forceActiveFocus() }

    onClosing: (close) => {
        if (recipe.dirty && !win.closing) {
            close.accepted = false
            quit()
        }
    }

    // Single keys go to the field being typed in; they work once Esc leaves it.
    Shortcut { sequences: ["?", "Shift+?"]; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; onActivated: win.historyVisible = !win.historyVisible }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; onActivated: win.newCalculation() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 2 }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 3 }
    Shortcut { sequence: "Ctrl+1"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 0 }
    Shortcut { sequence: "Ctrl+2"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 1 }
    Shortcut { sequence: "Ctrl+3"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 2 }
    Shortcut { sequence: "Ctrl+4"; context: Qt.ApplicationShortcut; onActivated: win.currentTab = 3 }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: win.save() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: win.openRecipe() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; onActivated: win.showCard() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: if (win.currentTab === 3) recipe.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", StandardKey.Redo]; context: Qt.ApplicationShortcut; onActivated: if (win.currentTab === 3) recipe.redo() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.historyVisible) win.historyVisible = false
            else win.leaveField()
        }
    }

    Connections {
        target: spaceKey
        function onTapped() { win.copyMain() }
    }
    Connections {
        target: backend
        function onMessage(text, error) { toast.show(text, error) }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: content
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: "#141416"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 16

                    TabBar {
                        id: tabs
                        currentIndex: win.currentTab
                        onCurrentIndexChanged: win.currentTab = currentIndex
                        background: null
                        Repeater {
                            model: [qsTr("Dilution"), qsTr("Molarity"), qsTr("Serial dilution"), qsTr("Buffer recipe")]
                            delegate: TabButton {
                                required property string modelData
                                required property int index
                                text: (index + 1) + "   " + modelData
                                width: implicitWidth
                                focusPolicy: Qt.NoFocus
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("Significant figures"); color: "#a1a1aa" }
                    SpinBox {
                        from: 1
                        to: 8
                        value: backend.significantFigures
                        Material.roundedScale: theme.radius
                        onValueModified: backend.significantFigures = value
                    }
                    Label { text: qsTr("Smallest pipette volume"); color: "#a1a1aa" }
                    TextField {
                        id: minimum
                        Layout.preferredWidth: 72
                        text: { translator.language; return Number(backend.minimumPipette).toLocaleString(Qt.locale(), 'f', 2) }
                        selectByMouse: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        Material.roundedScale: theme.radius
                        onEditingFinished: {
                            let value = NaN
                            try { value = Number.fromLocaleString(Qt.locale(), text) } catch (e) { value = Number(text) }
                            if (isNaN(value) || value < 0) {
                                backend.notify(qsTr("“%1” isn't a number").arg(text), true)
                                text = Number(backend.minimumPipette).toLocaleString(Qt.locale(), 'f', 2)
                            } else {
                                backend.minimumPipette = value
                            }
                        }
                    }
                    Label { text: "µL"; color: "#a1a1aa" }
                    ToolButton {
                        text: qsTr("History")
                        checkable: true
                        checked: win.historyVisible
                        focusPolicy: Qt.NoFocus
                        onClicked: win.historyVisible = checked
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.currentTab

                    DilutionPage {
                        id: dilutionPage
                        onCopyRequested: win.copyMain()
                        onCardRequested: win.showCard()
                    }
                    MolarityPage {
                        id: molarityPage
                        onCopyRequested: win.copyMain()
                        onCardRequested: win.showCard()
                    }
                    SerialPage {
                        id: serialPage
                        onCopyRequested: win.copyMain()
                        onCardRequested: win.showCard()
                    }
                    BufferPage {
                        id: bufferPage
                        onCopyRequested: win.copyMain()
                        onCardRequested: win.showCard()
                        onPresetRequested: (name) => win.guard(function() { recipe.loadPreset(name) })
                        onOpenRequested: win.openRecipe()
                    }
                }

                HistoryPanel {
                    Layout.preferredWidth: 340
                    Layout.fillHeight: true
                    visible: win.historyVisible
                    onPicked: (entry) => win.restoreHistory(entry)
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                leftPadding: 16
                verticalAlignment: Text.AlignVCenter
                text: qsTr("? shortcuts   ·   Esc leaves a field   ·   Space copies   ·   Ctrl+E recipe card")
                color: "#71717a"
                font.pixelSize: 12
            }
        }
    }

    HelpOverlay {
        anchors.fill: parent
        visible: win.helpVisible
        onClose: win.helpVisible = false
    }

    ExportSheet { id: exportSheet }

    Rectangle {
        id: toast
        property bool error: false
        function show(text, error) {
            toastLabel.text = text
            toast.error = error
            toast.opacity = 1
            toastTimer.restart()
        }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: Math.min(toastLabel.implicitWidth + 40, parent.width - 48)
        height: toastLabel.implicitHeight + 20
        radius: theme.radius
        color: toast.error ? "#3b1d1f" : "#27272a"
        border.color: toast.error ? "#ff6b6b" : theme.accent
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toast.parent.width - 88)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Timer {
            id: toastTimer
            interval: toast.error ? 6000 : 2500
            onTriggered: toast.opacity = 0
        }
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard the unsaved recipe?")
        Material.roundedScale: theme.radius
        Label { text: qsTr("Your changes to the buffer recipe aren't saved.") }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Cancel")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: qsTr("Discard")
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        onAccepted: {
            const action = win.pendingAction
            win.pendingAction = null
            if (action) action()
        }
        onRejected: win.pendingAction = null
    }
}
