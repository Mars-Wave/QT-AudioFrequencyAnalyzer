import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AudioAnalyzer 1.0

Window {
    width: 900
    height: 600
    visible: true
    title: "Audio Analyzer Pro"
    color: "#111111"

    AnalyzerInterface {
        id: analyzer
        sensitivity: 0.5
        gain: 1.0
        normalizationMode: 1

        Component.onCompleted: {
            analyzer.setInputMode(analyzer.MODE_MIC)
        }
    }

    property bool isCustomMode: analyzer.normalizationMode === 2

    FileDialog {
        id: fileDialog
        title: "Select Audio File"
        nameFilters: ["Audio files (*.mp3 *.wav *.ogg *.flac *.m4a)"]
        onAccepted: {
            analyzer.fileInput.setSource(selectedFile)
            analyzer.fileInput.play()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // ============================================
        // INPUT CONTROLS ROW
        // ============================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 15

            // 1. Mode Selector
            Label { text: "Input:"; color: "#aaa" }
            ComboBox {
                Layout.preferredWidth: 120
                model: ["Mic", "File", "IP Stream"]
                currentIndex: analyzer.inputMode
                onActivated: (idx) => analyzer.inputMode = idx
            }

            // 2. Microphone Specifics
            RowLayout {
                visible: analyzer.inputMode === 0
                Layout.fillWidth: true
                Label { text: "Device:"; color: "#666" }
                ComboBox {
                    Layout.fillWidth: true; Layout.maximumWidth: 300
                    model: analyzer.micInput.devices
                    currentIndex: analyzer.micInput.currentIndex
                    onActivated: (idx) => analyzer.micInput.currentIndex = idx
                    delegate: ItemDelegate {
                        text: modelData
                        width: parent.width
                        contentItem: Text{ text: modelData; color: "black" }
                    }
                }
                Label { text: "Input Vol:"; color: "#666" }
                Slider {
                    from: 0.0; to: 1.0
                    value: analyzer.micInput.volume
                    onMoved: analyzer.micInput.volume = value
                    Layout.preferredWidth: 80
                }
            }

            // 3. File Input Specifics
            RowLayout {
                visible: analyzer.inputMode === 1
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Load File"
                    onClicked: fileDialog.open()

                    // REPLACEMENT: Standard background overrides
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.down ? "#222" : "#444"
                        radius: 4
                    }
                }

                Button {
                    text: analyzer.fileInput.playing ? "Pause" : "Play"
                    onClicked: analyzer.fileInput.playing ? analyzer.fileInput.pause() : analyzer.fileInput.play()

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        // Use checking logic for color
                        color: analyzer.fileInput.playing ? "#cc3333" : "#33cc33"
                        radius: 4
                    }
                }

                CheckBox {
                    text: "Monitor Output"
                    checked: analyzer.fileInput.monitoring
                    onToggled: analyzer.fileInput.monitoring = checked
                    contentItem: Text {
                        text: parent.text
                        color: parent.checked ? "white" : "#666"
                        leftPadding: 30
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: analyzer.fileInput.duration
                    value: analyzer.fileInput.position
                    onMoved: analyzer.fileInput.seek(value)
                    handle: Rectangle {
                        x: parent.visualPosition * (parent.availableWidth - width)
                        y: (parent.availableHeight - height) / 2
                        width: 12; height: 12; radius: 6; color: "white"
                    }
                }
            }

            // 4. IP Input Specifics
            RowLayout {
                visible: analyzer.inputMode === 2
                Layout.fillWidth: true

                Label { text: "Stream URL:"; color: "#666" }

                TextField {
                    text: analyzer.ipInput.url
                    placeholderText: "udp://127.0.0.1:5555 or http://..."
                    color: "white"
                    background: Rectangle { color: "#333" }
                    Layout.fillWidth: true
                    Layout.maximumWidth: 300
                    selectByMouse: true
                    // Commit on Enter or Focus loss
                    onEditingFinished: analyzer.ipInput.url = text
                }

                Button {
                    text: analyzer.ipInput.listening ? "Stop" : "Connect"

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: analyzer.ipInput.listening ? "#cc3333" : "#33cc33"
                        radius: 4
                    }

                    onClicked: {
                        if(analyzer.ipInput.listening) analyzer.ipInput.stopListening();
                        else analyzer.ipInput.startListening();
                    }
                }

                CheckBox {
                    text: "Monitor"
                    checked: analyzer.ipInput.monitoring
                    onToggled: analyzer.ipInput.monitoring = checked
                    contentItem: Text {
                        text: parent.text; color: parent.checked ? "white" : "#666"
                        leftPadding: 30; verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // ============================================
        // VISUALIZER SETTINGS
        // ============================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 15
            Label { text: "Sensitivity"; color: "#666" }
            Slider {
                from: 0.0; to: 1.0; value: analyzer.sensitivity
                onMoved: analyzer.sensitivity = value
                Layout.preferredWidth: 100
            }

            Label { text: "Digital Gain"; color: "#666" }
            Slider {
                from: 0.0; to: 2.0; value: analyzer.gain
                onMoved: analyzer.gain = value
                Layout.preferredWidth: 100
            }

            Label { text: "Normalization"; color: "#666" }
            ComboBox {
                model: ["Reactive", "Stable", "Custom"]
                currentIndex: analyzer.normalizationMode
                onActivated: (idx) => analyzer.normalizationMode = idx
                Layout.preferredWidth: 110
            }
        }

        // ============================================
        // BARS AND RMS
        // ============================================
        ColumnLayout {
            Layout.fillWidth: true; spacing: 5
            RowLayout {
                Label { text: "MASTER RMS"; color: "#666"; font.pixelSize: 10; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: "CUSTOM LIMITS"; color: "#ff3333"; font.pixelSize: 10; font.bold: true; visible: isCustomMode; opacity: 0.8 }
            }
            Item {
                Layout.fillWidth: true; height: 30
                Rectangle {
                    anchors.fill: parent; color: "#1a1a1a"; border.color: "#333"
                    Rectangle {
                        height: parent.height - 2; y: 1; x: 1
                        width: (parent.width - 2) * (Math.max(0, Math.min(analyzer.rms, 100)) / 100)
                        color: "#aaaaaa"
                        Behavior on width { NumberAnimation { duration: 60; easing.type: Easing.OutQuad } }
                    }
                }

                RangeSlider {
                    id: rmsSlider
                    anchors.fill: parent; visible: isCustomMode; from: 0.0; to: 1.0
                    first.value: 0.0; second.value: 0.25
                    first.onValueChanged: analyzer.setRmsThresholds(first.value, second.value)
                    second.onValueChanged: analyzer.setRmsThresholds(first.value, second.value)

                    background: Item {}
                    first.handle: Rectangle { x: rmsSlider.first.visualPosition * (rmsSlider.availableWidth - width); y: -5; width: 4; height: parent.height + 10; color: "#ff3333"; border.color: "black" }
                    second.handle: Rectangle { x: rmsSlider.second.visualPosition * (rmsSlider.availableWidth - width); y: -5; width: 4; height: parent.height + 10; color: "#00ff00"; border.color: "black" }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Repeater {
                model: ["SUB", "BASS", "LMID", "MID", "HMID", "TREB", "AIR"]
                delegate: ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                    property int bandIndex: index
                    property real liveValue: {
                        switch(index) {
                            case 0: return analyzer.subBass; case 1: return analyzer.bass;
                            case 2: return analyzer.lowMid;  case 3: return analyzer.mid;
                            case 4: return analyzer.highMid; case 5: return analyzer.treble;
                            case 6: return analyzer.air;     default: return 0;
                        }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Rectangle { anchors.fill: parent; color: "#1a1a1a" }
                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                            height: parent.height * (Math.max(0, Math.min(parent.parent.liveValue, 100)) / 100)
                            color: isCustomMode ? "#555" : "#999"
                            Behavior on height { NumberAnimation { duration: 80; easing.type: Easing.Linear } }
                        }

                        RangeSlider {
                            id: bandSlider
                            anchors.fill: parent; orientation: Qt.Vertical; visible: isCustomMode
                            from: 0.0; to: 1.0; first.value: 0.0; second.value: 0.7
                            first.onValueChanged: analyzer.setBandThresholds(bandIndex, first.value, second.value)
                            second.onValueChanged: analyzer.setBandThresholds(bandIndex, first.value, second.value)

                            background: Item {}
                            first.handle: Rectangle { y: bandSlider.first.visualPosition * (bandSlider.availableHeight - height); width: parent.width + 4; x: -2; height: 4; color: "#ff3333" }
                            second.handle: Rectangle { y: bandSlider.second.visualPosition * (bandSlider.availableHeight - height); width: parent.width + 4; x: -2; height: 4; color: "#00ff00" }
                            Rectangle { x: (parent.width / 2) - 1; y: bandSlider.second.visualPosition * bandSlider.availableHeight; width: 2; height: (bandSlider.first.visualPosition * bandSlider.availableHeight) - y; color: "white"; opacity: 0.2 }
                        }
                    }
                    Text { Layout.alignment: Qt.AlignHCenter; text: modelData; color: "white"; font.pixelSize: 10; font.bold: true }
                }
            }
        }
    }
}
