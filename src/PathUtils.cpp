#include "PathUtils.h"

#include <algorithm>

QUrl parentOf(const QUrl &url)
{
    QUrl u = url;
    QString path = u.path();
    if (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const int idx = path.lastIndexOf(QLatin1Char('/'));
    path = (idx <= 0) ? QStringLiteral("/") : path.left(idx);
    u.setPath(path);
    return u;
}

bool allUrlsAlreadyIn(const QList<QUrl> &urls, const QUrl &destDir)
{
    return std::all_of(urls.constBegin(), urls.constEnd(), [&destDir](const QUrl &url) { return parentOf(url) == destDir; });
}
