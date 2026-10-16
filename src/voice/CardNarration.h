#pragma once

#include <QAudioFormat>
#include <QPointer>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QByteArray>
#include <optional>
#include "../core/cardmodel.h"
#include "VoiceAudio.h"

class AppController;
class VoiceController;
class QAudioSource;
class QIODevice;
class QTimer;

class CardNarration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cardId READ cardId NOTIFY cardChanged)
    Q_PROPERTY(QString variantKey READ variantKey WRITE setVariantKey NOTIFY variantChanged)
    Q_PROPERTY(QVariantList variants READ variants NOTIFY cardChanged)
    Q_PROPERTY(QVariantList sides READ sides NOTIFY stateChanged)
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool waitingForCapture READ waitingForCapture NOTIFY stateChanged)
    Q_PROPERTY(bool recordingPaused READ recordingPaused NOTIFY stateChanged)
    Q_PROPERTY(QString recordingSide READ recordingSide NOTIFY stateChanged)
    Q_PROPERTY(double recordingDuration READ recordingDuration NOTIFY stateChanged)
    Q_PROPERTY(double recordingLevel READ recordingLevel NOTIFY stateChanged)
    Q_PROPERTY(bool hasDraft READ hasDraft NOTIFY stateChanged)
    Q_PROPERTY(QString draftSide READ draftSide NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY stateChanged)
    Q_PROPERTY(QString playingSide READ playingSide NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    explicit CardNarration(AppController *app, VoiceController *voice, const QString &dataDirectory, QObject *parent = nullptr);
    ~CardNarration() override;

    QString cardId() const { return m_cardId; }
    QString variantKey() const { return m_variantKey; }
    QVariantList variants() const { return m_variants; }
    QVariantList sides() const { return m_sides; }
    bool sessionActive() const { return m_sessionActive; }
    bool recording() const { return m_recording; }
    bool waitingForCapture() const { return m_waitingForCapture; }
    bool recordingPaused() const { return m_recordingPaused; }
    QString recordingSide() const { return m_recordingSide; }
    double recordingDuration() const { return static_cast<double>(m_pcm.size()) / (16000.0 * 2.0); }
    double recordingLevel() const { return m_recordingLevel; }
    bool hasDraft() const { return !m_draftSide.isEmpty(); }
    QString draftSide() const { return m_draftSide; }
    bool busy() const { return m_busy; }
    bool previewPlaying() const { return m_previewPlaying; }
    QString playingSide() const { return m_playingSide; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool openFor(const QString &cardId);
    Q_INVOKABLE bool close();
    Q_INVOKABLE void setVariantKey(const QString &key);
    Q_INVOKABLE void startRecording(const QString &side);
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    Q_INVOKABLE void finishRecording();
    Q_INVOKABLE void discardDraft();
    Q_INVOKABLE void saveDraft();
    Q_INVOKABLE void play(const QString &side);
    Q_INVOKABLE bool playSaved(const QString &cardId, const QString &variantKey, const QString &side);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void remove(const QString &side);
    Q_INVOKABLE void confirmRecording(const QString &side);
    Q_INVOKABLE QVariantMap warningsForCard(const QVariantMap &cardMap) const;

signals:
    void cardChanged();
    void variantChanged();
    void stateChanged();

private slots:
    void narrationSaveFinished(const QString &cardId, const QString &variantKey, const QString &side,
        const QString &requestId, bool success);

private:
    void setState(const QString &status, const QString &error = {});
    bool rebuildCardState();
    void readCapture();
    void beginCapture(const QString &side);
    void loadAndPlay(const QString &name, const QString &side);
    QVariantMap card() const;
    QString mediaName(const QString &side) const;
    void stopCapture();
    void stopPlaybackInternal();
    bool playPcm(const QByteArray &pcm, const QString &side);
    bool invokeNarrationSave(const QString &side, const QString &name, const QString &requestId,
        const QString &fingerprint = {});
    void clearPendingSave();

    enum class PendingAction { None, Save, Remove, Confirm };

    QPointer<AppController> m_app;
    QPointer<VoiceController> m_voice;
    QString m_dataDirectory;
    QString m_cardId;
    QString m_variantKey;
    QVariantList m_variants;
    QVariantList m_sides;
    bool m_sessionActive = false;
    bool m_recording = false;
    bool m_recordingPaused = false;
    QString m_recordingSide;
    QString m_draftSide;
    bool m_busy = false;
    bool m_previewPlaying = false;
    QString m_status;
    QString m_error;
    QByteArray m_pcm;
    double m_recordingLevel = 0.0;
    bool m_waitingForCapture = false;
    QPointer<QAudioSource> m_source;
    QPointer<QIODevice> m_input;
    QPointer<QTimer> m_meterTimer;
    QAudioFormat m_captureFormat;
    VoicePcmConverter m_converter;
    QString m_playingSide;
    quint64 m_asyncGeneration = 0;
    quint64 m_playbackGeneration = 0;
    quint64 m_ownedSpeechGeneration = 0;
    QString m_pendingRequestId;
    QString m_pendingSide;
    QString m_pendingName;
    PendingAction m_pendingAction = PendingAction::None;
    std::optional<struct betterflash::model::Card> m_displayedCard;
    std::optional<struct betterflash::model::Card> m_captureCard;
    QString m_captureVariantKey;
};
