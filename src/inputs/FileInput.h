#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBuffer>
#include <QAudioBufferOutput> // New Qt6 replacement for QAudioProbe
#include <QAudioFormat>
#include <QUrl>

class FileInput : public QObject
{
    Q_OBJECT
    // Properties to expose to QML
    Q_PROPERTY(bool monitoring READ monitoring WRITE setMonitoring NOTIFY monitoringChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)

public:
    explicit FileInput(QObject *parent = nullptr) : QObject(parent) {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);

        // 1. Setup Audio Output (Speakers)
        // We attach this so the user can hear the music.
        m_player->setAudioOutput(m_output);
        m_output->setVolume(1.0);

        // 2. Setup Audio Buffer Output (Analysis)
        // We define the format we WANT the analyzer to receive.
        // QMediaPlayer will automatically convert source files to match this.
        QAudioFormat analysisFormat;
        analysisFormat.setSampleRate(44100);        // Standard sample rate
        analysisFormat.setChannelCount(2);          // Stereo
        analysisFormat.setSampleFormat(QAudioFormat::Float); // Analyzer engine likely prefers Floats

        m_bufferOutput = new QAudioBufferOutput(analysisFormat, this);

        // Connect the buffer output to the player
        m_player->setAudioBufferOutput(m_bufferOutput);

        // Connect the signal to our slot
        connect(m_bufferOutput, &QAudioBufferOutput::audioBufferReceived,
                this, &FileInput::onAudioBufferReceived);

        // 3. Standard Player Status Signals
        connect(m_player, &QMediaPlayer::durationChanged, this, &FileInput::durationChanged);
        connect(m_player, &QMediaPlayer::positionChanged, this, &FileInput::positionChanged);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, &FileInput::stateChanged);
    }

    // --- Monitoring (Hearing the audio) ---
    bool monitoring() const { return !m_output->isMuted(); }
    void setMonitoring(bool enable) {
        if (m_output->isMuted() == !enable) return;
        m_output->setMuted(!enable);
        emit monitoringChanged();
    }

    // --- Playback Controls ---
    qint64 duration() const { return m_player->duration(); }
    qint64 position() const { return m_player->position(); }
    bool playing() const { return m_player->playbackState() == QMediaPlayer::PlayingState; }

    Q_INVOKABLE void setSource(const QUrl &url) {
        m_player->setSource(url);
    }

    Q_INVOKABLE void play() { m_player->play(); }
    Q_INVOKABLE void pause() { m_player->pause(); }
    Q_INVOKABLE void stop() { m_player->stop(); }
    Q_INVOKABLE void seek(qint64 pos) { m_player->setPosition(pos); }

signals:
    void audioBufferReady(const QAudioBuffer &buffer);
    void monitoringChanged();
    void durationChanged();
    void positionChanged();
    void stateChanged();

private slots:
    void onAudioBufferReceived(const QAudioBuffer &buffer) {
        // Forward the normalized buffer to the analyzer
        emit audioBufferReady(buffer);
    }

private:
    QMediaPlayer*       m_player;
    QAudioOutput*       m_output;       // For the speakers
    QAudioBufferOutput* m_bufferOutput; // For the visualizer
};
