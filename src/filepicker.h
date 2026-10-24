#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <functional>

// Open and save dialogs, behind an interface so tests can answer them without
// a desktop. Done runs only when the person picked a file.
class FilePicker : public QObject {
    Q_OBJECT

public:
    struct Filter {
        QString name;
        QStringList patterns;
    };
    using Done = std::function<void(const QString &path)>;

    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void open(const QString &title, const QList<Filter> &filters, Done done) = 0;
    virtual void save(const QString &title, const QString &suggestedPath, const QList<Filter> &filters,
                      Done done) = 0;

signals:
    void failed(const QString &message);
};
