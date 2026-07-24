#pragma once

#include <QUrl>

// Returns the parent directory URL of the given path-like URL.
// Handles trailing slashes and stops at root ("/" stays "/").
QUrl parentOf(const QUrl &url);
