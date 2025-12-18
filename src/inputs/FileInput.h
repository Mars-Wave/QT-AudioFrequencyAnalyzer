#pragma once
#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBuffer>
#include <QAudioBufferOutput>
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
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)

public:
    explicit FileInput(QObject *parent = nullptr) : QObject(parent) {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);

        // 1. Setup Audio Output (Speakers)
        m_player->setAudioOutput(m_output);
        m_output->setVolume(1.0);

        // 2. Setup Audio Buffer Output (Analysis)
        QAudioFormat analysisFormat;
        analysisFormat.setSampleRate(44100);
        analysisFormat.setChannelCount(2);
        analysisFormat.setSampleFormat(QAudioFormat::Float);

        m_bufferOutput = new QAudioBufferOutput(analysisFormat, this);
        m_player->setAudioBufferOutput(m_bufferOutput);

        connect(m_bufferOutput, &QAudioBufferOutput::audioBufferReceived,
                this, &FileInput::onAudioBufferReceived);

        // 3. Standard Player Status Signals
        connect(m_player, &QMediaPlayer::durationChanged, this, &FileInput::durationChanged);
        connect(m_player, &QMediaPlayer::positionChanged, this, &FileInput::positionChanged);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, &FileInput::stateChanged);
        connect(m_player, &QMediaPlayer::sourceChanged, this, &FileInput::sourceChanged);
    }

    // --- Monitoring (Hearing the audio) ---
    bool monitoring() const { return !m_output->isMuted(); }
    void setMonitoring(bool enable) {
        if (m_output->isMuted() == !enable) return;
        m_output->setMuted(!enable);
        emit monitoringChanged();
    }

    // --- Volume Control ---
    float volume() const { return m_output->volume(); }
    void setVolume(float vol) {
        if (qFuzzyCompare(m_output->volume(), vol)) return;
        m_output->setVolume(vol);
        emit volumeChanged();
    }

    // --- Playback Controls ---
    qint64 duration() const { return m_player->duration(); }
    qint64 position() const { return m_player->position(); }
    bool playing() const { return m_player->playbackState() == QMediaPlayer::PlayingState; }
    QUrl source() const { return m_player->source(); }

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
    void volumeChanged();
    void sourceChanged();

private slots:
    void onAudioBufferReceived(const QAudioBuffer &buffer) {
        emit audioBufferReady(buffer);
    }

private:
    QMediaPlayer*       m_player;
    QAudioOutput*       m_output;
    QAudioBufferOutput* m_bufferOutput;
};
