#pragma once

#include <QDBusAbstractAdaptor>
#include <QStringList>

class MainWindow;

// Implements org.freedesktop.FileManager1 so "Show in folder"/"Show item" requests from
// browsers and other apps land on Minnow, instead of falling back to whatever else answers
// that bus name (typically Dolphin, activated lazily via its own D-Bus service file) - this
// only works while a Minnow window has actually claimed the name, see MainWindow's constructor.
class FileManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManagerAdaptor(MainWindow *window);

public Q_SLOTS:
    void ShowFolders(const QStringList &uris, const QString &startupId);
    void ShowItems(const QStringList &uris, const QString &startupId);
    void ShowItemProperties(const QStringList &uris, const QString &startupId);

private:
    MainWindow *m_window;
};
