#pragma once

#include <QListWidget>
#include <QUrl>
#include <QVector>

class PlacesSidebar : public QListWidget
{
    Q_OBJECT

public:
    explicit PlacesSidebar(QWidget *parent = nullptr);

    void setCurrentUrl(const QUrl &url);
    void pinPlace(const QUrl &url, const QString &section = QStringLiteral("Bookmarks"));
    bool isPinned(const QUrl &url) const;
    QStringList availableSections() const;

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

    QListWidgetItem *addPlaceItem(const QString &label, const QString &iconName, const QUrl &url, bool pinned);
    QListWidgetItem *addHeaderItem(const QString &title, bool reorderable = false);
    void rebuildAll();
    void refreshDrives();
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

    struct DriveEntry {
        QString label;
        QUrl url;
        bool operator==(const DriveEntry &other) const { return label == other.label && url == other.url; }
    };

    QVector<FixedPlace> m_fixedPlaces;
    QVector<PinnedEntry> m_pinned;
    // Every top-level section - Places, Devices, Network, Bookmarks, and any custom
    // sections - in display order. All of them are reorderable.
    QStringList m_sectionOrder;
    QVector<DriveEntry> m_drives;
    QVector<DriveEntry> m_networkShares;
    QListWidgetItem *m_dropHighlightItem = nullptr;
};
