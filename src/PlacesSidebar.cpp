#include "PlacesSidebar.h"

#include <QDir>
#include <QFont>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
constexpr int PinnedRole = Qt::UserRole + 1;
constexpr int UrlRole = Qt::UserRole;
constexpr int HeaderRole = Qt::UserRole + 2;
constexpr int HeaderNameRole = Qt::UserRole + 3;

const QString kDefaultSection = QStringLiteral("Bookmarks");
const QString kPlacesHeader = QStringLiteral("Places");
const QString kDevicesHeader = QStringLiteral("Devices");

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

    loadCustomSections();
    loadPinned();
    refreshDrives();

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QUrl url = item->data(UrlRole).toUrl();
        if (url.isValid())
            Q_EMIT placeActivated(url);
    });
    connect(this, &QListWidget::customContextMenuRequested, this, &PlacesSidebar::showSidebarContextMenu);
}

QListWidgetItem *PlacesSidebar::addPlaceItem(const QString &label, const QString &iconName, const QUrl &url, bool pinned)
{
    auto *item = new QListWidgetItem(QIcon::fromTheme(iconName), label);
    item->setData(UrlRole, url);
    item->setData(PinnedRole, pinned);
    addItem(item);
    return item;
}

QListWidgetItem *PlacesSidebar::addHeaderItem(const QString &title)
{
    auto *item = new QListWidgetItem(title.toUpper());
    item->setFlags(Qt::NoItemFlags);
    item->setData(HeaderRole, true);
    item->setData(HeaderNameRole, title);
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

bool PlacesSidebar::isFixedPlaceVisible(const QString &settingsKey) const
{
    QSettings settings;
    return settings.value(QStringLiteral("Places/%1").arg(settingsKey), true).toBool();
}

void PlacesSidebar::setFixedPlaceVisible(const QString &settingsKey, bool visible)
{
    QSettings settings;
    settings.setValue(QStringLiteral("Places/%1").arg(settingsKey), visible);
    rebuildAll();
}

void PlacesSidebar::rebuildAll()
{
    clear();

    addHeaderItem(kPlacesHeader);
    for (const auto &place : m_fixedPlaces) {
        if (isFixedPlaceVisible(place.settingsKey))
            addPlaceItem(place.label, place.iconName, place.url, false);
    }

    const QStringList sections = QStringList{kDefaultSection} + m_customSections;
    for (const QString &section : sections) {
        const bool isCustom = section != kDefaultSection;
        QVector<PinnedEntry> entries;
        for (const auto &p : m_pinned)
            if (p.section == section)
                entries << p;
        if (entries.isEmpty() && !isCustom)
            continue;
        addHeaderItem(section);
        for (const auto &p : entries)
            addPlaceItem(p.name, QStringLiteral("folder"), p.url, true);
    }

    if (!m_drives.isEmpty()) {
        addHeaderItem(kDevicesHeader);
        for (const auto &drive : m_drives)
            addPlaceItem(drive.label, QStringLiteral("drive-harddisk"), drive.url, false);
    }
}

void PlacesSidebar::refreshDrives()
{
    m_drives.clear();

    const QString homePath = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).canonicalPath();
    static const QSet<QString> excludedMountPoints = {
        QStringLiteral("/boot"),
        QStringLiteral("/boot/efi"),
    };
    for (const QStorageInfo &info : QStorageInfo::mountedVolumes()) {
        if (!isRealVolume(info))
            continue;
        const QString rootPath = info.rootPath();
        if (rootPath == QStringLiteral("/") || excludedMountPoints.contains(rootPath))
            continue;
        const QString rootWithSlash = rootPath.endsWith(QLatin1Char('/')) ? rootPath : rootPath + QLatin1Char('/');
        if (homePath == rootPath || homePath.startsWith(rootWithSlash))
            continue;
        QString label = info.name();
        if (label.isEmpty())
            label = QDir(rootPath).dirName();
        if (label.isEmpty())
            label = rootPath;
        m_drives << DriveEntry{label, QUrl::fromLocalFile(rootPath)};
    }

    rebuildAll();
}

