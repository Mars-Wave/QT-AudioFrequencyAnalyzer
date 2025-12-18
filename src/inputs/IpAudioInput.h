#pragma once
#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QUrl>

class IpAudioInput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool monitoring READ monitoring WRITE setMonitoring NOTIFY monitoringChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
    explicit IpAudioInput(QObject *parent = nullptr) : QObject(parent) {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);

        // Output for Monitoring (Speakers)
        m_player->setAudioOutput(m_output);
        m_output->setMuted(!m_monitoring);

        // Output for Analysis
        QAudioFormat analysisFormat;
        analysisFormat.setSampleRate(44100);
        analysisFormat.setChannelCount(2);
        analysisFormat.setSampleFormat(QAudioFormat::Float);

        m_bufferOutput = new QAudioBufferOutput(analysisFormat, this);
        m_player->setAudioBufferOutput(m_bufferOutput);

        connect(m_bufferOutput, &QAudioBufferOutput::audioBufferReceived,
                this, &IpAudioInput::audioBufferReady);

        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state){
            bool isPlaying = (state == QMediaPlayer::PlayingState);
            if (m_listening != isPlaying) {
                m_listening = isPlaying;
                emit listeningChanged();
            }
        });

        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error err, const QString &errorString){
            qWarning() << "IP Input Error:" << errorString;
            stopListening();
        });
    }

    QString url() const { return m_url; }
    void setUrl(const QString &u) {
        if(m_url == u) return;
        m_url = u;
        emit urlChanged();
        if(m_listening) startListening();
    }

    bool listening() const { return m_listening; }

    bool monitoring() const { return m_monitoring; }
    void setMonitoring(bool enable) {
        if(m_monitoring == enable) return;
        m_monitoring = enable;
        m_output->setMuted(!enable);
        emit monitoringChanged();
    }

    float volume() const { return m_output->volume(); }
    void setVolume(float vol) {
        if (qFuzzyCompare(m_output->volume(), vol)) return;
        m_output->setVolume(vol);
        emit volumeChanged();
    }

    Q_INVOKABLE void startListening() {
        if (m_url.isEmpty()) return;

        QString finalUrl = m_url;
        bool isPort = false;
        m_url.toInt(&isPort);

        if (isPort) {
            finalUrl = QString("udp://127.0.0.1:%1").arg(m_url);
        }
        else if (!m_url.contains("://")) {
            finalUrl = QString("udp://%1").arg(m_url);
        }

        m_player->setSource(QUrl(finalUrl));
        m_player->play();
    }

    Q_INVOKABLE void stopListening() {
        m_player->stop();
        m_player->setSource(QUrl());
    }

signals:
    void audioBufferReady(const QAudioBuffer &buffer);
    void urlChanged();
    void listeningChanged();
    void monitoringChanged();
    void volumeChanged();

private:
    QMediaPlayer*       m_player;
    QAudioOutput*       m_output;
    QAudioBufferOutput* m_bufferOutput;
    QString m_url = "http://stream.radiomast.io/my_stream";
    bool m_listening = false;
    bool m_monitoring = false;
};
