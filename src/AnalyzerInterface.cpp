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

float AnalyzerInterface::getBandLevel(int band) const
{
    switch (static_cast<AudioFFTworker::FrequencyBand>(band)) {
    case AudioFFTworker::SubBass: return m_subBass;
    case AudioFFTworker::Bass:    return m_bass;
    case AudioFFTworker::LowMid:  return m_lowMid;
    case AudioFFTworker::Mid:     return m_mid;
    case AudioFFTworker::HighMid: return m_highMid;
    case AudioFFTworker::Treble:  return m_treble;
    case AudioFFTworker::Air:     return m_air;
    default:                      return 0.0f;
    }
}

QList<qreal> AnalyzerInterface::bandValues() const
{
    // Return a list constructed on the fly.
    // Since this happens at UI framerate (e.g. 60fps), the overhead is negligible.
    return {static_cast<qreal>(m_subBass),
            static_cast<qreal>(m_bass),
            static_cast<qreal>(m_lowMid),
            static_cast<qreal>(m_mid),
            static_cast<qreal>(m_highMid),
            static_cast<qreal>(m_treble),
            static_cast<qreal>(m_air)};
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
