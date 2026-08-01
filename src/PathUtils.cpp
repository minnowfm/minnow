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

namespace
{
bool isSameOrDescendant(const QUrl &candidate, const QUrl &ancestor)
{
    if (candidate == ancestor)
        return true;
    // A matching path prefix means nothing across different locations - e.g. dragging
    // sftp://host-a/home/project onto sftp://host-b/home/project/archive just happens to
    // mirror the same directory structure on a different host, it's not actual nesting.
    if (candidate.scheme() != ancestor.scheme() || candidate.host() != ancestor.host()
        || candidate.port() != ancestor.port() || candidate.userName() != ancestor.userName())
        return false;
    QString ancestorPath = ancestor.path();
    if (!ancestorPath.endsWith(QLatin1Char('/')))
        ancestorPath += QLatin1Char('/');
    return candidate.path().startsWith(ancestorPath);
}
}

bool dropWouldBeNoOpOrInvalid(const QList<QUrl> &urls, const QUrl &destDir)
{
    const bool allAlreadyThere =
        std::all_of(urls.constBegin(), urls.constEnd(), [&destDir](const QUrl &url) { return parentOf(url) == destDir; });
    const bool wouldNestInsideItself =
        std::any_of(urls.constBegin(), urls.constEnd(), [&destDir](const QUrl &url) { return isSameOrDescendant(destDir, url); });
    return allAlreadyThere || wouldNestInsideItself;
}
