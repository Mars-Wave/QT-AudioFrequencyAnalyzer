#pragma once
#include <QObject>
#include "AudioEngine.h"
#include "inputs/MicrophoneInput.h"
#include "inputs/FileInput.h" // <--- Updated Include
#include "inputs/IpAudioInput.h"

class AnalyzerInterface : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int inputMode READ inputMode WRITE setInputMode NOTIFY inputModeChanged)

    // Modules
    Q_PROPERTY(MicrophoneInput* micInput READ micInput CONSTANT)
    Q_PROPERTY(FileInput* fileInput READ fileInput CONSTANT)
    Q_PROPERTY(IpAudioInput* ipInput READ ipInput CONSTANT)

    // Visual Data (Unchanged)
    Q_PROPERTY(float sensitivity READ sensitivity WRITE setSensitivity NOTIFY settingsChanged)
    Q_PROPERTY(float gain READ gain WRITE setGain NOTIFY settingsChanged)
    Q_PROPERTY(int normalizationMode READ normalizationMode WRITE setNormalizationMode NOTIFY settingsChanged)
    Q_PROPERTY(float rms READ rms NOTIFY statsChanged)
    Q_PROPERTY(float subBass READ subBass NOTIFY statsChanged)
    Q_PROPERTY(float bass READ bass NOTIFY statsChanged)
    Q_PROPERTY(float lowMid READ lowMid NOTIFY statsChanged)
    Q_PROPERTY(float mid READ mid NOTIFY statsChanged)
    Q_PROPERTY(float highMid READ highMid NOTIFY statsChanged)
    Q_PROPERTY(float treble READ treble NOTIFY statsChanged)
    Q_PROPERTY(float air READ air NOTIFY statsChanged)

public:
    enum InputMode {
        MODE_MIC = 0,
        MODE_FILE = 1,
        MODE_IP = 2
    };
    Q_ENUM(InputMode)

    explicit AnalyzerInterface(QObject *parent = nullptr);

    MicrophoneInput* micInput() const { return m_micInput; }
    FileInput* fileInput() const { return m_fileInput; }
    IpAudioInput* ipInput() const { return m_ipInput; }

    int inputMode() const { return m_inputMode; }
    Q_INVOKABLE void setInputMode(int mode);

    // Getters for Stats
    float rms() const { return m_rms; }
    float subBass() const { return m_subBass; }
    float bass() const { return m_bass; }
    float lowMid() const { return m_lowMid; }
    float mid() const { return m_mid; }
    float highMid() const { return m_highMid; }
    float treble() const { return m_treble; }
    float air() const { return m_air; }

    // Getters for Settings
    float sensitivity() const {return m_sensitivity; }
    float gain() const {return m_gain; }
    float normalizationMode() const {return m_normalizationMode; }

    // Setters
    void setSensitivity(float v) { if(m_sensitivity == v) return; m_sensitivity = v; m_engine->setDecay(v); emit settingsChanged(); }
    void setGain(float v) { if(m_gain == v) return; m_gain = v; m_engine->setGain(v); emit settingsChanged(); }
    void setNormalizationMode(int v) { if(m_normalizationMode == v) return; m_normalizationMode = v; m_engine->setNormalizationMode(v); emit settingsChanged(); }

    Q_INVOKABLE void setRmsThresholds(float min, float max) { m_engine->setCustomRmsRange(min, max); }
    Q_INVOKABLE void setBandThresholds(int index, float min, float max) { m_engine->setCustomBandRange(index, min, max); }

signals:
    void inputModeChanged();
    void statsChanged();
    void settingsChanged();

private slots:
    void onEngineData(float rms, float sb, float b, float lm, float m, float hm, float t, float a);

private:
    void updateInputRouting();

    AudioEngine* m_engine;
    MicrophoneInput* m_micInput;
    FileInput* m_fileInput;
    IpAudioInput* m_ipInput;

    int m_inputMode = -1;

    float m_rms=0, m_subBass=0, m_bass=0, m_lowMid=0, m_mid=0, m_highMid=0, m_treble=0, m_air=0;
    int m_normalizationMode = 1;
    float m_gain = 1.0f;
    float m_sensitivity = 0.5f;
};
