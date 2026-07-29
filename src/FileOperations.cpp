#include "FileOperations.h"
#include "PathUtils.h"
#include "PropertiesDialog.h"
#include "TaskManager.h"

#include <KArchive>
#include <KArchiveDirectory>
#include <KArchiveEntry>
#include <KArchiveFile>
#include <KIO/ApplicationLauncherJob>
#include <KIO/BatchRenameJob>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/EmptyTrashJob>
#include <KIO/FileUndoManager>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KIO/SimpleJob>
#include <KJobWidgets>
#include <KOpenWithDialog>
#include <KService>
#include <KTar>
#include <KZip>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrentRun>

#include <climits>
#include <memory>

#include <unistd.h>

namespace
{
// used by remove() and emptyTrash(), both irreversible - one setting gates both
bool confirmPermanentDelete(QWidget *parent, const QString &text)
{
    QSettings settings;
    if (!settings.value(QStringLiteral("Confirmations/ConfirmPermanentDelete"), true).toBool())
        return true;

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("Confirm Permanent Deletion"));
    box.setText(text);
    box.setInformativeText(QObject::tr("This action cannot be undone."));
    QPushButton *deleteButton = box.addButton(QObject::tr("Delete"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == deleteButton;
}

// longest suffix first, or "foo.tar.gz" would match plain ".gz"/".tar" instead of the whole thing
const QStringList &archiveSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
        QStringLiteral(".tgz"),    QStringLiteral(".tbz2"),    QStringLiteral(".txz"),
        QStringLiteral(".tar"),    QStringLiteral(".zip"),
    };
    return suffixes;
}

// called from the compress/extract worker thread, TaskManager lives on the GUI thread
void reportProgress(int taskId, qint64 bytesDone, qint64 totalBytes)
{
    if (totalBytes <= 0)
        return;
    const int percent = qBound(0, int(bytesDone * 100 / totalBytes), 100);
    QMetaObject::invokeMethod(TaskManager::self(), [taskId, percent] { TaskManager::self()->updateTaskProgress(taskId, percent); },
                               Qt::QueuedConnection);
}

// QFileInfo::symLinkTarget() resolves to an absolute path - that'd bake this machine's
// directory layout into the archive. readlink() gives us the raw, unresolved target.
QString rawSymLinkTarget(const QString &path)
{
    const QByteArray encoded = QFile::encodeName(path);
    char buffer[PATH_MAX];
    const ssize_t length = ::readlink(encoded.constData(), buffer, sizeof(buffer) - 1);
    if (length < 0)
        return QFileInfo(path).symLinkTarget(); // fallback, shouldn't normally happen
    return QFile::decodeName(QByteArray(buffer, length));
}

qint64 localSizeRecursive(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return info.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// basically a manual addLocalDirectory() - that call is one opaque blob with no way to
// report progress in between files, so we do the recursion + writeDir/addLocalFile ourselves
bool addPathRecursive(KArchive &archive, const QString &absolutePath, const QString &destPath, qint64 &bytesDone, qint64 totalBytes,
                      int taskId, QString *error)
{
    const QFileInfo info(absolutePath);
    // isDir() follows symlinks, so this has to be checked first - otherwise a symlink to a
    // parent dir recurses forever, and one pointing elsewhere pulls in stuff we don't want
    if (info.isSymLink()) {
        if (!archive.writeSymLink(destPath, rawSymLinkTarget(absolutePath))) {
            *error = QObject::tr("Could not add link \"%1\" to the archive.").arg(destPath);
            return false;
        }
        return true;
    }
    if (info.isDir()) {
        if (!archive.writeDir(destPath)) {
            *error = QObject::tr("Could not add folder \"%1\" to the archive.").arg(destPath);
            return false;
        }
        const QDir dir(absolutePath);
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            if (!addPathRecursive(archive, entry.absoluteFilePath(), destPath + QLatin1Char('/') + entry.fileName(), bytesDone,
                                   totalBytes, taskId, error))
                return false;
        }
        return true;
    }

    if (!archive.addLocalFile(absolutePath, destPath)) {
        *error = QObject::tr("Could not add \"%1\" to the archive.").arg(destPath);
        return false;
    }
    bytesDone += info.size();
    reportProgress(taskId, bytesDone, totalBytes);
    return true;
}

