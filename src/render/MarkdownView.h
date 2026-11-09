#pragma once

#include <QColor>
#include <QCache>
#include <QQuickPaintedItem>
#include <QTextDocument>
#include <QTextCursor>
#include <QHash>
#include <QImage>
#include <QSet>

class MarkdownView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY styleChanged)
    Q_PROPERTY(QString markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY styleChanged)
    Q_PROPERTY(QColor codeBackground READ codeBackground WRITE setCodeBackground NOTIFY styleChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY styleChanged)
    Q_PROPERTY(qreal baseFontSize READ baseFontSize WRITE setBaseFontSize NOTIFY styleChanged)
    Q_PROPERTY(QString renderError READ renderError NOTIFY renderErrorChanged)
    Q_PROPERTY(QString mediaRoot READ mediaRoot WRITE setMediaRoot NOTIFY styleChanged)
    Q_PROPERTY(bool clozePreview READ clozePreview WRITE setClozePreview NOTIFY styleChanged)
    Q_PROPERTY(bool selectable READ selectable WRITE setSelectable NOTIFY selectableChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QColor selectionBackground READ selectionBackground WRITE setSelectionBackground NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionForeground READ selectionForeground WRITE setSelectionForeground NOTIFY styleChanged)
public:
    explicit MarkdownView(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool dark);
    QString markdown() const { return m_markdown; }
    QColor foreground() const { return m_foreground; }
    QColor codeBackground() const { return m_codeBackground; }
    QColor accent() const { return m_accent; }
    qreal baseFontSize() const { return m_baseFontSize; }
    QString renderError() const { return m_renderError; }
    QString mediaRoot() const { return m_mediaRoot; }
    bool clozePreview() const { return m_clozePreview; }
    bool selectable() const { return m_selectable; }
    bool hasSelection() const { return m_selection.hasSelection(); }
    QColor selectionBackground() const { return m_selectionBackground; }
    QColor selectionForeground() const { return m_selectionForeground; }
    void setMarkdown(const QString &markdown);
    void setForeground(const QColor &color);
    void setCodeBackground(const QColor &color);
    void setAccent(const QColor &color);
    void setBaseFontSize(qreal size);
    void setMediaRoot(const QString &directory);
    void setClozePreview(bool enabled);
    void setSelectable(bool enabled);
    void setSelectionBackground(const QColor &color);
    void setSelectionForeground(const QColor &color);
    Q_INVOKABLE void copySelection() const;
    Q_INVOKABLE void selectAll();
    static QString plainText(const QString &markdown);
    // Returns prose suitable for narration, omitting fenced and indented code blocks.
    static QString spokenText(const QString &markdown);
signals:
    void markdownChanged();
    void styleChanged();
    void renderErrorChanged();
    void selectableChanged();
    void selectionChanged();
    void contextMenuRequested(qreal x, qreal y);
protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    void setSelection(const QTextCursor &cursor);
    void clearSelection();
    int textPosition(const QPointF &point) const;
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
    QTextCursor m_selection;
    QString m_markdown;
    QString m_renderError;
    QString m_mediaRoot;
    QCache<QString, QImage> m_images{32 * 1024 * 1024};
    QHash<QString, QString> m_imageErrors;
    QColor m_foreground;
    QColor m_codeBackground;
    QColor m_accent;
    QColor m_selectionBackground;
    QColor m_selectionForeground;
    qreal m_baseFontSize = 22.0;
    bool m_scheduled = false;
    bool m_darkMode = true;
    bool m_clozePreview = false;
    bool m_selectable = false;
    bool m_selecting = false;
    QSet<QString> m_codeKeys;
    QSet<QString> m_requestedCodeKeys;
    bool m_waitingForCodeCapacity = false;
};
