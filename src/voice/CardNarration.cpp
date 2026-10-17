#include "CardNarration.h"

#include "VoiceController.h"
#include "../core/appcontroller.h"
#include "../core/cardmodel.h"
#include "../core/mediabackup.h"
#include "../core/audiolimits.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCryptographicHash>
#include <QDir>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <expected>
#include <utility>

namespace {
constexpr qsizetype kMaxPcmBytes = betterflash::audio::NarrationMaxPcmBytes;
static_assert(kMaxPcmBytes == betterflash::audio::CanonicalSampleRate
    * betterflash::audio::CanonicalChannels * betterflash::audio::CanonicalBytesPerSample
    * betterflash::audio::NarrationMaxSeconds);

bool validSide(const QString &side) {
    return side == QStringLiteral("front") || side == QStringLiteral("back");
}

QString staleReason(const struct betterflash::model::Card &card, const QString &key, const QString &side) {
    if (!betterflash::model::narrationIsStale(card, key, side)) return {};
    if (card.narrationFingerprints.value(key + QLatin1Char('/') + side).toString().isEmpty())
        return QStringLiteral("This recording has no saved text reference. Confirm that it matches the text, or record it again.");
    return QStringLiteral("The text has changed since this recording was made or confirmed. Check the recording before using it.");
}

struct SavedRecording {
    QString name;
    QString fingerprint;
};

std::expected<struct betterflash::model::Card, QString> selectedCard(const AppController *app, const QString &id) {
    if (app) for (const QVariant &value : app->cards()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == id)
            return betterflash::model::cardFromMap(candidate);
    }
    return std::unexpected(QStringLiteral("The selected card no longer exists."));
}
}

