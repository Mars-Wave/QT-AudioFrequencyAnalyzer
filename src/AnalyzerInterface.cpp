#include "AnalyzerInterface.h"

AnalyzerInterface::AnalyzerInterface(QObject *parent) : QObject(parent)
{
    m_engine = new AudioEngine(this);

    m_micInput  = new MicrophoneInput(this);
    m_fileInput = new FileInput(this);
    m_ipInput   = new IpAudioInput(this);

    // Defaults
    m_sensitivity = 0.5f;
    m_engine->setDecay(m_sensitivity);
    m_engine->setGain(m_gain);
    m_engine->setNormalizationMode(m_normalizationMode);

    // Data Flow: Engine -> UI
    connect(m_engine, &AudioEngine::analysisReady, this, &AnalyzerInterface::onEngineData);

    // Auto-Reset Logic (Clear graphs when source changes)
    connect(m_micInput, &MicrophoneInput::currentIndexChanged, this, [this](){
        if(m_inputMode == MODE_MIC) m_engine->reset();
    });
    connect(m_ipInput, &IpAudioInput::listeningChanged, this, [this](){
        if(m_inputMode == MODE_IP && m_ipInput->listening()) m_engine->reset();
    });
    connect(m_fileInput, &FileInput::durationChanged, this, [this](){
        if(m_inputMode == MODE_FILE && m_fileInput->duration() > 0) m_engine->reset();
    });
}

void AnalyzerInterface::setInputMode(int mode) {
    if (m_inputMode == mode) return;

    // 1. Teardown previous mode
    if (m_inputMode == MODE_FILE) m_fileInput->stop();
    if (m_inputMode == MODE_IP)   m_ipInput->stopListening();
    if (m_inputMode == MODE_MIC)  m_micInput->stop();

    // 2. Set new mode
    m_inputMode = mode;
    m_engine->reset();
    updateInputRouting();

    emit inputModeChanged();
}

void AnalyzerInterface::updateInputRouting() {
    // Disconnect everything
    m_engine->disconnect(m_micInput, nullptr, nullptr, nullptr);
    m_engine->disconnect(m_fileInput, nullptr, nullptr, nullptr);
    m_engine->disconnect(m_ipInput, nullptr, nullptr, nullptr);

    // Connect and Start selected
    if (m_inputMode == MODE_MIC) {
        // Wiring
        connect(m_micInput, &MicrophoneInput::audioBufferReady,
                m_engine, &AudioEngine::processAudioBuffer);
        // Kickstart
        m_micInput->start();
    }
    else if (m_inputMode == MODE_FILE) {
        connect(m_fileInput, &FileInput::audioBufferReady,
                m_engine, &AudioEngine::processAudioBuffer);
    }
    else if (m_inputMode == MODE_IP) {
        connect(m_ipInput, &IpAudioInput::audioBufferReady,
                m_engine, &AudioEngine::processAudioBuffer);
    }
}

void AnalyzerInterface::onEngineData(float rms, float sb, float b, float lm, float m, float hm, float t, float a) {
    m_rms = rms;
    m_subBass = sb; m_bass = b; m_lowMid = lm; m_mid = m;
    m_highMid = hm; m_treble = t; m_air = a;
    emit statsChanged();
}
