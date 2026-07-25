#include "PlacesSidebar.h"
#include "FileOperations.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFormLayout>
#include <QIcon>
#include <QInputDialog>
#include <QIntValidator>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int PinnedRole = Qt::UserRole + 1;
constexpr int UrlRole = Qt::UserRole;
constexpr int HeaderRole = Qt::UserRole + 2;
constexpr int HeaderNameRole = Qt::UserRole + 3;

const QString kDefaultSection = QStringLiteral("Bookmarks");
const QString kPlacesHeader = QStringLiteral("Places");
const QString kDevicesHeader = QStringLiteral("Devices");
const QString kNetworkHeader = QStringLiteral("Network");
const QString kSectionMimeType = QStringLiteral("application/x-minnow-sidebar-section");

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

bool isNetworkFileSystem(const QByteArray &fsType)
{
    static const QSet<QByteArray> networkFileSystems = {
        "nfs", "nfs4", "cifs", "smb3", "smbfs", "afpfs", "afs", "davfs", "davfs2",
        "fuse.sshfs", "fuse.rclone", "fuse.davfs2", "fuse.cifs", "fuse.smbnetfs", "9p",
    };
    return networkFileSystems.contains(fsType) || fsType.startsWith("fuse.sshfs") || fsType.startsWith("nfs");
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
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);

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

    // loadPinned() has to run first - loadSectionOrder() needs m_pinned populated to
    // detect and migrate a legacy custom section that collides with a fixed section name.
    loadPinned();
    loadSectionOrder();
    refreshDrives();

    // Picks up newly mounted network shares (NFS/SMB/etc. mounted outside the app, e.g.
    // via fstab or a manual `mount`) without the user having to right-click - Refresh
    // Drives. refreshDrives() itself no-ops when nothing actually changed.
    auto *driveRefreshTimer = new QTimer(this);
    driveRefreshTimer->setInterval(5000);
    connect(driveRefreshTimer, &QTimer::timeout, this, &PlacesSidebar::refreshDrives);
    driveRefreshTimer->start();

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
    item->setFlags((item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable) & ~Qt::ItemIsDragEnabled);
    item->setData(UrlRole, url);
    item->setData(PinnedRole, pinned);
    addItem(item);
    return item;
}

QListWidgetItem *PlacesSidebar::addHeaderItem(const QString &title, bool reorderable)
{
    auto *item = new QListWidgetItem(title.toUpper());
    item->setFlags(reorderable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled) : Qt::NoItemFlags);
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
    // Items are about to be destroyed by clear() - drop the dangling reference rather
    // than trying to un-highlight it first.
    m_dropHighlightItem = nullptr;
    clear();

    // Every section - fixed (Places/Devices/Network) or user-defined (Bookmarks/custom) -
    // lives in one ordered list now, so any of them can be moved relative to any other
    // (e.g. Devices above Places) via the context menu's Move Up/Down or drag-and-drop.
    for (const QString &section : std::as_const(m_sectionOrder)) {
        if (section == kPlacesHeader) {
            addHeaderItem(kPlacesHeader, /*reorderable=*/true);
            for (const auto &place : m_fixedPlaces) {
                if (isFixedPlaceVisible(place.settingsKey))
                    addPlaceItem(place.label, place.iconName, place.url, false);
            }
        } else if (section == kDevicesHeader) {
            if (m_drives.isEmpty())
                continue;
            addHeaderItem(kDevicesHeader, /*reorderable=*/true);
            for (const auto &drive : m_drives)
                addPlaceItem(drive.label, QStringLiteral("drive-harddisk"), drive.url, false);
        } else if (section == kNetworkHeader) {
            if (m_networkShares.isEmpty())
                continue;
            addHeaderItem(kNetworkHeader, /*reorderable=*/true);
            for (const auto &share : m_networkShares)
                addPlaceItem(share.label, QStringLiteral("network-server"), share.url, false);
        } else {
            const bool isCustom = section != kDefaultSection;
            QVector<PinnedEntry> entries;
            for (const auto &p : m_pinned)
                if (p.section == section)
                    entries << p;
            if (entries.isEmpty() && !isCustom)
                continue;
            addHeaderItem(section, /*reorderable=*/true);
            for (const auto &p : entries)
                addPlaceItem(p.name, QStringLiteral("folder"), p.url, true);
        }
    }
}

