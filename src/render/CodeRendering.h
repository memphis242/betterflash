#pragma once

#include "LspHighlighter.h"
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>
#include <QObject>
#include <QSettings>
#include <QTextCharFormat>

struct CodeSpan {
    int offset = 0;
    int length = 0;
    QTextCharFormat format;
};

class CodeRendering final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList darkThemes READ darkThemes CONSTANT)
    Q_PROPERTY(QStringList lightThemes READ lightThemes CONSTANT)
    Q_PROPERTY(QString darkTheme READ darkTheme WRITE setDarkTheme NOTIFY changed)
    Q_PROPERTY(QString lightTheme READ lightTheme WRITE setLightTheme NOTIFY changed)
    Q_PROPERTY(bool languageServerEnabled READ languageServerEnabled WRITE setLanguageServerEnabled NOTIFY changed)
    Q_PROPERTY(QString clangdPath READ clangdPath NOTIFY changed)
    Q_PROPERTY(QString serverStatus READ serverStatus NOTIFY serverStatusChanged)
public:
    static CodeRendering *instance();
    QStringList darkThemes() const { return m_darkThemes; }
    QStringList lightThemes() const { return m_lightThemes; }
    QString darkTheme() const { return m_darkTheme; }
    QString lightTheme() const { return m_lightTheme; }
    bool languageServerEnabled() const { return m_serverEnabled; }
    QString clangdPath() const { return m_clangdPath; }
    QString serverStatus() const { return m_server.status(); }
    void setDarkTheme(const QString &name);
    void setLightTheme(const QString &name);
    void setLanguageServerEnabled(bool enabled);
    Q_INVOKABLE bool setClangdPath(const QString &path);
    bool restoreClangdPath(const QString &path);
    void setVisibleSources(QObject *view, const QSet<QString> &keys);
    KSyntaxHighlighting::Theme theme(bool dark) const;
    QList<struct CodeSpan> highlight(const QString &language, const QString &source, bool dark, bool requestSemantic, bool &semanticAccepted);
    static QString normalizedLanguage(const QString &language);
    static QColor readableColor(const QColor &color, const QColor &background);
signals:
    void changed();
    void tokensReady(const QString &key);
    void semanticCapacityAvailable();
    void serverStatusChanged();
private:
    explicit CodeRendering(QObject *parent);
    KSyntaxHighlighting::Repository m_repository;
    QSettings m_settings;
    LspHighlighter m_server;
    QStringList m_darkThemes;
    QStringList m_lightThemes;
    QString m_darkTheme;
    QString m_lightTheme;
    QString m_clangdPath;
    bool m_serverEnabled = true;
    QHash<QObject *, QSet<QString>> m_visibleSources;
    void updateVisibleSources();
};
