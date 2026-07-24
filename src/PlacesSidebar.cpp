#include "PlacesSidebar.h"

#include <QDir>
#include <QFont>
#include <QIcon>
#include <QMenu>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
constexpr int PinnedRole = Qt::UserRole + 1;
constexpr int UrlRole = Qt::UserRole;
constexpr int DriveRole = Qt::UserRole + 2;

bool isRealVolume(const QStorageInfo &info)
{
    static const QSet<QByteArray> pseudoFileSystems = {
        "tmpfs",   "devtmpfs",     "proc",    "sysfs",     "cgroup", "cgroup2",     "overlay",
        "squashfs", "devpts",      "debugfs", "tracefs",   "pstore", "bpf",         "mqueue",
        "hugetlbfs", "securityfs", "autofs",  "binfmt_misc", "configfs", "fusectl", "efivarfs",
    };
    if (!info.isValid() || !info.isReady())
        return false;
    if (pseudoFileSystems.contains(info.fileSystemType()))
        return false;
    return true;
}
}

PlacesSidebar::PlacesSidebar(QWidget *parent)
    : QListWidget(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(150);
    setIconSize(QSize(18, 18));
    setSpacing(2);
    setUniformItemSizes(false);
    setContextMenuPolicy(Qt::CustomContextMenu);

    m_fixedPlaces = {
        {tr("Home"), QStringLiteral("user-home"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)), QStringLiteral("Home")},
        {tr("Documents"), QStringLiteral("folder-documents"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)), QStringLiteral("Documents")},
        {tr("Downloads"), QStringLiteral("folder-download"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)), QStringLiteral("Downloads")},
        {tr("Pictures"), QStringLiteral("folder-pictures"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)), QStringLiteral("Pictures")},
        {tr("Music"), QStringLiteral("folder-music"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)), QStringLiteral("Music")},
        {tr("Videos"), QStringLiteral("folder-videos"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)), QStringLiteral("Videos")},
        {tr("Trash"), QStringLiteral("user-trash"), QUrl(QStringLiteral("trash:/")), QStringLiteral("Trash")},
    };

    m_placesHeader = addHeaderItem(tr("Places"));
    rebuildFixedPlaces();
    loadPinned();
    refreshDrives();

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
    if (pinned && m_bookmarksHeader)
        insertItem(row(m_bookmarksHeader) + 1, item);
    else
        addItem(item);
}

QListWidgetItem *PlacesSidebar::addHeaderItem(const QString &title)
{
    auto *item = new QListWidgetItem(title.toUpper());
    item->setFlags(Qt::NoItemFlags);
    QFont headerFont = font();
    headerFont.setPointSizeF(headerFont.pointSizeF() * 0.82);
    headerFont.setBold(true);
    item->setFont(headerFont);
    QColor textColor = palette().color(QPalette::WindowText);
    textColor.setAlpha(120);
    item->setForeground(textColor);
    addItem(item);
    return item;
}

void PlacesSidebar::ensureBookmarksHeader()
{
    if (m_bookmarksHeader)
        return;
    m_bookmarksHeader = addHeaderItem(tr("Bookmarks"));
}

void PlacesSidebar::ensureDevicesHeader()
{
    if (m_devicesHeader)
        return;
    m_devicesHeader = addHeaderItem(tr("Devices"));
}

bool PlacesSidebar::isFixedPlaceVisible(const QString &settingsKey) const
{
    QSettings settings;
    return settings.value(QStringLiteral("Places/%1").arg(settingsKey), true).toBool();
}

void PlacesSidebar::setFixedPlaceVisible(const QString &settingsKey, bool visible)
{
    QSettings settings;
    settings.setValue(QStringLiteral("Places/%1").arg(settingsKey), visible);
    rebuildFixedPlaces();
}

