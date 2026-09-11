#pragma once

#include <QHash>
#include <QIcon>
#include <QSet>

#include <KDirSortFilterProxyModel>

// Overlays generated thumbnails onto KDirSortFilterProxyModel's normal per-mimetype icons,
// keyed by file URL. BrowserTab feeds thumbnails in as KIO::PreviewJob produces them.
//
// Also tracks which items are currently sitting on the "cut" clipboard, so views can dim them
// (Dolphin-style) to show they're staged to be moved on the next paste.
class ThumbnailProxyModel : public KDirSortFilterProxyModel
{
    Q_OBJECT

public:
    // Custom role a delegate can query to find out whether an item should be painted as "cut".
    static constexpr int CutRole = Qt::UserRole + 100;

    explicit ThumbnailProxyModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    void setThumbnail(const QUrl &url, const QIcon &icon);
    // Moves a cached thumbnail to follow a renamed file instead of losing it - m_thumbnails is
    // keyed by URL, so a rename alone (no content change) would otherwise leave the cached icon
    // orphaned under the old URL until the whole folder gets relisted.
    void renameThumbnail(const QUrl &oldUrl, const QUrl &newUrl);
    void clearThumbnails();

    // Marks `urls` as staged on the cut clipboard - matching rows report true for CutRole so a
    // delegate can dim them. Pass an empty set to clear the marking (clipboard emptied, replaced
    // by a copy, or replaced by a cut elsewhere).
    void setCutUrls(const QSet<QString> &urls);

private:
    QHash<QString, QIcon> m_thumbnails;
    QSet<QString> m_cutUrls;
};