void PlacesSidebar::refreshDrives()
{
    QVector<DriveEntry> newDrives;
    QVector<DriveEntry> newNetworkShares;

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
        if (isNetworkFileSystem(info.fileSystemType()))
            newNetworkShares << DriveEntry{label, QUrl::fromLocalFile(rootPath)};
        else
            newDrives << DriveEntry{label, QUrl::fromLocalFile(rootPath)};
    }

    // Same set as before (the common case on every periodic re-check) - skip the
    // clear()+rebuild so the list doesn't flicker or drop scroll position/selection
    // every few seconds just to detect newly mounted network shares.
    if (newDrives == m_drives && newNetworkShares == m_networkShares)
        return;

    m_drives = newDrives;
    m_networkShares = newNetworkShares;
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
    // Places/Devices/Network aren't pin targets - only Bookmarks and custom sections are.
    QStringList result;
    for (const QString &name : m_sectionOrder) {
        if (name != kPlacesHeader && name != kDevicesHeader && name != kNetworkHeader)
            result << name;
    }
    return result;
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
        || name.compare(kNetworkHeader, Qt::CaseInsensitive) == 0 || name.compare(kDefaultSection, Qt::CaseInsensitive) == 0
        || m_sectionOrder.contains(name, Qt::CaseInsensitive))
        return;

    m_sectionOrder.append(name);
    saveSectionOrder();
    rebuildAll();
}

