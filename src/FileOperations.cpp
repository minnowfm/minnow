#include "FileOperations.h"

#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KIO/SimpleJob>
#include <KJobWidgets>

namespace FileOperations
{

void copyTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    KIO::Job *job = KIO::copy(sources, destDir, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void moveTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    KIO::Job *job = KIO::move(sources, destDir, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void trash(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;
    KIO::Job *job = KIO::trash(urls, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void remove(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;
    KIO::Job *job = KIO::del(urls, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void rename(const QUrl &url, const QString &newName, QWidget *parent)
{
    QUrl dest = url.adjusted(QUrl::RemoveFilename);
    dest.setPath(dest.path() + newName);
    KIO::Job *job = KIO::moveAs(url, dest, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void mkdir(const QUrl &parentDir, const QString &name, QWidget *parent)
{
    QUrl dest = parentDir;
    dest.setPath(dest.path() + (dest.path().endsWith('/') ? QString() : QStringLiteral("/")) + name);
    KIO::SimpleJob *job = KIO::mkdir(dest);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
}

void openUrl(const QUrl &url, QWidget *parent)
{
    auto *job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled, parent));
    job->start();
}

}
