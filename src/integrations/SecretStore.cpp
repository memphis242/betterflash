#include "SecretStore.h"

#include <QElapsedTimer>
#include <QDebug>
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
            m_environment = true;
            m_status = QStringLiteral("Using a read-only key from the environment. Change or unset its environment variable and restart to replace or remove it.");
            m_storageState = QStringLiteral("environment");
            return;
        }
    }
    QTimer::singleShot(0, this, &SecretStore::load);
}
SecretStore::~SecretStore() {
    if (!finishPending()) qWarning("KEYRING_PENDING: Credential changes did not finish. Check the desktop keyring before restarting.");
    const auto processes = findChildren<QProcess *>();
    for (auto *const process : processes) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            (void)process->waitForFinished(100); // The bounded drain already reports unfinished mutations.
        }
    }
}
bool SecretStore::finishPending(const int timeoutMs) {
    Q_ASSERT(timeoutMs >= 0);
    QElapsedTimer elapsed;
    elapsed.start();
    while (m_running || !m_operations.isEmpty()) {
        if (!m_running) runNext();
        if (!m_running) continue;
        const auto processes = findChildren<QProcess *>();
        QProcess *active = nullptr;
        for (auto *const process : processes) {
            if (!process->property("completed").toBool()) { active = process; break; }
        }
        Q_ASSERT(active);
        if (!active) {
            m_status = QStringLiteral("KEYRING_STATE: Cannot locate the pending credential helper. Verify the desktop keyring before restarting.");
            m_storageState = QStringLiteral("pending-failed");
            emit changed();
            return false;
        }
        const qint64 remaining = timeoutMs - elapsed.elapsed();
        if (remaining <= 0 || !active->waitForFinished(static_cast<int>(remaining))) {
            m_status = QStringLiteral("KEYRING_PENDING: Credential changes are still pending. Keep the app open and unlock the desktop keyring before retrying.");
            emit changed();
            return false;
        }
    }
    return true;
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
        if (!input.isEmpty() && process->write(input) != input.size()) {
            process->kill();
            return;
        }
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
            m_storageState = QStringLiteral("saved");
        } else {
            m_status = QStringLiteral("Enter a key to enable this service.");
            m_storageState = QStringLiteral("missing");
        }
        emit changed();
    });
}
void SecretStore::setKey(const QString &key) {
    if (m_environment) {
        m_status = QStringLiteral("The environment key is read-only. Change or unset its environment variable and restart to replace it.");
        emit changed();
        return;
    }
    const QString cleaned = key.trimmed();
    if (cleaned.isEmpty()) { clear(); return; }
    if (cleaned.size() > 4096 || cleaned.contains(QLatin1Char('\n')) || cleaned.contains(QLatin1Char('\r'))) {
        m_status = QStringLiteral("KEY_INVALID: Enter a single API key without line breaks.");
        m_storageState = QStringLiteral("invalid");
        emit changed();
        return;
    }
    m_key = cleaned;
    const quint64 revision = ++m_revision;
    m_status = QStringLiteral("Saving to the desktop keyring.");
    m_storageState = QStringLiteral("saving");
    emit changed();
    run({QStringLiteral("store"), QStringLiteral("--label=BetterFlash ") + m_service,
         QStringLiteral("application"), QStringLiteral("betterflash"), QStringLiteral("service"), m_service},
         cleaned.toUtf8() + '\n', [this, revision](int code, const QByteArray &) {
        if (revision != m_revision) return;
        if (code == 0) {
            m_status = QStringLiteral("Stored in the desktop keyring.");
            m_storageState = QStringLiteral("saved");
        } else {
            m_status = QStringLiteral("Kept for this session. Unlock the desktop keyring to save the key.");
            m_storageState = QStringLiteral("session-only");
        }
        emit changed();
    });
}
void SecretStore::clear() {
    if (m_environment) {
        m_status = QStringLiteral("The environment key is read-only. Unset its environment variable and restart to remove it.");
        emit changed();
        return;
    }
    m_key.clear();
    const quint64 revision = ++m_revision;
    m_status = QStringLiteral("Removing the stored key.");
    m_storageState = QStringLiteral("removing");
    emit changed();
    run({QStringLiteral("clear"), QStringLiteral("application"), QStringLiteral("betterflash"),
         QStringLiteral("service"), m_service}, {}, [this, revision](int code, const QByteArray &) {
        if (revision != m_revision) return;
        if (code == 0) {
            m_status = QStringLiteral("Key removed.");
            m_storageState = QStringLiteral("removed");
        } else {
            m_status = QStringLiteral("Key cleared for this session. Remove the saved key in your desktop keyring.");
            m_storageState = QStringLiteral("removal-failed");
        }
        emit changed();
    });
}
