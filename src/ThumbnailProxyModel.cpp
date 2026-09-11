#include "ThumbnailProxyModel.h"

#include <KDirModel>
#include <KFileItem>

ThumbnailProxyModel::ThumbnailProxyModel(QObject *parent)
    : KDirSortFilterProxyModel(parent)
{
}

QVariant ThumbnailProxyModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DecorationRole && !m_thumbnails.isEmpty()) {
        if (auto *dirModel = qobject_cast<KDirModel *>(sourceModel())) {
            const KFileItem item = dirModel->itemForIndex(mapToSource(index));
            if (!item.isNull()) {
                const auto it = m_thumbnails.constFind(item.url().toString());
                if (it != m_thumbnails.constEnd())
                    return *it;
            }
        }
    }
    if (role == CutRole) {
        if (m_cutUrls.isEmpty())
            return false;
        if (auto *dirModel = qobject_cast<KDirModel *>(sourceModel())) {
            const KFileItem item = dirModel->itemForIndex(mapToSource(index));
            if (!item.isNull())
                return m_cutUrls.contains(item.url().toString());
        }
        return false;
    }
    return KDirSortFilterProxyModel::data(index, role);
}

void ThumbnailProxyModel::setThumbnail(const QUrl &url, const QIcon &icon)
{
    m_thumbnails[url.toString()] = icon;

    if (auto *dirModel = qobject_cast<KDirModel *>(sourceModel())) {
        const QModelIndex sourceIndex = dirModel->indexForUrl(url);
        if (sourceIndex.isValid()) {
            const QModelIndex proxyIndex = mapFromSource(sourceIndex);
            if (proxyIndex.isValid())
                Q_EMIT dataChanged(proxyIndex, proxyIndex, {Qt::DecorationRole});
        }
    }
}

void ThumbnailProxyModel::renameThumbnail(const QUrl &oldUrl, const QUrl &newUrl)
{
    const auto it = m_thumbnails.constFind(oldUrl.toString());
    if (it == m_thumbnails.constEnd())
        return;
    const QIcon icon = *it;
    m_thumbnails.remove(oldUrl.toString());
    setThumbnail(newUrl, icon);
}

void ThumbnailProxyModel::clearThumbnails()
{
    if (m_thumbnails.isEmpty())
        return;
    m_thumbnails.clear();
    Q_EMIT layoutChanged();
}

void ThumbnailProxyModel::setCutUrls(const QSet<QString> &urls)
{
    if (m_cutUrls == urls)
        return;
    m_cutUrls = urls;
    // dataChanged rather than layoutChanged - only the CutRole appearance changed, rows and
    // their order didn't, so there's no need to disturb the current selection/scroll position.
    const int rows = rowCount();
    const int cols = columnCount();
    if (rows > 0 && cols > 0)
        Q_EMIT dataChanged(index(0, 0), index(rows - 1, cols - 1), {CutRole});
}
