#include "SecretStore.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <functional>

SecretStore::SecretStore(QString service, QString environmentVariable, QObject *parent)
    : QObject(parent), m_service(std::move(service)) {
    if (!environmentVariable.isEmpty()) {
        m_key = qEnvironmentVariable(environmentVariable.toLocal8Bit().constData()).trimmed();
        if (m_key.size() > 4096 || m_key.contains(QLatin1Char('\n')) || m_key.contains(QLatin1Char('\r'))) m_key.clear();
        if (!m_key.isEmpty()) {
            m_status = QStringLiteral("Using a key from the environment.");
            return;
        }
    }
    QTimer::singleShot(0, this, &SecretStore::load);
}
SecretStore::~SecretStore() {
    const auto processes = findChildren<QProcess *>();
    for (auto *const process : processes) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) { process->kill(); process->waitForFinished(100); }
    }
}
void SecretStore::run(const QStringList &arguments, const QByteArray &input,
                     const std::function<void(int, const QByteArray &)> &callback) {
    m_operations.enqueue(Operation{arguments, input, callback});
    if (!m_running) runNext();
}
void SecretStore::runNext() {
    Q_ASSERT(!m_running);
    if (m_operations.isEmpty()) return;
    const struct Operation operation = m_operations.dequeue();
    m_running = true;
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (executable.isEmpty()) {
        operation.callback(-1, {});
        m_running = false;
        QTimer::singleShot(0, this, [this] { if (!m_running) runNext(); });
        return;
    }
    auto *const process = new QProcess(this);
    auto *const timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::started, this, [process, input = operation.input, timeout] {
        if (!input.isEmpty()) process->write(input);
        process->closeWriteChannel();
        timeout->start(10000);
    });
    const auto complete = [this, process, callback = operation.callback](const int code) {
        if (process->property("completed").toBool()) return;
        process->setProperty("completed", true);
        const QByteArray result = process->readAllStandardOutput().left(8192);
        callback(code, result);
        process->deleteLater();
        m_running = false;
        QTimer::singleShot(0, this, [this] { if (!m_running) runNext(); });
    };
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(status == QProcess::NormalExit ? code : -1);
    });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(-1);
    });
    process->start(executable, operation.arguments);
}
void SecretStore::load() {
    if (m_revision != 0) return;
    const quint64 revision = m_revision;
    run({QStringLiteral("lookup"), QStringLiteral("application"), QStringLiteral("betterflash"),
         QStringLiteral("service"), m_service}, {}, [this, revision](int code, const QByteArray &output) {
        if (revision != m_revision) return;
        const QByteArray clean = output.trimmed();
        if (code == 0 && !clean.isEmpty() && clean.size() <= 4096 && !clean.contains('\n') && !clean.contains('\r')) {
            m_key = QString::fromUtf8(output).trimmed();
            m_status = QStringLiteral("Stored in the desktop keyring.");
        } else m_status = QStringLiteral("Enter a key to enable this service.");
        emit changed();
    });
}
void SecretStore::setKey(const QString &key) {
    const QString cleaned = key.trimmed();
    if (cleaned.isEmpty()) { clear(); return; }
    if (cleaned.size() > 4096 || cleaned.contains(QLatin1Char('\n')) || cleaned.contains(QLatin1Char('\r'))) {
        m_status = QStringLiteral("KEY_INVALID: Enter a single API key without line breaks.");
        emit changed();
        return;
    }
    m_key = cleaned;
    const quint64 revision = ++m_revision;
    m_status = QStringLiteral("Saving to the desktop keyring.");
    emit changed();
    run({QStringLiteral("store"), QStringLiteral("--label=BetterFlash ") + m_service,
         QStringLiteral("application"), QStringLiteral("betterflash"), QStringLiteral("service"), m_service},
         cleaned.toUtf8() + '\n', [this, revision](int code, const QByteArray &) {
        if (revision != m_revision) return;
        m_status = code == 0 ? QStringLiteral("Stored in the desktop keyring.")
            : QStringLiteral("Kept for this session. Unlock the desktop keyring to save the key.");
        emit changed();
    });
}
void SecretStore::clear() {
    m_key.clear();
    const quint64 revision = ++m_revision;
    m_status = QStringLiteral("Removing the stored key.");
    emit changed();
    run({QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("betterflash"),
         QStringLiteral("service"), m_service}, {}, [this, revision](int code, const QByteArray &) {
        if (revision != m_revision) return;
        m_status = code == 0 ? QStringLiteral("Key removed.")
            : QStringLiteral("Key cleared for this session. Remove the saved key in your desktop keyring.");
        emit changed();
    });
}
