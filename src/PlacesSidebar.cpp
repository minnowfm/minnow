#include "PlacesSidebar.h"

#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>

namespace
{
constexpr int PinnedRole = Qt::UserRole + 1;
constexpr int UrlRole = Qt::UserRole;
}

PlacesSidebar::PlacesSidebar(QWidget *parent)
    : QListWidget(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(150);
    setIconSize(QSize(18, 18));
    setSpacing(2);
    setUniformItemSizes(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    addPlace(tr("Home"), QStringLiteral("user-home"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)));
    addPlace(tr("Documents"), QStringLiteral("folder-documents"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
    addPlace(tr("Downloads"), QStringLiteral("folder-download"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
    addPlace(tr("Pictures"), QStringLiteral("folder-pictures"),
             QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));
    addPlace(tr("Trash"), QStringLiteral("user-trash"), QUrl(QStringLiteral("trash:/")));

    loadPinned();

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QUrl url = item->data(UrlRole).toUrl();
        if (url.isValid())
            Q_EMIT placeActivated(url);
    });
    connect(this, &QListWidget::customContextMenuRequested, this, &PlacesSidebar::showSidebarContextMenu);
}

void PlacesSidebar::addPlace(const QString &label, const QString &iconName, const QUrl &url, bool pinned)
{
    auto *item = new QListWidgetItem(QIcon::fromTheme(iconName), label);
    item->setData(UrlRole, url);
    item->setData(PinnedRole, pinned);
    if (pinned && m_separator)
        insertItem(row(m_separator) + 1, item);
    else
        addItem(item);
}

void PlacesSidebar::ensureSeparator()
{
    if (m_separator)
        return;
    m_separator = new QListWidgetItem();
    m_separator->setFlags(Qt::NoItemFlags);
    m_separator->setSizeHint(QSize(1, 9));
    addItem(m_separator);
}

bool PlacesSidebar::isPinned(const QUrl &url) const
{
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(UrlRole).toUrl() == url)
            return item(i)->data(PinnedRole).toBool();
    }
    return false;
}

void PlacesSidebar::pinPlace(const QUrl &url)
{
    if (isPinned(url))
        return;

    ensureSeparator();
    const QString name = url.fileName().isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : url.fileName();
    addPlace(name, QStringLiteral("folder"), url, true);
    savePinned();
}

void PlacesSidebar::loadPinned()
{
    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("PinnedPlaces"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QUrl url = settings.value(QStringLiteral("url")).toUrl();
        const QString name = settings.value(QStringLiteral("name")).toString();
        if (url.isValid()) {
            ensureSeparator();
            addPlace(name, QStringLiteral("folder"), url, true);
        }
    }
    settings.endArray();
}

void PlacesSidebar::savePinned()
{
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("PinnedPlaces"));
    int idx = 0;
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem *it = item(i);
        if (!it->data(PinnedRole).toBool())
            continue;
        settings.setArrayIndex(idx++);
        settings.setValue(QStringLiteral("url"), it->data(UrlRole).toUrl());
        settings.setValue(QStringLiteral("name"), it->text());
    }
    settings.endArray();
}

void PlacesSidebar::showSidebarContextMenu(const QPoint &pos)
{
    QListWidgetItem *it = itemAt(pos);
    if (!it || !it->data(PinnedRole).toBool())
        return;

    QMenu menu(this);
    QAction *removeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove from Sidebar"));
    connect(removeAction, &QAction::triggered, this, [this, it] {
        delete it;
        bool anyPinned = false;
        for (int i = 0; i < count(); ++i) {
            if (item(i)->data(PinnedRole).toBool()) {
                anyPinned = true;
                break;
            }
        }
        if (!anyPinned && m_separator) {
            delete m_separator;
            m_separator = nullptr;
        }
        savePinned();
    });
    menu.exec(viewport()->mapToGlobal(pos));
}

void PlacesSidebar::setCurrentUrl(const QUrl &url)
{
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(UrlRole).toUrl() == url) {
            setCurrentRow(i);
            return;
        }
    }
    clearSelection();
    setCurrentRow(-1);
}