qint64 archiveSizeRecursive(const KArchiveDirectory *dir)
{
    qint64 total = 0;
    for (const QString &name : dir->entries()) {
        const KArchiveEntry *entry = dir->entry(name);
        if (!entry)
            continue;
        if (entry->isDirectory())
            total += archiveSizeRecursive(static_cast<const KArchiveDirectory *>(entry));
        else if (entry->isFile())
            total += static_cast<const KArchiveFile *>(entry)->size();
    }
    return total;
}

// same idea as addPathRecursive() above but for extraction - copyTo(recursive=true) can't report progress
bool extractRecursive(const KArchiveDirectory *dir, const QString &destDir, qint64 &bytesDone, qint64 totalBytes, int taskId,
                      QString *error)
{
    if (!QDir().mkpath(destDir)) {
        *error = QObject::tr("Could not create destination folder \"%1\".").arg(destDir);
        return false;
    }
    for (const QString &name : dir->entries()) {
        const KArchiveEntry *entry = dir->entry(name);
        if (!entry)
            continue;
        if (entry->isDirectory()) {
            if (!extractRecursive(static_cast<const KArchiveDirectory *>(entry), destDir + QLatin1Char('/') + name, bytesDone,
                                   totalBytes, taskId, error))
                return false;
            continue;
        }
        // KArchive has no distinct symlink entry type, a link still reports isFile() - check
        // for it before the copyTo() branch or it just gets dereferenced into a regular file
        const QString linkTarget = entry->symLinkTarget();
        if (!linkTarget.isEmpty()) {
            const QString linkPath = destDir + QLatin1Char('/') + name;
            if (!QFile::link(linkTarget, linkPath)) {
                *error = QObject::tr("Could not recreate link \"%1\".").arg(name);
                return false;
            }
            continue;
        }

        const auto *file = static_cast<const KArchiveFile *>(entry);
        if (!file->copyTo(destDir)) {
            *error = QObject::tr("Could not extract \"%1\".").arg(name);
            return false;
        }
        bytesDone += file->size();
        reportProgress(taskId, bytesDone, totalBytes);
    }
    return true;
}
}

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

QString uniqueFilePath(const QString &dirPath, const QString &baseName, const QString &extension)
{
    QString candidate = dirPath + QLatin1Char('/') + baseName + extension;
    for (int counter = 2; QFileInfo::exists(candidate); ++counter)
        candidate = dirPath + QLatin1Char('/') + baseName + QStringLiteral(" (%1)").arg(counter) + extension;
    return candidate;
}

QString archiveBaseName(const QString &fileName)
{
    const QString lowerName = fileName.toLower();
    for (const QString &suffix : archiveSuffixes()) {
        if (lowerName.endsWith(suffix))
            return fileName.left(fileName.length() - suffix.length());
    }
    return fileName;
}

void copyTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::copy(sources, destDir, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    TaskManager::self()->trackJob(job, QObject::tr("Copying %n item(s)", "", sources.size()));
}

void moveTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    KIO::CopyJob *job = KIO::move(sources, destDir, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Move, sources, destDir, job);
    TaskManager::self()->trackJob(job, QObject::tr("Moving %n item(s)", "", sources.size()));
}

void trash(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;
    KIO::Job *job = KIO::trash(urls, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, urls, QUrl(QStringLiteral("trash:/")), job);
    // no TaskManager::trackJob() here - deletions don't belong in the activity list
}

void remove(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;

    const QString text = urls.size() == 1
        ? QObject::tr("Permanently delete \"%1\"?").arg(urls.first().fileName())
        : QObject::tr("Permanently delete %n item(s)?", "", urls.size());
    if (!confirmPermanentDelete(parent, text))
        return;

    KIO::Job *job = KIO::del(urls, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    // no FileUndoManager::recordJob() - permanent means permanent, there's no undo to record
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
    TaskManager::self()->trackJob(job, QObject::tr("Creating folder \"%1\"").arg(name));
}

void openUrl(const QUrl &url, QWidget *parent)
{
    auto *job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled, parent));
    // off by default in KIO (assumes a browser/mail-client caller) - this is what gives us
    // the "run or open as text?" prompt on executables, like Dolphin does
    job->setShowOpenOrExecuteDialog(true);
    job->start();
}

void showProperties(const KFileItemList &items, QWidget *parent)
{
    if (items.isEmpty())
        return;
    auto *dialog = new PropertiesDialog(items, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void openWith(const QList<QUrl> &urls, QWidget *parent)
{
    if (urls.isEmpty())
        return;
    auto *dialog = new KOpenWithDialog(urls, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog, &QDialog::accepted, dialog, [dialog, urls, parent] {
        const KService::Ptr service = dialog->service();
        if (!service)
            return;
        auto *job = new KIO::ApplicationLauncherJob(service);
        job->setUrls(urls);
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled, parent));
        job->start();
    });
    dialog->show();
}

