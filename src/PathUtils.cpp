#include "PathUtils.h"

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
