#include <QtTest>

#include "voice/VoiceAudio.h"
#include "voice/VoiceController.h"
#include "voice/VoiceTranscriber.h"
#include "voice/VoiceWorker.h"

#include <QAudioSource>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
class DelayedReply final : public QNetworkReply {
public:
    DelayedReply(const QNetworkRequest &request, QByteArray body, const int status, const int delay,
        const QNetworkReply::NetworkError error, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body)) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (error != QNetworkReply::NoError) setError(error, QStringLiteral("Simulated provider failure"));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delay, this, [this] {
            m_complete = true;
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Cancelled test request"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_complete ? m_body.size() - m_position : 0; }
protected:
    qint64 readData(char *const target, const qint64 capacity) override {
        if (!m_complete) return 0;
        const qint64 count = std::min(capacity, static_cast<qint64>(m_body.size()) - m_position);
        if (count <= 0) return -1;
        std::memcpy(target, m_body.constData() + m_position, static_cast<size_t>(count));
        m_position += count;
        return count;
    }
private:
    const QByteArray m_body;
    qint64 m_position = 0;
    bool m_complete = false;
};

class MockSpeechNetwork final : public QNetworkAccessManager {
public:
    QByteArray response = QByteArrayLiteral("{\"text\":\"good\"}");
    int status = 200;
    int delay = 10;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QList<QNetworkRequest> requests;
    QList<QByteArray> uploads;
protected:
    QNetworkReply *createRequest(const Operation operation, const QNetworkRequest &request, QIODevice *const outgoing) override {
        Q_ASSERT(operation == PostOperation && outgoing);
        requests.append(request);
        uploads.append(outgoing->readAll());
        return new DelayedReply(request, response, status, delay, error, this);
    }
};

QByteArray pcmFrame(const qint16 amplitude) {
    QByteArray frame;
    frame.reserve(VoiceUtteranceBuffer::FrameBytes);
    const qint16 little = qToLittleEndian(amplitude);
    for (int sample = 0; sample < VoiceUtteranceBuffer::FrameBytes / 2; ++sample)
        frame.append(reinterpret_cast<const char *>(&little), 2);
    return frame;
}
}

class VoiceTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void commands_data();
    void commands();
    void answersAreNotCommands_data();
    void answersAreNotCommands();
    void explicitPartialRecall();
    void namedDeckAndSpokenDays();
    void silenceIsBoundedAndNeverSubmitted();
    void silenceEndsSpeech();
    void utterancesAreLimitedToSixtySeconds();
    void cancelledRecordingIsDiscarded();
    void nativeStereoConversionIsContinuous();
    void invalidSamplesAreClamped();
    void wavIsPcmMono16k();
    void requestsHaveBoundedTimeAndNoRedirects();
    void cancelledHttpRepliesCannotDispatch();
    void replacedUtteranceUsesTheCurrentEpoch();
    void invalidHttpResponsesSurfaceErrors_data();
    void invalidHttpResponsesSurfaceErrors();
    void networkFailuresSurfaceErrors_data();
    void networkFailuresSurfaceErrors();
    void missingKeyCannotStartCapture();
    void invalidKeyCannotStartCapture_data();
    void invalidKeyCannotStartCapture();
    void textCommandsUseTheReviewDispatchPath();
    void nonsecretConfigurationPersists();
private:
    QTemporaryDir m_settings;
};

