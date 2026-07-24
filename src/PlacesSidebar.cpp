#include "PlacesSidebar.h"

#include <QStandardPaths>
#include <QIcon>

PlacesSidebar::PlacesSidebar(QWidget *parent)
    : QListWidget(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(150);
    setIconSize(QSize(18, 18));
    setSpacing(2);
    setUniformItemSizes(true);

    addPlace(tr("Home"), QStringLiteral("user-home"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)));
    addPlace(tr("Documents"), QStringLiteral("folder-documents"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
    addPlace(tr("Downloads"), QStringLiteral("folder-download"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
    addPlace(tr("Pictures"), QStringLiteral("folder-pictures"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));
    addPlace(tr("Trash"), QStringLiteral("user-trash"), QUrl(QStringLiteral("trash:/")));

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        Q_EMIT placeActivated(item->data(Qt::UserRole).toUrl());
    });
}

void PlacesSidebar::addPlace(const QString &label, const QString &iconName, const QUrl &url)
{
    auto *item = new QListWidgetItem(QIcon::fromTheme(iconName), label, this);
    item->setData(Qt::UserRole, url);
    addItem(item);
}

void PlacesSidebar::setCurrentUrl(const QUrl &url)
{
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(Qt::UserRole).toUrl() == url) {
            setCurrentRow(i);
            return;
        }
    }
    clearSelection();
    setCurrentRow(-1);
}
