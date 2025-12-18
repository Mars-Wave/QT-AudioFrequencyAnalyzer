import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import AudioAnalyzer 1.0

RowLayout {
    spacing: 15

    // Dependency injection
    required property var analyzer

    // Helper function
    function formatTime(milliseconds) {
        if (milliseconds <= 0 || isNaN(milliseconds)) return "0:00";
        var totalSeconds = Math.floor(milliseconds / 1000);
        var minutes = Math.floor(totalSeconds / 60);
        var seconds = totalSeconds % 60;
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds;
    }

    FileDialog {
        id: fileDialog
        title: "Select Audio File"
        nameFilters: ["Audio files (*.mp3 *.wav *.ogg *.flac *.m4a)"]
        onAccepted: {
            analyzer.fileInput.setSource(selectedFile)
            analyzer.fileInput.play()
        }
    }

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
            contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.down ? "#222" : "#444"; radius: 4 }
        }

        Button {
            text: analyzer.fileInput.playing ? "Pause" : "Play"
            enabled: analyzer.fileInput.duration > 0
            onClicked: analyzer.fileInput.playing ? analyzer.fileInput.pause() : analyzer.fileInput.play()
            contentItem: Text { text: parent.text; color: parent.enabled ? "white" : "#555"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: !parent.enabled ? "#222" : (analyzer.fileInput.playing ? "#cc3333" : "#33cc33"); radius: 4 }
        }

        ColumnLayout {
            spacing: 5
            CheckBox {
                text: "Monitor Output"
                checked: analyzer.fileInput.monitoring
                onToggled: analyzer.fileInput.monitoring = checked
                contentItem: Text { text: parent.text; color: parent.checked ? "white" : "#666"; leftPadding: 30; verticalAlignment: Text.AlignVCenter }
            }
            RowLayout {
                visible: analyzer.fileInput.monitoring
                spacing: 5
                Label { text: "Vol:"; color: "#888" }
                Slider {
                    Layout.preferredWidth: 80
                    from: 0.0; to: 1.0
                    value: analyzer.fileInput.volume
                    onMoved: analyzer.fileInput.volume = value
                    // Simplified background for brevity
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: analyzer.fileInput.duration > 0

            Label {
                text: {
                    var url = analyzer.fileInput.source.toString();
                    return decodeURIComponent(url.substring(url.lastIndexOf('/') + 1));
                }
                color: "#aaa"; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true
            }

            Slider {
                Layout.fillWidth: true
                from: 0; to: analyzer.fileInput.duration
                value: analyzer.fileInput.position
                onMoved: analyzer.fileInput.seek(value)
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: formatTime(analyzer.fileInput.position); color: "#888"; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Label { text: formatTime(analyzer.fileInput.duration); color: "#888"; font.pixelSize: 9 }
            }
        }
    }

    // 4. IP Input Specifics
    RowLayout {
        visible: analyzer.inputMode === 2
        Layout.fillWidth: true
        spacing: 10

        Label { text: "Stream URL:"; color: "#666" }
        TextField {
            text: analyzer.ipInput.url
            placeholderText: "udp://127.0.0.1:5555"
            color: "white"; background: Rectangle { color: "#333" }
            Layout.fillWidth: true; Layout.maximumWidth: 300
            selectByMouse: true
            onEditingFinished: analyzer.ipInput.url = text
        }
        Button {
            text: analyzer.ipInput.listening ? "Stop" : "Connect"
            onClicked: analyzer.ipInput.listening ? analyzer.ipInput.stopListening() : analyzer.ipInput.startListening()
            contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: analyzer.ipInput.listening ? "#cc3333" : "#33cc33"; radius: 4 }
        }
        CheckBox {
            text: "Monitor"
            checked: analyzer.ipInput.monitoring
            onToggled: analyzer.ipInput.monitoring = checked
            contentItem: Text { text: parent.text; color: parent.checked ? "white" : "#666"; leftPadding: 30; verticalAlignment: Text.AlignVCenter }
        }
    }
}
