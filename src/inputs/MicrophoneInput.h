#pragma once
#include <QObject>
#include <QAudioInput>
#include <QAudioOutput>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioBuffer>
#include <QStringList>


class MicrophoneInput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    // Input Volume (Hardware/OS Level)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    // Monitor controls
    Q_PROPERTY(bool monitoring READ monitoring WRITE setMonitoring NOTIFY monitoringChanged)

public:
    explicit MicrophoneInput(QObject *parent = nullptr) : QObject(parent) {
        refreshDevices();
        connect(&m_devicesWatcher, &QMediaDevices::audioInputsChanged, this, &MicrophoneInput::refreshDevices);

        if (m_currentIndex == -1 && !m_deviceList.isEmpty()) {
            QAudioDevice defaultDev = QMediaDevices::defaultAudioInput();
            for(int i=0; i<m_deviceList.size(); i++) {
                if(m_deviceList[i].id() == defaultDev.id()) {
                    m_currentIndex = i;
                    break;
                }
            }
            if(m_currentIndex == -1) m_currentIndex = 0;
        }
    }

    inline ~MicrophoneInput() { stop(); }
    inline QStringList devices() const { return m_deviceNames; }
    inline int currentIndex() const { return m_currentIndex; }

    // Returns 0.0 - 1.0
    inline float volume() const { return m_volume; }
    inline bool monitoring() const { return m_monitoring; }

    void setCurrentIndex(int index);

    void setVolume(float v);

    void setMonitoring(bool enable);

    Q_INVOKABLE void start();

    void stop();

signals:
    void devicesChanged();
    void currentIndexChanged();
    void volumeChanged();
    void monitoringChanged();
    void audioBufferReady(const QAudioBuffer& buffer);

private slots:
    void refreshDevices();

    void onReadyRead();

private:
    void restart() { stop(); start(); }

    void startMonitor();

    void stopMonitor();

    QMediaDevices m_devicesWatcher;
    QList<QAudioDevice> m_deviceList;
    QStringList m_deviceNames;
    int m_currentIndex = -1;
    float m_volume = 1.0f;
    bool m_monitoring = false;

    QAudioInput* m_audioInput = nullptr;
    QAudioSource* m_source = nullptr;
    QIODevice* m_ioDevice = nullptr;

    // Monitor
    QAudioOutput* m_monitorOutput = nullptr;
    QAudioSink*   m_monitorSink = nullptr;
    QIODevice*    m_monitorIO = nullptr;
};
