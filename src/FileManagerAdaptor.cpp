#include "FileManagerAdaptor.h"
#include "MainWindow.h"

FileManagerAdaptor::FileManagerAdaptor(MainWindow *window)
    : QDBusAbstractAdaptor(window)
    , m_window(window)
{
}

void FileManagerAdaptor::ShowFolders(const QStringList &uris, const QString &startupId)
{
    Q_UNUSED(startupId);
    for (const QString &uri : uris)
        m_window->revealFolder(QUrl(uri));
}

void FileManagerAdaptor::ShowItems(const QStringList &uris, const QString &startupId)
{
    Q_UNUSED(startupId);
    for (const QString &uri : uris)
        m_window->revealItem(QUrl(uri));
}

void FileManagerAdaptor::ShowItemProperties(const QStringList &uris, const QString &startupId)
{
    Q_UNUSED(startupId);
    for (const QString &uri : uris)
        m_window->revealItemProperties(QUrl(uri));
}
