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
// Shared by remove() and emptyTrash() - both are irreversible, so both are gated by the
// same "Confirmations/ConfirmPermanentDelete" setting rather than needing their own toggle.
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

// Ordered longest-suffix-first so a name like "foo.tar.gz" matches ".tar.gz" (and gets its
// whole compound extension stripped for the extracted folder name) rather than stopping at
// the generic ".gz"/".tar" it also happens to end with.
const QStringList &archiveSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
        QStringLiteral(".tgz"),    QStringLiteral(".tbz2"),    QStringLiteral(".txz"),
        QStringLiteral(".tar"),    QStringLiteral(".zip"),
    };
    return suffixes;
}

// Appends " (2)", " (3)", ... before `extension` until the result doesn't already exist.
// `extension` includes the leading dot, or is empty for a plain directory name.
QString uniqueFilePath(const QString &dirPath, const QString &baseName, const QString &extension)
{
    QString candidate = dirPath + QLatin1Char('/') + baseName + extension;
    for (int counter = 2; QFileInfo::exists(candidate); ++counter)
        candidate = dirPath + QLatin1Char('/') + baseName + QStringLiteral(" (%1)").arg(counter) + extension;
    return candidate;
}

// Dispatched from the worker thread doing the actual compress/extract - TaskManager lives on
// the GUI thread, so this can't just be a direct call.
void reportProgress(int taskId, qint64 bytesDone, qint64 totalBytes)
{
    if (totalBytes <= 0)
        return;
    const int percent = qBound(0, int(bytesDone * 100 / totalBytes), 100);
    QMetaObject::invokeMethod(TaskManager::self(), [taskId, percent] { TaskManager::self()->updateTaskProgress(taskId, percent); },
                               Qt::QueuedConnection);
}

// QFileInfo::symLinkTarget() resolves to an absolute path, losing a relative link's actual
// (portable) target and leaking this machine's directory layout into the archive. readlink()
// returns the link's raw, unresolved contents instead.
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

// Recreates what KArchive::addLocalDirectory() does internally (recurse, writeDir for each
// directory, addLocalFile for each file) but one file at a time, so progress can be reported
// after each one - addLocalDirectory() itself is a single opaque call with no way to hook in.
bool addPathRecursive(KArchive &archive, const QString &absolutePath, const QString &destPath, qint64 &bytesDone, qint64 totalBytes,
                      int taskId, QString *error)
{
    const QFileInfo info(absolutePath);
    // Checked before isDir() specifically because isDir() follows symlinks - without this, a
    // symlink to a directory gets recursed into as if it were a real subdirectory, which for a
    // link to an ancestor directory recurses forever, and for a link elsewhere pulls in content
    // outside the selected tree. Store the link itself instead of following it.
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

// Recreates what KArchiveDirectory::copyTo(dest, recursive=true) does internally, but one file
// at a time so progress can be reported after each one.
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
    // Not tracked in Activity (and so no notification either) - deletion, trash or permanent,
    // shouldn't show up there at all.
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
    // Permanent deletion is intentionally not recorded with FileUndoManager - it can't be undone.
    // Not tracked in the Activity list either - it's effectively instant, nothing to show progress for.
}

void rename(const QUrl &url, const QString &newName, QWidget *parent)
{
    const QUrl dest = renameDestination(url, newName);
    KIO::Job *job = KIO::moveAs(url, dest, KIO::DefaultFlags);
    KJobWidgets::setWindow(job, parent);
    job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Rename, {url}, dest, job);
    // Not tracked in the Activity list - a rename is effectively instant, nothing to show progress for.
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
    // Off by default (KIO assumes non-file-manager callers like browsers/mail clients) -
    // this is what makes double-clicking an executable prompt "run or open as text?"
    // (Dolphin's behavior) instead of just failing with no associated application.
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

    // Kept in sync with the terminal list SettingsTab advertises as options.
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
    // Not tracked in Activity - same reasoning as the single-item rename: effectively instant.
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

    // Reserved synchronously, right after picking the name, rather than leaving actual file
    // creation to the worker thread - otherwise a second compress triggered before the first
    // worker gets around to opening its output would compute the same "unique" name (the real
    // file doesn't exist yet as far as uniqueFilePath() can see) and both would write to it
    // concurrently.
    QFile reservation(destPath);
    if (!reservation.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(parent, QObject::tr("Compress"), QObject::tr("Could not create \"%1\".").arg(destPath));
        return;
    }
    reservation.close();

    // KArchive's API is synchronous, so running it directly on the GUI thread would freeze
    // the whole window for any selection large enough to take more than an instant (this is
    // exactly what happened before this fix - a multi-item compress made Minnow "Not
    // Responding"). QtConcurrent::run moves the actual I/O to a worker thread; the
    // QFutureWatcher marshals the result back for the error dialog, if any.
    //
    // The watcher (and its finished connection) is parented/anchored to TaskManager - which
    // outlives every tab/window - rather than to `parent` (the tab that started this). If it
    // were tied to the tab, closing that tab mid-operation would destroy the watcher, the
    // connection would never fire, and finishTask() would never run: the task would stay
    // "active" forever and block the whole app from closing. `parent` is still used for the
    // error dialog, but only through a QPointer so a since-destroyed tab doesn't dangle.
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

    QString baseName = fileName;
    for (const QString &suffix : archiveSuffixes()) {
        if (lowerName.endsWith(suffix)) {
            baseName = fileName.left(fileName.length() - suffix.length());
            break;
        }
    }
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

    // Same reasoning as compressToArchive(): KArchive is synchronous, so the actual
    // open/copyTo work happens on a worker thread instead of blocking the GUI thread, and the
    // watcher is anchored to TaskManager (not `parent`) for the same reason - so closing the
    // initiating tab mid-extraction can't leave the task stuck "active" forever.
    const int taskId = TaskManager::self()->startTask(QObject::tr("Extracting \"%1\"").arg(fileName));
    const QPointer<QWidget> safeParent(parent);

    auto *watcher = new QFutureWatcher<QString>(TaskManager::self());
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, watcher, [watcher, fileName, destPath, safeParent, taskId] {
        const QString error = watcher->result();
        TaskManager::self()->finishTask(taskId, !error.isEmpty());
        if (!error.isEmpty()) {
            // destPath is a fresh, uniquely-named directory created just for this extraction -
            // on failure (archive wouldn't open, or extraction stopped partway), remove
            // whatever got written instead of leaving a partial tree that a retry can't tell
            // apart from a real one (it just gets its own "(2)" suffix instead of overwriting).
            // Done before the (modal) warning dialog so cleanup isn't gated on the user
            // noticing and dismissing it.
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
