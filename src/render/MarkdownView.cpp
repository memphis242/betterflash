#include "MarkdownView.h"

#include <QAbstractTextDocumentLayout>
#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>

#include <latex.h>
#include <platform/qt/graphic_qt.h>

static void initializeMathResource() { Q_INIT_RESOURCE(math_assets); }

namespace {
QString initializeMath() {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    static QString result;
    static bool initialized = false;
    if (initialized)
        return result;
    initializeMathResource();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/tex/0e3707f6");
    QDirIterator files(QStringLiteral(":/tex"), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString resource = files.next();
        const QString output = directory + QLatin1Char('/') + resource.mid(QStringLiteral(":/tex/").size());
        if (QFile::exists(output))
            continue;
        if (!QDir().mkpath(QFileInfo(output).absolutePath()))
            return QStringLiteral("MATH_STORAGE: Cannot prepare formula fonts in %1.").arg(directory);
        QFile source(resource);
        QSaveFile destination(output);
        if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)
            || destination.write(source.readAll()) < 0 || !destination.commit())
            return QStringLiteral("MATH_STORAGE: Cannot write formula resources in %1.").arg(directory);
    }
    try {
        tex::LaTeX::init(directory.toStdString());
        initialized = true;
    } catch (const std::exception &error) {
        result = QStringLiteral("MATH_INIT: %1").arg(QString::fromUtf8(error.what()));
    }
    return result;
}

QImage renderMath(const QString &formula, const QColor &foreground, const qreal size, const int width) {
    Q_ASSERT(width > 0 && size > 0);
    static QCache<QString, QImage> cache(8 * 1024);
    const QString key = formula + QLatin1Char('|') + foreground.name(QColor::HexArgb)
        + QString::number(size) + QLatin1Char('|') + QString::number(width);
    if (const QImage *image = cache.object(key))
        return *image;
    const QString mathError = initializeMath();
    if (!mathError.isEmpty())
        throw std::runtime_error(mathError.toStdString());
    if (formula.size() > 8192)
        throw std::runtime_error("MATH_SIZE: A formula must be shorter than 8192 characters.");
    const std::unique_ptr<tex::TeXRender> render(tex::LaTeX::parse(formula.toStdWString(), width,
        static_cast<float>(size), 5.0F, foreground.rgba()));
    const int imageWidth = std::clamp(render->getWidth() + 8, 1, 4096);
    const int imageHeight = std::clamp(render->getHeight() + 8, 1, 4096);
    QImage image(imageWidth * 2, imageHeight * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    tex::Graphics2D_qt graphics(&painter);
    render->draw(graphics, 4, 4);
    painter.end();
    cache.insert(key, new QImage(image), std::max(1, static_cast<int>(image.sizeInBytes() / 1024)));
    return image;
}

const QHash<QString, QString> &keywordPatterns() {
    static const QHash<QString, QString> patterns{
        {QStringLiteral("python"), QStringLiteral("\\b(?:def|class|return|if|elif|else|for|while|in|import|from|as|with|try|except|raise|yield|async|await|None|True|False|and|or|not|pass)\\b")},
        {QStringLiteral("cpp"), QStringLiteral("\\b(?:const|constexpr|consteval|static_assert|auto|struct|class|return|if|else|for|while|switch|case|break|namespace|using|template|typename|public|private|protected|int|double|float|bool|void|nullptr|true|false|include|std)\\b")},
        {QStringLiteral("rust"), QStringLiteral("\\b(?:fn|let|mut|const|pub|impl|struct|enum|trait|use|mod|match|if|else|for|while|loop|return|Self|self|Some|None|Ok|Err|true|false|async|await)\\b")},
        {QStringLiteral("javascript"), QStringLiteral("\\b(?:const|let|var|function|class|return|if|else|for|while|switch|case|break|import|export|from|async|await|new|throw|try|catch|interface|type|extends|implements|public|private|true|false|null|undefined)\\b")},
        {QStringLiteral("sql"), QStringLiteral("\\b(?:SELECT|FROM|WHERE|JOIN|INNER|OUTER|LEFT|RIGHT|ON|GROUP|BY|ORDER|ASC|DESC|INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|TABLE|INDEX|PRIMARY|KEY|NULL|NOT|AND|OR|LIMIT|AS|DISTINCT|COUNT|SUM)\\b")},
        {QStringLiteral("shell"), QStringLiteral("\\b(?:if|then|else|elif|fi|for|in|do|done|while|case|esac|function|export|local|set|echo|printf|sudo)\\b")},
        {QStringLiteral("json"), QStringLiteral("\\b(?:true|false|null)\\b")}
    };
    return patterns;
}
QString normalizedLanguage(QString language) {
    language = language.toLower();
    static const QHash<QString, QString> aliases{
        {QStringLiteral("c++"), QStringLiteral("cpp")}, {QStringLiteral("c"), QStringLiteral("cpp")},
        {QStringLiteral("cc"), QStringLiteral("cpp")}, {QStringLiteral("h"), QStringLiteral("cpp")},
        {QStringLiteral("py"), QStringLiteral("python")}, {QStringLiteral("js"), QStringLiteral("javascript")},
        {QStringLiteral("ts"), QStringLiteral("javascript")}, {QStringLiteral("typescript"), QStringLiteral("javascript")},
        {QStringLiteral("bash"), QStringLiteral("shell")}, {QStringLiteral("sh"), QStringLiteral("shell")},
        {QStringLiteral("rs"), QStringLiteral("rust")}
    };
    return aliases.value(language, language);
}
}

