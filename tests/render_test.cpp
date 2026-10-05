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
