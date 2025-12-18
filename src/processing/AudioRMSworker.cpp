#include "AudioRMSworker.h"
#include <cmath>
#include <cstdint>
#include <QDebug>

void AudioRMSworker::initialize(uint16_t rmsChunkSize) {
    if (rmsChunkSize == 0) {
        qWarning() << "AudioRMSworker: RMS chunk size cannot be 0. Using 1.";
        rmsChunkSize = 1;
    }
    m_currentChunkSize = rmsChunkSize;
    m_rmsQueue.resize(m_currentChunkSize);
    m_rmsQueue.clear();
}

void AudioRMSworker::processSampleBatch(const QVector<float>& samples) {
    if (m_shutdownRequested || m_currentChunkSize == 0) return;

    for (float sample : samples) {
        if (m_shutdownRequested) break;
        m_rmsQueue.enqueue(sample);
        if (m_rmsQueue.isFull()) {
            float sumOfSquares = m_rmsQueue.getSumOfSquares();
            if (m_currentChunkSize > 0) { // Ensure no division by zero
                emit rmsValueCalculated(std::sqrt(sumOfSquares / static_cast<float>(m_currentChunkSize)));
            }
            m_rmsQueue.clear();
        }
    }
}