void VoiceTest::initTestCase() {
    QVERIFY(m_settings.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("BetterFlashVoiceTests"));
    QCoreApplication::setApplicationName(QStringLiteral("VoiceTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
}

void VoiceTest::commands_data() {
    QTest::addColumn<QString>("phrase");
    QTest::addColumn<QString>("command");
    QTest::newRow("start") << QStringLiteral("start review") << QStringLiteral("start_review");
    QTest::newRow("reveal") << QStringLiteral("Flashcard, show answer.") << QStringLiteral("show_answer");
    QTest::newRow("good") << QStringLiteral("good") << QStringLiteral("good");
    QTest::newRow("missed") << QStringLiteral("flashcard missed") << QStringLiteral("missed");
    QTest::newRow("hard") << QStringLiteral("HARD!") << QStringLiteral("hard");
    QTest::newRow("easy") << QStringLiteral("easy") << QStringLiteral("easy");
    QTest::newRow("again") << QStringLiteral("again") << QStringLiteral("again");
    QTest::newRow("defer") << QStringLiteral("defer card") << QStringLiteral("defer");
    QTest::newRow("pause") << QStringLiteral("pause review") << QStringLiteral("pause");
    QTest::newRow("resume") << QStringLiteral("resume") << QStringLiteral("resume");
    QTest::newRow("end") << QStringLiteral("end review") << QStringLiteral("stop");
    QTest::newRow("repeat") << QStringLiteral("repeat") << QStringLiteral("repeat");
}

void VoiceTest::commands() {
    QFETCH(QString, phrase);
    QFETCH(QString, command);
    const struct VoiceWorker::ParsedUtterance parsed = VoiceWorker::parseUtterance(phrase);
    QVERIFY(parsed.isCommand);
    QCOMPARE(parsed.command, command);
}

void VoiceTest::answersAreNotCommands_data() {
    QTest::addColumn<QString>("answer");
    QTest::newRow("embedded-grade") << QStringLiteral("That was a good example of partial pressure");
    QTest::newRow("embedded-number") << QStringLiteral("Two of three laws describe motion");
    QTest::newRow("embedded-start") << QStringLiteral("We start reviewing in the fall");
    QTest::newRow("embedded-pause") << QStringLiteral("A pause can be hard to detect");
    QTest::newRow("name") << QStringLiteral("Groq, Vosk and Anki");
    QTest::newRow("fraction-zero") << QStringLiteral("partial two of zero");
    QTest::newRow("fraction-overflow") << QStringLiteral("partial four of three");
    QTest::newRow("invalid-days") << QStringLiteral("postpone 999999999999 days");
    QTest::newRow("negative-days") << QStringLiteral("postpone -1 days");
    QTest::newRow("zero-days") << QStringLiteral("postpone zero days");
    QTest::newRow("empty") << QString();
}

void VoiceTest::answersAreNotCommands() {
    QFETCH(QString, answer);
    QVERIFY(!VoiceWorker::parseUtterance(answer).isCommand);
}

void VoiceTest::explicitPartialRecall() {
    const struct VoiceWorker::ParsedUtterance first = VoiceWorker::parseUtterance(QStringLiteral("two of three"));
    QCOMPARE(first.command, QStringLiteral("partial"));
    QCOMPARE(first.argument.toDouble(), 2.0 / 3.0);
    const struct VoiceWorker::ParsedUtterance second = VoiceWorker::parseUtterance(QStringLiteral("Flashcard partial twenty-one out of thirty points."));
    QCOMPARE(second.argument.toDouble(), 0.7);
    const struct VoiceWorker::ParsedUtterance defaultPartial = VoiceWorker::parseUtterance(QStringLiteral("partial"));
    QCOMPARE(defaultPartial.argument.toDouble(), 0.5);
}

void VoiceTest::namedDeckAndSpokenDays() {
    const struct VoiceWorker::ParsedUtterance deck = VoiceWorker::parseUtterance(QStringLiteral("Flashcard start review of Human Biology."));
    QCOMPARE(deck.command, QStringLiteral("start_review"));
    QCOMPARE(deck.argument.toString(), QStringLiteral("Human Biology"));
    const struct VoiceWorker::ParsedUtterance days = VoiceWorker::parseUtterance(QStringLiteral("postpone for fourteen days"));
    QCOMPARE(days.command, QStringLiteral("postpone"));
    QCOMPARE(days.argument.toInt(), 14);
    const struct VoiceWorker::ParsedUtterance weeks = VoiceWorker::parseUtterance(QStringLiteral("postpone one week"));
    QCOMPARE(weeks.argument.toInt(), 7);
}

void VoiceTest::silenceIsBoundedAndNeverSubmitted() {
    VoiceUtteranceBuffer buffer;
    const QByteArray silence = pcmFrame(0);
    for (int frame = 0; frame < 5000; ++frame) {
        QVERIFY(!buffer.append(silence));
        QVERIFY(buffer.bufferedBytes() <= VoiceUtteranceBuffer::FrameBytes * 10);
    }
}

void VoiceTest::silenceEndsSpeech() {
    VoiceUtteranceBuffer buffer;
    const QByteArray speech = pcmFrame(3000);
    const QByteArray silence = pcmFrame(0);
    for (int frame = 0; frame < 10; ++frame) QVERIFY(!buffer.append(speech));
    for (int frame = 0; frame < 44; ++frame) QVERIFY(!buffer.append(silence));
    const auto utterance = buffer.append(silence);
    QVERIFY(utterance.has_value());
    QCOMPARE(utterance->size(), VoiceUtteranceBuffer::FrameBytes * 55);
    QCOMPARE(buffer.bufferedBytes(), 0);
    for (int frame = 0; frame < 100; ++frame) QVERIFY(!buffer.append(silence));
}

void VoiceTest::utterancesAreLimitedToSixtySeconds() {
    VoiceUtteranceBuffer buffer;
    const QByteArray speech = pcmFrame(1000);
    for (int frame = 0; frame < 2999; ++frame) QVERIFY(!buffer.append(speech));
    const auto utterance = buffer.append(speech);
    QVERIFY(utterance.has_value());
    QCOMPARE(utterance->size(), VoiceUtteranceBuffer::MaxBytes);
}

void VoiceTest::cancelledRecordingIsDiscarded() {
    VoiceUtteranceBuffer buffer;
    const QByteArray speech = pcmFrame(1000);
    const QByteArray silence = pcmFrame(0);
    for (int frame = 0; frame < 10; ++frame) QVERIFY(!buffer.append(speech));
    buffer.reset();
    for (int frame = 0; frame < 100; ++frame) QVERIFY(!buffer.append(silence));
    QVERIFY(buffer.bufferedBytes() <= VoiceUtteranceBuffer::FrameBytes * 10);
}

void VoiceTest::nativeStereoConversionIsContinuous() {
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    QByteArray native;
    for (int frame = 0; frame < 4800; ++frame) {
        const float left = static_cast<float>(std::sin(frame * 0.03) * 0.8);
        const float right = left * 0.25F;
        native.append(reinterpret_cast<const char *>(&left), sizeof(left));
        native.append(reinterpret_cast<const char *>(&right), sizeof(right));
    }
    VoicePcmConverter whole;
    VoicePcmConverter chunks;
    QVERIFY(whole.configure(format));
    QVERIFY(chunks.configure(format));
    const QByteArray expected = whole.convert(native);
    QByteArray actual;
    for (qsizetype position = 0; position < native.size(); position += 127)
        actual.append(chunks.convert(QByteArrayView(native).sliced(position, std::min(qsizetype(127), native.size() - position))));
    QCOMPARE(actual, expected);
    QCOMPARE(actual.size(), 3200);
}

void VoiceTest::invalidSamplesAreClamped() {
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    VoicePcmConverter converter;
    QVERIFY(converter.configure(format));
    const float samples[] = {2.0F, -2.0F, std::numeric_limits<float>::infinity(), 0.0F};
    const QByteArray pcm = converter.convert(QByteArrayView(reinterpret_cast<const char *>(samples), sizeof(samples)));
    QCOMPARE(qFromLittleEndian<qint16>(pcm.constData()), qint16(32767));
    QCOMPARE(qFromLittleEndian<qint16>(pcm.constData() + 2), qint16(-32768));
    QCOMPARE(qFromLittleEndian<qint16>(pcm.constData() + 4), qint16(0));
    QAudioFormat invalid;
    QVERIFY(!converter.configure(invalid));
}

void VoiceTest::wavIsPcmMono16k() {
    const QByteArray pcm = pcmFrame(2000);
    const QByteArray wav = voiceWav(pcm);
    QCOMPARE(wav.first(4), QByteArrayLiteral("RIFF"));
    QCOMPARE(wav.sliced(8, 4), QByteArrayLiteral("WAVE"));
    QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 20), quint16(1));
    QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 22), quint16(1));
    QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + 24), quint32(16000));
    QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 34), quint16(16));
    QCOMPARE(wav.sliced(44), pcm);
}