void emptyTrash(QWidget *parent)
{
    if (!confirmPermanentDelete(parent, QObject::tr("Permanently delete everything in the Trash?")))
        return;

    KIO::Job *job = KIO::emptyTrash();
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    TaskManager::self()->trackJob(job, QObject::tr("Emptying Trash"));
}

void openTerminal(const QUrl &url, QWidget *parent)
{
    Q_UNUSED(parent);
    if (!url.isLocalFile())
        return;

    const QSettings settings;
    const QString preferred = settings.value(QStringLiteral("Terminal/Command")).toString();
    if (!preferred.isEmpty()) {
        const QString path = QStandardPaths::findExecutable(preferred);
        if (!path.isEmpty()) {
            QProcess::startDetached(path, {}, url.toLocalFile());
            return;
        }
    }

    // keep this list matching what SettingsTab offers as options
    static const QStringList candidates = {
        QStringLiteral("konsole"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("xterm"),
        QStringLiteral("alacritty"),
        QStringLiteral("kitty"),
        QStringLiteral("foot"),
        QStringLiteral("wezterm"),
        QStringLiteral("tilix"),
    };
    for (const QString &candidate : candidates) {
        const QString path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty()) {
            QProcess::startDetached(path, {}, url.toLocalFile());
            return;
        }
    }
}

void batchRename(const QList<QUrl> &urls, const QString &newNamePattern, QChar placeHolder, QWidget *parent)
{
    if (urls.size() < 2)
        return;
    KIO::BatchRenameJob *job = KIO::batchRename(urls, newNamePattern, /*startIndex=*/1, placeHolder);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::BatchRename, urls, parentOf(urls.first()), job);
}

void cutToClipboard(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return;
    auto *mime = new QMimeData();
    mime->setUrls(urls);
    mime->setData(QStringLiteral("application/x-kde-cutselection"), QByteArrayLiteral("1"));
    QApplication::clipboard()->setMimeData(mime);
}

void copyToClipboard(const QList<QUrl> &urls)
{
    if (urls.isEmpty())
        return;
    auto *mime = new QMimeData();
    mime->setUrls(urls);
    QApplication::clipboard()->setMimeData(mime);
}

void pasteClipboard(const QUrl &destDir, QWidget *parent)
{
    const QMimeData *clip = QApplication::clipboard()->mimeData();
    if (!clip || !clip->hasUrls())
        return;
    const bool isCut = clip->data(QStringLiteral("application/x-kde-cutselection")) == QByteArrayLiteral("1");
    const QList<QUrl> urls = clip->urls();
    if (isCut)
        moveTo(urls, destDir, parent);
    else
        copyTo(urls, destDir, parent);
}

bool isArchive(const QUrl &url)
{
    if (!url.isLocalFile())
        return false;
    if (!QFileInfo(url.toLocalFile()).isFile())
        return false;
    const QString name = url.fileName().toLower();
    for (const QString &suffix : archiveSuffixes()) {
        if (name.endsWith(suffix))
            return true;
    }
    return false;
}