CardNarration::CardNarration(AppController *app, VoiceController *voice, const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_app(app), m_voice(voice), m_dataDirectory(dataDirectory) {
    Q_ASSERT(m_app);
    Q_ASSERT(m_voice);
    connect(m_voice, &VoiceController::utteranceFinished, this, [this] {
        if (!m_previewPlaying) return;
        m_previewPlaying = false;
        m_ownedSpeechGeneration = 0;
        m_playingSide.clear();
        emit stateChanged();
    });
    connect(m_voice, &VoiceController::speakingChanged, this, [this] {
        if (!m_previewPlaying || m_ownedSpeechGeneration == 0 || m_voice->speaking()) return;
        m_previewPlaying = false;
        m_ownedSpeechGeneration = 0;
        m_playingSide.clear();
        emit stateChanged();
    });
    connect(m_app, &AppController::cardNarrationSaved, this, &CardNarration::narrationSaveFinished);
    connect(m_app, &AppController::cardsChanged, this, [this] {
        if (m_sessionActive) rebuildCardState();
    });
}

CardNarration::~CardNarration() {
    ++m_asyncGeneration;
    stopCapture();
    stopPlaybackInternal();
    if (m_sessionActive && m_voice) m_voice->endNarrationSession();
}

QVariantMap CardNarration::card() const {
    const auto selected = selectedCard(m_app, m_cardId);
    return selected ? betterflash::model::toMap(*selected) : QVariantMap{};
}

QVariantMap CardNarration::warningsForCard(const QVariantMap &cardMap) const {
    struct betterflash::model::Card candidate{};
    candidate.id = cardMap.value(QStringLiteral("id")).toString();
    candidate.deckId = cardMap.value(QStringLiteral("deckId")).toString();
    candidate.kind = cardMap.value(QStringLiteral("kind")).toString();
    candidate.front = cardMap.value(QStringLiteral("front")).toString();
    candidate.back = cardMap.value(QStringLiteral("back")).toString();
    candidate.narrations = cardMap.value(QStringLiteral("narrations")).toMap();
    candidate.narrationFingerprints = cardMap.value(QStringLiteral("narrationFingerprints")).toMap();
    const auto keys = betterflash::model::variantKeys(candidate);
    bool front = false;
    bool back = false;
    int count = 0;
    for (auto it = candidate.narrations.cbegin(); it != candidate.narrations.cend(); ++it) {
        if (it.value().toString().isEmpty()) continue;
        const QStringList parts = it.key().split(QLatin1Char('/'));
        if (parts.size() != 2 || !validSide(parts.at(1))) {
            front = true;
            back = true;
            ++count;
            continue;
        }
        const QString &key = parts.at(0);
        const QString &side = parts.at(1);
        if (keys && keys->contains(key) && !betterflash::model::narrationIsStale(candidate, key, side)) continue;
        ++count;
        const bool question = side == QStringLiteral("front");
        if (key == QStringLiteral("forward")) {
            if (question) front = true; else back = true;
        } else if (key == QStringLiteral("reverse")) {
            if (question) back = true; else front = true;
        } else if (key.startsWith(QLatin1Char('c'))) {
            front = true;
            if (!question && !candidate.back.trimmed().isEmpty()) back = true;
        } else {
            front = true;
            back = true;
        }
    }
    return {{QStringLiteral("hasStale"), count > 0}, {QStringLiteral("front"), front},
        {QStringLiteral("back"), back}, {QStringLiteral("count"), count}};
}

void CardNarration::setState(const QString &status, const QString &error) {
    m_status = status;
    m_error = error;
    emit stateChanged();
}

bool CardNarration::openFor(const QString &cardId) {
    if (m_recording || m_waitingForCapture || m_busy || hasDraft()) return false;
    const auto selected = selectedCard(m_app, cardId.trimmed());
    if (!selected) {
        setState(QStringLiteral("Narration unavailable."), selected.error());
        if (m_voice) m_voice->reportNarrationError(selected.error());
        return false;
    }
    const auto keys = betterflash::model::variantKeys(*selected);
    if (!keys || keys->isEmpty()) {
        const QString message = QStringLiteral("This card has no valid review direction. Edit the card to restore a valid question and answer.");
        setState(QStringLiteral("Narration unavailable."), message);
        if (m_voice) m_voice->reportNarrationError(message);
        return false;
    }
    stopPlaybackInternal();
    if (!m_sessionActive && (!m_voice || !m_voice->beginNarrationSession())) {
        const QString message = QStringLiteral("Finish the current speech test or recording, then open narration again.");
        setState(QStringLiteral("Narration unavailable."), message);
        if (m_voice) m_voice->reportNarrationError(message);
        return false;
    }
    ++m_asyncGeneration;
    m_cardId = selected->id;
    m_variantKey = keys->first();
    m_sessionActive = true;
    rebuildCardState();
    emit cardChanged();
    emit variantChanged();
    setState(QStringLiteral("Choose question or answer, then record your narration."));
    return true;
}

bool CardNarration::close() {
    if (m_recording || m_waitingForCapture || m_busy || hasDraft()) return false;
    ++m_asyncGeneration;
    stopPlaybackInternal();
    m_sessionActive = false;
    m_cardId.clear();
    m_variantKey.clear();
    m_variants.clear();
    m_sides.clear();
    m_pcm.clear();
    m_displayedCard.reset();
    m_captureCard.reset();
    m_captureVariantKey.clear();
    if (m_voice) m_voice->endNarrationSession();
    emit cardChanged();
    emit variantChanged();
    emit stateChanged();
    return true;
}

void CardNarration::setVariantKey(const QString &key) {
    if (!m_sessionActive || key == m_variantKey || m_recording || m_waitingForCapture || m_busy || hasDraft()) return;
    const auto selected = selectedCard(m_app, m_cardId);
    if (!selected) { setState(QStringLiteral("Narration unavailable."), selected.error()); return; }
    const auto keys = betterflash::model::variantKeys(*selected);
    if (!keys || !keys->contains(key)) return;
    stopPlaybackInternal();
    m_variantKey = key;
    rebuildCardState();
    emit variantChanged();
}

QString CardNarration::mediaName(const QString &side) const {
    if (!validSide(side)) return {};
    return card().value(QStringLiteral("narrations")).toMap().value(m_variantKey + QLatin1Char('/') + side).toString();
}

bool CardNarration::rebuildCardState() {
    const auto selected = selectedCard(m_app, m_cardId);
    m_variants.clear();
    m_sides.clear();
    if (!selected) {
        m_displayedCard.reset();
        emit cardChanged();
        setState(QStringLiteral("Narration unavailable."), selected.error());
        return false;
    }
    const auto keys = betterflash::model::variantKeys(*selected);
    if (!keys || keys->isEmpty()) {
        m_displayedCard.reset();
        emit cardChanged();
        setState(QStringLiteral("Narration unavailable."), QStringLiteral("This card has no valid review direction."));
        return false;
    }
    if (!keys->contains(m_variantKey)) {
        if (m_recording || m_waitingForCapture || hasDraft() || m_busy) {
            m_displayedCard.reset();
            emit cardChanged();
            setState(QStringLiteral("Review direction changed."), QStringLiteral("Discard this recording and select an available direction."));
            return false;
        }
        m_variantKey = keys->first();
        emit variantChanged();
    }
    m_displayedCard = *selected;
    for (const QString &key : *keys) {
        const QString label = key == QStringLiteral("forward") ? QStringLiteral("Forward")
            : key == QStringLiteral("reverse") ? QStringLiteral("Inverted")
            : QStringLiteral("Cloze %1").arg(key.mid(1));
        const bool stale = betterflash::model::narrationIsStale(*selected, key, QStringLiteral("front"))
            || betterflash::model::narrationIsStale(*selected, key, QStringLiteral("back"));
        m_variants.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("stale"), stale}});
    }
    for (const QString &side : {QStringLiteral("front"), QStringLiteral("back")}) {
        const bool front = side == QStringLiteral("front");
        const QString name = selected->narrations.value(m_variantKey + QLatin1Char('/') + side).toString();
        const QString reason = staleReason(*selected, m_variantKey, side);
        m_sides.append(QVariantMap{{QStringLiteral("side"), side},
            {QStringLiteral("label"), front ? QStringLiteral("Question") : QStringLiteral("Answer")},
            {QStringLiteral("markdown"), front ? betterflash::model::question(*selected, m_variantKey) : betterflash::model::answer(*selected, m_variantKey)},
            {QStringLiteral("hasRecording"), !name.isEmpty()},
            {QStringLiteral("stale"), !reason.isEmpty()}, {QStringLiteral("staleReason"), reason}});
    }
    emit cardChanged();
    emit stateChanged();
    return true;
}

