#pragma once

#include <QObject>
#include <QVector>
#include "ArithmeticCircularQueue.h"

class AudioRMSworker : public QObject
{
    Q_OBJECT
public:
    explicit AudioRMSworker(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~AudioRMSworker() = default;

public slots:
    void initialize(uint16_t rmsChunkSize);
    void processSampleBatch(const QVector<float>& samples);
    void inline markForShutdown() { m_shutdownRequested = true; }

signals:
    void rmsValueCalculated(float rmsValue);

private:
    static constexpr uint16_t DEFAULT_RMS_QUEUE_SIZE  = 64                      ;
    ArithmeticCircularQueue<float> m_rmsQueue = {DEFAULT_RMS_QUEUE_SIZE};
    uint16_t m_currentChunkSize                       =  DEFAULT_RMS_QUEUE_SIZE ;
    bool m_shutdownRequested                          = false                   ;
};
