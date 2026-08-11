#pragma once

#include "DiskUsageIndicator.h"

#include <QListWidget>
#include <QUrl>
#include <QVector>

template<typename T>
class QFutureWatcher;

class PlacesSidebar : public QListWidget
{
    Q_OBJECT

public:
    explicit PlacesSidebar(QWidget *parent = nullptr);

    void setCurrentUrl(const QUrl &url);
    void pinPlace(const QUrl &url, const QString &section = QStringLiteral("Bookmarks"));
    bool isPinned(const QUrl &url) const;
    QStringList availableSections() const;
    void setDriveDiskUsageEnabled(bool enabled);
    void setDriveDiskUsageStyle(DiskUsageIndicator::Style style);

signals:
    void placeActivated(const QUrl &url);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;

private:
    struct FixedPlace {
        QString label;
        QString iconName;
        QUrl url;
        QString settingsKey;
    };

    struct PinnedEntry {
        QString name;
        QUrl url;
        QString section;
    };

    // bytesTotal/bytesAvailable are baked in by the background scan (scanVolumes()) so rebuildAll()
    // never has to touch QStorageInfo itself - a stat() on an unreachable network share can block for
    // a long time and would otherwise stall the whole UI thread.
    struct DriveEntry {
        QString label;
        QUrl url;
        qint64 bytesTotal = 0;
        qint64 bytesAvailable = 0;
        // deliberately ignores exact byte counts - only the rounded percent-used "bucket" - so the
        // 5s refresh timer doesn't rebuild (and lose scroll/selection) over a few bytes of drift
        bool operator==(const DriveEntry &other) const;
    };

    struct ScanResult {
        QVector<DriveEntry> drives;
        QVector<DriveEntry> networkShares;
        qint64 homeBytesTotal = 0;
        qint64 homeBytesAvailable = 0;
        bool homeOnSeparateVolume = false;
    };

    // A removable volume UDisks2 knows about but that isn't mounted anywhere yet (plugged in but
    // no auto-mount daemon picked it up) - clicking it calls Filesystem.Mount() instead of navigating.
    struct UnmountedDrive {
        QString label;
        QString blockObjectPath; // org.freedesktop.UDisks2 object path, e.g. /org/freedesktop/UDisks2/block_devices/sda1
        bool operator==(const UnmountedDrive &other) const { return label == other.label && blockObjectPath == other.blockObjectPath; }
    };

    QListWidgetItem *addPlaceItem(const QString &label, const QString &iconName, const QUrl &url, bool pinned);
    QListWidgetItem *addHeaderItem(const QString &title, bool reorderable = false);
    // Overwrites the item's label (Text style) or attaches usage data for the delegate to paint
    // an inline bar (Bar style). No-ops if disk usage display is off or bytesTotal is unknown.
    void applyUsageDisplay(QListWidgetItem *item, const QString &baseLabel, qint64 bytesTotal, qint64 bytesAvailable);
    void rebuildAll();
    void refreshDrives();
    void applyScanResult(const ScanResult &result);
    static ScanResult scanVolumes();
    static QVector<UnmountedDrive> scanUnmountedDrives();
    void mountAndNavigate(const QString &blockObjectPath);
    bool isFixedPlaceVisible(const QString &settingsKey) const;
    void setFixedPlaceVisible(const QString &settingsKey, bool visible);
    void createSection();
    void deleteSection(const QString &name);
    void addNetworkFolder();
    void loadPinned();
    void savePinned();
    void loadSectionOrder();
    void saveSectionOrder();
    bool isReorderableSection(const QString &name) const;
    bool sectionIsVisible(const QString &name) const;
    int nextVisibleSectionIndex(int fromIdx, int direction) const;
    void moveSection(const QString &name, int direction);
    void setSectionDropHighlight(QListWidgetItem *item);
    void showSidebarContextMenu(const QPoint &pos);

    QVector<FixedPlace> m_fixedPlaces;
    QVector<PinnedEntry> m_pinned;
    QStringList m_sectionOrder; // display order for all sections (fixed + custom), all reorderable
    QVector<DriveEntry> m_drives;
    QVector<DriveEntry> m_networkShares;
    bool m_drivesInitialized = false;
    QListWidgetItem *m_dropHighlightItem = nullptr;
    bool m_diskUsageEnabled = false;
    DiskUsageIndicator::Style m_diskUsageStyle = DiskUsageIndicator::Style::Text;
    qint64 m_homeBytesTotal = 0;
    qint64 m_homeBytesAvailable = 0;
    bool m_homeOnSeparateVolume = false;
    QFutureWatcher<ScanResult> *m_scanWatcher = nullptr;
    QVector<UnmountedDrive> m_unmountedDrives;
};
