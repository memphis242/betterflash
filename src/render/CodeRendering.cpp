#include "CodeRendering.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/State>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <cmath>

static void initializeCodeThemes() { Q_INIT_RESOURCE(code_themes); }

namespace {
using Theme = KSyntaxHighlighting::Theme;

qreal luminance(const QColor &color) {
    const auto linear = [](qreal value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

Theme::TextStyle semanticStyle(const QString &kind) {
    static const QHash<QString, Theme::TextStyle> styles{
        {QStringLiteral("namespace"), Theme::DataType}, {QStringLiteral("type"), Theme::DataType},
        {QStringLiteral("class"), Theme::DataType}, {QStringLiteral("enum"), Theme::DataType},
        {QStringLiteral("interface"), Theme::DataType}, {QStringLiteral("struct"), Theme::DataType},
        {QStringLiteral("typeParameter"), Theme::DataType}, {QStringLiteral("parameter"), Theme::Variable},
        {QStringLiteral("variable"), Theme::Variable}, {QStringLiteral("property"), Theme::Variable},
        {QStringLiteral("enumMember"), Theme::Constant}, {QStringLiteral("function"), Theme::Function},
        {QStringLiteral("method"), Theme::Function}, {QStringLiteral("macro"), Theme::Preprocessor},
        {QStringLiteral("keyword"), Theme::Keyword}, {QStringLiteral("comment"), Theme::Comment},
        {QStringLiteral("string"), Theme::String}, {QStringLiteral("number"), Theme::DecVal},
        {QStringLiteral("operator"), Theme::Operator}, {QStringLiteral("label"), Theme::ControlFlow}
    };
    return styles.value(kind, Theme::Normal);
}

class BlockHighlighter final : public KSyntaxHighlighting::AbstractHighlighter {
public:
    QList<struct CodeSpan> spans;
    void highlight(const QString &source) {
        KSyntaxHighlighting::State state;
        const QStringList lines = source.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            state = highlightLine(QStringView(line), state);
            m_offset += line.size() + 1;
        }
    }
protected:
    void applyFormat(const int offset, const int length, const KSyntaxHighlighting::Format &format) override {
        if (!length || !format.isValid()) return;
        const Theme colors = theme();
        QTextCharFormat style;
        const QColor foreground = format.hasTextColor(colors) ? format.textColor(colors) : QColor(colors.textColor(Theme::Normal));
        style.setForeground(CodeRendering::readableColor(foreground, QColor(colors.editorColor(Theme::BackgroundColor))));
        style.setFontWeight(format.isBold(colors) ? QFont::Bold : QFont::Normal);
        style.setFontItalic(format.isItalic(colors));
        spans.append({m_offset + offset, length, style});
    }
private:
    int m_offset = 0;
};
}

CodeRendering *CodeRendering::instance() {
    Q_ASSERT(qApp);
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    static CodeRendering *const settings = new CodeRendering(qApp);
    return settings;
}
CodeRendering::CodeRendering(QObject *parent) : QObject(parent), m_server(this) {
    initializeCodeThemes();
    m_repository.addCustomSearchPath(QStringLiteral(":/betterflash-code"));
    const QList<Theme> themes = m_repository.themes();
    for (const Theme &theme : themes) {
        if (!theme.isValid()) continue;
        const QColor background(theme.editorColor(Theme::BackgroundColor));
        if (luminance(background) < .35) m_darkThemes.append(theme.name());
        else m_lightThemes.append(theme.name());
    }
    m_darkThemes.sort(Qt::CaseInsensitive);
    m_lightThemes.sort(Qt::CaseInsensitive);
    m_darkTheme = m_settings.value(QStringLiteral("codeRendering/darkTheme"), QStringLiteral("Iceberg Dark")).toString();
    m_lightTheme = m_settings.value(QStringLiteral("codeRendering/lightTheme"), QStringLiteral("Iceberg Light")).toString();
    if (!m_darkThemes.contains(m_darkTheme)) m_darkTheme = m_repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme).name();
    if (!m_lightThemes.contains(m_lightTheme)) m_lightTheme = m_repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme).name();
    m_serverEnabled = m_settings.value(QStringLiteral("codeRendering/languageServerEnabled"), true).toBool();
    m_clangdPath = m_settings.value(QStringLiteral("codeRendering/clangdPath"), QStandardPaths::findExecutable(QStringLiteral("clangd"))).toString();
    connect(&m_server, &LspHighlighter::ready, this, &CodeRendering::tokensReady);
    connect(&m_server, &LspHighlighter::capacityAvailable, this, &CodeRendering::semanticCapacityAvailable);
    connect(&m_server, &LspHighlighter::statusChanged, this, &CodeRendering::serverStatusChanged);
    m_server.configure(m_serverEnabled, m_clangdPath);
}
void CodeRendering::setDarkTheme(const QString &name) {
    if (m_darkTheme == name || !m_darkThemes.contains(name)) return;
    m_darkTheme = name;
    m_settings.setValue(QStringLiteral("codeRendering/darkTheme"), name);
    emit changed();
}
void CodeRendering::setLightTheme(const QString &name) {
    if (m_lightTheme == name || !m_lightThemes.contains(name)) return;
    m_lightTheme = name;
    m_settings.setValue(QStringLiteral("codeRendering/lightTheme"), name);
    emit changed();
}
void CodeRendering::setLanguageServerEnabled(const bool enabled) {
    if (enabled == m_serverEnabled) return;
    m_serverEnabled = enabled;
    m_settings.setValue(QStringLiteral("codeRendering/languageServerEnabled"), enabled);
    m_server.configure(enabled, m_clangdPath);
    emit changed();
}
bool CodeRendering::setClangdPath(const QString &path) {
    const QString requested = path.trimmed();
    const QString resolved = requested.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("clangd"))
        : QStandardPaths::findExecutable(requested);
    const QFileInfo executable(resolved);
    if (resolved.isEmpty() || !executable.isFile() || !executable.isExecutable()) return false;
    m_clangdPath = executable.absoluteFilePath();
    m_settings.setValue(QStringLiteral("codeRendering/clangdPath"), m_clangdPath);
    m_server.configure(m_serverEnabled, m_clangdPath);
    emit changed();
    return true;
}
void CodeRendering::setVisibleSources(QObject *const view, const QSet<QString> &keys) {
    Q_ASSERT(view);
    if (!m_visibleSources.contains(view)) {
        connect(view, &QObject::destroyed, this, [this, view] {
            m_visibleSources.remove(view);
            updateVisibleSources();
        });
    } else if (m_visibleSources.value(view) == keys) return;
    m_visibleSources.insert(view, keys);
    updateVisibleSources();
}
void CodeRendering::updateVisibleSources() {
    QSet<QString> keys;
    for (auto iterator = m_visibleSources.cbegin(); iterator != m_visibleSources.cend(); ++iterator) keys.unite(iterator.value());
    m_server.retain(keys);
}
KSyntaxHighlighting::Theme CodeRendering::theme(const bool dark) const {
    const Theme selected = m_repository.theme(dark ? m_darkTheme : m_lightTheme);
    return selected.isValid() ? selected : m_repository.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme : KSyntaxHighlighting::Repository::LightTheme);
}
QString CodeRendering::normalizedLanguage(const QString &language) {
    const QString name = language.trimmed().section(QLatin1Char(' '), 0, 0).toLower();
    static const QHash<QString, QString> aliases{
        {QStringLiteral("c++"), QStringLiteral("cpp")}, {QStringLiteral("cc"), QStringLiteral("cpp")},
        {QStringLiteral("cxx"), QStringLiteral("cpp")}, {QStringLiteral("hpp"), QStringLiteral("cpp")},
        {QStringLiteral("h"), QStringLiteral("c")}, {QStringLiteral("py"), QStringLiteral("python")},
        {QStringLiteral("js"), QStringLiteral("javascript")}, {QStringLiteral("ts"), QStringLiteral("typescript")},
        {QStringLiteral("rs"), QStringLiteral("rust")}, {QStringLiteral("sh"), QStringLiteral("bash")},
        {QStringLiteral("shell"), QStringLiteral("bash")}, {QStringLiteral("cs"), QStringLiteral("c#")},
        {QStringLiteral("yml"), QStringLiteral("yaml")}
    };
    return aliases.value(name, name);
}
QColor CodeRendering::readableColor(const QColor &color, const QColor &background) {
    const qreal bg = luminance(background);
    const auto contrast = [bg](const QColor &candidate) {
        const qreal fg = luminance(candidate);
        return (std::max(fg, bg) + .05) / (std::min(fg, bg) + .05);
    };
    const QColor original = color.isValid() ? color : (bg < .18 ? QColor(Qt::white) : QColor(Qt::black));
    if (contrast(original) >= 4.5) return original;
    const QColor endpoint = contrast(QColor(Qt::white)) > contrast(QColor(Qt::black)) ? QColor(Qt::white) : QColor(Qt::black);
    for (int step = 1; step <= 20; ++step) {
        const qreal amount = step / 20.0;
        const QColor candidate = QColor::fromRgbF(original.redF() + (endpoint.redF() - original.redF()) * amount,
            original.greenF() + (endpoint.greenF() - original.greenF()) * amount,
            original.blueF() + (endpoint.blueF() - original.blueF()) * amount);
        if (contrast(candidate) >= 4.5) return candidate;
    }
    return endpoint;
}
QList<struct CodeSpan> CodeRendering::highlight(const QString &rawLanguage, const QString &source, const bool dark, const bool requestSemantic, bool &semanticAccepted) {
    const QString language = normalizedLanguage(rawLanguage);
    static const QHash<QString, QString> names{
        {QStringLiteral("cpp"), QStringLiteral("C++")}, {QStringLiteral("c"), QStringLiteral("C")},
        {QStringLiteral("python"), QStringLiteral("Python")}, {QStringLiteral("javascript"), QStringLiteral("JavaScript")},
        {QStringLiteral("typescript"), QStringLiteral("TypeScript")}, {QStringLiteral("bash"), QStringLiteral("Bash")},
        {QStringLiteral("c#"), QStringLiteral("C#")}, {QStringLiteral("rust"), QStringLiteral("Rust")},
        {QStringLiteral("json"), QStringLiteral("JSON")}, {QStringLiteral("yaml"), QStringLiteral("YAML")},
        {QStringLiteral("sql"), QStringLiteral("SQL")}, {QStringLiteral("html"), QStringLiteral("HTML")},
        {QStringLiteral("css"), QStringLiteral("CSS")}, {QStringLiteral("go"), QStringLiteral("Go")}
    };
    KSyntaxHighlighting::Definition definition = m_repository.definitionForName(names.value(language, rawLanguage));
    if (!definition.isValid() && !language.isEmpty()) definition = m_repository.definitionForFileName(QStringLiteral("snippet.") + language);
    const Theme colors = theme(dark);
    BlockHighlighter highlighter;
    highlighter.setDefinition(definition);
    highlighter.setTheme(colors);
    highlighter.highlight(source);
    const QList<struct SemanticToken> tokens = m_server.tokens(language, source, requestSemantic, semanticAccepted);
    if (!tokens.isEmpty()) {
        QList<int> lineOffsets{0};
        for (qsizetype i = 0; i < source.size(); ++i) {
            if (source.at(i) == QLatin1Char('\n')) lineOffsets.append(static_cast<int>(i + 1));
        }
        for (const struct SemanticToken &token : tokens) {
            Q_ASSERT(token.line >= 0 && token.line < lineOffsets.size());
            const Theme::TextStyle style = semanticStyle(token.kind);
            if (style == Theme::Normal) continue;
            QTextCharFormat format;
            format.setForeground(readableColor(QColor(colors.textColor(style)), QColor(colors.editorColor(Theme::BackgroundColor))));
            format.setFontWeight(colors.isBold(style) ? QFont::Bold : QFont::Normal);
            format.setFontItalic(colors.isItalic(style));
            highlighter.spans.append({lineOffsets.at(token.line) + token.column, token.length, format});
        }
    }
    return highlighter.spans;
}