void compressToArchive(const QList<QUrl> &sources, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    for (const QUrl &url : sources) {
        if (!url.isLocalFile()) {
            QMessageBox::warning(parent, QObject::tr("Compress"), QObject::tr("Only local files can be compressed."));
            return;
        }
    }

    const QString dirPath = parentOf(sources.first()).toLocalFile();
    const QString baseName = sources.size() == 1 ? sources.first().fileName() : QObject::tr("Archive");
    const QString destPath = uniqueFilePath(dirPath, baseName, QStringLiteral(".zip"));

    // reserve the filename right now, synchronously - if we left this to the worker thread,
    // firing off a second compress before the first worker even opens its output file would
    // hand out the same "unique" name twice
    QFile reservation(destPath);
    if (!reservation.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(parent, QObject::tr("Compress"), QObject::tr("Could not create \"%1\".").arg(destPath));
        return;
    }
    reservation.close();

    // KArchive is synchronous - used to just call this straight on the GUI thread and a
    // big enough selection would freeze the whole window ("Not Responding" and everything).
    // QtConcurrent::run offloads the actual I/O, QFutureWatcher brings the result back.
    //
    // watcher lives under TaskManager, not `parent` - if it were parented to the tab that
    // started this, closing that tab mid-compress would kill the watcher before its finished
    // signal fires, and the task would just sit "active" forever and block app shutdown.
    // still use `parent` for the error dialog, just through a QPointer so it can't dangle.
    const int taskId = TaskManager::self()->startTask(QObject::tr("Compressing %n item(s)", "", sources.size()));
    const QPointer<QWidget> safeParent(parent);

    auto *watcher = new QFutureWatcher<QString>(TaskManager::self());
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, watcher, [watcher, destPath, safeParent, taskId] {
        const QString error = watcher->result();
        TaskManager::self()->finishTask(taskId, !error.isEmpty());
        if (!error.isEmpty()) {
            QMessageBox::warning(safeParent, QObject::tr("Compress"), error);
            QFile::remove(destPath);
        }
        watcher->deleteLater();
    });

    QFuture<QString> future = QtConcurrent::run([sources, destPath, taskId]() -> QString {
        qint64 totalBytes = 0;
        for (const QUrl &url : sources)
            totalBytes += localSizeRecursive(url.toLocalFile());

        KZip zip(destPath);
        if (!zip.open(QIODevice::WriteOnly))
            return QObject::tr("Could not create \"%1\": %2").arg(destPath, zip.errorString());

        qint64 bytesDone = 0;
        QString error;
        for (const QUrl &url : sources) {
            if (!addPathRecursive(zip, url.toLocalFile(), url.fileName(), bytesDone, totalBytes, taskId, &error)) {
                zip.close();
                return error;
            }
        }
        if (!zip.close())
            return QObject::tr("Could not finish writing \"%1\": %2").arg(destPath, zip.errorString());
        return QString();
    });
    watcher->setFuture(future);
}

void extractArchive(const QUrl &archiveUrl, QWidget *parent)
{
    if (!archiveUrl.isLocalFile())
        return;

    const QString path = archiveUrl.toLocalFile();
    const QString fileName = archiveUrl.fileName();
    const QString lowerName = fileName.toLower();

    const QString baseName = archiveBaseName(fileName);
    if (baseName == fileName) {
        QMessageBox::warning(parent, QObject::tr("Extract"), QObject::tr("\"%1\" is not a recognized archive format.").arg(fileName));
        return;
    }

    const QString destPath = uniqueFilePath(parentOf(archiveUrl).toLocalFile(), baseName, QString());
    if (!QDir().mkpath(destPath)) {
        QMessageBox::warning(parent, QObject::tr("Extract"), QObject::tr("Could not create destination folder \"%1\".").arg(destPath));
        return;
    }

    const bool isZip = lowerName.endsWith(QStringLiteral(".zip"));

    // same deal as compressToArchive() above - worker thread + TaskManager-anchored watcher
    const int taskId = TaskManager::self()->startTask(QObject::tr("Extracting \"%1\"").arg(fileName));
    const QPointer<QWidget> safeParent(parent);

    auto *watcher = new QFutureWatcher<QString>(TaskManager::self());
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, watcher, [watcher, fileName, destPath, safeParent, taskId] {
        const QString error = watcher->result();
        TaskManager::self()->finishTask(taskId, !error.isEmpty());
        if (!error.isEmpty()) {
            // clean up the partial extraction before showing the (modal) dialog - otherwise
            // a retry can't tell this half-written tree apart from a real one, it'd just get
            // its own "(2)" folder instead of overwriting it
            QDir(destPath).removeRecursively();
            QMessageBox::warning(safeParent, QObject::tr("Extract"), error);
        }
        watcher->deleteLater();
    });

    QFuture<QString> future = QtConcurrent::run([path, fileName, destPath, isZip, taskId]() -> QString {
        std::unique_ptr<KArchive> archive;
        if (isZip)
            archive = std::make_unique<KZip>(path);
        else
            archive = std::make_unique<KTar>(path);

        if (!archive->open(QIODevice::ReadOnly))
            return QObject::tr("Could not open \"%1\": %2").arg(fileName, archive->errorString());

        const KArchiveDirectory *rootDir = archive->directory();
        if (!rootDir) {
            archive->close();
            return QObject::tr("Could not read \"%1\".").arg(fileName);
        }

        const qint64 totalBytes = archiveSizeRecursive(rootDir);
        qint64 bytesDone = 0;
        QString error;
        const bool ok = extractRecursive(rootDir, destPath, bytesDone, totalBytes, taskId, &error);
        archive->close();

        return ok ? QString() : error;
    });
    watcher->setFuture(future);
}

}
