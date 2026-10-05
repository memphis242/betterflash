#include "render/MarkdownView.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

class RenderTest final : public QObject {
    Q_OBJECT
private:
    void style(MarkdownView &view) {
        view.setWidth(640);
        view.setForeground(QColor(QStringLiteral("#f7eee8")));
        view.setCodeBackground(QColor(QStringLiteral("#211722")));
        view.setAccent(QColor(QStringLiteral("#e7a17d")));
    }
private slots:
    void rendersMarkdownCodeAndMath() {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("render glyph failed.*")));
        MarkdownView view;
        style(view);
        view.setMarkdown(QStringLiteral("# Formula\n\n$\\frac{a+b}{2}$\n\n$$\\sum_{i=1}^{n} i=\\frac{n(n+1)}{2}$$\n\n```cpp\nconst int count = 4;\n```"));
        QTRY_VERIFY_WITH_TIMEOUT(view.implicitHeight() > 80, 10000);
        QVERIFY2(view.renderError().isEmpty(), qPrintable(view.renderError()));
        QImage output(640, qCeil(view.implicitHeight()), QImage::Format_ARGB32_Premultiplied);
        output.fill(Qt::transparent);
        QPainter painter(&output);
        view.paint(&painter);
        painter.end();
        const QString artifactDirectory = qEnvironmentVariable("BETTERFLASH_TEST_ARTIFACT_DIR");
        if (!artifactDirectory.isEmpty()) {
            QDir().mkpath(artifactDirectory);
            QVERIFY(output.save(artifactDirectory + QStringLiteral("/markdown-render.png")));
        }
        bool painted = false;
        for (int y = 0; y < output.height() && !painted; ++y)
            for (int x = 0; x < output.width(); ++x)
                if (qAlpha(output.pixel(x, y)) > 0) { painted = true; break; }
        QVERIFY(painted);
    }
    void fencesDoNotInterpretMath() {
        MarkdownView view;
        style(view);
        view.setMarkdown(QStringLiteral("```python\nprice = '$\\frac{$'\n```"));
        QTRY_VERIFY(view.implicitHeight() > 0);
        QVERIFY(view.renderError().isEmpty());
    }
    void formulaGlyphsAreVisible() {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("render glyph failed.*")));
        MarkdownView view; style(view);
        view.setMarkdown(QStringLiteral("$\\frac{a+b}{2}$"));
        QTRY_VERIFY(view.implicitHeight() > 0);
        QVERIFY(view.renderError().isEmpty());
        QImage output(640, qCeil(view.implicitHeight()), QImage::Format_ARGB32_Premultiplied);
        output.fill(Qt::transparent);
        QPainter painter(&output); view.paint(&painter); painter.end();
        int pixels = 0;
        for (int y = 0; y < output.height(); ++y)
            for (int x = 0; x < output.width(); ++x) pixels += qAlpha(output.pixel(x, y)) > 0 ? 1 : 0;
        QVERIFY(pixels > 50);
    }
    void rejectsCorruptImagesAndLinkedMediaRoots() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString name = QStringLiteral("a").repeated(64) + QStringLiteral(".png");
        QImage image(40, 20, QImage::Format_RGB32); image.fill(Qt::green);
        QVERIFY(image.save(directory.filePath(name)));
        MarkdownView view; style(view); view.setMediaRoot(directory.path());
        view.setMarkdown(QStringLiteral("![corrupted](media:%1)").arg(name));
        QTRY_VERIFY(view.renderError().startsWith(QStringLiteral("IMAGE_INTEGRITY")));
        const QString link = directory.filePath(QStringLiteral("linked"));
        QVERIFY(QFile::link(directory.path(), link));
        view.setMediaRoot(link);
        view.setMarkdown(QStringLiteral("![linked](media:%1)").arg(name));
        QTRY_VERIFY(view.renderError().startsWith(QStringLiteral("IMAGE_PATH")));
    }
    void rejectsExternalAndArbitraryLocalImageResources() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("private.png"));
        QImage image(40, 20, QImage::Format_RGB32); image.fill(Qt::green);
        QVERIFY(image.save(path));
        for (const QString &url : {QUrl::fromLocalFile(path).toString(), QStringLiteral("https://127.0.0.1/private.png"),
                                 QStringLiteral("data:image/png;base64,AAAA")}) {
            MarkdownView view; style(view);
            view.setMarkdown(QStringLiteral("![external](%1)").arg(url));
            QTRY_VERIFY(view.renderError().startsWith(QStringLiteral("IMAGE_SOURCE")));
            QImage output(640, qCeil(view.implicitHeight()), QImage::Format_ARGB32_Premultiplied);
            output.fill(Qt::transparent);
            QPainter painter(&output); view.paint(&painter); painter.end();
            bool exposed = false;
            for (int y = 0; y < output.height() && !exposed; ++y)
                for (int x = 0; x < output.width(); ++x)
                    if (QColor::fromRgba(output.pixel(x, y)) == QColor(Qt::green)) { exposed = true; break; }
            QVERIFY(!exposed);
        }
    }
    void rendersStoredImagesAndRejectsTraversal() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QImage image(200, 120, QImage::Format_RGB32);
        image.fill(QColor(QStringLiteral("#4f925a")));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        const QString name = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".png");
        QVERIFY(image.save(temporary.filePath(name)));
        MarkdownView view;
        style(view);
        view.setMediaRoot(temporary.path());
        view.setMarkdown(QStringLiteral("![diagram](media:%1)").arg(name));
        QTRY_VERIFY(view.implicitHeight() >= 120);
        QVERIFY2(view.renderError().isEmpty(), qPrintable(view.renderError()));
        view.setMarkdown(QStringLiteral("![invalid](media:../private.png)"));
        QTRY_VERIFY(view.renderError().startsWith(QStringLiteral("IMAGE_PATH")));
    }
    void longContentReflowsAtNarrowWidths() {
        MarkdownView view;
        style(view);
        view.setMarkdown(QStringLiteral("A short readable phrase with enough words to reflow.\n\n").repeated(15));
        QTRY_VERIFY(view.implicitHeight() > 100);
        const qreal wideHeight = view.implicitHeight();
        view.setWidth(200);
        QTRY_VERIFY(view.implicitHeight() > wideHeight);
    }
};

QTEST_MAIN(RenderTest)
#include "render_test.moc"
