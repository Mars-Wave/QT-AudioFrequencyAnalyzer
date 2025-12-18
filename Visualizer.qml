import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import AudioAnalyzer 1.0

ColumnLayout {
    spacing: 5
    required property var analyzer

    property bool isCustomMode: analyzer.normalizationMode === 2

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

    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
        Repeater {
            model: ["SUB", "BASS", "LMID", "MID", "HMID", "TREB", "AIR"]

            delegate: ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                property int bandIndex: index
                property real liveValue: analyzer.bandValues[bandIndex]

                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Rectangle { anchors.fill: parent; color: "#1a1a1a" }
                    Rectangle {
                        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right

                        // Ensure we handle potential undefined values during initialization safely
                        height: parent.height * (Math.max(0, Math.min(parent.parent.liveValue || 0, 100)) / 100)

                        color: isCustomMode ? "#555" : "#999"
                        Behavior on height { NumberAnimation { duration: 10; easing.type: Easing.Linear } }
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
                Text {
                    Layout.alignment: Qt.AlignHCenter;
                    text: modelData; // The label from the array
                    color: "white"; font.pixelSize: 10; font.bold: true
                }
            }
        }
    }
}
