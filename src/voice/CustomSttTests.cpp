#include "CustomSttTests.h"

#include "VoiceAudio.h"
#include "VoiceWorker.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QSettings>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <algorithm>

namespace {
constexpr qsizetype MaxRecordingBytes = VoiceUtteranceBuffer::MaxBytes;
constexpr qsizetype MaxSourceBytes = 64 * 1024 * 1024;
constexpr int MaxEntries = 5;
static_assert(MaxRecordingBytes % 2 == 0);
static_assert(MaxSourceBytes >= MaxRecordingBytes);

bool managedPath(const QString &directory, const QString &path) {
    const QFileInfo rootInfo(QDir::cleanPath(directory));
    const QFileInfo candidateInfo(QDir::cleanPath(path));
    const QString root = rootInfo.canonicalFilePath() + QDir::separator();
    const QString candidate = candidateInfo.canonicalFilePath();
    static const QRegularExpression fileName(QStringLiteral(R"(^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\.wav$)"));
    return !root.isEmpty() && !candidate.isEmpty() && candidateInfo.isFile() && !candidateInfo.isSymLink()
        && candidate.startsWith(root) && fileName.match(candidateInfo.fileName()).hasMatch();
}

QString normalizedWords(const QString &text) {
    QString value = text.toCaseFolded();
    value.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return value.simplified();
}

struct WaveData {
    QAudioFormat format;
    QByteArray bytes;
};

bool decodeWave(const QByteArray &wave, WaveData *output, QString *error) {
    if (!output || wave.size() < 44 || wave.mid(0, 4) != QByteArrayLiteral("RIFF")
        || wave.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        if (error) *error = QStringLiteral("Import a PCM WAV recording.");
        return false;
    }
    int offset = 12;
    quint16 formatCode = 0;
    quint16 channels = 0;
    quint16 bits = 0;
    quint32 sampleRate = 0;
    int dataOffset = -1;
    quint32 dataSize = 0;
    while (offset + 8 <= wave.size()) {
        const QByteArray id = wave.mid(offset, 4);
        const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(wave.constData() + offset + 4));
        offset += 8;
        if (size > static_cast<quint32>(wave.size() - offset)) break;
        if (id == QByteArrayLiteral("fmt ") && size >= 16) {
            const uchar *const data = reinterpret_cast<const uchar *>(wave.constData() + offset);
            formatCode = qFromLittleEndian<quint16>(data);
            channels = qFromLittleEndian<quint16>(data + 2);
            sampleRate = qFromLittleEndian<quint32>(data + 4);
            bits = qFromLittleEndian<quint16>(data + 14);
        } else if (id == QByteArrayLiteral("data")) {
            dataOffset = offset;
            dataSize = size;
            break;
        }
        offset += static_cast<int>(size) + static_cast<int>(size & 1U);
    }
    if (formatCode != 1 || channels < 1 || channels > 8 || sampleRate < 8000 || sampleRate > 192000
        || (bits != 8 && bits != 16 && bits != 32) || dataOffset < 0 || dataSize == 0
        || dataSize > static_cast<quint32>(MaxSourceBytes)) {
        if (error) *error = QStringLiteral("The WAV must contain bounded PCM audio at 8-192 kHz.");
        return false;
    }
    QAudioFormat format;
    format.setSampleRate(static_cast<int>(sampleRate));
    format.setChannelCount(static_cast<int>(channels));
    format.setSampleFormat(bits == 8 ? QAudioFormat::UInt8 : bits == 16 ? QAudioFormat::Int16 : QAudioFormat::Int32);
    if (dataSize % format.bytesPerFrame() != 0) {
        if (error) *error = QStringLiteral("The WAV contains an incomplete audio frame. Export it as PCM WAV again.");
        return false;
    }
    const double sourceDuration = static_cast<double>(dataSize) / (sampleRate * channels * (bits / 8));
    if (sourceDuration > 60.0) {
        if (error) *error = QStringLiteral("The recording must be no longer than 60 seconds.");
        return false;
    }
    output->format = format;
    output->bytes = wave.mid(dataOffset, static_cast<qsizetype>(dataSize));
    return true;
}
}

