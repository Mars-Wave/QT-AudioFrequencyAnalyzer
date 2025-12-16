#include "AudioEngine.h"
#include <QDebug>

// Helper for power of 2
enum POWER { UP, DOWN };
static constexpr int getBoundedPowerOf2(int value, POWER increase, int minBound, int maxBound) {
    int result = (increase == POWER::UP) ? value * 2 : value / 2;
    return std::clamp(result, minBound, maxBound);
}

AudioEngine::AudioEngine(QObject *parent) : QObject(parent)
{
    m_customBandMins.assign(7, 0.0f);
    m_customBandMaxs.assign(7, 25000.0f);

    // Initialize Queues
    for(int i=0; i<7; i++) m_bandQueues[i].resize(SMOOTHING_QUEUESIZE);
    m_rmsQueue.resize(SMOOTHING_QUEUESIZE);

    m_workerThread = new QThread(this);
    m_analyzer = new ThreadedAudioFrequencyAnalyzer(); // No parent, moving to thread
    m_analyzer->moveToThread(m_workerThread);

    // Wire up Signals
    connect(m_analyzer, &ThreadedAudioFrequencyAnalyzer::newRMSValue, this, &AudioEngine::handleRawRMS);
    connect(m_analyzer, &ThreadedAudioFrequencyAnalyzer::newFFTValue, this, &AudioEngine::handleRawFFT);
    connect(m_analyzer, &ThreadedAudioFrequencyAnalyzer::analyzerError, this, &AudioEngine::onAnalyzerError);

    // Wire up dynamic config request
    connect(this, &AudioEngine::requestAnalyzerRMSChunkSize, m_analyzer, &ThreadedAudioFrequencyAnalyzer::setRMSChunkSize);

    m_workerThread->start();

    // --- INITIALIZATION SEQUENCE ---
    QMetaObject::invokeMethod(m_analyzer, "initialize", Qt::QueuedConnection, Q_ARG(int, 2));
    QMetaObject::invokeMethod(m_analyzer, "configureAnalyzer", Qt::QueuedConnection,
                              Q_ARG(uint32_t, FFT_CHUNK_SIZE),
                              Q_ARG(uint16_t, m_desiredRmsChunkSize));

    // Fix: Force push initial values so it works without moving sliders
    setNormalizationMode(AUDIO_NORMALIZATION_MODE::STABLE); // Default
    pushCustomRangesToAnalyzer();

    // Output Timer
    m_outputTimer.setInterval(44);
    m_outputTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_outputTimer, &QTimer::timeout, this, &AudioEngine::onTick);
    m_outputTimer.start();
}

AudioEngine::~AudioEngine() {
    m_outputTimer.stop();
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_analyzer;
}

void AudioEngine::reset() {
    // Invoke reset on the worker thread
    QMetaObject::invokeMethod(m_analyzer, "resetAnalyzer", Qt::QueuedConnection);

    // Clear local smoothing queues
    m_rmsQueue.clear();
    for(int i=0; i<7; i++) m_bandQueues[i].clear();
    m_smoothRms = 0;
    for(int i=0; i<7; i++) m_smoothBands[i] = 0;
}

void AudioEngine::processAudioBuffer(const QAudioBuffer& buffer) {
    QMetaObject::invokeMethod(m_analyzer, "analyzeAudioBuffer",
                              Qt::QueuedConnection, Q_ARG(QAudioBuffer, buffer));
}

void AudioEngine::setDecay(float decay01) {
    // Map 0-1 to 0.0001-9.0
    float mapped = 0.0001f + (decay01 * (9.0f - 0.0001f));
    m_decay = mapped;
}

void AudioEngine::setGain(float gain01) {
    m_gain.store(gain01);
}

void AudioEngine::setNormalizationMode(int mode) {
    m_currentMode = mode;
    auto enumMode = static_cast<AUDIO_NORMALIZATION_MODE>(mode);
    QMetaObject::invokeMethod(m_analyzer, "setNormalizationMode",
                              Qt::QueuedConnection,
                              Q_ARG(AUDIO_NORMALIZATION_MODE, enumMode));

    // Re-push custom ranges if switching to Custom
    if (enumMode == AUDIO_NORMALIZATION_MODE::CUSTOM) {
        pushCustomRangesToAnalyzer();
    }
}

void AudioEngine::setCustomRmsRange(float min01, float max01) {
    m_customRmsMin = min01 * BACKEND_ABSOLUTE_MAX;
    m_customRmsMax = max01 * BACKEND_ABSOLUTE_MAX;
    if (m_customRmsMax <= m_customRmsMin) m_customRmsMax = m_customRmsMin + 100.0f;

    QMetaObject::invokeMethod(m_analyzer, "setForcedRmsMinMax",
                              Qt::QueuedConnection,
                              Q_ARG(float, m_customRmsMin),
                              Q_ARG(float, m_customRmsMax));
}

