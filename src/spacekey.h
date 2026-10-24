#pragma once

#include <QObject>

// Space copies the main result, even from a number field, which has no use
// for spaces. Fields where people type words (names, notes) set the property
// acceptsSpace and keep the key. A Shortcut can't do this: text fields claim
// Space before shortcuts see it.
class SpaceKey : public QObject {
    Q_OBJECT

public:
    explicit SpaceKey(QObject *parent = nullptr) : QObject(parent) {}

signals:
    void tapped();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