CustomSttTests::CustomSttTests(QObject *parent)
    : QObject(parent), m_directory(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("voice/custom-tests"))) {
    QDir().mkpath(m_directory);
    load();
}

CustomSttTests::~CustomSttTests() {
    cancelTests();
    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
    }
    const QDeadlineTimer deadline(1000);
    for (const QPointer<QThread> &thread : std::as_const(m_threads))
        if (thread && thread->isRunning() && !deadline.hasExpired()) thread->wait(deadline);
}

QVariantList CustomSttTests::entries() const {
    QVariantList result;
    for (const struct Entry &entry : m_entries) {
        result.append(QVariantMap{{QStringLiteral("id"), entry.id}, {QStringLiteral("label"), entry.label},
            {QStringLiteral("expected"), entry.expected}, {QStringLiteral("transcript"), entry.transcript},
            {QStringLiteral("status"), entry.status}, {QStringLiteral("error"), entry.error},
            {QStringLiteral("duration"), entry.duration}, {QStringLiteral("wordErrorRate"), entry.wordErrorRate},
            {QStringLiteral("accuracy"), entry.wordErrorRate < 0.0 ? -1.0 : std::max(0.0, 1.0 - entry.wordErrorRate)},
            {QStringLiteral("substitutions"), entry.substitutions}, {QStringLiteral("deletions"), entry.deletions},
            {QStringLiteral("insertions"), entry.insertions}, {QStringLiteral("source"), entry.path}});
    }
    return result;
}

bool CustomSttTests::canRun() const {
    if (m_busy || m_recording || m_entries.isEmpty()) return false;
    if (m_provider == QStringLiteral("local") && (!QDir(m_modelPath).exists()
        || (!QFileInfo::exists(QDir(m_modelPath).filePath(QStringLiteral("../runtime/libvosk.so")))
            && !QFileInfo::exists(QDir(m_modelPath).filePath(QStringLiteral("../runtime/libvosk.dll")))
            && qEnvironmentVariableIsEmpty("BETTERFLASH_VOSK_LIBRARY")))) return false;
    if (m_provider == QStringLiteral("groq") && m_apiKey.isEmpty()) return false;
    for (const struct Entry &entry : m_entries)
        if (entry.expected.trimmed().isEmpty() || entry.path.isEmpty()) return false;
    return true;
}

void CustomSttTests::setConfiguration(QString provider, QString modelPath, QString model, QString apiKey) {
    provider = provider.trimmed().toLower();
    modelPath = modelPath.trimmed();
    model = model.trimmed();
    apiKey = apiKey.trimmed();
    if (m_provider == provider && m_modelPath == modelPath && m_model == model && m_apiKey == apiKey) return;
    if (m_busy) cancelTests();
    const QString nextConfiguration = provider + QLatin1Char('|') + modelPath + QLatin1Char('|') + model;
    const QString nextCredentialHash = QString::fromLatin1(QCryptographicHash::hash(apiKey.toUtf8(), QCryptographicHash::Sha256).toHex());
    bool resultsChanged = false;
    for (struct Entry &entry : m_entries) {
        if (!entry.resultConfiguration.isEmpty() && (entry.resultConfiguration != nextConfiguration || entry.resultCredentialHash != nextCredentialHash)) {
            resultsChanged = true;
            entry.transcript.clear();
            entry.status = entry.expected.isEmpty() ? QStringLiteral("Needs expected transcript") : QStringLiteral("Ready");
            entry.error.clear();
            entry.wordErrorRate = -1.0;
            entry.substitutions = entry.deletions = entry.insertions = 0;
            entry.resultConfiguration.clear();
            entry.resultCredentialHash.clear();
        }
    }
    m_provider = std::move(provider);
    m_modelPath = std::move(modelPath);
    m_model = std::move(model);
    m_apiKey = std::move(apiKey);
    persist();
    if (resultsChanged) emit entriesChanged();
    emit stateChanged();
}

