#include "FileOperations.h"
#include "PathUtils.h"
#include "PropertiesDialog.h"

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

#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

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

    const QString text = urls.size() == 1
        ? QObject::tr("Permanently delete \"%1\"?").arg(urls.first().fileName())
        : QObject::tr("Permanently delete %n item(s)?", "", urls.size());
    if (!confirmPermanentDelete(parent, text))
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
}

}
