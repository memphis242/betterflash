#pragma once

#include <QObject>

class TextFocusDismissal final : public QObject {
    Q_OBJECT

public:
    explicit TextFocusDismissal(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
