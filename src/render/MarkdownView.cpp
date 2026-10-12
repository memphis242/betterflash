#include "MarkdownView.h"
#include "CodeRendering.h"

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
#include <QTextFrame>
#include <QBitArray>
#include <QUuid>
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


}

MarkdownView::MarkdownView(QQuickItem *parent) : QQuickPaintedItem(parent), m_document(this) {
    setAntialiasing(true);
    m_document.setUndoRedoEnabled(false);
    m_document.setDocumentMargin(0);
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        m_requestedCodeKeys.clear();
        if (!isVisible()) CodeRendering::instance()->setVisibleSources(this, {});
        scheduleRender();
    });
    connect(this, &QQuickItem::windowChanged, this, &MarkdownView::scheduleRender);
    connect(CodeRendering::instance(), &CodeRendering::semanticCapacityAvailable, this, [this] {
        if (isVisible() && m_waitingForCodeCapacity) scheduleRender();
    });
    connect(CodeRendering::instance(), &CodeRendering::changed, this, [this] {
        m_requestedCodeKeys.clear();
        scheduleRender();
    });
    connect(CodeRendering::instance(), &CodeRendering::tokensReady, this, [this](const QString &key) {
        if (m_codeKeys.contains(key)) scheduleRender();
    });
}
void MarkdownView::setDarkMode(const bool dark) {
    if (m_darkMode == dark) return;
    m_darkMode = dark;
    emit styleChanged();
    scheduleRender();
}
void MarkdownView::setMarkdown(const QString &markdown) {
    if (m_markdown == markdown) return;
    m_markdown = markdown;
    m_requestedCodeKeys.clear();
    CodeRendering::instance()->setVisibleSources(this, {});
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

    constexpr int codeFrameProperty = QTextFormat::UserProperty + 1;
    using CodeTheme = KSyntaxHighlighting::Theme;
    CodeRendering *const rendering = CodeRendering::instance();
    const CodeTheme codeTheme = rendering->theme(m_darkMode);
    const QColor codeSurface(codeTheme.editorColor(CodeTheme::BackgroundColor));
    const QColor codeInk = CodeRendering::readableColor(QColor(codeTheme.textColor(CodeTheme::Normal)), codeSurface);
    m_codeKeys.clear();
    struct Formula { QString marker; QString source; };
    struct Fence { QString marker; QString language; QString source; };
    QList<struct Formula> formulas;
    QList<struct Fence> fences;
    const QString markerPrefix = QStringLiteral("BF") + QUuid::createUuid().toString(QUuid::Id128);
    QString markdown;
    static const QRegularExpression mathPattern(QStringLiteral(R"((?<!\\)(\$\$[\s\S]+?\$\$|\$(?!\s|\$)[^\n$]+?\$|\\\[[\s\S]+?\\\]|\\\([^\n]+?\\\)))"));
    static const QRegularExpression inlinePattern(QStringLiteral(R"((?<!\\)(`+)(?!`)[\s\S]*?\1(?!`))"));
    static const QRegularExpression fencePattern(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})(.*)$)"));
    Q_ASSERT(mathPattern.isValid() && inlinePattern.isValid() && fencePattern.isValid());
    QString sourceMarkdown = m_markdown.left(256 * 1024);
    sourceMarkdown.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    sourceMarkdown.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = sourceMarkdown.split(QLatin1Char('\n'));
    QString prose;
    const auto flushProse = [&] {
        QBitArray codeCharacters(prose.size());
        auto inlineMatches = inlinePattern.globalMatch(prose);
        while (inlineMatches.hasNext()) {
            const auto match = inlineMatches.next();
            codeCharacters.fill(true, match.capturedStart(), match.capturedEnd());
        }
        qsizetype previous = 0;
        auto matches = mathPattern.globalMatch(prose);
        while (matches.hasNext() && formulas.size() < 200) {
            const auto match = matches.next();
            bool inCode = false;
            for (qsizetype position = match.capturedStart(); position < match.capturedEnd(); ++position) {
                if (codeCharacters.testBit(position)) { inCode = true; break; }
            }
            if (inCode) continue;
            const QString marker = markerPrefix + QStringLiteral("MATH%1TOKEN").arg(formulas.size());
            markdown += prose.mid(previous, match.capturedStart() - previous) + marker;
            formulas.append({marker, match.captured()});
            previous = match.capturedEnd();
        }
        markdown += prose.mid(previous);
        prose.clear();
    };
    for (qsizetype lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &line = lines.at(lineIndex);
        const auto opening = fencePattern.match(line);
        if (!opening.hasMatch() || (opening.captured(1).startsWith(QLatin1Char('`')) && opening.captured(2).contains(QLatin1Char('`')))) {
            prose += line + QLatin1Char('\n');
            continue;
        }
        flushProse();
        const QString delimiter = opening.captured(1);
        const QString marker = markerPrefix + QStringLiteral("CODE%1TOKEN").arg(fences.size());
        QStringList source;
        while (++lineIndex < lines.size()) {
            const auto closing = fencePattern.match(lines.at(lineIndex));
            if (closing.hasMatch() && closing.captured(1).front() == delimiter.front()
                && closing.captured(1).size() >= delimiter.size() && closing.captured(2).trimmed().isEmpty()) break;
            source.append(lines.at(lineIndex));
        }
        fences.append({marker, opening.captured(2).trimmed().section(QLatin1Char(' '), 0, 0), source.join(QLatin1Char('\n'))});
        markdown += QLatin1Char('\n') + marker + QStringLiteral("\n\n");
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
    QSet<QString> visibleCode;
    if (isVisible() && window()) {
        for (const struct Fence &fence : fences) visibleCode.insert(LspHighlighter::key(CodeRendering::normalizedLanguage(fence.language), fence.source));
    }
    m_waitingForCodeCapacity = false;
    rendering->setVisibleSources(this, visibleCode);
    for (const struct Fence &fence : fences) {
        QTextCursor marker = m_document.find(fence.marker);
        if (marker.isNull()) continue;
        marker.removeSelectedText();
        QTextFrameFormat frameFormat;
        frameFormat.setProperty(codeFrameProperty, true);
        frameFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        frameFormat.setPadding(14);
        frameFormat.setTopMargin(8);
        frameFormat.setBottomMargin(12);
        frameFormat.setBorder(0);
        frameFormat.setBackground(codeSurface);
        QTextFrame *const frame = marker.insertFrame(frameFormat);
        QTextCursor code = frame->firstCursorPosition();
        QFont codeFont(QStringLiteral("IBM Plex Mono"));
        codeFont.setPixelSize(qRound(m_baseFontSize * .9));
        codeFont.setFixedPitch(true);
        QTextCharFormat codeFormat;
        codeFormat.setFont(codeFont);
        codeFormat.setForeground(codeInk);
        code.insertText(fence.source, codeFormat);
        const int sourceStart = frame->firstPosition();
        const QString language = CodeRendering::normalizedLanguage(fence.language);
        const QString key = LspHighlighter::key(language, fence.source);
        m_codeKeys.insert(key);
        const bool requestSemantic = isVisible() && window() && !m_requestedCodeKeys.contains(key);
        bool accepted = false;
        const QList<struct CodeSpan> spans = rendering->highlight(language, fence.source, m_darkMode, requestSemantic, accepted);
        if (requestSemantic && accepted) m_requestedCodeKeys.insert(key);
        if (requestSemantic && !accepted) m_waitingForCodeCapacity = true;
        for (const struct CodeSpan &span : spans) {
            Q_ASSERT(span.offset >= 0 && span.length >= 0 && span.offset + span.length <= fence.source.size());
            QTextCursor token(&m_document);
            token.setPosition(sourceStart + span.offset);
            token.setPosition(sourceStart + span.offset + span.length, QTextCursor::KeepAnchor);
            token.mergeCharFormat(span.format);
        }
    }
    for (QTextBlock block = m_document.begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        QTextBlockFormat format = block.blockFormat();
        if (cursor.currentFrame()->frameFormat().boolProperty(codeFrameProperty)) {
            format.setTopMargin(0);
            format.setBottomMargin(0);
            format.setLineHeight(118, QTextBlockFormat::ProportionalHeight);
            format.clearBackground();
            cursor.setBlockFormat(format);
            continue;
        }
        const bool code = format.hasProperty(QTextFormat::BlockCodeFence) || format.hasProperty(QTextFormat::BlockCodeLanguage);
        if (code) {
            // Nested Markdown fences and indented code retain the document parser's structure.
            format.setBackground(codeSurface);
            format.setTopMargin(0);
            format.setBottomMargin(0);
            format.setLeftMargin(14);
            format.setRightMargin(14);
            cursor.setBlockFormat(format);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat codeFormat;
            codeFormat.setForeground(codeInk);
            codeFormat.setFontFixedPitch(true);
            cursor.mergeCharFormat(codeFormat);
        } else {
            format.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
            format.setBottomMargin(m_baseFontSize * 0.45);
            cursor.setBlockFormat(format);
            QList<struct CodeSpan> inlineFormats;
            for (auto iterator = block.begin(); !iterator.atEnd(); ++iterator) {
                const QTextFragment fragment = iterator.fragment();
                if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch() || fragment.charFormat().isImageFormat()) continue;
                QTextCharFormat inlineCode;
                inlineCode.setBackground(codeSurface);
                inlineCode.setForeground(codeInk);
                inlineCode.setFontWeight(QFont::DemiBold);
                inlineFormats.append({fragment.position(), fragment.length(), inlineCode});
            }
            for (const struct CodeSpan &span : inlineFormats) {
                QTextCursor token(&m_document);
                token.setPosition(span.offset);
                token.setPosition(span.offset + span.length, QTextCursor::KeepAnchor);
                token.mergeCharFormat(span.format);
            }
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
