#include "MicrophoneInput.h"
#include <QMediaDevices>
#include <QDebug>

void MicrophoneInput::refreshDevices()
{
    m_deviceList = QMediaDevices::audioInputs();

    // Update string list for QML
    QStringList newNames;
    for (const auto &dev : std::as_const(m_deviceList)) {
        newNames.append(dev.description());
    }

    if (m_deviceNames != newNames) {
        m_deviceNames = newNames;
        emit devicesChanged();
    }

    // Attempt to auto-select default if we have no selection
    if (m_currentIndex == -1 && !m_deviceList.isEmpty()) {
        QAudioDevice defaultDev = QMediaDevices::defaultAudioInput();
        for(int i=0; i<m_deviceList.size(); i++) {
            if(m_deviceList[i].id() == defaultDev.id()) {
                setCurrentIndex(i);
                return;
            }
        }
        // Fallback
        setCurrentIndex(0);
    }
}

void MicrophoneInput::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_deviceList.size() || m_currentIndex == index) return;
    m_currentIndex = index;
    emit currentIndexChanged();

    // If we are currently running, we must restart to switch devices
    if (m_source) {
        restart();
    }
}

void MicrophoneInput::setVolume(float v)
{
    // Clamp 0.0 to 1.0
    float safeV = std::max(0.0f, std::min(v, 1.0f));
    if (qFuzzyCompare(m_volume, safeV)) return;

    m_volume = safeV;

    // Apply immediately if hardware exists
    if (m_audioInput) {
        m_audioInput->setVolume(m_volume);
    }
    emit volumeChanged();
}

void MicrophoneInput::start()
{
    // 1. Safety Cleanup
    stop();

    // 2. LAZY INIT: If devices aren't loaded yet (startup race condition), load them now
    if (m_deviceList.isEmpty()) {
        refreshDevices();
    }

    // 3. AUTO-SELECT: If still no index, force select 0
    if (m_currentIndex < 0 && !m_deviceList.isEmpty()) {
        m_currentIndex = 0;
        emit currentIndexChanged();
    }

    // 4. Validation
    if (m_currentIndex < 0 || m_currentIndex >= m_deviceList.size()) {
        qWarning() << "MicrophoneInput: No device selected or available.";
        return;
    }

    auto device = m_deviceList[m_currentIndex];

    // 5. Format Setup
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);

    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();
    }

    // 6. Create Hardware Abstraction (Volume Control)
    m_audioInput = new QAudioInput(device, this);
    m_audioInput->setVolume(m_volume);

    // 7. Create Audio Source (Data Stream)
    m_source = new QAudioSource(device, format, this);

    // 8. Open Stream
    m_ioDevice = m_source->start();

    if (m_ioDevice) {
        connect(m_ioDevice, &QIODevice::readyRead, this, &MicrophoneInput::onReadyRead);
    } else {
        qWarning() << "MicrophoneInput: Failed to open device" << device.description();
    }

    if (m_monitoring) startMonitor();
}

void MicrophoneInput::stop()
{
    stopMonitor();

    if (m_source) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
    }

    if (m_audioInput) {
        delete m_audioInput;
        m_audioInput = nullptr;
    }

    m_ioDevice = nullptr;
}

void MicrophoneInput::onReadyRead()
{
    if (!m_ioDevice || !m_source) return;

    QByteArray data = m_ioDevice->readAll();
    if (data.isEmpty()) return;

    // Send to Analyzer
    QAudioBuffer buffer(data, m_source->format());
    emit audioBufferReady(buffer);

    // Send to Monitor
    if (m_monitoring && m_monitorIO) {
        m_monitorIO->write(data);
    }
}

void MicrophoneInput::setMonitoring(bool enable) {
    if (m_monitoring == enable) return;
    m_monitoring = enable;
    if (m_monitoring) startMonitor();
    else stopMonitor();
    emit monitoringChanged();
}

void MicrophoneInput::startMonitor() {
    if (m_monitorSink) return; // Already monitoring

    // Get Default Output
    QAudioDevice outDev = QMediaDevices::defaultAudioOutput();

    // Match the Mic format if possible
    QAudioFormat format = m_source ? m_source->format() : outDev.preferredFormat();

    m_monitorOutput = new QAudioOutput(outDev, this);
    m_monitorOutput->setVolume(1.0); // Monitoring volume (usually full, controlled by OS)

    m_monitorSink = new QAudioSink(outDev, format, this);
    m_monitorIO = m_monitorSink->start();
}

void MicrophoneInput::stopMonitor() {
    if (m_monitorSink) {
        m_monitorSink->stop();
        delete m_monitorSink;
        m_monitorSink = nullptr;
    }
    if (m_monitorOutput) {
        delete m_monitorOutput;
        m_monitorOutput = nullptr;
    }
    m_monitorIO = nullptr;
}
