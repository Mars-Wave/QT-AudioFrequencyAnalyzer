#pragma once

#include <QAudioFormat>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSemaphore>
#include <deque>
#include <QMediaPlayer>
#include <array>

#include "ArithmeticCircularQueue.h"
#include "AudioFFTworker.h"
#include "AudioRMSworker.h"

class QThread;
class QAudioBuffer;

enum AUDIO_NORMALIZATION_MODE  {
    REACTIVE, STABLE, CUSTOM, NumModes
};
static_assert(AUDIO_NORMALIZATION_MODE::NumModes == 3, "AUDIO_NORMALIZATION_MODE offers 3 modes");

class ThreadedAudioFrequencyAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit ThreadedAudioFrequencyAnalyzer(QObject* parent = nullptr): QObject(parent) {
        // Initialize arrays to safe default values. resetAnalyzer() will set proper run-time values.
        m_fftMinEver.fill(std::numeric_limits<float>::max());
        m_fftMaxEver.fill(0.0f);
        m_forcedFftMin.fill(0.0f);
        m_forcedFftMax.fill(1.0f);
    };
    ~ThreadedAudioFrequencyAnalyzer() override;

    inline bool canProcessSamples() const noexcept {
        return m_numWorkerThreads > 0 &&
               m_isReadyToProcessFFT &&
               (m_availableSlotsSemaphore.available() > 0);
    }

public slots:
    void initialize(int numFFTthreads);
    void configureAnalyzer(uint32_t fftChunkSize, uint16_t rmsChunkSize);
    void setRMSChunkSize(uint16_t newSize);
    void resetAnalyzer();
    void analyzeAudioBuffer(const QAudioBuffer& buffer);

    bool incThreads();
    bool decThreads();

    // --- Normalization Control ---
    void setNormalizationMode(AUDIO_NORMALIZATION_MODE mode);
    void loadPersistentMinMax(float rmsMin, float rmsMax, const QVector<float>& fftMins, const QVector<float>& fftMaxs);
    void setForcedRmsMinMax(float minVal, float maxVal);
    void setForcedFftMinMax(const QVector<float>& minVals, const QVector<float>& maxVals);

signals:
    void newRMSValue(float rmsValue);
    void newFFTValue(float subBass, float bass, float lowMid, float mid, float highMid, float treble, float air,
                     uint16_t fftSamplesLost);
    void newMinMaxValues(float rmsMin, float rmsMax, const QVector<float>& fftMins, const QVector<float>& fftMaxs);
    void analyzerError(const QString& statusMessage);

private slots:
    void onRMSValueReady(float rawRmsValue);
    void handleWorkerAnalysisComplete(qint64 sequenceNumber,
                                      float subBass, float bass, float lowMid, float mid,
                                      float highMid, float treble, float air);