bool PlacesSidebar::isPinned(const QUrl &url) const
{
    for (const auto &p : m_pinned)
        if (p.url == url)
            return true;
    for (const auto &place : m_fixedPlaces)
        if (place.url == url)
            return true;
    return false;
}

QStringList PlacesSidebar::availableSections() const
{
    return QStringList{kDefaultSection} + m_customSections;
}

void PlacesSidebar::pinPlace(const QUrl &url, const QString &section)
{
    if (isPinned(url))
        return;

    const QString name = url.fileName().isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : url.fileName();
    m_pinned.append({name, url, section});
    savePinned();
    rebuildAll();
}

void PlacesSidebar::createSection()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Section"), tr("Section name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.compare(kPlacesHeader, Qt::CaseInsensitive) == 0 || name.compare(kDevicesHeader, Qt::CaseInsensitive) == 0
        || name.compare(kDefaultSection, Qt::CaseInsensitive) == 0 || m_customSections.contains(name, Qt::CaseInsensitive))
        return;

    m_customSections.append(name);
    saveCustomSections();
    rebuildAll();
}

void PlacesSidebar::deleteSection(const QString &name)
{
    m_customSections.removeAll(name);
    for (int i = m_pinned.size() - 1; i >= 0; --i) {
        if (m_pinned[i].section == name)
            m_pinned.removeAt(i);
    }
    saveCustomSections();
    savePinned();
    rebuildAll();
}

void PlacesSidebar::loadPinned()
{
    m_pinned.clear();
    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("PinnedPlaces"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QUrl url = settings.value(QStringLiteral("url")).toUrl();
        const QString name = settings.value(QStringLiteral("name")).toString();
        const QString section = settings.value(QStringLiteral("section"), kDefaultSection).toString();
        if (url.isValid())
            m_pinned.append({name, url, section});
    }
    settings.endArray();
}

void PlacesSidebar::savePinned()
{
    QSettings settings;
    settings.remove(QStringLiteral("PinnedPlaces"));
    settings.beginWriteArray(QStringLiteral("PinnedPlaces"));
    for (int i = 0; i < m_pinned.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("url"), m_pinned[i].url);
        settings.setValue(QStringLiteral("name"), m_pinned[i].name);
        settings.setValue(QStringLiteral("section"), m_pinned[i].section);
    }
    settings.endArray();
}

void PlacesSidebar::loadCustomSections()
{
    QSettings settings;
    m_customSections = settings.value(QStringLiteral("CustomSections")).toStringList();
}

void PlacesSidebar::saveCustomSections()
{
    QSettings settings;
    settings.setValue(QStringLiteral("CustomSections"), m_customSections);
}

void PlacesSidebar::showSidebarContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QListWidgetItem *it = itemAt(pos);

    if (it && it->data(PinnedRole).toBool()) {
        const QUrl url = it->data(UrlRole).toUrl();
        QAction *removeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove from Sidebar"));
        connect(removeAction, &QAction::triggered, this, [this, url] {
            for (int i = 0; i < m_pinned.size(); ++i) {
                if (m_pinned[i].url == url) {
                    m_pinned.removeAt(i);
                    break;
                }
            }
            savePinned();
            rebuildAll();
        });
        menu.addSeparator();
    } else if (it && it->data(HeaderRole).toBool()) {
        const QString name = it->data(HeaderNameRole).toString();
        if (m_customSections.contains(name)) {
            QAction *deleteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete Section"));
            connect(deleteAction, &QAction::triggered, this, [this, name] { deleteSection(name); });
            menu.addSeparator();
        }
    }

    QAction *newSectionAction = menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), tr("Create New Section…"));
    connect(newSectionAction, &QAction::triggered, this, &PlacesSidebar::createSection);

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
