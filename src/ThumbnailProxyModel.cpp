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
