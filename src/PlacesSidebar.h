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
    QListWidgetItem *addHeaderItem(const QString &title);
    void rebuildAll();
    void refreshDrives();
    bool isFixedPlaceVisible(const QString &settingsKey) const;
    void setFixedPlaceVisible(const QString &settingsKey, bool visible);
    void createSection();
    void deleteSection(const QString &name);
    void loadPinned();
    void savePinned();
    void loadCustomSections();
    void saveCustomSections();
    void showSidebarContextMenu(const QPoint &pos);

    struct DriveEntry {
        QString label;
        QUrl url;
    };

    QVector<FixedPlace> m_fixedPlaces;
    QVector<PinnedEntry> m_pinned;
    QStringList m_customSections;
    QVector<DriveEntry> m_drives;
};