void AudioEngine::setCustomBandRange(int index, float min01, float max01) {
    if (index < 0 || index >= 7) return;
    float curvedMin = std::pow(min01, 4.0f);
    float curvedMax = std::pow(max01, 4.0f);

    m_customBandMins[index] = curvedMin * BACKEND_ABSOLUTE_MAX;
    m_customBandMaxs[index] = curvedMax * BACKEND_ABSOLUTE_MAX;

    pushCustomRangesToAnalyzer();
}

void AudioEngine::pushCustomRangesToAnalyzer() {
    QVector<float> qMins(m_customBandMins.begin(), m_customBandMins.end());
    QVector<float> qMaxs(m_customBandMaxs.begin(), m_customBandMaxs.end());

    QMetaObject::invokeMethod(m_analyzer, "setForcedFftMinMax",
                              Qt::QueuedConnection,
                              Q_ARG(QVector<float>, qMins),
                              Q_ARG(QVector<float>, qMaxs));
}

void AudioEngine::handleRawRMS(float val) {
    m_rawRms = val;
    QMutexLocker locker(&m_rmsCountLock);
    m_rmsPacketsSinceLastTick++;
}

void AudioEngine::handleRawFFT(float sb, float b, float lm, float m, float hm, float t, float a, uint16_t lost) {
    m_rawBands[0] = sb; m_rawBands[1] = b; m_rawBands[2] = lm;
    m_rawBands[3] = m;  m_rawBands[4] = hm; m_rawBands[5] = t; m_rawBands[6] = a;
    adjustThreadLoad(lost);
}

void AudioEngine::onAnalyzerError(const QString& msg) {
    qWarning() << "Analyzer Error:" << msg;
}

void AudioEngine::adjustThreadLoad(uint16_t lostSamples) {
    // (Logic identical to previous implementation)
    int maxLosable = 1 + FFT_CHUNK_SIZE / 16;
    static int lossAbove = 0;
    static int lossBelow = 0;

    if (lostSamples > maxLosable) {
        lossAbove++; lossBelow = 0;
        if (lossAbove >= 1) {
            QMetaObject::invokeMethod(m_analyzer, "incThreads", Qt::QueuedConnection);
            lossAbove = 0;
        }
    } else if (lostSamples == 0) {
        lossBelow++; lossAbove = 0;
        if (lossBelow >= 255) {
            QMetaObject::invokeMethod(m_analyzer, "decThreads", Qt::QueuedConnection);
            lossBelow = 0;
        }
    }
}

void AudioEngine::adjustRmsChunkSize(int packetsReceived) {
    // (Simple logic to keep RMS responsive)
    if (packetsReceived > 16) {
        m_desiredRmsChunkSize = std::min(m_desiredRmsChunkSize * 2, 8192);
        emit requestAnalyzerRMSChunkSize(m_desiredRmsChunkSize);
    } else if (packetsReceived < 4) {
        m_desiredRmsChunkSize = std::max(m_desiredRmsChunkSize / 2, 32);
        emit requestAnalyzerRMSChunkSize(m_desiredRmsChunkSize);
    }
}

void AudioEngine::onTick() {
    int packetsThisTick = 0;
    {
        QMutexLocker locker(&m_rmsCountLock);
        packetsThisTick = m_rmsPacketsSinceLastTick;
        m_rmsPacketsSinceLastTick = 0;
    }
    adjustRmsChunkSize(packetsThisTick);

    auto smoothAndDecay = [&](float rawInput, float &smoothState, ArithmeticCircularQueue<float>& queue) -> float {
        float target = rawInput * m_gain.load();
        const double currentDecayFactor = (m_decay * m_decay) / 50.0;
        smoothState += (target - smoothState) * currentDecayFactor;
        queue.enqueue(smoothState);
        return std::clamp(queue.getMean(), 0.0f, 100.0f);
    };

    float outRms = smoothAndDecay(m_rawRms, m_smoothRms, m_rmsQueue);
    float outBands[7];
    for(int i=0; i<7; i++) {
        outBands[i] = smoothAndDecay(m_rawBands[i], m_smoothBands[i], m_bandQueues[i]);
    }

    emit analysisReady(outRms, outBands[0], outBands[1], outBands[2],
                       outBands[3], outBands[4], outBands[5], outBands[6]);
}
