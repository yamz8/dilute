#pragma once

#include <QVariantMap>

#include "filepicker.h"

class QDBusPendingCallWatcher;

// The file chooser of xdg-desktop-portal over D-Bus, so dialogs look like the
// rest of the desktop. Follows Omacut's portalfilepicker.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void open(const QString &title, const QList<Filter> &filters, Done done) override;
    void save(const QString &title, const QString &suggestedPath, const QList<Filter> &filters,
              Done done) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    void requestFile(const QString &method, const QString &title, QVariantMap options, Done done);
    bool connectToRequestPath(const QString &path);
    void clearPending();
    // Best-effort Close of the pending portal request, if there is one.
    void closePendingRequest();

    QString m_pendingPath;
    Done m_pendingDone;
    bool m_pending = false;
    // Bumped per request, so a late reply for a replaced request is ignored.
    int m_pendingGeneration = 0;
    QDBusPendingCallWatcher *m_pendingWatcher = nullptr;
};