void CardNarration::startRecording(const QString &side) {
    if (!m_sessionActive || !validSide(side) || m_recording || m_waitingForCapture || m_busy || hasDraft()) return;
    if (!rebuildCardState()) return;
    stopPlaybackInternal();
    m_recordingSide = side;
    if (!m_voice || !m_voice->narrationCaptureReady()) {
        m_waitingForCapture = true;
        setState(QStringLiteral("Waiting for the review microphone to stop."));
        const quint64 generation = ++m_asyncGeneration;
        auto *const timer = new QTimer(this);
        timer->setInterval(50);
        connect(timer, &QTimer::timeout, this, [this, timer, generation, side, polls = 0]() mutable {
            if (generation != m_asyncGeneration || !m_sessionActive || !m_waitingForCapture) { timer->stop(); timer->deleteLater(); return; }
            if (m_voice && m_voice->narrationCaptureReady()) {
                timer->stop(); timer->deleteLater(); m_waitingForCapture = false; beginCapture(side); return;
            }
            if (++polls >= 100) {
                timer->stop(); timer->deleteLater(); m_waitingForCapture = false; m_recordingSide.clear();
                setState(QStringLiteral("Recording unavailable."), QStringLiteral("The review microphone did not stop. Disable Voice Control and try recording again."));
            }
        });
        timer->start();
        return;
    }
    beginCapture(side);
}