void PlacesSidebar::addNetworkFolder()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Network Folder"));

    auto *form = new QFormLayout;

    auto *protocolCombo = new QComboBox(&dialog);
    protocolCombo->addItems({QStringLiteral("sftp"), QStringLiteral("ftp"), QStringLiteral("smb"), QStringLiteral("nfs"), QStringLiteral("webdav"), QStringLiteral("webdavs")});
    form->addRow(tr("Protocol:"), protocolCombo);

    auto *hostEdit = new QLineEdit(&dialog);
    hostEdit->setPlaceholderText(tr("example.com"));
    form->addRow(tr("Host:"), hostEdit);

    auto *portEdit = new QLineEdit(&dialog);
    portEdit->setPlaceholderText(tr("(default)"));
    portEdit->setValidator(new QIntValidator(1, 65535, &dialog));
    form->addRow(tr("Port:"), portEdit);

    auto *userEdit = new QLineEdit(&dialog);
    userEdit->setPlaceholderText(tr("(optional)"));
    form->addRow(tr("Username:"), userEdit);

    auto *pathEdit = new QLineEdit(&dialog);
    pathEdit->setText(QStringLiteral("/"));
    form->addRow(tr("Path:"), pathEdit);

    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(tr("(uses the host name)"));
    form->addRow(tr("Sidebar name:"), nameEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString host = hostEdit->text().trimmed();
    if (host.isEmpty())
        return;

    QUrl url;
    url.setScheme(protocolCombo->currentText());
    url.setHost(host);
    if (!portEdit->text().isEmpty())
        url.setPort(portEdit->text().toInt());
    if (!userEdit->text().trimmed().isEmpty())
        url.setUserName(userEdit->text().trimmed());
    QString path = pathEdit->text().trimmed();
    if (path.isEmpty() || !path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    url.setPath(path);

    const QString name = nameEdit->text().trimmed().isEmpty() ? host : nameEdit->text().trimmed();
    m_pinned.append({name, url, kDefaultSection});
    savePinned();
    rebuildAll();
    Q_EMIT placeActivated(url);
}

void PlacesSidebar::deleteSection(const QString &name)
{
    if (name == kDefaultSection || name == kPlacesHeader || name == kDevicesHeader || name == kNetworkHeader)
        return;
    m_sectionOrder.removeAll(name);
    for (int i = m_pinned.size() - 1; i >= 0; --i) {
        if (m_pinned[i].section == name)
            m_pinned.removeAt(i);
    }
    saveSectionOrder();
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

void PlacesSidebar::loadSectionOrder()
{
    QSettings settings;
    // Same settings key as the older "custom sections only" list - a config saved before
    // every section became reorderable just won't have some/all of the fixed names yet.
    // Insert any missing ones at their original fixed position (Places first, Bookmarks
    // right after, Devices/Network last) rather than just appending, so an existing
    // config's visual order doesn't jump around on first load after this change.
    m_sectionOrder = settings.value(QStringLiteral("CustomSections")).toStringList();

    // "Network" only became a reserved fixed-section name in this version - a pre-existing
    // config could have a user-created custom section by that exact name. Without this,
    // its bookmarks would silently stop rendering (the position now takes the fixed
    // network-mounts branch in rebuildAll() instead of the generic custom-section one) and
    // the section could no longer be deleted. Rename the legacy one out of the way first.
    if (m_sectionOrder.contains(kNetworkHeader)) {
        bool hasLegacyPins = false;
        for (const auto &pinned : m_pinned) {
            if (pinned.section == kNetworkHeader) {
                hasLegacyPins = true;
                break;
            }
        }
        if (hasLegacyPins) {
            const QString migratedName = QStringLiteral("Network (Bookmarks)");
            m_sectionOrder[m_sectionOrder.indexOf(kNetworkHeader)] = migratedName;
            for (auto &pinned : m_pinned) {
                if (pinned.section == kNetworkHeader)
                    pinned.section = migratedName;
            }
            savePinned();
        }
    }

    if (!m_sectionOrder.contains(kPlacesHeader))
        m_sectionOrder.prepend(kPlacesHeader);
    if (!m_sectionOrder.contains(kDefaultSection))
        m_sectionOrder.insert(m_sectionOrder.indexOf(kPlacesHeader) + 1, kDefaultSection);
    if (!m_sectionOrder.contains(kDevicesHeader))
        m_sectionOrder.append(kDevicesHeader);
    if (!m_sectionOrder.contains(kNetworkHeader))
        m_sectionOrder.append(kNetworkHeader);
}

void PlacesSidebar::saveSectionOrder()
{
    QSettings settings;
    settings.setValue(QStringLiteral("CustomSections"), m_sectionOrder);
}

bool PlacesSidebar::isReorderableSection(const QString &name) const
{
    return m_sectionOrder.contains(name);
}

bool PlacesSidebar::sectionIsVisible(const QString &section) const
{
    if (section == kPlacesHeader)
        return true;
    if (section == kDevicesHeader)
        return !m_drives.isEmpty();
    if (section == kNetworkHeader)
        return !m_networkShares.isEmpty();

    const bool isCustom = section != kDefaultSection;
    if (isCustom)
        return true;
    for (const auto &p : m_pinned)
        if (p.section == section)
            return true;
    return false;
}

int PlacesSidebar::nextVisibleSectionIndex(int fromIdx, int direction) const
{
    int idx = fromIdx;
    while (true) {
        idx += direction;
        if (idx < 0 || idx >= m_sectionOrder.size())
            return -1;
        if (sectionIsVisible(m_sectionOrder.at(idx)))
            return idx;
    }
}

void PlacesSidebar::moveSection(const QString &name, int direction)
{
    // Swap with the next *visible* neighbor, not just the next list entry - otherwise
    // "Move Up" past a hidden/empty section (e.g. an unused Bookmarks) would silently
    // do nothing the user can see.
    const int idx = m_sectionOrder.indexOf(name);
    if (idx < 0)
        return;
    const int targetIdx = nextVisibleSectionIndex(idx, direction);
    if (targetIdx < 0)
        return;
    m_sectionOrder.swapItemsAt(idx, targetIdx);
    saveSectionOrder();
    rebuildAll();
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
        const int idx = m_sectionOrder.indexOf(name);

        QAction *moveUpAction = menu.addAction(QIcon::fromTheme(QStringLiteral("go-up")), tr("Move Section Up"));
        moveUpAction->setEnabled(idx >= 0 && nextVisibleSectionIndex(idx, -1) >= 0);
        connect(moveUpAction, &QAction::triggered, this, [this, name] { moveSection(name, -1); });

        QAction *moveDownAction = menu.addAction(QIcon::fromTheme(QStringLiteral("go-down")), tr("Move Section Down"));
        moveDownAction->setEnabled(idx >= 0 && nextVisibleSectionIndex(idx, 1) >= 0);
        connect(moveDownAction, &QAction::triggered, this, [this, name] { moveSection(name, 1); });

        menu.addSeparator();

        const bool isFixedSection = name == kDefaultSection || name == kPlacesHeader || name == kDevicesHeader || name == kNetworkHeader;
        if (!isFixedSection) {
            QAction *deleteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete Section"));
            connect(deleteAction, &QAction::triggered, this, [this, name] { deleteSection(name); });
            menu.addSeparator();
        }
    }

    QAction *newSectionAction = menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), tr("Create New Section…"));
    connect(newSectionAction, &QAction::triggered, this, &PlacesSidebar::createSection);

    QAction *networkAction = menu.addAction(QIcon::fromTheme(QStringLiteral("network-server")), tr("Add Network Folder…"));
    connect(networkAction, &QAction::triggered, this, &PlacesSidebar::addNetworkFolder);

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

