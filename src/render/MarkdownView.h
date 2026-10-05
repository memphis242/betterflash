#pragma once

#include <QColor>
#include <QCache>
#include <QQuickPaintedItem>
#include <QTextDocument>
#include <QHash>
#include <QImage>
#include <QSet>

class MarkdownView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY styleChanged)
    Q_PROPERTY(QColor codeBackground READ codeBackground WRITE setCodeBackground NOTIFY styleChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY styleChanged)
    Q_PROPERTY(qreal baseFontSize READ baseFontSize WRITE setBaseFontSize NOTIFY styleChanged)
    Q_PROPERTY(QString renderError READ renderError NOTIFY renderErrorChanged)
    Q_PROPERTY(QString mediaRoot READ mediaRoot WRITE setMediaRoot NOTIFY styleChanged)
public:
    explicit MarkdownView(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    QString markdown() const { return m_markdown; }
    QColor foreground() const { return m_foreground; }
    QColor codeBackground() const { return m_codeBackground; }
    QColor accent() const { return m_accent; }
    qreal baseFontSize() const { return m_baseFontSize; }
    QString renderError() const { return m_renderError; }
    QString mediaRoot() const { return m_mediaRoot; }
    void setMarkdown(const QString &markdown);
    void setForeground(const QColor &color);
    void setCodeBackground(const QColor &color);
    void setAccent(const QColor &color);
    void setBaseFontSize(qreal size);
    void setMediaRoot(const QString &directory);
    static QString plainText(const QString &markdown);
signals:
    void markdownChanged();
    void styleChanged();
    void renderErrorChanged();
protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
private:
    void scheduleRender();
    void rebuild();
    QVariant loadImage(const QUrl &url);
    void reportImageError(const QString &message);
    class Document final : public QTextDocument {
    public:
        explicit Document(MarkdownView *view) : m_view(view) { Q_ASSERT(m_view); }
    protected:
        QVariant loadResource(int type, const QUrl &url) override {
            if (type != QTextDocument::ImageResource) return QByteArray{};
            const QVariant image = m_view->loadImage(url);
            return image.isValid() ? image : QVariant::fromValue(QImage());
        }
    private:
        MarkdownView *const m_view;
    };
    Document m_document;
    QString m_markdown;
    QString m_renderError;
    QString m_mediaRoot;
    QCache<QString, QImage> m_images{32 * 1024 * 1024};
    QHash<QString, QString> m_imageErrors;
    QColor m_foreground;
    QColor m_codeBackground;
    QColor m_accent;
    qreal m_baseFontSize = 22.0;
    bool m_scheduled = false;
};