void CardNarration::beginCapture(const QString &side) {
    if (!m_sessionActive || !validSide(side) || hasDraft() || m_busy || !m_voice || !m_voice->narrationCaptureReady()) return;
    const auto selected = selectedCard(m_app, m_cardId);
    if (!selected) {
        m_recordingSide.clear();
        setState(QStringLiteral("Recording unavailable."), selected.error());
        return;
    }
    const auto keys = betterflash::model::variantKeys(*selected);
    if (!keys || !keys->contains(m_variantKey)) {
        m_recordingSide.clear();
        setState(QStringLiteral("Recording unavailable."), QStringLiteral("The card or review direction changed. Reopen narration and try again."));
        return;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) { m_recordingSide.clear(); setState(QStringLiteral("Recording unavailable."), QStringLiteral("No microphone is available. Select an input in system sound settings.")); return; }
    m_captureFormat = device.preferredFormat();
    if (!m_converter.configure(m_captureFormat)) { m_recordingSide.clear(); setState(QStringLiteral("Recording unavailable."), QStringLiteral("The default microphone format is unsupported. Select another input in system sound settings.")); return; }
    m_captureCard = *selected;
    m_captureVariantKey = m_variantKey;
    m_recordingSide = side;
    m_pcm.clear();
    m_recordingLevel = 0.0;
    m_source = new QAudioSource(device, m_captureFormat, this);
    const QPointer<QAudioSource> source = m_source;
    m_recording = true;
    m_recordingPaused = false;
    connect(source, &QAudioSource::stateChanged, this, [this, source](QAudio::State) {
        if (!source || m_source != source || source->error() == QAudio::NoError) return;
        finishRecording();
        setState(hasDraft() ? QStringLiteral("Microphone stopped. Your recording was retained.") : QStringLiteral("Recording failed."),
            QStringLiteral("The microphone reported an audio capture error. Check its connection and system permissions before trying again."));
    });
    m_input = source->start();
    if (!m_input || !m_source || source->error() != QAudio::NoError) {
        stopCapture();
        m_captureCard.reset();
        m_captureVariantKey.clear();
        setState(QStringLiteral("Recording unavailable."), QStringLiteral("The microphone could not be opened. Check its connection and system permissions."));
        return;
    }
    connect(m_input, &QIODevice::readyRead, this, &CardNarration::readCapture);
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(100);
    connect(m_meterTimer, &QTimer::timeout, this, [this] { m_recordingLevel *= 0.85; emit stateChanged(); });
    m_meterTimer->start();
    setState(QStringLiteral("Recording. Stop when the narration is complete, up to 5 minutes."));
}

void CardNarration::readCapture() {
    if (!m_input || !m_recording) return;
    const QByteArray input = m_input->readAll();
    if (m_recordingPaused) return;
    const QByteArray converted = m_converter.convert(input);
    const qsizetype count = std::min(std::max<qsizetype>(0, kMaxPcmBytes - m_pcm.size()), converted.size()) & ~qsizetype(1);
    if (count > 0) {
        m_pcm.append(converted.constData(), count);
        double squareSum = 0.0;
        for (qsizetype offset = 0; offset < count; offset += 2) {
            const double sample = qFromLittleEndian<qint16>(converted.constData() + offset) / 32768.0;
            squareSum += sample * sample;
        }
        m_recordingLevel = std::clamp(std::sqrt(squareSum / static_cast<double>(count / 2)) * 4.0, 0.0, 1.0);
    }
    if (m_pcm.size() >= kMaxPcmBytes) {
        const QPointer<QAudioSource> source = m_source;
        QTimer::singleShot(0, this, [this, source] {
            if (source && m_source == source && m_recording) finishRecording();
        });
    }
}

void CardNarration::pauseRecording() {
    if (!m_recording || m_recordingPaused || !m_source) return;
    readCapture();
    m_recordingPaused = true;
    m_source->suspend();
    if (!m_recording || !m_source) return;
    m_recordingLevel = 0.0;
    setState(QStringLiteral("Recording paused."));
}