void VoiceTest::requestsHaveBoundedTimeAndNoRedirects() {
    const QNetworkRequest request = VoiceWorker::groqRequest(QStringLiteral("test-key"));
    QCOMPARE(request.url().scheme(), QStringLiteral("https"));
    QCOMPARE(request.url().host(), QStringLiteral("api.groq.com"));
    QCOMPARE(request.rawHeader(QByteArrayLiteral("Authorization")), QByteArrayLiteral("Bearer test-key"));
    QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
    QVERIFY(request.transferTimeout() > 0 && request.transferTimeout() <= 30000);
}

void VoiceTest::cancelledHttpRepliesCannotDispatch() {
    MockSpeechNetwork network;
    VoiceTranscriber transcriber(QStringLiteral("test-key"), QStringLiteral("whisper-large-v3-turbo"), nullptr, &network);
    const QSignalSpy transcripts(&transcriber, &VoiceTranscriber::transcriptReady);
    const QSignalSpy errors(&transcriber, &VoiceTranscriber::errorOccurred);
    const QSignalSpy finished(&transcriber, &VoiceTranscriber::finished);
    transcriber.submit(pcmFrame(1000), 17);
    QVERIFY(transcriber.busy());
    transcriber.cancel();
    QVERIFY(!transcriber.busy());
    QTest::qWait(30);
    QCOMPARE(transcripts.size(), 0);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(finished.size(), 0);
}

