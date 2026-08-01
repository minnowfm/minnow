#pragma once

#include <QList>
#include <QUrl>

// Returns the parent directory URL of the given path-like URL.
// Handles trailing slashes and stops at root ("/" stays "/").
QUrl parentOf(const QUrl &url);

// True if dropping `urls` onto `destDir` wouldn't be a real move/copy: either every url is
// already directly in destDir (dropping something back where it already is - the path bar's
// current-location segment, a sidebar entry for the folder you're already in, the view
// background itself), or destDir is one of the urls itself, or nested inside one of them
// (dropping a folder onto its own icon, or into one of its own subfolders). KIO rejects both,
// but only after the fact, as a confusing "could not rename" error.
bool dropWouldBeNoOpOrInvalid(const QList<QUrl> &urls, const QUrl &destDir);