QMimeData *PlacesSidebar::mimeData(const QList<QListWidgetItem *> &items) const
{
    if (items.size() != 1)
        return nullptr;
    QListWidgetItem *item = items.first();
    if (!item->data(HeaderRole).toBool())
        return nullptr;
    const QString name = item->data(HeaderNameRole).toString();
    if (!isReorderableSection(name))
        return nullptr;

    auto *mime = new QMimeData();
    mime->setData(kSectionMimeType, name.toUtf8());
    return mime;
}

void PlacesSidebar::setSectionDropHighlight(QListWidgetItem *item)
{
    if (m_dropHighlightItem == item)
        return;
    if (m_dropHighlightItem)
        m_dropHighlightItem->setBackground(Qt::NoBrush);
    m_dropHighlightItem = item;
    if (m_dropHighlightItem) {
        QColor highlight = palette().color(QPalette::Highlight);
        highlight.setAlphaF(0.25f);
        m_dropHighlightItem->setBackground(highlight);
    }
}

void PlacesSidebar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(kSectionMimeType) || event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void PlacesSidebar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(kSectionMimeType)) {
        const QString draggedSection = QString::fromUtf8(event->mimeData()->data(kSectionMimeType));
        QListWidgetItem *hovered = itemAt(event->position().toPoint());
        const QString hoveredName = hovered ? hovered->data(HeaderNameRole).toString() : QString();
        if (hovered && hovered->data(HeaderRole).toBool() && isReorderableSection(hoveredName) && hoveredName != draggedSection) {
            setSectionDropHighlight(hovered);
            event->acceptProposedAction();
        } else {
            setSectionDropHighlight(nullptr);
            event->ignore();
        }
        return;
    }

    setSectionDropHighlight(nullptr);
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    QListWidgetItem *hovered = itemAt(event->position().toPoint());
    const QUrl destUrl = hovered ? hovered->data(UrlRole).toUrl() : QUrl();
    if (hovered && !hovered->data(HeaderRole).toBool() && destUrl.isValid())
        event->acceptProposedAction();
    else
        event->ignore();
}

void PlacesSidebar::dragLeaveEvent(QDragLeaveEvent *event)
{
    setSectionDropHighlight(nullptr);
    QListWidget::dragLeaveEvent(event);
}

void PlacesSidebar::dropEvent(QDropEvent *event)
{
    setSectionDropHighlight(nullptr);

    if (event->mimeData()->hasFormat(kSectionMimeType)) {
        const QString draggedSection = QString::fromUtf8(event->mimeData()->data(kSectionMimeType));
        QListWidgetItem *hovered = itemAt(event->position().toPoint());
        const QString targetSection = hovered ? hovered->data(HeaderNameRole).toString() : QString();
        if (!hovered || !hovered->data(HeaderRole).toBool() || !isReorderableSection(targetSection)
            || targetSection == draggedSection) {
            event->ignore();
            return;
        }

        const int fromIdx = m_sectionOrder.indexOf(draggedSection);
        const int targetIdx = m_sectionOrder.indexOf(targetSection);
        if (fromIdx < 0 || targetIdx < 0) {
            event->ignore();
            return;
        }

        // Which half of the target row was dropped on decides before-vs-after - otherwise
        // there'd be no way to move a section past its immediate neighbor (dropping "before"
        // an already-adjacent item is a no-op).
        const bool dropBelow = event->position().toPoint().y() > visualItemRect(hovered).center().y();
        int insertIdx = dropBelow ? targetIdx + 1 : targetIdx;
        m_sectionOrder.removeAt(fromIdx);
        if (fromIdx < insertIdx)
            --insertIdx;
        insertIdx = qBound(0, insertIdx, m_sectionOrder.size());

        m_sectionOrder.insert(insertIdx, draggedSection);
        saveSectionOrder();
        rebuildAll();
        event->acceptProposedAction();
        return;
    }

    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    QListWidgetItem *hovered = itemAt(event->position().toPoint());
    const QUrl destDir = hovered ? hovered->data(UrlRole).toUrl() : QUrl();
    if (!hovered || hovered->data(HeaderRole).toBool() || !destDir.isValid()) {
        event->ignore();
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    if (event->proposedAction() == Qt::MoveAction)
        FileOperations::moveTo(urls, destDir, this);
    else
        FileOperations::copyTo(urls, destDir, this);
    event->acceptProposedAction();
}
