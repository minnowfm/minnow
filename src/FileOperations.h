#pragma once

#include <KFileItem>
#include <QList>
#include <QUrl>
#include <QWidget>

// wraps KIO jobs so MainWindow doesn't have to deal with job wiring itself
namespace FileOperations
{
// kept separate from the actual job-starting calls below so these can be unit tested
// without a filesystem or a running KIO session
QUrl renameDestination(const QUrl &url, const QString &newName);
QUrl mkdirDestination(const QUrl &parentDir, const QString &name);
// " (2)", " (3)", ... before `extension`, until it doesn't collide with something on disk.
// `extension` includes the dot, empty for plain directories.
QString uniqueFilePath(const QString &dirPath, const QString &baseName, const QString &extension);
// strips .zip/.tar/.tar.gz/etc from fileName (longest match wins), unchanged if no match
QString archiveBaseName(const QString &fileName);

void copyTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent);
void moveTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent);
void trash(const QList<QUrl> &urls, QWidget *parent);
void remove(const QList<QUrl> &urls, QWidget *parent);
void rename(const QUrl &url, const QString &newName, QWidget *parent);
void mkdir(const QUrl &parentDir, const QString &name, QWidget *parent);
void openUrl(const QUrl &url, QWidget *parent);
void showProperties(const KFileItemList &items, QWidget *parent);
void openWith(const QList<QUrl> &urls, QWidget *parent);
void emptyTrash(QWidget *parent);
void openTerminal(const QUrl &url, QWidget *parent);
void batchRename(const QList<QUrl> &urls, const QString &newNamePattern, QChar placeHolder, QWidget *parent);

void cutToClipboard(const QList<QUrl> &urls);
void copyToClipboard(const QList<QUrl> &urls);
void pasteClipboard(const QUrl &destDir, QWidget *parent);

bool isArchive(const QUrl &url);
void compressToArchive(const QList<QUrl> &sources, QWidget *parent);
void extractArchive(const QUrl &archiveUrl, QWidget *parent);
}
