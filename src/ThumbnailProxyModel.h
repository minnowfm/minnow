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
    void clearThumbnails();

private:
    QHash<QString, QIcon> m_thumbnails;
};
