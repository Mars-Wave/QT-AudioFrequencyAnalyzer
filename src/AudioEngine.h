#pragma once
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QAudioBuffer>
#include <QMutex>
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

// Include your provided header
#include "processing/ThreadedAudioFrequencyAnalyzer.h"
#include "processing/ArithmeticCircularQueue.h"

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    void processAudioBuffer(const QAudioBuffer& buffer);

    // Call this to clear historical data (Reactive/Stable stats)
    void reset();

    // Standard Controls
    void setDecay(float decay01);
    void setGain(float gain01);
    void setNormalizationMode(int mode);

    // Custom Thresholds
    void setCustomRmsRange(float min01, float max01);
    void setCustomBandRange(int index, float min01, float max01);

signals:
    void analysisReady(float rms,
                       float subBass, float bass, float lowMid,
                       float mid, float highMid, float treble, float air);

    void requestAnalyzerRMSChunkSize(uint16_t newSize);

private slots:
    void onTick();
    void handleRawRMS(float val);
    void handleRawFFT(float sb, float b, float lm, float m, float hm, float t, float a, uint16_t lost);
    void onAnalyzerError(const QString& msg);

private:
    void adjustThreadLoad(uint16_t lostSamples);
    void adjustRmsChunkSize(int packetsReceived);
    void pushCustomRangesToAnalyzer();

    QThread* m_workerThread = nullptr;
    ThreadedAudioFrequencyAnalyzer* m_analyzer = nullptr;
    QTimer m_outputTimer;

    std::atomic<float> m_gain = {1.0f};
    double m_decay = 0.07;
    int m_currentMode = 0;

    static constexpr uint8_t SMOOTHING_QUEUESIZE = 4;
    ArithmeticCircularQueue<float> m_rmsQueue{SMOOTHING_QUEUESIZE};
    ArithmeticCircularQueue<float> m_bandQueues[7]; // 7 Bands

    float m_rawRms = 0;
    float m_rawBands[7] = {0};
    float m_smoothRms = 0;
    float m_smoothBands[7] = {0};

    QMutex m_rmsCountLock;
    int m_rmsPacketsSinceLastTick = 0;

    // Adaptive Constants
    uint16_t m_desiredRmsChunkSize = 64;
    static constexpr uint16_t FFT_CHUNK_SIZE = 4096;

    // Custom Range Storage
    static constexpr float BACKEND_ABSOLUTE_MAX = 100000.0f;
    static constexpr float BACKEND_DEFAULT_CEILING = 25000.0f;
    float m_customRmsMin = 0.0f;
    float m_customRmsMax = BACKEND_DEFAULT_CEILING;
    std::vector<float> m_customBandMins;
    std::vector<float> m_customBandMaxs;
};
