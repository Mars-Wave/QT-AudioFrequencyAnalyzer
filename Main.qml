import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AudioAnalyzer 1.0

Window {
    width: 900
    height: 600
    visible: true
    title: "Threaded Audio Frequency Analyzer"
    color: "#111111"

    AnalyzerInterface {
        id: mainAnalyzer
        sensitivity: 0.5
        gain: 1.0
        normalizationMode: 1

        Component.onCompleted: {
            mainAnalyzer.setInputMode(mainAnalyzer.MODE_MIC)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        // 1. Input Section (Mic, File, IP)
        InputSelection {
            Layout.fillWidth: true
            analyzer: mainAnalyzer
        }

        // 2. Settings Section (Sensitivity, Gain, Norm Mode)
        AudioToggles {
            Layout.fillWidth: true
            analyzer: mainAnalyzer
        }

        // 3. Visualizer Section (RMS and Frequency Bars)
        Visualizer {
            Layout.fillWidth: true
            Layout.fillHeight: true
            analyzer: mainAnalyzer
        }
    }
}
