#pragma once

#include <QHash>
#include <QIcon>

#include <KDirSortFilterProxyModel>

// Overlays generated thumbnails onto KDirSortFilterProxyModel's normal per-mimetype icons,
// keyed by file URL. BrowserTab feeds thumbnails in as KIO::PreviewJob produces them.
class ThumbnailProxyModel : public KDirSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ThumbnailProxyModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    void setThumbnail(const QUrl &url, const QIcon &icon);
    // Moves a cached thumbnail to follow a renamed file instead of losing it - m_thumbnails is
    // keyed by URL, so a rename alone (no content change) would otherwise leave the cached icon
    // orphaned under the old URL until the whole folder gets relisted.
    void renameThumbnail(const QUrl &oldUrl, const QUrl &newUrl);
    void clearThumbnails();

private:
    QHash<QString, QIcon> m_thumbnails;
};
