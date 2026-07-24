#pragma once

#include <QListWidget>
#include <QUrl>

class PlacesSidebar : public QListWidget
{
    Q_OBJECT

public:
    explicit PlacesSidebar(QWidget *parent = nullptr);

    void setCurrentUrl(const QUrl &url);

signals:
    void placeActivated(const QUrl &url);

private:
    void addPlace(const QString &label, const QString &iconName, const QUrl &url);
};