void CardNarration::resumeRecording() {
    if (!m_recording || !m_recordingPaused || !m_source) return;
    if (m_input) m_input->readAll();
    const bool configured = m_converter.configure(m_captureFormat);
    Q_ASSERT(configured);
    m_recordingPaused = false;
    m_source->resume();
    if (!m_recording || !m_source) return;
    setState(QStringLiteral("Recording. Stop when the narration is complete, up to 5 minutes."));
}

void CardNarration::stopCapture() {
    ++m_asyncGeneration;
    m_waitingForCapture = false;
    if (m_meterTimer) { m_meterTimer->stop(); m_meterTimer->deleteLater(); m_meterTimer = nullptr; }
    if (m_input) m_input->disconnect(this);
    m_input = nullptr;
    if (m_source) { m_source->disconnect(this); m_source->stop(); m_source->deleteLater(); m_source = nullptr; }
    m_converter.reset();
    m_recording = false;
    m_recordingPaused = false;
    m_recordingSide.clear();
    m_recordingLevel = 0.0;
}

void CardNarration::finishRecording() {
    if (!m_recording && !m_waitingForCapture) return;
    const QString side = m_recordingSide;
    readCapture();
    stopCapture();
    if (m_pcm.size() < 640) {
        m_pcm.clear();
        m_captureCard.reset();
        m_captureVariantKey.clear();
        setState(QStringLiteral("Recording discarded."), QStringLiteral("Record at least 20 milliseconds of audio."));
        return;
    }
    m_draftSide = side;
    setState(QStringLiteral("Recording ready. Save it to use it during review."));
}

void CardNarration::discardDraft() {
    if (m_busy) return;
    stopCapture();
    stopPlaybackInternal();
    m_pcm.clear();
    m_draftSide.clear();
    m_captureCard.reset();
    m_captureVariantKey.clear();
    if (m_sessionActive) rebuildCardState();
    setState(QStringLiteral("Recording discarded."));
}

bool CardNarration::invokeNarrationSave(const QString &side, const QString &name, const QString &requestId,
    const QString &fingerprint) {
    return m_app && m_app->setCardNarration(m_cardId, m_variantKey, side, name, requestId, fingerprint);
}

void CardNarration::clearPendingSave() {
    m_busy = false;
    m_pendingRequestId.clear();
    m_pendingSide.clear();
    m_pendingName.clear();
    m_pendingAction = PendingAction::None;
}

void CardNarration::narrationSaveFinished(const QString &cardId, const QString &variantKey, const QString &side,
    const QString &requestId, const bool success) {
    if (m_pendingAction == PendingAction::None || m_pendingRequestId.isEmpty()
        || requestId != m_pendingRequestId || cardId != m_cardId || variantKey != m_variantKey || side != m_pendingSide) return;
    const PendingAction action = m_pendingAction;
    clearPendingSave();
    if (!success) {
        setState(action == PendingAction::Confirm ? QStringLiteral("Recording could not be confirmed.") : QStringLiteral("Narration could not be saved."),
            QStringLiteral("The card update was rejected. Check the reported storage error and try again."));
        return;
    }
    if (action == PendingAction::Save) {
        m_pcm.clear();
        m_draftSide.clear();
        m_captureCard.reset();
        m_captureVariantKey.clear();
    }
    rebuildCardState();
    switch (action) {
    case PendingAction::Save: setState(QStringLiteral("Narration saved.")); break;
    case PendingAction::Remove: setState(QStringLiteral("Narration removed.")); break;
    case PendingAction::Confirm: setState(QStringLiteral("Recording confirmed to match the text.")); break;
    case PendingAction::None: Q_ASSERT(false); break;
    }
}

