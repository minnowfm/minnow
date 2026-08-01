#pragma once

#include <QList>
#include <QUrl>

// Returns the parent directory URL of the given path-like URL.
// Handles trailing slashes and stops at root ("/" stays "/").
QUrl parentOf(const QUrl &url);

// True if every url already lives directly in destDir - dropping them "onto" it (the path
// bar's current-location segment, a sidebar entry for the folder you're already in, the view
// background itself) would be a no-op, not a real move/copy. KIO rejects a job whose source
// and destination are identical, which without this check surfaces as a confusing "could not
// rename" error for something that should have just done nothing.
bool allUrlsAlreadyIn(const QList<QUrl> &urls, const QUrl &destDir);