private:
    void cleanupResources();
    void initializeFFTWorkers();
    void processMono  (const QAudioBuffer& buffer);
    void processStereo(const QAudioBuffer& buffer);

    // -- Audio Decoding & Pre-processing --
    QAudioFormat::SampleFormat m_sampleFormat        = QAudioFormat::Unknown;
    qreal                      m_fPeakValue          = 1.0                  ;
    float                      m_invPeakValue        = 1.0                  ;
    int                        m_iChannels           = -1                   ;
    uint32_t                   m_currentSampleRate   = 0                    ;
    bool                       m_isFormatKnown       = false                ;
    bool                       m_isReadyToProcessFFT = false                ;

    // -- RMS Calculation --
    uint32_t       m_rmsChunkSize   = 64     ;
    QThread*       m_rmsThread      = nullptr;
    AudioRMSworker* m_rmsWorker      = nullptr;
    QVector<float> m_rmsBatchBuffer = {}     ;

    // -- FFT Data Preparation --
    ArithmeticCircularQueue<float> m_fftSample = {}  ;
    uint32_t m_currentFFTChunkSize                     = 4096;
    uint32_t m_fftChunkExcedent                        = 0   ;
    uint16_t m_fftSamplesCurrentlyLost                 = 0   ;

    // -- Multithreading for FFT --
    QAtomicInt             m_numWorkerThreads        = {}          ;
    QList<QThread*>        m_threads                 = {}          ;
    QList<AudioFFTworker*> m_analyzers               = {}          ;
    QSemaphore             m_availableSlotsSemaphore = QSemaphore();
    QMutex m_dispatchMutex        = {};
    qint64 m_inputSequenceNumber  = 0 ;
    int    m_nextFFTworkerIndex   = 0 ;

    // --- Normalization members ---
    QMutex m_resultsMutex = {}; // Protects all normalization state and sequence numbers
    qint64 m_outputSequenceNumber = 0 ;
    std::atomic<AUDIO_NORMALIZATION_MODE> m_normalizationMode = {AUDIO_NORMALIZATION_MODE::REACTIVE};

    // Stable/Forced Mode Values
    float m_rmsMinEver = std::numeric_limits<float>::max(), m_rmsMaxEver = 0.0f;
    std::array<float, AudioFFTworker::FrequencyBand::NumBands> m_fftMinEver;
    std::array<float, AudioFFTworker::FrequencyBand::NumBands> m_fftMaxEver;
    float m_forcedRmsMin = 0.0f, m_forcedRmsMax = 1.0f;
    std::array<float, AudioFFTworker::FrequencyBand::NumBands> m_forcedFftMin;
    std::array<float, AudioFFTworker::FrequencyBand::NumBands> m_forcedFftMax;

    // Reactive Mode Trackers
    struct RollingValueTracker {
        struct DataPoint { qint64 timestampMs; float value; };
        explicit RollingValueTracker(qint64 windowDurationMs) : m_currentMaxValue(0.0f), m_currentMinValue(std::numeric_limits<float>::max()), m_windowDurationMs(windowDurationMs) {}
        explicit RollingValueTracker() : m_currentMaxValue(0.0f), m_currentMinValue(std::numeric_limits<float>::max()), m_windowDurationMs(ROLLING_MAX_WINDOW_MS) {}
        void addDataPoint(qint64 timestampMs, float newValue);
        inline float getMaxValue() const { return m_currentMaxValue; }
        inline float getMinValue() const { return m_currentMinValue; }
        inline void reset() { m_dataPoints.clear(); m_currentMaxValue = 0.0f; m_currentMinValue = std::numeric_limits<float>::max(); }
    private:
        std::deque<DataPoint> m_dataPoints;
        float m_currentMaxValue;
        float m_currentMinValue;
        qint64 m_windowDurationMs;
    };
    struct RollingBandValuesTracker  {
        struct BandDataPoint { qint64 timestampMs; std::array<float, AudioFFTworker::FrequencyBand::NumBands> values; };
        explicit RollingBandValuesTracker(qint64 windowDurationMs) : m_windowDurationMs(windowDurationMs) { reset(); }
        explicit RollingBandValuesTracker(): m_windowDurationMs(ROLLING_MAX_WINDOW_MS) { reset(); }
        void addDataPoint(qint64 timestampMs, const std::array<float, AudioFFTworker::FrequencyBand::NumBands>& newValues);
        const std::array<float, AudioFFTworker::FrequencyBand::NumBands>& getCurrentMaxValues() const { return m_currentMaxValues; }
        const std::array<float, AudioFFTworker::FrequencyBand::NumBands>& getCurrentMinValues() const { return m_currentMinValues; }
        void reset();
    private:
        std::deque<BandDataPoint> m_dataPoints;
        std::array<float, AudioFFTworker::FrequencyBand::NumBands> m_currentMaxValues;
        std::array<float, AudioFFTworker::FrequencyBand::NumBands> m_currentMinValues;
        qint64 m_windowDurationMs;
    };
    RollingBandValuesTracker m_rollingBandTracker;
    RollingValueTracker      m_rollingRmsTracker;

    static constexpr qint64 ROLLING_MAX_WINDOW_MS = 20000;
    static constexpr float EPSILON = 1e-6f;
};

Q_DECLARE_METATYPE(AUDIO_NORMALIZATION_MODE)