void CardNarration::saveDraft() {
    if (!m_sessionActive || !hasDraft() || m_busy || m_recording || m_waitingForCapture || !rebuildCardState()) return;
    if (!m_captureCard || m_captureVariantKey != m_variantKey) {
        setState(QStringLiteral("Narration could not be saved."), QStringLiteral("The recording has no matching text reference. Discard it and record this side again."));
        return;
    }
    stopPlaybackInternal();
    m_busy = true;
    m_pendingSide = m_draftSide;
    m_pendingRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingAction = PendingAction::Save;
    const QString requestId = m_pendingRequestId;
    const quint64 generation = ++m_asyncGeneration;
    const QByteArray pcm = m_pcm;
    const struct betterflash::model::Card capturedCard = *m_captureCard;
    const QString capturedKey = m_captureVariantKey;
    const QString side = m_draftSide;
    const QString directory = QDir(m_dataDirectory).filePath(QStringLiteral("media"));
    using Result = std::expected<struct SavedRecording, QString>;
    auto *const watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation, requestId] {
        const Result result = watcher->result();
        watcher->deleteLater();
        if (generation != m_asyncGeneration || requestId != m_pendingRequestId) return;
        if (!result) { clearPendingSave(); setState(QStringLiteral("Narration could not be saved."), result.error()); return; }
        m_pendingName = result->name;
        if (!invokeNarrationSave(m_pendingSide, m_pendingName, requestId, result->fingerprint)) {
            clearPendingSave();
            setState(QStringLiteral("Narration could not be saved."), QStringLiteral("The card update could not be queued. Try again after other changes finish."));
        }
    });
    watcher->setFuture(QtConcurrent::run([pcm, directory, capturedCard, capturedKey, side]() -> Result {
        const QByteArray wav = voiceWav(pcm);
        const QString name = QString::fromLatin1(QCryptographicHash::hash(wav, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".wav");
        const struct betterflash::model::MediaAttachment attachment{name, wav};
        const auto result = betterflash::model::writeBackupMedia(directory, {attachment});
        if (!result) return std::unexpected(result.error());
        struct betterflash::model::Card recordingCard = capturedCard;
        recordingCard.narrations.insert(capturedKey + QLatin1Char('/') + side, name);
        const QString fingerprint = betterflash::model::narrationFingerprint(recordingCard, capturedKey, side);
        if (fingerprint.isEmpty()) return std::unexpected(QStringLiteral("The captured card text cannot be matched to this recording. Record this side again."));
        return SavedRecording{name, fingerprint};
    }));
    setState(QStringLiteral("Saving narration."));
}

void CardNarration::stopPlaybackInternal() {
    ++m_playbackGeneration;
    const quint64 owned = std::exchange(m_ownedSpeechGeneration, 0);
    m_previewPlaying = false;
    m_playingSide.clear();
    if (m_voice && owned != 0 && m_voice->speechGeneration() == owned) m_voice->stopSpeaking();
}

void CardNarration::loadAndPlay(const QString &name, const QString &side) {
    stopPlaybackInternal();
    if (!m_voice) return;
    const quint64 generation = m_playbackGeneration;
    const quint64 speechGeneration = m_voice->speechGeneration();
    const QString directory = QDir(m_dataDirectory).filePath(QStringLiteral("media"));
    m_previewPlaying = true;
    m_playingSide = side;
    using Result = std::expected<QByteArray, QString>;
    auto *const watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation, speechGeneration, side] {
        const Result result = watcher->result();
        watcher->deleteLater();
        if (generation != m_playbackGeneration) return;
        if (!m_voice || m_voice->speechGeneration() != speechGeneration) { stopPlaybackInternal(); emit stateChanged(); return; }
        if (!result) {
            stopPlaybackInternal();
            setState(QStringLiteral("Narration could not be played."), result.error());
            m_voice->reportNarrationError(result.error());
            return;
        }
        if (!playPcm(*result, side)) setState(QStringLiteral("Narration could not be played."), QStringLiteral("Check the default audio output in system sound settings."));
    });
    watcher->setFuture(QtConcurrent::run([directory, name]() -> Result {
        if (!betterflash::model::validNarrationMediaName(name)) return std::unexpected(QStringLiteral("This card has an invalid narration reference. Remove it and record this side again."));
        const auto media = betterflash::model::readBackupMedia(directory, {name});
        if (!media) return std::unexpected(QStringLiteral("The saved narration is missing or damaged: %1 Restore its media or record this side again.").arg(media.error()));
        Q_ASSERT(media->size() == 1);
        return media->first().bytes.mid(44);
    }));
    emit stateChanged();
}

