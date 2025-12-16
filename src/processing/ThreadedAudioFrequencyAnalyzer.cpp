#include "ThreadedAudioFrequencyAnalyzer.h"
#include <QDebug>
#include <QThread>
#include <QAudioBuffer>
#include <QDateTime>

namespace {
float mapToRange(float inValue, float minInRange, float maxInRange, float minOutRange, float maxOutRange, bool safeMap = false) {

    if (safeMap) {
        if (inValue < minInRange) {
            inValue = minInRange;
        }

        if (inValue > maxInRange) {
            inValue = maxInRange;
        }

        if (qFuzzyCompare(maxInRange, minInRange)) {
            return maxInRange;
        }
    }

    Q_ASSERT(!qFuzzyCompare(maxInRange, minInRange));

    double slope = (maxOutRange - minOutRange) / (maxInRange - minInRange);
    return minOutRange + slope * (inValue - minInRange);
}
}

// --- Rolling Tracker Implementation ---
void ThreadedAudioFrequencyAnalyzer::RollingValueTracker::addDataPoint(qint64 timestampMs, float newValue) {
    while (!m_dataPoints.empty() && (timestampMs - m_dataPoints.front().timestampMs > m_windowDurationMs)) { m_dataPoints.pop_front(); }
    m_dataPoints.push_back({timestampMs, newValue});

    m_currentMaxValue = 0.0f;
    m_currentMinValue = std::numeric_limits<float>::max();

    for (const auto& point : m_dataPoints) {
        m_currentMaxValue = std::max(m_currentMaxValue, point.value);
        m_currentMinValue = std::min(m_currentMinValue, point.value);
    }
}
void ThreadedAudioFrequencyAnalyzer::RollingBandValuesTracker::addDataPoint(
    qint64 timestampMs, const std::array<float, AudioFFTworker::FrequencyBand::NumBands>& newValues) {
    while (!m_dataPoints.empty() && (timestampMs - m_dataPoints.front().timestampMs > m_windowDurationMs)) { m_dataPoints.pop_front(); }
    m_dataPoints.push_back({timestampMs, newValues});
    m_currentMaxValues.fill(0.0f);
    m_currentMinValues.fill(std::numeric_limits<float>::max());
    for (const auto& dataPoint : m_dataPoints) {
        for (size_t i = 0; i < AudioFFTworker::FrequencyBand::NumBands; ++i) {
            m_currentMaxValues[i] = std::max(m_currentMaxValues[i], dataPoint.values[i]);
            m_currentMinValues[i] = std::min(m_currentMinValues[i], dataPoint.values[i]);
        }
    }
}
void ThreadedAudioFrequencyAnalyzer::RollingBandValuesTracker::reset() {
    m_dataPoints.clear();
    m_currentMaxValues.fill(0.0f);
    m_currentMinValues.fill(std::numeric_limits<float>::max());
}
// --- End of Rolling Tracker Implementation ---

//---------------------------------------------------------------------------------------------------------------

