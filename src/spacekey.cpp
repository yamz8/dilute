#include "spacekey.h"

#include <QGuiApplication>
#include <QKeyEvent>

bool SpaceKey::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    // Events reach the filter once per object on their way (window, item), so
    // react only at the window, and only to a bare Space.
    if (key->key() != Qt::Key_Space || key->modifiers() != Qt::NoModifier || !watched->isWindowType())
        return QObject::eventFilter(watched, event);
    QObject *focus = QGuiApplication::focusObject();
    if (focus && focus->property("acceptsSpace").toBool())
        return false;
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat())
        emit tapped();
    return true;
}
