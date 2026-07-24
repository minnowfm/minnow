#include "FileOperations.h"
#include "PathUtils.h"

#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/FileUndoManager>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KIO/SimpleJob>
#include <KJobWidgets>

namespace FileOperations
{

QUrl renameDestination(const QUrl &url, const QString &newName)
{
    const QUrl dir = parentOf(url);
    QString dirPath = dir.path();
    if (!dirPath.endsWith(QLatin1Char('/')))
        dirPath += QLatin1Char('/');
    QUrl dest = dir;
    dest.setPath(dirPath + newName);
    return dest;
}

QUrl mkdirDestination(const QUrl &parentDir, const QString &name)
{
    QUrl dest = parentDir;
    dest.setPath(dest.path() + (dest.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name);
    return dest;
}

void copyTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::copy(sources, destDir, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordCopyJob(job);
}

void moveTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::move(sources, destDir, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Move, sources, destDir, job);
}

void trash(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;
    KIO::Job *job = KIO::trash(urls, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, urls, QUrl(QStringLiteral("trash:/")), job);
}

void remove(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;
    KIO::Job *job = KIO::del(urls, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    // Permanent deletion is intentionally not recorded with FileUndoManager - it can't be undone.
}

void rename(const QUrl &url, const QString &newName, QWidget *parent)
{
    const QUrl dest = renameDestination(url, newName);
    KIO::Job *job = KIO::moveAs(url, dest, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Rename, {url}, dest, job);
}

void mkdir(const QUrl &parentDir, const QString &name, QWidget *parent)
{
    const QUrl dest = mkdirDestination(parentDir, name);
    KIO::SimpleJob *job = KIO::mkdir(dest);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, dest, job);
}

void openUrl(const QUrl &url, QWidget *parent)
{
    auto *job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled, parent));
    job->start();
}

}