void PlacesSidebar::rebuildFixedPlaces()
{
    for (int i = count() - 1; i >= 0; --i) {
        QListWidgetItem *it = item(i);
        if (it == m_placesHeader || it == m_bookmarksHeader || it == m_devicesHeader)
            continue;
        if (it->data(PinnedRole).toBool() || it->data(DriveRole).toBool())
            continue;
        delete it;
    }

    int insertPos = row(m_placesHeader) + 1;
    for (const auto &place : m_fixedPlaces) {
        if (!isFixedPlaceVisible(place.settingsKey))
            continue;
        auto *item = new QListWidgetItem(QIcon::fromTheme(place.iconName), place.label);
        item->setData(UrlRole, place.url);
        item->setData(PinnedRole, false);
        insertItem(insertPos++, item);
    }
}

void PlacesSidebar::refreshDrives()
{
    for (int i = count() - 1; i >= 0; --i) {
        QListWidgetItem *it = item(i);
        if (it->data(DriveRole).toBool())
            delete it;
    }
    if (m_devicesHeader) {
        delete m_devicesHeader;
        m_devicesHeader = nullptr;
    }

    const QString homePath = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).canonicalPath();
    static const QSet<QString> excludedMountPoints = {
        QStringLiteral("/boot"),
        QStringLiteral("/boot/efi"),
    };

    QVector<QStorageInfo> drives;
    for (const QStorageInfo &info : QStorageInfo::mountedVolumes()) {
        if (!isRealVolume(info))
            continue;
        const QString rootPath = info.rootPath();
        if (rootPath == QStringLiteral("/"))
            continue;
        if (excludedMountPoints.contains(rootPath))
            continue;
        const QString rootWithSlash = rootPath.endsWith(QLatin1Char('/')) ? rootPath : rootPath + QLatin1Char('/');
        if (homePath == rootPath || homePath.startsWith(rootWithSlash))
            continue;
        drives << info;
    }

    if (drives.isEmpty())
        return;

    ensureDevicesHeader();
    for (const QStorageInfo &info : drives) {
        QString label = info.name();
        if (label.isEmpty())
            label = QDir(info.rootPath()).dirName();
        if (label.isEmpty())
            label = info.rootPath();

        auto *item = new QListWidgetItem(QIcon::fromTheme(QStringLiteral("drive-harddisk")), label);
        item->setData(UrlRole, QUrl::fromLocalFile(info.rootPath()));
        item->setData(PinnedRole, false);
        item->setData(DriveRole, true);
        addItem(item);
    }
}

bool PlacesSidebar::isPinned(const QUrl &url) const
{
    for (int i = 0; i < count(); ++i) {
        if (item(i)->data(UrlRole).toUrl() == url)
            return true;
    }
    return false;
}

void PlacesSidebar::pinPlace(const QUrl &url)
{
    if (isPinned(url))
        return;

    ensureBookmarksHeader();
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
        if (url.isValid() && !isPinned(url)) {
            ensureBookmarksHeader();
            addPlace(name, QStringLiteral("folder"), url, true);
        }
    }
    settings.endArray();
}

void PlacesSidebar::savePinned()
{
    QSettings settings;
    settings.remove(QStringLiteral("PinnedPlaces"));
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
    QMenu menu(this);
    QListWidgetItem *it = itemAt(pos);

    if (it && it->data(PinnedRole).toBool()) {
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
            if (!anyPinned && m_bookmarksHeader) {
                delete m_bookmarksHeader;
                m_bookmarksHeader = nullptr;
            }
            savePinned();
        });
        menu.addSeparator();
    }

    QAction *refreshAction = menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Refresh Drives"));
    connect(refreshAction, &QAction::triggered, this, &PlacesSidebar::refreshDrives);

    QMenu *placesMenu = menu.addMenu(tr("Show in Sidebar"));
    for (const auto &place : m_fixedPlaces) {
        QAction *act = placesMenu->addAction(place.label);
        act->setCheckable(true);
        act->setChecked(isFixedPlaceVisible(place.settingsKey));
        const QString key = place.settingsKey;
        connect(act, &QAction::triggered, this, [this, key](bool checked) {
            setFixedPlaceVisible(key, checked);
        });
    }

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