// --- Normalization Control ---
void ThreadedAudioFrequencyAnalyzer::setNormalizationMode(AUDIO_NORMALIZATION_MODE mode) {
    {
        QMutexLocker lock(&m_dispatchMutex);
        QMutexLocker lock2(&m_resultsMutex);
        m_rollingBandTracker.reset();
        m_rollingRmsTracker.reset();
    }
    m_normalizationMode.store(mode);
}
void ThreadedAudioFrequencyAnalyzer::loadPersistentMinMax(float rmsMin, float rmsMax, const QVector<float>& fftMins, const QVector<float>& fftMaxs) {
    QMutexLocker lock(&m_resultsMutex);
    m_rmsMinEver = rmsMin;
    m_rmsMaxEver = rmsMax;
    if (fftMins.size() == AudioFFTworker::FrequencyBand::NumBands && fftMaxs.size() == AudioFFTworker::FrequencyBand::NumBands) {
        std::copy(fftMins.begin(), fftMins.end(), m_fftMinEver.begin());
        std::copy(fftMaxs.begin(), fftMaxs.end(), m_fftMaxEver.begin());
    } else {
        qWarning() << "loadPersistentMinMax: Invalid vector sizes provided. Expected" << AudioFFTworker::FrequencyBand::NumBands;
    }
}
void ThreadedAudioFrequencyAnalyzer::setForcedRmsMinMax(float minVal, float maxVal) {
    QMutexLocker lock(&m_resultsMutex);
    m_forcedRmsMin = minVal;
    m_forcedRmsMax = maxVal;
}
void ThreadedAudioFrequencyAnalyzer::setForcedFftMinMax(const QVector<float>& minVals, const QVector<float>& maxVals) {
    QMutexLocker lock(&m_resultsMutex);
    if (minVals.size() == AudioFFTworker::FrequencyBand::NumBands && maxVals.size() == AudioFFTworker::FrequencyBand::NumBands) {
        std::copy(minVals.begin(), minVals.end(), m_forcedFftMin.begin());
        std::copy(maxVals.begin(), maxVals.end(), m_forcedFftMax.begin());
    } else {
        qWarning() << "setForcedFftMinMax: Invalid vector sizes provided. Expected" << AudioFFTworker::FrequencyBand::NumBands;
    }
}
// --- End of Normalization Control ---


//---------------------------------------------------------------------------------------------------------------