void VoiceTest::replacedUtteranceUsesTheCurrentEpoch() {
    MockSpeechNetwork network;
    network.response = QByteArrayLiteral("{\"text\":\"wrong old card\"}");
    VoiceTranscriber transcriber(QStringLiteral("test-key"), QStringLiteral("whisper-large-v3-turbo"), nullptr, &network);
    const QSignalSpy transcripts(&transcriber, &VoiceTranscriber::transcriptReady);
    const QSignalSpy finished(&transcriber, &VoiceTranscriber::finished);
    transcriber.submit(pcmFrame(1000), 17);
    network.response = QByteArrayLiteral("{\"text\":\"current card answer\"}");
    transcriber.submit(pcmFrame(2000), 18);
    QTRY_COMPARE(transcripts.size(), 1);
    QCOMPARE(transcripts.first().at(0).toString(), QStringLiteral("current card answer"));
    QCOMPARE(transcripts.first().at(1).toULongLong(), quint64(18));
    QCOMPARE(finished.size(), 1);
    QVERIFY(!transcriber.busy());
    QCOMPARE(network.requests.size(), 2);
    QVERIFY(network.uploads.last().contains(QByteArrayLiteral("name=\"model\"")));
    QVERIFY(network.uploads.last().contains(QByteArrayLiteral("whisper-large-v3-turbo")));
    QVERIFY(network.uploads.last().contains(QByteArrayLiteral("RIFF")));
    QVERIFY(!network.uploads.last().contains(QByteArrayLiteral("test-key")));
}

void VoiceTest::invalidHttpResponsesSurfaceErrors_data() {
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("code");
    QTest::addColumn<bool>("fatal");
    QTest::newRow("authentication") << 401 << QByteArrayLiteral("{}") << QStringLiteral("GROQ_KEY_REJECTED") << true;
    QTest::newRow("rate-limit") << 429 << QByteArrayLiteral("{}") << QStringLiteral("GROQ_RATE_LIMITED") << false;
    QTest::newRow("redirect") << 302 << QByteArrayLiteral("{}") << QStringLiteral("VOICE_REDIRECT_BLOCKED") << false;
    QTest::newRow("malformed-json") << 200 << QByteArrayLiteral("not JSON") << QStringLiteral("VOICE_TRANSCRIPT_INVALID") << false;
    QTest::newRow("missing-text") << 200 << QByteArrayLiteral("{}") << QStringLiteral("VOICE_TRANSCRIPT_INVALID") << false;
    QTest::newRow("too-large") << 200 << QByteArray(65537, 'a') << QStringLiteral("VOICE_RESPONSE_TOO_LARGE") << false;
}

void VoiceTest::invalidHttpResponsesSurfaceErrors() {
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(QString, code);
    QFETCH(bool, fatal);
    MockSpeechNetwork network;
    network.status = status;
    network.response = body;
    VoiceTranscriber transcriber(QStringLiteral("test-key"), QStringLiteral("whisper-large-v3-turbo"), nullptr, &network);
    const QSignalSpy errors(&transcriber, &VoiceTranscriber::errorOccurred);
    const QSignalSpy transcripts(&transcriber, &VoiceTranscriber::transcriptReady);
    transcriber.submit(pcmFrame(1000), 42);
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().at(0).toString(), code);
    QCOMPARE(errors.first().at(2).toBool(), fatal);
    QCOMPARE(errors.first().at(3).toULongLong(), quint64(42));
    QCOMPARE(transcripts.size(), 0);
    QVERIFY(!transcriber.busy());
}

