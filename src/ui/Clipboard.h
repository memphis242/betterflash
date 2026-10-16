#pragma once

#include <QObject>
#include <QString>

class Clipboard final : public QObject {
    Q_OBJECT

public:
    explicit Clipboard(QObject *parent = nullptr);

    Q_INVOKABLE bool copyText(const QString &text) const;
};
