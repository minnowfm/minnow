#pragma once

#include <KFileItem>
#include <QList>
#include <QUrl>
#include <QWidget>

// Thin wrappers around KIO jobs so MainWindow doesn't deal with job wiring directly.
namespace FileOperations
{
// Pure path computation, split out from the job-starting functions below so it's
// unit-testable without touching the filesystem or needing a running KIO session.
QUrl renameDestination(const QUrl &url, const QString &newName);
QUrl mkdirDestination(const QUrl &parentDir, const QString &name);

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
}