void VoiceTest::networkFailuresSurfaceErrors_data() {
    QTest::addColumn<int>("networkError");
    QTest::addColumn<QString>("code");
    QTest::newRow("timeout") << int(QNetworkReply::TimeoutError) << QStringLiteral("VOICE_REQUEST_TIMEOUT");
    QTest::newRow("network-unavailable") << int(QNetworkReply::NetworkSessionFailedError) << QStringLiteral("VOICE_NETWORK_ERROR");
}

void VoiceTest::networkFailuresSurfaceErrors() {
    QFETCH(int, networkError);
    QFETCH(QString, code);
    MockSpeechNetwork network;
    network.status = 0;
    network.error = static_cast<QNetworkReply::NetworkError>(networkError);
    VoiceTranscriber transcriber(QStringLiteral("test-key"), QStringLiteral("whisper-large-v3-turbo"), nullptr, &network);
    const QSignalSpy errors(&transcriber, &VoiceTranscriber::errorOccurred);
    const QSignalSpy finished(&transcriber, &VoiceTranscriber::finished);
    transcriber.submit(pcmFrame(1000), 43);
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().at(0).toString(), code);
    QCOMPARE(finished.size(), 1);
    QVERIFY(!transcriber.busy());
}

void VoiceTest::missingKeyCannotStartCapture() {
    VoiceWorker worker(QString{});
    const QSignalSpy ready(&worker, &VoiceWorker::ready);
    const QSignalSpy errors(&worker, &VoiceWorker::errorOccurred);
    const QSignalSpy transcript(&worker, &VoiceWorker::transcriptReady);
    worker.initialize();
    QCOMPARE(ready.size(), 0);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.first().at(0).toString(), QStringLiteral("GROQ_KEY_MISSING"));
    QVERIFY(worker.findChild<QAudioSource *>() == nullptr);
    worker.setListening(true, 1);
    QCOMPARE(transcript.size(), 0);
}

void VoiceTest::invalidKeyCannotStartCapture_data() {
    QTest::addColumn<QString>("key");
    QTest::newRow("line-break") << QStringLiteral("fake\r\nsecond-line");
    QTest::newRow("oversized") << QString(4097, QLatin1Char('a'));
}

void VoiceTest::invalidKeyCannotStartCapture() {
    QFETCH(QString, key);
    VoiceWorker worker(QString{});
    worker.setApiKey(key);
    const QSignalSpy errors(&worker, &VoiceWorker::errorOccurred);
    worker.initialize();
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.first().at(0).toString(), QStringLiteral("GROQ_KEY_INVALID"));
    QVERIFY(worker.findChild<QAudioSource *>() == nullptr);
}

void VoiceTest::textCommandsUseTheReviewDispatchPath() {
    VoiceController controller;
    const QSignalSpy commands(&controller, &VoiceController::commandRecognized);
    const QSignalSpy answers(&controller, &VoiceController::answerRecognized);
    QVERIFY(!controller.enabled());
    QVERIFY(!controller.listening());
    controller.processTranscript(QStringLiteral("The outcome is good, with partial pressure."));
    QCOMPARE(answers.size(), 1);
    QCOMPARE(commands.size(), 0);
    controller.processTranscript(QStringLiteral("flashcard partial two of three"));
    QCOMPARE(commands.size(), 1);
    QCOMPARE(commands.first().at(0).toString(), QStringLiteral("partial"));
    QCOMPARE(commands.first().at(1).toDouble(), 2.0 / 3.0);
    QVERIFY(!controller.listening());
}

void VoiceTest::nonsecretConfigurationPersists() {
    {
        VoiceController controller;
        controller.setSttProvider(QStringLiteral("local test"));
        controller.setModelPath(QStringLiteral("/tmp/betterflash-test-model"));
        controller.setSttModel(QStringLiteral("whisper-large-v3"));
    }
    VoiceController restored;
    QCOMPARE(restored.sttProvider(), QStringLiteral("local"));
    QCOMPARE(restored.modelPath(), QStringLiteral("/tmp/betterflash-test-model"));
    QCOMPARE(restored.sttModel(), QStringLiteral("whisper-large-v3"));
    QVERIFY(!restored.enabled());
}

QTEST_MAIN(VoiceTest)
#include "voice_test.moc"