ThreadedAudioFrequencyAnalyzer::~ThreadedAudioFrequencyAnalyzer() {
    qInfo() << "ThreadedAudioFrequencyAnalyzer: Shutting down...";
    cleanupResources();
    qInfo() << "ThreadedAudioFrequencyAnalyzer: Shutdown complete.";
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::initialize(int numFFTthreads){
    QMutexLocker lock(&m_resultsMutex);
    m_rollingBandTracker = RollingBandValuesTracker(ROLLING_MAX_WINDOW_MS);
    m_rollingRmsTracker = RollingValueTracker(ROLLING_MAX_WINDOW_MS);
    lock.unlock();

    // Initialize RMS Worker
    m_rmsThread = new QThread(this);
    m_rmsWorker = new AudioRMSworker();
    m_rmsWorker->moveToThread(m_rmsThread);
    connect(m_rmsThread, &QThread::finished, m_rmsWorker, &QObject::deleteLater);
    connect(m_rmsWorker, &AudioRMSworker::rmsValueCalculated, this, &ThreadedAudioFrequencyAnalyzer::onRMSValueReady);
    m_rmsThread->setObjectName(u"RMSWorkerThread"_f);
    m_rmsThread->start();

    // -- FFT threads --
    int initialFFTthreads = numFFTthreads > 0 ? numFFTthreads : QThread::idealThreadCount();
    initialFFTthreads = initialFFTthreads > 0 ? initialFFTthreads : 1;
    for (int i = 0; i < initialFFTthreads; ++i) {
        if (!incThreads()) {
            qWarning() << "ThreadedAudioFrequencyAnalyzer: Failed to create initial FFT worker thread" << i;
        }
    }
    qInfo() << "ThreadedAudioFrequencyAnalyzer: Started with" << m_numWorkerThreads.loadAcquire() << "FFT worker threads and 1 RMS worker thread.";
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::cleanupResources() {
    QMutexLocker lock(&m_dispatchMutex);
    QMutexLocker lock2(&m_resultsMutex);
    m_isReadyToProcessFFT = false; // Stop processing
    m_isFormatKnown = false;

    for (QThread* thread : std::as_const(m_threads)) {
        if (thread->isRunning()) {
            int threadIndex = m_threads.indexOf(thread);
            if (threadIndex != -1 && threadIndex < m_analyzers.size()) {
                AudioFFTworker* analyzer = m_analyzers.at(threadIndex);
                QMetaObject::invokeMethod(analyzer, "markForShutdown", Qt::QueuedConnection);
            }
            thread->quit();
        }
    }

    if (m_rmsWorker && m_rmsThread->isRunning()) {
        QMetaObject::invokeMethod(m_rmsWorker, "markForShutdown", Qt::QueuedConnection);
    }
    if (m_rmsThread && m_rmsThread->isRunning()) {
        m_rmsThread->quit();
    }

    for (QThread* thread : std::as_const(m_threads)) {
        if (!thread->wait(1000)) {
            qWarning() << "FFT Worker Thread" << thread->objectName() << "did not finish gracefully. Terminating.";
            thread->terminate(); thread->wait();
        }
    }

    if (m_rmsThread) {
        if (!m_rmsThread->wait(1000)) {
            qWarning() << "RMS Worker Thread did not finish gracefully. Terminating.";
            m_rmsThread->terminate();
            m_rmsThread->wait();
        }
    }// m_rmsWorker is deleted via QThread::finished connection, m_rmsThread is deleted as child of this.

    m_analyzers.clear();
    m_threads.clear();
    m_numWorkerThreads.storeRelaxed(0);
    m_availableSlotsSemaphore.tryAcquire(m_availableSlotsSemaphore.available()); // Drain semaphore
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::configureAnalyzer(uint32_t fftChunkSize, uint16_t rmsChunkSize) {
    QMutexLocker lock(&m_dispatchMutex);
    QMutexLocker lock2(&m_resultsMutex);
    m_currentFFTChunkSize = fftChunkSize > 0 ? fftChunkSize : 4096;
    m_rmsChunkSize = rmsChunkSize > 0 ? rmsChunkSize : 64;

    m_fftSample.resize(m_currentFFTChunkSize);
    m_fftSample.clear();
    m_rmsBatchBuffer.clear();
    m_rmsBatchBuffer.reserve(m_rmsChunkSize);
    m_rollingBandTracker.reset();
    m_rollingRmsTracker.reset();

    m_isReadyToProcessFFT = false;
    m_isFormatKnown = false;
    m_iChannels = -1;
    m_currentSampleRate = 0;

    if (m_rmsWorker) {
        QMetaObject::invokeMethod(m_rmsWorker, "initialize", Qt::QueuedConnection,
                                  Q_ARG(uint16_t, m_rmsChunkSize));
    }
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::resetAnalyzer() {
    QMutexLocker lock(&m_dispatchMutex);
    QMutexLocker lock2(&m_resultsMutex);
    m_isReadyToProcessFFT = false;
    m_isFormatKnown = false;
    m_iChannels = -1;
    m_currentSampleRate = 0;
    m_fftSample.clear();
    m_rmsBatchBuffer.clear();
    m_rollingBandTracker.reset();
    m_rollingRmsTracker.reset();
    m_inputSequenceNumber  = 0;
    m_outputSequenceNumber = 0;
    m_rmsMinEver = std::numeric_limits<float>::max(); m_rmsMaxEver = 0.0f;
    m_fftMinEver.fill(std::numeric_limits<float>::max());
    m_fftMaxEver.fill(0.0f);
    m_forcedFftMin.fill(0.0f);
    m_forcedFftMax.fill(1.0f);

    if (m_rmsWorker) {
        QMetaObject::invokeMethod(m_rmsWorker, "initialize", Qt::QueuedConnection, Q_ARG(uint16_t, m_rmsChunkSize));
    }

    emit newFFTValue(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0);
    emit newRMSValue(0.f);
    // Let Min/Max values be handled by UI service, since they have access to "minimumDefault" and "maximumDefault" values
    // emit newMinMaxValues(m_rmsMinEver, m_rmsMaxEver,
    //                     QVector<float>(m_fftMinEver.begin(), m_fftMinEver.end()),
    //                     QVector<float>(m_fftMaxEver.begin(), m_fftMaxEver.end()));
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::initializeFFTWorkers()
{
    QMutexLocker lock(&m_dispatchMutex);
    if (m_currentSampleRate == 0 || m_currentFFTChunkSize == 0) {
        qWarning() << "ThreadedAudioFrequencyAnalyzer: Cannot initialize FFT workers, invalid sample rate or FFT chunk size.";
        m_isReadyToProcessFFT = false;
        return;
    }

    qInfo() << "ThreadedAudioFrequencyAnalyzer: Initializing/Re-initializing FFT workers. Sample Rate:"
            << m_currentSampleRate << "FFT Chunk Size:" << m_currentFFTChunkSize;

    for (AudioFFTworker* analyzer : std::as_const(m_analyzers)) {
        bool success = QMetaObject::invokeMethod(analyzer, "initialize", Qt::QueuedConnection,
                                                 Q_ARG(uint32_t, m_currentSampleRate),
                                                 Q_ARG(uint32_t, m_currentFFTChunkSize));
        if (!success) {
            qWarning() << "Failed to invoke initialize on FFT worker analyzer" << (analyzer ? analyzer->objectName() : "unknown");
        }
    }
    m_isReadyToProcessFFT = true;
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::analyzeAudioBuffer(const QAudioBuffer& buffer)
{
    if (buffer.frameCount() == 0) return;

    if (!m_isFormatKnown) {
        const QAudioFormat& format = buffer.format();
        m_iChannels = format.channelCount();
        m_sampleFormat = format.sampleFormat();

        QMutexLocker lock(&m_dispatchMutex);
        m_currentSampleRate = format.sampleRate();
        lock.unlock();

        if (m_iChannels != 1 && m_iChannels != 2) {
            emit analyzerError(QString(u"Unsupported channel count: %1"_f).arg(m_iChannels));
            resetAnalyzer(); return;
        }
        if (m_currentSampleRate <= 0) {
            emit analyzerError(QString(u"Invalid sample rate: %1"_f).arg(m_currentSampleRate));
            resetAnalyzer(); return;
        }

        switch (m_sampleFormat) {
        case QAudioFormat::Int32: m_fPeakValue = static_cast<qreal>(std::numeric_limits<qint32>::max()); break;
        case QAudioFormat::Int16: m_fPeakValue = static_cast<qreal>(std::numeric_limits<qint16>::max()); break;
        case QAudioFormat::UInt8: m_fPeakValue = 127.0; break;
        case QAudioFormat::Float: m_fPeakValue = 1.0; break;
        default:
            emit analyzerError(QString(u"Unsupported sample format: %1"_f).arg(m_sampleFormat));
            resetAnalyzer(); return;
        }
        m_invPeakValue = (m_fPeakValue != 0.0) ? (1.0f / static_cast<float>(m_fPeakValue)) : 1.0f;
        m_isFormatKnown = true;

        initializeFFTWorkers();
    }

    if (!m_isReadyToProcessFFT) {
        return;
    }

    if (m_iChannels == 1) processMono(buffer);
    else if (m_iChannels == 2) processStereo(buffer);
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::processMono(const QAudioBuffer& buffer)
{
    const int frameCount = buffer.frameCount();
    const float* floatData = nullptr;
    const qint32* s32Data = nullptr;
    const qint16* s16Data = nullptr;
    const quint8* u8Data = nullptr;

    if (m_sampleFormat == QAudioFormat::Float) floatData = buffer.constData<float>();
    else if (m_sampleFormat == QAudioFormat::Int32) s32Data = buffer.constData<qint32>();
    else if (m_sampleFormat == QAudioFormat::Int16) s16Data = buffer.constData<qint16>();
    else if (m_sampleFormat == QAudioFormat::UInt8) u8Data = buffer.constData<quint8>();
    else return;

    uint16_t currentRmsChunkSize;
    {
        QMutexLocker lock(&m_dispatchMutex);
        currentRmsChunkSize = m_rmsChunkSize;
    }

    for (int i = 0; i < frameCount; ++i) {
        float sampleValue = 0.0f;
        if (floatData) sampleValue = floatData[i];
        else if (s32Data) sampleValue = static_cast<float>(s32Data[i]);
        else if (s16Data) sampleValue = static_cast<float>(s16Data[i]);
        else if (u8Data) sampleValue = static_cast<float>(u8Data[i]) - 128.0f;

        sampleValue *= m_invPeakValue;

        m_rmsBatchBuffer.append(sampleValue);
        m_fftSample.enqueue(sampleValue);
        m_fftChunkExcedent++;

        if (static_cast<uint32_t>(m_rmsBatchBuffer.size()) >= currentRmsChunkSize) {
            if (m_rmsWorker) {
                QMetaObject::invokeMethod(m_rmsWorker, "processSampleBatch", Qt::QueuedConnection, Q_ARG(QVector<float>, m_rmsBatchBuffer));
            }
            m_rmsBatchBuffer.clear();
        }

        if (m_fftChunkExcedent >= m_currentFFTChunkSize) {
            if (canProcessSamples()) {
                QMutexLocker locker(&m_dispatchMutex);
                if (m_analyzers.isEmpty()) {
                    m_fftSamplesCurrentlyLost += m_fftChunkExcedent;
                    m_fftChunkExcedent = 0;
                    continue;
                }
                m_availableSlotsSemaphore.acquire();
                m_fftChunkExcedent = 0;
                m_fftSamplesCurrentlyLost = 0;
                QMetaObject::invokeMethod(m_analyzers[m_nextFFTworkerIndex],
                                          "processSamples",
                                          Qt::QueuedConnection,
                                          Q_ARG(ArithmeticCircularQueue<float>, m_fftSample),
                                          Q_ARG(qint64, m_inputSequenceNumber));
                m_nextFFTworkerIndex = (m_nextFFTworkerIndex + 1) % m_numWorkerThreads.loadAcquire();
                m_inputSequenceNumber++;
            } else {
                m_fftSamplesCurrentlyLost += m_fftChunkExcedent;
            }
        }
    }
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::processStereo(const QAudioBuffer& buffer)
{
    const int frameCount = buffer.frameCount();
    const QAudioBuffer::S32S* s32sData = nullptr;
    const QAudioBuffer::S16S* s16sData = nullptr;
    const QAudioBuffer::U8S* u8sData = nullptr;
    const float* floatData = nullptr;

    uint16_t currentRmsChunkSize;
    {
        QMutexLocker lock(&m_dispatchMutex);
        currentRmsChunkSize = m_rmsChunkSize;
    }

    if (m_sampleFormat == QAudioFormat::Float) floatData = buffer.constData<float>();
    else if (m_sampleFormat == QAudioFormat::Int32) s32sData = buffer.constData<QAudioBuffer::S32S>();
    else if (m_sampleFormat == QAudioFormat::Int16) s16sData = buffer.constData<QAudioBuffer::S16S>();
    else if (m_sampleFormat == QAudioFormat::UInt8) u8sData = buffer.constData<QAudioBuffer::U8S>();
    else return;

    for (int i = 0; i < frameCount; ++i) {
        float left = 0.0f, right = 0.0f;
        if (floatData) { left = floatData[i*2]; right = floatData[i*2+1]; }
        else if (s32sData) { left = static_cast<float>(s32sData[i].channels[0]); right = static_cast<float>(s32sData[i].channels[1]); }
        else if (s16sData) { left = static_cast<float>(s16sData[i].channels[0]); right = static_cast<float>(s16sData[i].channels[1]); }
        else if (u8sData) { left = static_cast<float>(u8sData[i].channels[0]) - 128.0f; right = static_cast<float>(u8sData[i].channels[1]) - 128.0f; }

        float sampleValue = (left + right) * 0.5f * m_invPeakValue;

        m_rmsBatchBuffer.append(sampleValue);
        m_fftSample.enqueue(sampleValue);
        m_fftChunkExcedent++;

        if (static_cast<uint32_t>(m_rmsBatchBuffer.size()) >= currentRmsChunkSize) {
            if (m_rmsWorker) {
                QMetaObject::invokeMethod(m_rmsWorker, "processSampleBatch", Qt::QueuedConnection, Q_ARG(QVector<float>, m_rmsBatchBuffer));
            }
            m_rmsBatchBuffer.clear();
        }

        if (m_fftChunkExcedent >= m_currentFFTChunkSize) {
            if (canProcessSamples()) {
                QMutexLocker locker(&m_dispatchMutex);
                if (m_analyzers.isEmpty()) {
                    m_fftSamplesCurrentlyLost += m_fftChunkExcedent;
                    m_fftChunkExcedent = 0;
                    continue;
                }
                m_availableSlotsSemaphore.acquire();
                m_fftChunkExcedent = 0;
                m_fftSamplesCurrentlyLost = 0;
                QMetaObject::invokeMethod(m_analyzers[m_nextFFTworkerIndex],
                                          "processSamples",
                                          Q_ARG(ArithmeticCircularQueue<float>, m_fftSample),
                                          Q_ARG(qint64, m_inputSequenceNumber));
                m_nextFFTworkerIndex = (m_nextFFTworkerIndex + 1) % m_numWorkerThreads.loadAcquire();
                m_inputSequenceNumber++;
            } else {
                m_fftSamplesCurrentlyLost += m_fftChunkExcedent;
            }
        }
    }
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::setRMSChunkSize(uint16_t newSize)
{
    QMutexLocker lock(&m_dispatchMutex);
    m_rmsChunkSize = newSize;
    m_rmsBatchBuffer.clear();
    m_rmsBatchBuffer.reserve(m_rmsChunkSize);

    if (m_rmsWorker) {
        QMetaObject::invokeMethod(m_rmsWorker, "initialize", Qt::QueuedConnection, Q_ARG(uint16_t, m_rmsChunkSize));
    }
    lock.unlock();
    qDebug() << "ThreadedAudioFrequencyAnalyzer: RMS chunk size set to" << newSize;
}

//---------------------------------------------------------------------------------------------------------------

bool ThreadedAudioFrequencyAnalyzer::incThreads() {
    QMutexLocker lock(&m_dispatchMutex);

    if (m_numWorkerThreads.loadAcquire() >= QThread::idealThreadCount() * 2) {
        return false;
    }

    QThread* thread = new QThread();
    AudioFFTworker* fftWorker = new AudioFFTworker(this);
    fftWorker->moveToThread(thread);

    connect(fftWorker, &AudioFFTworker::fftValueCalculated, this, &ThreadedAudioFrequencyAnalyzer::handleWorkerAnalysisComplete);
    connect(thread, &QThread::finished, fftWorker, &QObject::deleteLater);

    thread->setObjectName(QString(u"FFTWorker-%1"_f).arg(m_threads.count()+1));
    m_threads.append(thread);
    m_analyzers.append(fftWorker);

    bool needsInit = (m_currentSampleRate > 0 && m_currentFFTChunkSize > 0);
    uint32_t sr = m_currentSampleRate;
    uint32_t cs = m_currentFFTChunkSize;

    lock.unlock();

    thread->start();

    if (needsInit) {
        QMetaObject::invokeMethod(fftWorker, "initialize", Qt::QueuedConnection, Q_ARG(uint32_t, sr), Q_ARG(uint32_t, cs));
    }

    lock.relock();
    m_numWorkerThreads.fetchAndAddRelaxed(1);
    m_availableSlotsSemaphore.release();
    lock.unlock();

    qDebug() << "ThreadedAudioFrequencyAnalyzer: Increased FFT workers to" << m_numWorkerThreads.loadAcquire();
    return true;
}

//---------------------------------------------------------------------------------------------------------------

bool ThreadedAudioFrequencyAnalyzer::decThreads() {
    QMutexLocker lock(&m_dispatchMutex);
    if (m_numWorkerThreads.loadAcquire() <= 1) return false;

    if (!m_availableSlotsSemaphore.tryAcquire()) {
        qWarning() << "ThreadedAudioFrequencyAnalyzer: Cannot decrease threads, no idle FFT worker slot available to remove.";
        return false;
    }

    QThread* threadToQuit = m_threads.takeLast();
    AudioFFTworker* analyzerToMark = m_analyzers.takeLast();
    m_numWorkerThreads.fetchAndSubRelaxed(1);

    if (m_nextFFTworkerIndex >= m_numWorkerThreads.loadAcquire() && m_numWorkerThreads.loadAcquire() > 0) {
        m_nextFFTworkerIndex %= m_numWorkerThreads.loadAcquire();
    }

    if (analyzerToMark) QMetaObject::invokeMethod(analyzerToMark, "markForShutdown", Qt::QueuedConnection);

    lock.unlock();

    if (threadToQuit) {
        threadToQuit->quit();
        qDebug() << "ThreadedAudioFrequencyAnalyzer: Decreased FFT workers to" << m_numWorkerThreads.loadAcquire();
    } else {
        m_availableSlotsSemaphore.release();
        qWarning() << "ThreadedAudioFrequencyAnalyzer: Error during decThreads, thread lists empty unexpectedly.";
    }
    return true;
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::onRMSValueReady(float rawRmsValue)
{
    float finalRmsValue = 0.0f;

    // TRICK: Perform dummification
    const float dummifiedRms = mapToRange(rawRmsValue, 0.0f, 1.0f, 0.f, 100000.f);

    QMutexLocker locker(&m_resultsMutex);

    switch (m_normalizationMode.load()) {
    case AUDIO_NORMALIZATION_MODE::REACTIVE: {
        m_rollingRmsTracker.addDataPoint(QDateTime::currentMSecsSinceEpoch(), dummifiedRms);
        const float minRmsInWindow = m_rollingRmsTracker.getMinValue();
        const float maxRmsInWindow = m_rollingRmsTracker.getMaxValue();
        const float range = maxRmsInWindow - minRmsInWindow;

        if (range > EPSILON) {
            finalRmsValue = mapToRange(dummifiedRms, minRmsInWindow, maxRmsInWindow, 0.0f, 100.0f, true);
        }

        // In REACTIVE mode, publish the current dynamic window's range.
        const auto& fftMins = m_rollingBandTracker.getCurrentMinValues();
        const auto& fftMaxs = m_rollingBandTracker.getCurrentMaxValues();
        emit newMinMaxValues(minRmsInWindow, maxRmsInWindow,
                             QVector<float>(fftMins.begin(), fftMins.end()),
                             QVector<float>(fftMaxs.begin(), fftMaxs.end()));
        break;
    }
    case AUDIO_NORMALIZATION_MODE::STABLE: {
        bool minMaxChanged = false;
        if (dummifiedRms > m_rmsMaxEver) { m_rmsMaxEver = dummifiedRms; minMaxChanged = true; }
        if (dummifiedRms < m_rmsMinEver) { m_rmsMinEver = dummifiedRms; minMaxChanged = true; }

        const float range = m_rmsMaxEver - m_rmsMinEver;
        if (range > EPSILON) {
            const float upperBound = m_rmsMaxEver - 0.25f * range;
            finalRmsValue = mapToRange(dummifiedRms, m_rmsMinEver, upperBound, 0.0f, 100.0f, true);
        } else {
            finalRmsValue = (dummifiedRms > m_rmsMinEver) ? 100.0f : 0.0f;
        }
        // In STABLE mode, only publish when the persistent "ever" range changes.
        if (minMaxChanged) {
            emit newMinMaxValues(m_rmsMinEver, m_rmsMaxEver,
                                 QVector<float>(m_fftMinEver.begin(), m_fftMinEver.end()),
                                 QVector<float>(m_fftMaxEver.begin(), m_fftMaxEver.end()));
        }
        break;
    }
    case AUDIO_NORMALIZATION_MODE::CUSTOM: {
        const float range = m_forcedRmsMax - m_forcedRmsMin;
        if (range > EPSILON) {
            finalRmsValue = mapToRange(dummifiedRms, m_forcedRmsMin, m_forcedRmsMax, 0.0f, 100.0f, true);
        }
        break;
    }
    case AUDIO_NORMALIZATION_MODE::NumModes:
        finalRmsValue = dummifiedRms;
        break;
    }
    locker.unlock();

    emit newRMSValue(finalRmsValue);
}

//---------------------------------------------------------------------------------------------------------------

void ThreadedAudioFrequencyAnalyzer::handleWorkerAnalysisComplete(qint64 sequenceNumber,
                                                                  float subBass, float bass, float lowMid, float mid,
                                                                  float highMid, float treble, float air)
{
    if (AudioFFTworker* worker = qobject_cast<AudioFFTworker*>(sender()); !worker || !worker->isMarkedForShutdown()) {
        m_availableSlotsSemaphore.release();
    }

    QMutexLocker locker(&m_resultsMutex);
    if (sequenceNumber < m_outputSequenceNumber) return;
    m_outputSequenceNumber = sequenceNumber + 1;

    uint16_t samplesLostSnapshot = m_fftSamplesCurrentlyLost;
    m_fftSamplesCurrentlyLost = 0;

    // TRICK: Perform dummification
    const auto dummifyBand = [](const float &b) -> float {
        return mapToRange(b, 0.f, 900000.f, 0.f, 100000.f);
    };

    std::array<float, AudioFFTworker::FrequencyBand::NumBands> bands = {dummifyBand(subBass),
                                                                        dummifyBand(bass),
                                                                        dummifyBand(lowMid),
                                                                        dummifyBand(mid),
                                                                        dummifyBand(highMid),
                                                                        dummifyBand(treble),
                                                                        dummifyBand(air)};

    switch (m_normalizationMode.load()) {
    case AUDIO_NORMALIZATION_MODE::REACTIVE: {
        m_rollingBandTracker.addDataPoint(QDateTime::currentMSecsSinceEpoch(), bands);
        const auto& bandMins = m_rollingBandTracker.getCurrentMinValues();
        const auto& bandMaxs = m_rollingBandTracker.getCurrentMaxValues();

        for (size_t i = 0; i < bands.size(); ++i) {
            const float range = bandMaxs[i] - bandMins[i];
            if (range > EPSILON) {
                bands[i] = mapToRange(bands[i], bandMins[i], bandMaxs[i], 0.0f, 100.0f, true);
            } else {
                bands[i] = (bands[i] > EPSILON) ? 100.0f : 0.0f;
            }
        }
        // In REACTIVE mode, always publish the current dynamic window's range.
        emit newMinMaxValues(m_rollingRmsTracker.getMinValue(), m_rollingRmsTracker.getMaxValue(),
                             QVector<float>(bandMins.begin(), bandMins.end()),
                             QVector<float>(bandMaxs.begin(), bandMaxs.end()));
        break;
    }
    case AUDIO_NORMALIZATION_MODE::STABLE: {
        bool minMaxChanged = false;
        for (size_t i = 0; i < bands.size(); ++i) {
            if (bands[i] > m_fftMaxEver[i]) { m_fftMaxEver[i] = bands[i]; minMaxChanged = true; }
            if (bands[i] < m_fftMinEver[i]) { m_fftMinEver[i] = bands[i]; minMaxChanged = true; }
        }

        for (size_t i = 0; i < bands.size(); ++i) {
            const float range = m_fftMaxEver[i] - m_fftMinEver[i];
            if (range > EPSILON) {
                const float upperBound = m_fftMaxEver[i] - 0.25f * range;
                bands[i] = mapToRange(bands[i], m_fftMinEver[i], upperBound, 0.0f, 100.0f, true);
            } else {
                bands[i] = (bands[i] > m_fftMinEver[i]) ? 100.0f : 0.0f;
            }
        }

        // In STABLE mode, only publish when the persistent "ever" range changes.
        if (minMaxChanged) {
            emit newMinMaxValues(m_rmsMinEver, m_rmsMaxEver,
                                 QVector<float>(m_fftMinEver.begin(), m_fftMinEver.end()),
                                 QVector<float>(m_fftMaxEver.begin(), m_fftMaxEver.end()));
        }
        break;
    }
    case AUDIO_NORMALIZATION_MODE::CUSTOM: {
        for (size_t i = 0; i < bands.size(); ++i) {
            const float range = m_forcedFftMax[i] - m_forcedFftMin[i];
            if (range > EPSILON) {
                bands[i] = mapToRange(bands[i], m_forcedFftMin[i], m_forcedFftMax[i], 0.0f, 100.0f, true);
            } else {
                bands[i] = 0.0f;
            }
        }
        break;
    }
    case AUDIO_NORMALIZATION_MODE::NumModes:
        break; // noop
    }

    locker.unlock();

    emit newFFTValue(bands[0], bands[1], bands[2], bands[3], bands[4], bands[5], bands[6], samplesLostSnapshot);
}