MarkdownView::MarkdownView(QQuickItem *parent) : QQuickPaintedItem(parent), m_document(this) {
    setAntialiasing(true);
    m_document.setUndoRedoEnabled(false);
    m_document.setDocumentMargin(0);
}
void MarkdownView::setMarkdown(const QString &markdown) {
    if (m_markdown == markdown) return;
    m_markdown = markdown;
    m_imageErrors.clear();
    emit markdownChanged();
    scheduleRender();
}
void MarkdownView::setForeground(const QColor &color) {
    if (m_foreground == color) return;
    m_foreground = color;
    emit styleChanged(); scheduleRender();
}
void MarkdownView::setCodeBackground(const QColor &color) {
    if (m_codeBackground == color) return;
    m_codeBackground = color;
    emit styleChanged(); scheduleRender();
}
void MarkdownView::setAccent(const QColor &color) {
    if (m_accent == color) return;
    m_accent = color;
    emit styleChanged(); scheduleRender();
}
void MarkdownView::setBaseFontSize(const qreal size) {
    const qreal bounded = std::clamp(size, 12.0, 64.0);
    if (qFuzzyCompare(m_baseFontSize, bounded)) return;
    m_baseFontSize = bounded;
    emit styleChanged(); scheduleRender();
}
void MarkdownView::setMediaRoot(const QString &directory) {
    if (m_mediaRoot == directory) return;
    m_mediaRoot = directory;
    m_images.clear();
    m_imageErrors.clear();
    emit styleChanged(); scheduleRender();
}
void MarkdownView::reportImageError(const QString &message) {
    if (m_renderError == message) return;
    m_renderError = message;
    emit renderErrorChanged();
}
QVariant MarkdownView::loadImage(const QUrl &url) {
    const QString name = url.toString();
    if (const QImage *const image = m_images.object(name)) return *image;
    if (m_imageErrors.contains(name)) { reportImageError(m_imageErrors.value(name)); return {}; }
    const auto decode = [](const QByteArray &data) -> QImage {
        QBuffer buffer;
        buffer.setData(data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        const QSize size = reader.size();
        if (!size.isValid() || static_cast<qint64>(size.width()) * size.height() > 32000000) return {};
        reader.setAutoTransform(true);
        if (size.width() > 1600 || size.height() > 1600)
            reader.setScaledSize(size.scaled(QSize(1600, 1600), Qt::KeepAspectRatio));
        return reader.read();
    };
    if (url.scheme() == QStringLiteral("media")) {
        const QString fileName = name.mid(6);
        static const QRegularExpression valid(QStringLiteral("^[a-f0-9]{64}\\.(png|jpg|jpeg|webp|gif)$"));
        const QString path = QDir(m_mediaRoot).filePath(fileName);
        if (m_mediaRoot.isEmpty() || !valid.match(fileName).hasMatch()
            || QFileInfo(m_mediaRoot).isSymLink() || QFileInfo(path).isSymLink()) {
            reportImageError(QStringLiteral("IMAGE_PATH: Attach an image using the editor's image control.")); return {};
        }
        QFile source(path);
        if (!QFileInfo(path).isFile() || !source.open(QIODevice::ReadOnly) || source.size() <= 0 || source.size() > 20 * 1024 * 1024) {
            reportImageError(QStringLiteral("IMAGE_MISSING: Sync or import the collection's images, then reopen the card.")); return {};
        }
        const QByteArray bytes = source.read(20 * 1024 * 1024 + 1);
        if (bytes.size() > 20 * 1024 * 1024
            || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != fileName.left(64)) {
            reportImageError(QStringLiteral("IMAGE_INTEGRITY: The attached image does not match its filename. Restore it from a backup or sync the collection.")); return {};
        }
        const QImage image = decode(bytes);
        if (image.isNull()) { reportImageError(QStringLiteral("IMAGE_DECODE: Attach a readable PNG, JPEG, WebP, or GIF.")); return {}; }
        m_images.insert(name, new QImage(image), static_cast<int>(image.sizeInBytes()));
        return image;
    }
    reportImageError(QStringLiteral("IMAGE_SOURCE: Attach a local copy using the editor's image control. Only collection images are rendered."));
    return {};
}
void MarkdownView::geometryChange(const QRectF &current, const QRectF &previous) {
    QQuickPaintedItem::geometryChange(current, previous);
    if (!qFuzzyCompare(current.width(), previous.width())) scheduleRender();
}
void MarkdownView::scheduleRender() {
    if (m_scheduled) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] { m_scheduled = false; rebuild(); });
}
void MarkdownView::rebuild() {
    if (width() <= 0 || !m_foreground.isValid()) return;
    if (!m_renderError.isEmpty()) { m_renderError.clear(); emit renderErrorChanged(); }
    const qreal documentWidth = std::max(1.0, width());
    QFont font(QStringLiteral("IBM Plex Mono"));
    font.setPixelSize(qRound(m_baseFontSize));
    m_document.setDefaultFont(font);
    QTextOption textOptions;
    textOptions.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_document.setDefaultTextOption(textOptions);
    m_document.setTextWidth(documentWidth);

    struct Formula { QString marker; QString source; };
    QList<struct Formula> formulas;
    QString markdown;
    QString fence;
    const QRegularExpression mathPattern(QStringLiteral(R"((?<!\\)(\$\$[\s\S]+?\$\$|\$(?!\s|\$)[^\n$]+?\$|\\\[[\s\S]+?\\\]|\\\([^\n]+?\\\)))"));
    const QRegularExpression fencePattern(QStringLiteral("^\\s*(`{3,}|~{3,})"));
    const QStringList lines = m_markdown.left(256 * 1024).split(QLatin1Char('\n'));
    QString prose;
    const auto flushProse = [&] {
        qsizetype previous = 0;
        auto matches = mathPattern.globalMatch(prose);
        while (matches.hasNext() && formulas.size() < 200) {
            const auto match = matches.next();
            const QString marker = QStringLiteral("BFMATH%1TOKEN").arg(formulas.size());
            markdown += prose.mid(previous, match.capturedStart() - previous) + marker;
            formulas.append({marker, match.captured()});
            previous = match.capturedEnd();
        }
        markdown += prose.mid(previous);
        prose.clear();
    };
    for (const QString &line : lines) {
        const auto fenceMatch = fencePattern.match(line);
        if (fenceMatch.hasMatch()) {
            if (fence.isEmpty()) { flushProse(); fence = fenceMatch.captured(1).left(1); }
            else if (line.trimmed().startsWith(fence)) fence.clear();
            markdown += line + QLatin1Char('\n');
        } else if (!fence.isEmpty()) markdown += line + QLatin1Char('\n');
        else prose += line + QLatin1Char('\n');
    }
    flushProse();
    m_document.setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    QTextCursor entire(&m_document);
    entire.select(QTextCursor::Document);
    QTextCharFormat normal;
    normal.setForeground(m_foreground);
    entire.mergeCharFormat(normal);
    QString error;
    for (const struct Formula &formula : formulas) {
        QTextCursor cursor = m_document.find(formula.marker);
        if (cursor.isNull()) continue;
        try {
            const QImage image = renderMath(formula.source, m_foreground, m_baseFontSize,
                std::max(1, qRound(documentWidth)));
            const QUrl resource(QStringLiteral("bfmath:/%1").arg(formula.marker));
            m_document.addResource(QTextDocument::ImageResource, resource, image);
            QTextImageFormat format;
            format.setName(resource.toString());
            const qreal imageWidth = image.width() / image.devicePixelRatio();
            const qreal imageHeight = image.height() / image.devicePixelRatio();
            const qreal scale = std::min(1.0, documentWidth / imageWidth);
            format.setWidth(imageWidth * scale);
            format.setHeight(imageHeight * scale);
            format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
            cursor.insertImage(format);
        } catch (const std::exception &exception) {
            error = QStringLiteral("MATH_INVALID: %1").arg(QString::fromUtf8(exception.what()));
            cursor.insertText(formula.source);
        }
    }
    for (QTextBlock block = m_document.begin(); block.isValid(); block = block.next()) {
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const QTextFragment content = fragment.fragment();
            if (!content.isValid() || !content.charFormat().isImageFormat()) continue;
            QTextImageFormat imageFormat = content.charFormat().toImageFormat();
            if (imageFormat.name().startsWith(QStringLiteral("bfmath:"))) continue;
            const QImage image = loadImage(QUrl(imageFormat.name())).value<QImage>();
            if (image.isNull()) continue;
            const qreal scale = std::min({1.0, documentWidth / image.width(), 600.0 / image.height()});
            imageFormat.setWidth(image.width() * scale);
            imageFormat.setHeight(image.height() * scale);
            QTextCursor cursor(&m_document);
            cursor.setPosition(content.position());
            cursor.setPosition(content.position() + content.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(imageFormat);
        }
    }
    QString language;
    for (QTextBlock block = m_document.begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        QTextBlockFormat format = block.blockFormat();
        const bool code = format.hasProperty(QTextFormat::BlockCodeFence)
            || format.hasProperty(QTextFormat::BlockCodeLanguage);
        if (code) {
            const QString blockLanguage = format.stringProperty(QTextFormat::BlockCodeLanguage);
            if (!blockLanguage.isEmpty()) language = normalizedLanguage(blockLanguage);
            format.setBackground(m_codeBackground);
            format.setTopMargin(4);
            format.setBottomMargin(4);
            cursor.setBlockFormat(format);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat codeFormat;
            codeFormat.setFontFamilies({QStringLiteral("IBM Plex Mono"), QStringLiteral("monospace")});
            codeFormat.setFontFixedPitch(true);
            codeFormat.setFontPointSize(m_baseFontSize * 0.65);
            cursor.mergeCharFormat(codeFormat);
            const QString pattern = keywordPatterns().value(language);
            if (!pattern.isEmpty()) {
                auto tokens = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).globalMatch(block.text());
                while (tokens.hasNext()) {
                    const auto token = tokens.next();
                    cursor.setPosition(block.position() + token.capturedStart());
                    cursor.setPosition(block.position() + token.capturedEnd(), QTextCursor::KeepAnchor);
                    QTextCharFormat keyword;
                    keyword.setFontWeight(QFont::Bold);
                    keyword.setForeground(m_accent);
                    cursor.mergeCharFormat(keyword);
                }
            }
        } else {
            language.clear();
            format.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
            format.setBottomMargin(m_baseFontSize * 0.45);
            cursor.setBlockFormat(format);
        }
    }
    if (!error.isEmpty() && error != m_renderError) { m_renderError = error; emit renderErrorChanged(); }
    setImplicitHeight(std::ceil(m_document.size().height()) + 2);
    update();
}
void MarkdownView::paint(QPainter *painter) {
    if (!m_foreground.isValid()) return;
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, m_foreground);
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    m_document.documentLayout()->draw(painter, context);
}
QString MarkdownView::plainText(const QString &markdown) {
    class PlainDocument final : public QTextDocument {
    protected:
        QVariant loadResource(int, const QUrl &) override { return QImage(); }
    };
    PlainDocument document;
    document.setLayoutEnabled(false);
    QString text = markdown;
    static const QRegularExpression images(QStringLiteral("!\\[([^\\]]*)\\]\\([^)]*\\)"));
    text.replace(images, QStringLiteral("[Image: \\1]"));
    document.setMarkdown(text, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    return document.toPlainText();
}