void CustomSttTests::addRecording(const QUrl &source) {
    if (m_recording || m_busy || m_entries.size() >= MaxEntries) return;
    QString path;
    double duration = 0.0;
    QString error;
    if (!importWave(source, &path, &duration, &error)) {
        setState(false, QStringLiteral("Recording import failed."), error);
        return;
    }
    struct Entry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.label = QFileInfo(source.toLocalFile()).completeBaseName();
    entry.path = path;
    entry.duration = duration;
    entry.status = QStringLiteral("Needs expected transcript");
    m_entries.append(std::move(entry));
    if (!persist()) { m_entries.removeLast(); QFile::remove(path); setState(false, QStringLiteral("Recording import failed."), QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage.")); return; }
    notifyEntries();
    setState(false, QStringLiteral("Add an expected transcript before running the test."));
}

void CustomSttTests::setExpected(const QString &id, const QString &text) {
    if (m_busy || m_recording) return;
    struct Entry *const entry = find(id);
    if (!entry) return;
    const QString value = text.trimmed();
    if (value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size() > 512) {
        setState(false, QStringLiteral("Expected transcript rejected."), QStringLiteral("Use at most 512 words per recording."));
        return;
    }
    if (entry->expected == value) return;
    entry->expected = value;
    entry->transcript.clear();
    entry->status = value.isEmpty() ? QStringLiteral("Needs expected transcript") : QStringLiteral("Ready");
    entry->error.clear();
    entry->wordErrorRate = -1.0;
    entry->substitutions = entry->deletions = entry->insertions = 0;
    entry->resultConfiguration.clear();
    persist();
    notifyEntries();
    emit stateChanged();
}

void CustomSttTests::setLabel(const QString &id, const QString &text) {
    if (m_busy || m_recording) return;
    struct Entry *const entry = find(id);
    if (!entry) return;
    const QString value = text.trimmed();
    if (value.isEmpty() || entry->label == value) return;
    entry->label = value;
    if (!persist()) { setState(false, QStringLiteral("Label could not be saved."), QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage.")); return; }
    notifyEntries();
}

void CustomSttTests::removeRecording(const QString &id) {
    if (m_busy || m_recording) return;
    const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&id](const struct Entry &entry) { return entry.id == id; });
    if (it == m_entries.cend()) return;
    const struct Entry removed = *it;
    const qsizetype index = std::distance(m_entries.cbegin(), it);
    m_entries.erase(it);
    if (!persist()) { m_entries.insert(index, removed); setState(false, QStringLiteral("Recording removal failed."), QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage.")); return; }
    if (managedPath(m_directory, removed.path)) QFile::remove(removed.path);
    notifyEntries();
    emit stateChanged();
}

void CustomSttTests::runTests() {
    if (!canRun()) return;
    if (m_provider == QStringLiteral("local") && m_modelPath.isEmpty()) { setState(false, QStringLiteral("Tests cannot run."), QStringLiteral("Select a local speech model first.")); return; }
    if (m_provider == QStringLiteral("groq") && m_apiKey.isEmpty()) { setState(false, QStringLiteral("Tests cannot run."), QStringLiteral("Add a Groq API key first.")); return; }
    const quint64 generation = ++m_generation;
    const QVector<struct Entry> savedEntries = m_entries;
    m_busy = true;
    m_current = -1;
    for (struct Entry &entry : m_entries) { entry.transcript.clear(); entry.error.clear(); entry.status = QStringLiteral("Queued"); entry.wordErrorRate = -1.0; entry.substitutions = entry.deletions = entry.insertions = 0; entry.resultConfiguration.clear(); entry.resultCredentialHash.clear(); }
    if (!persist()) {
        m_entries = savedEntries;
        m_busy = false;
        setState(false, QStringLiteral("Custom tests could not start."), QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage."));
        notifyEntries();
        return;
    }
    notifyEntries();
    setState(true, QStringLiteral("Running custom speech tests."));
    startNext(generation);
}

void CustomSttTests::cancelTests() {
    if (!m_busy && !m_recording) return;
    if (m_recording) stopRecording();
    ++m_generation;
    m_busy = false;
    retireWorker();
    for (struct Entry &entry : m_entries) if (entry.status == QStringLiteral("Queued") || entry.status == QStringLiteral("Running")) entry.status = QStringLiteral("Canceled");
    notifyEntries();
    if (!persist()) setState(false, QStringLiteral("Custom tests canceled."), QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage."));
    else setState(false, QStringLiteral("Custom tests canceled."));
}

void CustomSttTests::startRecording() {
    if (m_busy || m_recording || m_entries.size() >= MaxEntries) {
        if (m_entries.size() >= MaxEntries) setState(false, QStringLiteral("Recording unavailable."), QStringLiteral("Custom tests support up to five recordings."));
        return;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) { setState(false, QStringLiteral("Recording unavailable."), QStringLiteral("No microphone is available.")); return; }
    const QAudioFormat format = device.preferredFormat();
    if (!m_converter.configure(format)) { setState(false, QStringLiteral("Recording unavailable."), QStringLiteral("The default microphone format is unsupported.")); return; }
    m_recordedPcm.clear();
    m_audioSource = new QAudioSource(device, format, this);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) { delete m_audioSource; m_audioSource = nullptr; setState(false, QStringLiteral("Recording unavailable."), QStringLiteral("The microphone could not be opened.")); return; }
    connect(m_audioDevice, &QIODevice::readyRead, this, [this] {
        const QByteArray converted = m_converter.convert(m_audioDevice->readAll());
        const qsizetype available = MaxRecordingBytes - m_recordedPcm.size();
        if (available > 0) m_recordedPcm.append(converted.constData(), std::min(available & ~qsizetype(1), converted.size()));
        if (m_recordedPcm.size() >= MaxRecordingBytes) stopRecording();
    });
    connect(m_audioSource, &QAudioSource::stateChanged, this, [this](const QAudio::State) {
        if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
            const QString message = QStringLiteral("The microphone reported an audio capture error.");
            m_audioSource->stop();
            delete m_audioSource;
            m_audioSource = nullptr;
            m_audioDevice = nullptr;
            m_recordedPcm.clear();
            m_recording = false;
            setState(false, QStringLiteral("Recording failed."), message);
        }
    });
    m_recording = true;
    setState(false, QStringLiteral("Recording. Stop when the phrase is complete."));
}

void CustomSttTests::stopRecording() {
    if (!m_recording || !m_audioSource) return;
    m_audioSource->stop();
    m_audioDevice = nullptr;
    m_recording = false;
    if (m_recordedPcm.size() < VoiceUtteranceBuffer::FrameBytes) { delete m_audioSource; m_audioSource = nullptr; m_recordedPcm.clear(); setState(false, QStringLiteral("Recording discarded."), QStringLiteral("Record at least 20 ms of audio.")); return; }
    if (m_entries.size() >= MaxEntries) { delete m_audioSource; m_audioSource = nullptr; m_recordedPcm.clear(); setState(false, QStringLiteral("Recording discarded."), QStringLiteral("Custom tests support up to five recordings.")); return; }
    struct Entry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.label = QStringLiteral("Microphone recording %1").arg(m_entries.size() + 1);
    entry.path = QDir(m_directory).filePath(entry.id + QStringLiteral(".wav"));
    entry.duration = static_cast<double>(m_recordedPcm.size()) / (VoiceUtteranceBuffer::SampleRate * 2.0);
    entry.status = QStringLiteral("Needs expected transcript");
    QSaveFile file(entry.path);
    if (!file.open(QIODevice::WriteOnly) || file.write(voiceWav(m_recordedPcm)) != 44 + m_recordedPcm.size() || !file.commit()) { file.cancelWriting(); QFile::remove(entry.path); delete m_audioSource; m_audioSource = nullptr; m_recordedPcm.clear(); setState(false, QStringLiteral("Recording could not be saved."), QStringLiteral("Check available application storage.")); return; }
    const QString savedPath = entry.path;
    m_entries.append(std::move(entry));
    delete m_audioSource;
    m_audioSource = nullptr;
    m_recordedPcm.clear();
    if (!persist()) { m_entries.removeLast(); QFile::remove(savedPath); setState(false, QStringLiteral("Recording could not be saved."), QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage.")); return; }
    notifyEntries();
    setState(false, QStringLiteral("Add an expected transcript before running the test."));
}

struct CustomSttTests::Entry *CustomSttTests::find(const QString &id) { for (struct Entry &entry : m_entries) if (entry.id == id) return &entry; return nullptr; }
const struct CustomSttTests::Entry *CustomSttTests::find(const QString &id) const { for (const struct Entry &entry : m_entries) if (entry.id == id) return &entry; return nullptr; }

void CustomSttTests::load() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice/customTests"));
    const QStringList ids = settings.value(QStringLiteral("ids")).toStringList();
    for (const QString &id : ids) {
        struct Entry entry;
        entry.id = id;
        entry.label = settings.value(id + QStringLiteral("/label")).toString();
        entry.path = settings.value(id + QStringLiteral("/path")).toString();
        entry.expected = settings.value(id + QStringLiteral("/expected")).toString();
        if (!managedPath(m_directory, entry.path) || !QFileInfo::exists(entry.path)) continue;
        entry.duration = settings.value(id + QStringLiteral("/duration")).toDouble();
        entry.status = entry.expected.isEmpty() ? QStringLiteral("Needs expected transcript") : QStringLiteral("Ready");
        entry.transcript = settings.value(id + QStringLiteral("/transcript")).toString();
        entry.error = settings.value(id + QStringLiteral("/error")).toString();
        entry.wordErrorRate = settings.value(id + QStringLiteral("/wer"), -1.0).toDouble();
        entry.substitutions = settings.value(id + QStringLiteral("/substitutions")).toInt();
        entry.deletions = settings.value(id + QStringLiteral("/deletions")).toInt();
        entry.insertions = settings.value(id + QStringLiteral("/insertions")).toInt();
        entry.resultConfiguration = settings.value(id + QStringLiteral("/resultConfiguration")).toString();
        entry.resultCredentialHash = settings.value(id + QStringLiteral("/resultCredentialHash")).toString();
        if (!entry.transcript.isEmpty() && !entry.resultConfiguration.isEmpty()) entry.status = settings.value(id + QStringLiteral("/status"), QStringLiteral("Completed")).toString();
        if (entry.status == QStringLiteral("Queued") || entry.status == QStringLiteral("Running")) entry.status = QStringLiteral("Canceled");
        m_entries.append(std::move(entry));
        if (m_entries.size() == MaxEntries) break;
    }
}

bool CustomSttTests::persist() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice/customTests"));
    QStringList ids;
    for (const struct Entry &entry : m_entries) {
        ids.append(entry.id);
        settings.setValue(entry.id + QStringLiteral("/label"), entry.label);
        settings.setValue(entry.id + QStringLiteral("/path"), entry.path);
        settings.setValue(entry.id + QStringLiteral("/expected"), entry.expected);
        settings.setValue(entry.id + QStringLiteral("/duration"), entry.duration);
        settings.setValue(entry.id + QStringLiteral("/transcript"), entry.transcript);
        settings.setValue(entry.id + QStringLiteral("/status"), entry.status);
        settings.setValue(entry.id + QStringLiteral("/error"), entry.error);
        settings.setValue(entry.id + QStringLiteral("/wer"), entry.wordErrorRate);
        settings.setValue(entry.id + QStringLiteral("/substitutions"), entry.substitutions);
        settings.setValue(entry.id + QStringLiteral("/deletions"), entry.deletions);
        settings.setValue(entry.id + QStringLiteral("/insertions"), entry.insertions);
        settings.setValue(entry.id + QStringLiteral("/resultConfiguration"), entry.resultConfiguration);
        settings.setValue(entry.id + QStringLiteral("/resultCredentialHash"), entry.resultCredentialHash);
    }
    settings.setValue(QStringLiteral("ids"), ids);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        m_status = QStringLiteral("Custom test data could not be saved.");
        m_error = QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage permissions and available space.");
        emit stateChanged();
        return false;
    }
    return true;
}

void CustomSttTests::notifyEntries() { emit entriesChanged(); emit stateChanged(); }
void CustomSttTests::setState(const bool busy, QString status, QString error) { m_busy = busy; m_status = std::move(status); m_error = std::move(error); emit stateChanged(); }

bool CustomSttTests::importWave(const QUrl &source, QString *path, double *duration, QString *error) const {
    if (!path || !duration || !source.isLocalFile()) { if (error) *error = QStringLiteral("Choose a local WAV recording."); return false; }
    QFile sourceFile(source.toLocalFile());
    if (!sourceFile.open(QIODevice::ReadOnly) || sourceFile.size() > MaxSourceBytes) { if (error) *error = QStringLiteral("The recording could not be opened or is too large."); return false; }
    WaveData wave;
    if (!decodeWave(sourceFile.readAll(), &wave, error)) return false;
    VoicePcmConverter converter;
    if (!converter.configure(wave.format)) { if (error) *error = QStringLiteral("The WAV format is not supported."); return false; }
    QByteArray pcm = converter.convert(wave.bytes);
    if (pcm.size() > MaxRecordingBytes || pcm.size() < VoiceUtteranceBuffer::FrameBytes) { if (error) *error = QStringLiteral("The recording must be between 20 ms and 60 seconds."); return false; }
    const QString target = QDir(m_directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".wav"));
    QSaveFile targetFile(target);
    if (!targetFile.open(QIODevice::WriteOnly) || targetFile.write(voiceWav(pcm)) != 44 + pcm.size() || !targetFile.commit()) { targetFile.cancelWriting(); QFile::remove(target); if (error) *error = QStringLiteral("The normalized recording could not be saved."); return false; }
    *path = target;
    *duration = static_cast<double>(pcm.size()) / (VoiceUtteranceBuffer::SampleRate * 2.0);
    return true;
}

void CustomSttTests::startNext(const quint64 generation) {
    if (!m_busy || generation != m_generation) return;
    ++m_current;
    if (m_current >= m_entries.size()) {
        m_busy = false;
        m_status = QStringLiteral("Custom speech tests complete.");
        m_error.clear();
        if (!persist()) m_error = QStringLiteral("VOICE_CUSTOM_SAVE: Check application storage.");
        emit stateChanged();
        emit entriesChanged();
        return;
    }
    struct Entry &entry = m_entries[m_current];
    entry.status = QStringLiteral("Running");
    notifyEntries();
    auto *const thread = new QThread;
    auto *const worker = new VoiceWorker(m_modelPath);
    worker->setProvider(m_provider);
    worker->setSttModel(m_model);
    worker->setApiKey(m_apiKey);
    worker->setRuntimePath(QDir::cleanPath(QDir(m_modelPath).filePath(QStringLiteral("../runtime"))));
    worker->setOpenTranscriptionTest(true);
    const QString id = entry.id;
    const QString recordingPath = entry.path;
    worker->moveToThread(thread);
    m_thread = thread;
    m_worker = worker;
    m_threads.append(thread);
    connect(thread, &QThread::started, worker, &VoiceWorker::initializeForTest);
    const quint64 workerEpoch = ++m_nextWorkerGeneration;
    Q_ASSERT(workerEpoch != 0);
    m_workerGeneration = workerEpoch;
    connect(worker, &VoiceWorker::ready, worker, [worker, recordingPath] {
        QFile file(recordingPath);
        if (!file.open(QIODevice::ReadOnly)) { emit worker->testFinished({}, false, QStringLiteral("The recording could not be opened.")); return; }
        worker->testAudio(file.readAll());
    });
    connect(worker, &VoiceWorker::testFinished, this, [this, generation, workerEpoch, id](const QString &transcript, const bool, const QString &workerError) { finishCurrent(generation, workerEpoch, id, transcript, workerError); });
    connect(worker, &VoiceWorker::errorOccurred, this, [this, generation, workerEpoch, id](const QString &code, const QString &message, const bool) { finishCurrent(generation, workerEpoch, id, {}, code + QStringLiteral(": ") + message); });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    QTimer::singleShot(45000, this, [this, generation, workerEpoch, id] {
        if (m_busy && generation == m_generation && workerEpoch == m_workerGeneration && m_current >= 0
            && m_current < m_entries.size() && m_entries.at(m_current).id == id)
            finishCurrent(generation, workerEpoch, id, {}, QStringLiteral("VOICE_CUSTOM_TEST_TIMEOUT: The recognition request timed out."));
    });
}

void CustomSttTests::finishCurrent(const quint64 generation, const quint64 workerEpoch, const QString &id, const QString &transcript, const QString &workerError) {
    if (!m_busy || generation != m_generation || workerEpoch != m_workerGeneration) return;
    struct Entry *const entry = find(id);
    if (!entry || m_current < 0 || m_current >= m_entries.size() || m_entries.at(m_current).id != id) return;
    m_workerGeneration = 0;
    const QString boundedTranscript = transcript.left(16384);
    entry->transcript = boundedTranscript;
    entry->error = workerError;
    if (boundedTranscript.isEmpty()) { entry->status = QStringLiteral("Failed"); entry->wordErrorRate = -1.0; }
    else { score(entry->expected, boundedTranscript, &entry->substitutions, &entry->deletions, &entry->insertions, &entry->wordErrorRate); entry->status = workerError.isEmpty() && entry->wordErrorRate == 0.0 ? QStringLiteral("Passed") : QStringLiteral("Completed"); }
    entry->resultConfiguration = m_provider + QLatin1Char('|') + m_modelPath + QLatin1Char('|') + m_model;
    entry->resultCredentialHash = QString::fromLatin1(QCryptographicHash::hash(m_apiKey.toUtf8(), QCryptographicHash::Sha256).toHex());
    persist();
    retireWorker();
    notifyEntries();
    QTimer::singleShot(0, this, [this, generation] { if (m_busy && generation == m_generation) startNext(generation); });
}

void CustomSttTests::retireWorker() {
    const QPointer<VoiceWorker> worker = m_worker;
    const QPointer<QThread> thread = m_thread;
    m_worker = nullptr;
    m_thread = nullptr;
    if (worker && thread) {
        thread->requestInterruption();
        QMetaObject::invokeMethod(worker, [worker, thread] { if (worker) worker->shutdown(); if (thread) thread->quit(); }, Qt::QueuedConnection);
    }
}

void CustomSttTests::score(const QString &expected, const QString &actual, int *substitutions, int *deletions, int *insertions, double *wer) {
    const QStringList a = normalizedWords(expected).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList b = normalizedWords(actual).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const int rows = a.size() + 1;
    const int columns = b.size() + 1;
    QVector<int> cost(rows * columns);
    QVector<char> operation(rows * columns, 0);
    for (int i = 0; i < rows; ++i) cost[i * columns] = i;
    for (int j = 0; j < columns; ++j) cost[j] = j;
    for (int i = 1; i < rows; ++i) for (int j = 1; j < columns; ++j) {
        const int substitution = cost[(i - 1) * columns + j - 1] + (a.at(i - 1) == b.at(j - 1) ? 0 : 1);
        const int deletion = cost[(i - 1) * columns + j] + 1;
        const int insertion = cost[i * columns + j - 1] + 1;
        const int value = std::min({substitution, deletion, insertion});
        cost[i * columns + j] = value;
        operation[i * columns + j] = value == substitution ? 0 : value == deletion ? 1 : 2;
    }
    int i = a.size(), j = b.size(), s = 0, d = 0, ins = 0;
    while (i > 0 || j > 0) { const char op = i > 0 && j > 0 ? operation[i * columns + j] : i > 0 ? 1 : 2; if (op == 0) { if (a.at(i - 1) != b.at(j - 1)) ++s; --i; --j; } else if (op == 1) { ++d; --i; } else { ++ins; --j; } }
    *substitutions = s; *deletions = d; *insertions = ins;
    *wer = a.isEmpty() ? (b.isEmpty() ? 0.0 : 1.0) : static_cast<double>(s + d + ins) / a.size();
}
