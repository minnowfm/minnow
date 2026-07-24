#pragma once

#include <QListWidget>
#include <QUrl>

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
    void addPlace(const QString &label, const QString &iconName, const QUrl &url, bool pinned = false);
    void ensureSeparator();
    void loadPinned();
    void savePinned();
    void showSidebarContextMenu(const QPoint &pos);

    QListWidgetItem *m_separator = nullptr;
};
