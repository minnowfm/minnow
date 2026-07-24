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
    void pinPlace(const QUrl &url);
    bool isPinned(const QUrl &url) const;

signals:
    void placeActivated(const QUrl &url);

private:
    struct FixedPlace {
        QString label;
        QString iconName;
        QUrl url;
        QString settingsKey;
    };

    void addPlace(const QString &label, const QString &iconName, const QUrl &url, bool pinned = false);
    QListWidgetItem *addHeaderItem(const QString &title);
    void ensureBookmarksHeader();
    void ensureDevicesHeader();
    void loadPinned();
    void savePinned();
    void rebuildFixedPlaces();
    void refreshDrives();
    bool isFixedPlaceVisible(const QString &settingsKey) const;
    void setFixedPlaceVisible(const QString &settingsKey, bool visible);
    void showSidebarContextMenu(const QPoint &pos);

    QVector<FixedPlace> m_fixedPlaces;
    QListWidgetItem *m_placesHeader = nullptr;
    QListWidgetItem *m_bookmarksHeader = nullptr;
    QListWidgetItem *m_devicesHeader = nullptr;
};
