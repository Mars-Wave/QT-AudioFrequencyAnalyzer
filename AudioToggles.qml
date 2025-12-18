import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import AudioAnalyzer 1.0

RowLayout {
    spacing: 15
    required property var analyzer

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