void CardNarration::play(const QString &side) {
    if (!m_sessionActive || !validSide(side) || m_recording || m_waitingForCapture || m_busy) return;
    if (hasDraft() && m_draftSide == side) { playPcm(m_pcm, side); return; }
    const QString name = mediaName(side);
    if (name.isEmpty()) { setState(QStringLiteral("No narration is saved for this side.")); return; }
    loadAndPlay(name, side);
}

bool CardNarration::playSaved(const QString &cardId, const QString &variantKey, const QString &side) {
    if (m_sessionActive || !validSide(side)) return false;
    const auto selected = selectedCard(m_app, cardId);
    if (!selected) return false;
    const auto keys = betterflash::model::variantKeys(*selected);
    if (!keys || !keys->contains(variantKey)) return false;
    const QString name = selected->narrations.value(variantKey + QLatin1Char('/') + side).toString();
    if (name.isEmpty()) return false;
    loadAndPlay(name, side);
    return true;
}

bool CardNarration::playPcm(const QByteArray &pcm, const QString &side) {
    stopPlaybackInternal();
    if (!m_voice || !m_voice->playRecordedPcm(pcm)) { emit stateChanged(); return false; }
    m_ownedSpeechGeneration = m_voice->speechGeneration();
    m_previewPlaying = true;
    m_playingSide = side;
    emit stateChanged();
    return true;
}

void CardNarration::stopPlayback() { stopPlaybackInternal(); emit stateChanged(); }

void CardNarration::remove(const QString &side) {
    if (!m_sessionActive || !m_app || !validSide(side) || m_recording || m_waitingForCapture || m_busy || hasDraft()) return;
    if (mediaName(side).isEmpty() || !rebuildCardState()) return;
    stopPlaybackInternal();
    m_pendingRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingSide = side;
    m_pendingName.clear();
    m_pendingAction = PendingAction::Remove;
    m_busy = true;
    if (!invokeNarrationSave(side, {}, m_pendingRequestId)) {
        clearPendingSave();
        setState(QStringLiteral("Narration could not be removed."), QStringLiteral("The card update could not be queued. Try again after other changes finish."));
        return;
    }
    setState(QStringLiteral("Removing narration."));
}

void CardNarration::confirmRecording(const QString &side) {
    if (!m_sessionActive || !m_app || !m_displayedCard || !validSide(side)
        || m_recording || m_waitingForCapture || m_busy || hasDraft()) return;
    const struct betterflash::model::Card displayedCard = *m_displayedCard;
    if (!betterflash::model::narrationIsStale(displayedCard, m_variantKey, side)) return;
    const QString name = displayedCard.narrations.value(m_variantKey + QLatin1Char('/') + side).toString();
    const QString fingerprint = betterflash::model::narrationFingerprint(displayedCard, m_variantKey, side);
    if (name.isEmpty() || fingerprint.isEmpty()) {
        setState(QStringLiteral("Recording could not be confirmed."), QStringLiteral("The card or recording changed. Reopen narration and check this side again."));
        return;
    }
    stopPlaybackInternal();
    m_pendingRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingSide = side;
    m_pendingName = name;
    m_pendingAction = PendingAction::Confirm;
    m_busy = true;
    if (!m_app->confirmCardNarration(m_cardId, m_variantKey, side, name, fingerprint, m_pendingRequestId)) {
        clearPendingSave();
        setState(QStringLiteral("Recording could not be confirmed."), QStringLiteral("The card update could not be queued. Try again after other changes finish."));
        return;
    }
    setState(QStringLiteral("Confirming that the recording matches the text."));
}
