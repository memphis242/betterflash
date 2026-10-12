#include "LspHighlighter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUrl>
#include <cmath>
#include <limits>

namespace {
constexpr int maximumSource = 32 * 1024;
constexpr int maximumResponse = 2 * 1024 * 1024;
constexpr int maximumQueued = 24;
constexpr int maximumTokens = 20000;
static_assert(maximumSource < maximumResponse);

bool natural(const QJsonValue &value, int &result) {
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number < 0 || number > std::numeric_limits<int>::max()
        || number != std::floor(number)) return false;
    result = static_cast<int>(number);
    return true;
}
}

LspHighlighter::LspHighlighter(QObject *parent) : QObject(parent),
    m_directory(QDir::tempPath() + QStringLiteral("/betterflash-code-XXXXXX")) {
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(15000);
    m_idle.setSingleShot(true);
    m_idle.setInterval(60000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("CODE_SERVER_TIMEOUT: The language server took too long. Syntax colors remain available. Check its path in Code rendering settings."));
    });
    connect(&m_idle, &QTimer::timeout, this, [this] {
        stop();
        setStatus(QStringLiteral("Language server ready; it starts when a C or C++ block is displayed."));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &LspHighlighter::read);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_stopping) fail(QStringLiteral("CODE_SERVER_START: Cannot run the configured language server. Choose a working clangd executable in Code rendering settings."));
    });
    connect(&m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_stopping) {
            m_stopping = false;
            if (!m_queue.isEmpty() && !m_failed && m_enabled) start();
            return;
        }
        fail(QStringLiteral("CODE_SERVER_EXIT: The language server stopped. Syntax colors remain available. Save the server setting to retry."));
    });
    connect(&m_process, &QProcess::started, this, [this] {
        const QString root = QUrl::fromLocalFile(m_directory.path() + QLatin1Char('/')).toString();
        send(QStringLiteral("initialize"), {
            {QStringLiteral("processId"), static_cast<double>(QCoreApplication::applicationPid())},
            {QStringLiteral("rootUri"), root},
            {QStringLiteral("capabilities"), QJsonObject{
                {QStringLiteral("general"), QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}}},
                {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("semanticTokens"), QJsonObject{
                    {QStringLiteral("requests"), QJsonObject{{QStringLiteral("full"), true}}},
                    {QStringLiteral("tokenTypes"), QJsonArray{QStringLiteral("namespace"), QStringLiteral("type"), QStringLiteral("class"), QStringLiteral("enum"), QStringLiteral("interface"), QStringLiteral("struct"), QStringLiteral("typeParameter"), QStringLiteral("parameter"), QStringLiteral("variable"), QStringLiteral("property"), QStringLiteral("enumMember"), QStringLiteral("function"), QStringLiteral("method"), QStringLiteral("macro"), QStringLiteral("keyword"), QStringLiteral("comment"), QStringLiteral("string"), QStringLiteral("number"), QStringLiteral("operator")}},
                    {QStringLiteral("tokenModifiers"), QJsonArray{}},
                    {QStringLiteral("formats"), QJsonArray{QStringLiteral("relative")}},
                    {QStringLiteral("multilineTokenSupport"), false},
                    {QStringLiteral("overlappingTokenSupport"), false}
                }}}}
            }},
            {QStringLiteral("initializationOptions"), QJsonObject{{QStringLiteral("fallbackFlags"), QJsonArray{}}}}
        }, 1);
        m_timeout.start();
    });
}
LspHighlighter::~LspHighlighter() {
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
QString LspHighlighter::key(const QString &language, const QString &source) {
    return QString::fromLatin1(QCryptographicHash::hash(language.toUtf8() + '\0' + source.toUtf8(), QCryptographicHash::Sha256).toHex());
}
void LspHighlighter::setStatus(const QString &status) {
    if (status == m_status) return;
    m_status = status;
    emit statusChanged();
}
void LspHighlighter::configure(const bool enabled, const QString &program) {
    stop();
    m_enabled = enabled;
    m_program = program;
    m_failed = false;
    m_cache.clear();
    m_queue.clear();
    m_pending.clear();
    if (!enabled) setStatus(QStringLiteral("Language server disabled. Native syntax highlighting remains available for all recognized languages."));
    else if (program.isEmpty()) setStatus(QStringLiteral("CODE_SERVER_MISSING: clangd was not found. Install it or select its executable below; syntax highlighting remains available."));
    else setStatus(QStringLiteral("Language server ready; it starts when a C or C++ block is displayed."));
}
QList<struct SemanticToken> LspHighlighter::tokens(const QString &language, const QString &source, const bool requestIfMissing, bool &accepted) {
    accepted = true;
    if (!m_enabled || m_program.isEmpty() || m_failed || source.isEmpty() || source.size() > maximumSource
        || (language != QStringLiteral("cpp") && language != QStringLiteral("c"))) return {};
    const QString hash = key(language, source);
    if (const auto *const cached = m_cache.object(hash)) return *cached;
    if (!requestIfMissing || m_pending.contains(hash)) return {};
    if (!m_liveKeys.contains(hash) || m_queue.size() >= maximumQueued) {
        accepted = false;
        return {};
    }
    m_queue.enqueue({hash, source, language});
    m_pending.insert(hash);
    m_idle.stop();
    if (m_process.state() == QProcess::NotRunning && !m_stopping) start();
    else if (m_initialized) next();
    return {};
}
void LspHighlighter::retain(const QSet<QString> &keys) {
    m_liveKeys = keys;
    for (qsizetype index = m_queue.size(); index > 0; --index) {
        if (keys.contains(m_queue.at(index - 1).key)) continue;
        m_pending.remove(m_queue.at(index - 1).key);
        m_queue.removeAt(index - 1);
    }
    if (!m_current.key.isEmpty() && !keys.contains(m_current.key)) {
        send(QStringLiteral("$/cancelRequest"), {{QStringLiteral("id"), m_requestId}});
        send(QStringLiteral("textDocument/didClose"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_uri}}}});
        m_pending.remove(m_current.key);
        m_current = {};
        m_requestId = 0;
        m_timeout.stop();
        next();
    }
    if (m_queue.size() < maximumQueued) emit capacityAvailable();
}
void LspHighlighter::start() {
#ifdef Q_OS_ANDROID
    fail(QStringLiteral("CODE_SERVER_PLATFORM: Android uses native syntax highlighting; external language servers are available on desktop."));
    return;
#endif
    if (!m_directory.isValid()) { fail(QStringLiteral("CODE_SERVER_STORAGE: Cannot create an isolated temporary workspace. Check available disk space.")); return; }
    m_initialized = false;
    m_buffer.clear();
    m_process.setWorkingDirectory(m_directory.path());
    m_process.setProgram(m_program);
    m_process.setArguments({QStringLiteral("--enable-config=false"), QStringLiteral("--background-index=false"),
        QStringLiteral("--clang-tidy=false"), QStringLiteral("--log=error"), QStringLiteral("-j=2"),
        QStringLiteral("--compile-commands-dir=") + m_directory.path()});
    setStatus(QStringLiteral("Starting clangd for C and C++ semantic highlighting..."));
    m_process.start();
    m_timeout.start();
}
void LspHighlighter::send(const QString &method, const QJsonObject &parameters, const int id) {
    QJsonObject object{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method}, {QStringLiteral("params"), parameters}};
    if (id) object.insert(QStringLiteral("id"), id);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    m_process.write(QByteArrayLiteral("Content-Length: ") + QByteArray::number(bytes.size()) + QByteArrayLiteral("\r\n\r\n") + bytes);
}
void LspHighlighter::read() {
    if (m_stopping || m_failed) { m_process.readAllStandardOutput(); return; }
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > maximumResponse) { fail(QStringLiteral("CODE_SERVER_RESPONSE: The language server response is too large. Save its setting to retry.")); return; }
    while (!m_buffer.isEmpty()) {
        const qsizetype headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (m_buffer.size() > 8192) fail(QStringLiteral("CODE_SERVER_PROTOCOL: Invalid language server message header. Choose a clangd executable."));
            return;
        }
        qint64 length = -1;
        const QList<QByteArray> headers = m_buffer.left(headerEnd).split('\n');
        for (const QByteArray &header : headers) {
            const qsizetype colon = header.indexOf(':');
            if (colon < 0 || header.left(colon).trimmed().toLower() != QByteArrayLiteral("content-length")) continue;
            bool ok = false;
            length = header.mid(colon + 1).trimmed().toLongLong(&ok);
            if (!ok) length = -1;
        }
        if (length < 0 || length > maximumResponse - 8192) { fail(QStringLiteral("CODE_SERVER_PROTOCOL: Invalid language server message length. Choose a clangd executable.")); return; }
        const qsizetype bodyStart = headerEnd + 4;
        if (m_buffer.size() - bodyStart < length) return;
        struct QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(m_buffer.mid(bodyStart, length), &error);
        m_buffer.remove(0, bodyStart + length);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { fail(QStringLiteral("CODE_SERVER_PROTOCOL: Invalid language server JSON. Choose a clangd executable.")); return; }
        response(document.object());
        if (m_failed) return;
    }
}
void LspHighlighter::response(const QJsonObject &object) {
    if (object.contains(QStringLiteral("method"))) {
        if (object.contains(QStringLiteral("id"))) {
            const QJsonObject reply{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), object.value(QStringLiteral("id"))},
                {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), -32601}, {QStringLiteral("message"), QStringLiteral("Method not supported")}}}};
            const QByteArray bytes = QJsonDocument(reply).toJson(QJsonDocument::Compact);
            m_process.write(QByteArrayLiteral("Content-Length: ") + QByteArray::number(bytes.size()) + QByteArrayLiteral("\r\n\r\n") + bytes);
        }
        return;
    }
    int id = 0;
    if (!natural(object.value(QStringLiteral("id")), id)) return;
    if (id == 1) {
        const QJsonObject capabilities = object.value(QStringLiteral("result")).toObject().value(QStringLiteral("capabilities")).toObject();
        const QString encoding = capabilities.value(QStringLiteral("positionEncoding")).toString(QStringLiteral("utf-16"));
        const QJsonObject provider = capabilities.value(QStringLiteral("semanticTokensProvider")).toObject();
        if (object.contains(QStringLiteral("error")) || (encoding != QStringLiteral("utf-16")) || (!provider.value(QStringLiteral("full")).toBool() && !provider.value(QStringLiteral("full")).isObject())) {
            fail(QStringLiteral("CODE_SERVER_CAPABILITY: This server cannot provide full UTF-16 semantic tokens. Choose a current clangd executable.")); return;
        }
        m_legend.clear();
        const QJsonArray legend = provider.value(QStringLiteral("legend")).toObject().value(QStringLiteral("tokenTypes")).toArray();
        if (legend.size() > 256) { fail(QStringLiteral("CODE_SERVER_PROTOCOL: The server returned an invalid token legend.")); return; }
        for (const QJsonValue &entry : legend) {
            if (!entry.isString() || entry.toString().size() > 64) { fail(QStringLiteral("CODE_SERVER_PROTOCOL: The server returned an invalid token legend.")); return; }
            m_legend.append(entry.toString());
        }
        send(QStringLiteral("initialized"), {});
        m_initialized = true;
        m_timeout.stop();
        next();
        return;
    }
    if (id != m_requestId || m_current.key.isEmpty()) return;
    const QJsonValue dataValue = object.value(QStringLiteral("result")).toObject().value(QStringLiteral("data"));
    const QJsonArray data = dataValue.toArray();
    if (object.contains(QStringLiteral("error")) || (!dataValue.isArray() && !object.value(QStringLiteral("result")).isNull()) || data.size() % 5 || data.size() / 5 > maximumTokens) {
        fail(QStringLiteral("CODE_SERVER_TOKENS: The server could not highlight this snippet. Syntax colors remain available. Save the server setting to retry.")); return;
    }
    const QStringList lines = m_current.source.split(QLatin1Char('\n'));
    QList<struct SemanticToken> tokens;
    int line = 0;
    int column = 0;
    for (qsizetype i = 0; i < data.size(); i += 5) {
        int delta = 0, offset = 0, length = 0, kind = 0, modifiers = 0;
        if (!natural(data.at(i), delta) || !natural(data.at(i + 1), offset) || !natural(data.at(i + 2), length)
            || !natural(data.at(i + 3), kind) || !natural(data.at(i + 4), modifiers)
            || delta >= lines.size() || line > lines.size() - 1 - delta) { fail(QStringLiteral("CODE_SERVER_TOKENS: The server returned invalid token positions.")); return; }
        line += delta;
        if (delta) column = 0;
        if (offset > lines.at(line).size() - column) { fail(QStringLiteral("CODE_SERVER_TOKENS: The server returned invalid token columns.")); return; }
        column += offset;
        if (length > lines.at(line).size() - column || kind >= m_legend.size()) { fail(QStringLiteral("CODE_SERVER_TOKENS: The server returned an invalid token range.")); return; }
        if (length) tokens.append({line, column, length, m_legend.at(kind)});
    }
    send(QStringLiteral("textDocument/didClose"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_uri}}}});
    const QString completed = m_current.key;
    const qsizetype cost = sizeof(struct SemanticToken) * tokens.size() + m_current.source.size() * 2 + 256;
    m_cache.insert(completed, new QList<struct SemanticToken>(tokens), static_cast<int>(cost));
    m_pending.remove(completed);
    m_current = {};
    m_timeout.stop();
    setStatus(QStringLiteral("clangd active for C and C++. Other languages use native syntax highlighting."));
    emit ready(completed);
    next();
}
void LspHighlighter::next() {
    if (!m_initialized || !m_current.key.isEmpty()) return;
    if (m_queue.isEmpty()) { m_idle.start(); return; }
    m_current = m_queue.dequeue();
    // The compilation database is supplied as protocol data, not discovered from a project.
    const bool cpp = m_current.language == QStringLiteral("cpp");
    const QString path = m_directory.filePath(cpp ? QStringLiteral("snippet.cpp") : QStringLiteral("snippet.c"));
    m_uri = QUrl::fromLocalFile(path).toString();
    send(QStringLiteral("workspace/didChangeConfiguration"), {{QStringLiteral("settings"), QJsonObject{
        {QStringLiteral("compilationDatabaseChanges"), QJsonObject{{path, QJsonObject{
            {QStringLiteral("workingDirectory"), m_directory.path()},
            {QStringLiteral("compilationCommand"), QJsonArray{QStringLiteral("clang"), cpp ? QStringLiteral("-std=c++23") : QStringLiteral("-std=c23"), QStringLiteral("-fsyntax-only"), path}}
        }}}}
    }}});
    send(QStringLiteral("textDocument/didOpen"), {{QStringLiteral("textDocument"), QJsonObject{
        {QStringLiteral("uri"), m_uri}, {QStringLiteral("languageId"), m_current.language},
        {QStringLiteral("version"), 1}, {QStringLiteral("text"), m_current.source}
    }}});
    if (m_nextId == std::numeric_limits<int>::max()) m_nextId = 2;
    m_requestId = m_nextId++;
    send(QStringLiteral("textDocument/semanticTokens/full"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_uri}}}}, m_requestId);
    m_timeout.start();
    emit capacityAvailable();
}
void LspHighlighter::stop() {
    m_timeout.stop();
    m_idle.stop();
    m_initialized = false;
    m_buffer.clear();
    m_queue.clear();
    m_pending.clear();
    m_current = {};
    if (m_process.state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process.kill();
    }
}
void LspHighlighter::fail(const QString &message) {
    m_failed = true;
    stop();
    setStatus(message);
}
