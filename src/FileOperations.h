#pragma once

#include <QList>
#include <QUrl>
#include <QWidget>

// Thin wrappers around KIO jobs so MainWindow doesn't deal with job wiring directly.
namespace FileOperations
{
void copyTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent);
void moveTo(const QList<QUrl> &sources, const QUrl &destDir, QWidget *parent);
void trash(const QList<QUrl> &urls, QWidget *parent);
void remove(const QList<QUrl> &urls, QWidget *parent);
void rename(const QUrl &url, const QString &newName, QWidget *parent);
void mkdir(const QUrl &parentDir, const QString &name, QWidget *parent);
void openUrl(const QUrl &url, QWidget *parent);
}
