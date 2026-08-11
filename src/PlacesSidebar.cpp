#include "PlacesSidebar.h"
#include "FileOperations.h"
#include "PathUtils.h"

#include <KIO/Global>

#include <QComboBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QIcon>
#include <QInputDialog>
#include <QIntValidator>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace
{
constexpr int PinnedRole = Qt::UserRole + 1;
constexpr int UrlRole = Qt::UserRole;
constexpr int HeaderRole = Qt::UserRole + 2;
constexpr int HeaderNameRole = Qt::UserRole + 3;
constexpr int DiskUsagePercentRole = Qt::UserRole + 4; // set only for Bar style - paints the inline meter
constexpr int UnmountedObjectPathRole = Qt::UserRole + 5; // set only on not-yet-mounted drive rows

const QString kDefaultSection = QStringLiteral("Bookmarks");
const QString kPlacesHeader = QStringLiteral("Places");
const QString kDevicesHeader = QStringLiteral("Devices");
const QString kNetworkHeader = QStringLiteral("Network");
const QString kSectionMimeType = QStringLiteral("application/x-minnow-sidebar-section");
const QString kHomeSettingsKey = QStringLiteral("Home");

int usageBucket(qint64 bytesTotal, qint64 bytesAvailable)
{
    return bytesTotal > 0 ? static_cast<int>((bytesTotal - bytesAvailable) * 100 / bytesTotal) : -1;
}

// Paints a small inline usage meter to the right of the icon+text for rows carrying
// DiskUsagePercentRole, instead of a separate sub-row (setItemWidget + a nested layout widget
// turned out to silently never paint - some interaction with how QAbstractItemView reparents
// index widgets. Delegate painting sidesteps that entirely).
class UsageBarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QVariant percentData = index.data(DiskUsagePercentRole);
        if (!percentData.isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        // paint at full width, same as every other row - narrowing option.rect here would also
        // narrow the selection/hover background, making rows with a bar look shorter than the rest
        QStyledItemDelegate::paint(painter, option, index);

        static constexpr int barHeight = 6;
        static constexpr int rightMargin = 10;
        static constexpr int gap = 12;
        static constexpr int minBarWidth = 24;
        // icon (PlacesSidebar::setIconSize is 18px) + icon-text spacing + the row's left margin -
        // rough, but close enough to sit the bar right after the text instead of stranded at the
        // row's far edge with a big empty gap for short labels like "nas" or "Data"
        static constexpr int iconAndPadding = 30;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);

        const QString text = index.data(Qt::DisplayRole).toString();
        const int textWidth = option.fontMetrics.horizontalAdvance(text);
        const int naturalBarLeft = option.rect.left() + iconAndPadding + textWidth + gap;
        const int barRight = option.rect.right() - rightMargin;
        const int barLeft = qMin(naturalBarLeft, barRight - minBarWidth); // fills the rest of the row, not just a fixed width

        const qreal radius = barHeight / 2.0;
        const QRectF track(barLeft, option.rect.top() + (option.rect.height() - barHeight) / 2.0, barRight - barLeft, barHeight);
        QPainterPath trackPath;
        trackPath.addRoundedRect(track, radius, radius);
        painter->fillPath(trackPath, option.palette.color(QPalette::Mid));

        const double percentFree = percentData.toDouble();
        const double usedFraction = 1.0 - (percentFree / 100.0);
        const qreal fillWidth = qBound(0.0, track.width() * usedFraction, track.width());
        if (fillWidth > 0) {
            painter->setClipPath(trackPath);
            QPainterPath fillPath;
            fillPath.addRoundedRect(QRectF(track.left(), track.top(), fillWidth, track.height()), radius, radius);
            painter->fillPath(fillPath, DiskUsageIndicator::colorForPercentFree(percentFree));
        }
        painter->restore();
    }
};

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
    setIconSize(QSize(18, 18));
    setSpacing(2);
    setUniformItemSizes(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setItemDelegate(new UsageBarDelegate(this));

    m_fixedPlaces = {
        {tr("Home"), QStringLiteral("user-home"),
         QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)), kHomeSettingsKey},
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

    QSettings settings;
    m_diskUsageEnabled = settings.value(QStringLiteral("Sidebar/ShowDriveDiskUsage"), false).toBool();
    m_diskUsageStyle = settings.value(QStringLiteral("Sidebar/DiskUsageStyle"), static_cast<int>(DiskUsageIndicator::Style::Text)).toInt()
            == static_cast<int>(DiskUsageIndicator::Style::Bar)
        ? DiskUsageIndicator::Style::Bar
        : DiskUsageIndicator::Style::Text;

    loadPinned(); // must run before loadSectionOrder(), which needs m_pinned for the legacy-section migration
    loadSectionOrder();
    // Places/Bookmarks show up immediately, before drives are known - refreshDrives() below
    // scans in the background (a stat() on an unresponsive network share can block for a long
    // time) and triggers a second rebuildAll() once Devices/Network are known.
    rebuildAll();
    refreshDrives();

    // catches shares mounted outside the app (fstab, manual `mount`) so the user doesn't
    // have to hit Refresh Drives themselves. no-ops if nothing changed.
    auto *driveRefreshTimer = new QTimer(this);
    driveRefreshTimer->setInterval(5000);
    connect(driveRefreshTimer, &QTimer::timeout, this, &PlacesSidebar::refreshDrives);
    driveRefreshTimer->start();

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString unmountedPath = item->data(UnmountedObjectPathRole).toString();
        if (!unmountedPath.isEmpty()) {
            mountAndNavigate(unmountedPath);
            return;
        }
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
    m_dropHighlightItem = nullptr; // clear() below destroys it, don't try to un-highlight first
    clear();

    // fixed and custom sections all live in one ordered list, so e.g. Devices can be
    // dragged above Places via the context menu or drag-and-drop
    for (const QString &section : std::as_const(m_sectionOrder)) {
        if (section == kPlacesHeader) {
            addHeaderItem(kPlacesHeader, /*reorderable=*/true);
            for (const auto &place : m_fixedPlaces) {
                if (!isFixedPlaceVisible(place.settingsKey))
                    continue;
                QListWidgetItem *item = addPlaceItem(place.label, place.iconName, place.url, false);
                // only Home can be on its own volume - the rest are subdirectories of it
                if (place.settingsKey == kHomeSettingsKey && m_homeOnSeparateVolume)
                    applyUsageDisplay(item, place.label, m_homeBytesTotal, m_homeBytesAvailable);
            }
        } else if (section == kDevicesHeader) {
            if (m_drives.isEmpty() && m_unmountedDrives.isEmpty())
                continue;
            addHeaderItem(kDevicesHeader, /*reorderable=*/true);
            for (const auto &drive : m_drives) {
                QListWidgetItem *item = addPlaceItem(drive.label, QStringLiteral("drive-harddisk"), drive.url, false);
                applyUsageDisplay(item, drive.label, drive.bytesTotal, drive.bytesAvailable);
            }
            // plugged in but not mounted anywhere yet (no auto-mount daemon watching) - clicking
            // one calls Filesystem.Mount() via UDisks2 instead of navigating, see mountAndNavigate()
            for (const auto &unmounted : m_unmountedDrives) {
                QListWidgetItem *item = addPlaceItem(unmounted.label, QStringLiteral("drive-removable-media"), QUrl(), false);
                item->setData(UnmountedObjectPathRole, unmounted.blockObjectPath);
            }
        } else if (section == kNetworkHeader) {
            if (m_networkShares.isEmpty())
                continue;
            addHeaderItem(kNetworkHeader, /*reorderable=*/true);
            for (const auto &share : m_networkShares) {
                QListWidgetItem *item = addPlaceItem(share.label, QStringLiteral("network-server"), share.url, false);
                applyUsageDisplay(item, share.label, share.bytesTotal, share.bytesAvailable);
            }
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

bool PlacesSidebar::DriveEntry::operator==(const DriveEntry &other) const
{
    return label == other.label && url == other.url && usageBucket(bytesTotal, bytesAvailable) == usageBucket(other.bytesTotal, other.bytesAvailable);
}

void PlacesSidebar::applyUsageDisplay(QListWidgetItem *item, const QString &baseLabel, qint64 bytesTotal, qint64 bytesAvailable)
{
    if (!m_diskUsageEnabled || bytesTotal <= 0)
        return;
    if (m_diskUsageStyle == DiskUsageIndicator::Style::Text)
        item->setText(tr("%1 — %2 free of %3").arg(baseLabel, KIO::convertSize(bytesAvailable), KIO::convertSize(bytesTotal)));
    else
        item->setData(DiskUsagePercentRole, bytesAvailable * 100.0 / bytesTotal);
}

// Runs off the UI thread (via QtConcurrent in refreshDrives()) - stat()ing an unresponsive
// network share can block for a long time, and used to stall the whole app at every launch
// and every 5s refresh.
PlacesSidebar::ScanResult PlacesSidebar::scanVolumes()
{
    ScanResult result;

    const QString homePath = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).canonicalPath();
    const QStorageInfo homeInfo(homePath);
    result.homeOnSeparateVolume = homeInfo.rootPath() != QStringLiteral("/");
    if (result.homeOnSeparateVolume) {
        result.homeBytesTotal = homeInfo.bytesTotal();
        result.homeBytesAvailable = homeInfo.bytesAvailable();
    }

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
        DriveEntry entry{label, QUrl::fromLocalFile(rootPath), info.bytesTotal(), info.bytesAvailable()};
        if (isNetworkFileSystem(info.fileSystemType()))
            result.networkShares << entry;
        else
            result.drives << entry;
    }
    return result;
}

// UDisks2 knows about removable volumes the moment they're plugged in, whether or not anything
// auto-mounted them - a fresh system with no auto-mount daemon running never gets a /proc/mounts
// entry at all, which is all scanVolumes() above can see. Talking to udisks2 is local D-Bus IPC
// to a system daemon, not real disk/network I/O, so unlike scanVolumes() this runs synchronously
// on the UI thread - no risk of the kind of stall that justified backgrounding that one.
QVector<PlacesSidebar::UnmountedDrive> PlacesSidebar::scanUnmountedDrives()
{
    QVector<UnmountedDrive> result;

    auto getProperties = [](const QString &objectPath, const QString &interfaceName) -> QVariantMap {
        QDBusInterface propsInterface(QStringLiteral("org.freedesktop.UDisks2"), objectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::systemBus());
        const QDBusReply<QVariantMap> reply = propsInterface.call(QStringLiteral("GetAll"), interfaceName);
        return reply.isValid() ? reply.value() : QVariantMap();
    };

    QDBusInterface managerInterface(QStringLiteral("org.freedesktop.UDisks2"), QStringLiteral("/org/freedesktop/UDisks2/Manager"),
                                     QStringLiteral("org.freedesktop.UDisks2.Manager"), QDBusConnection::systemBus());
    const QDBusReply<QList<QDBusObjectPath>> blockDevicesReply = managerInterface.call(QStringLiteral("GetBlockDevices"), QVariantMap());
    if (!blockDevicesReply.isValid())
        return result; // no udisks2 on this system, or nothing usable on the bus - degrade quietly

    QHash<QString, bool> driveRemovableCache; // drive object path -> Removable, so multi-partition drives ask once

    for (const QDBusObjectPath &blockPath : blockDevicesReply.value()) {
        const QVariantMap blockProps = getProperties(blockPath.path(), QStringLiteral("org.freedesktop.UDisks2.Block"));
        if (blockProps.isEmpty() || blockProps.value(QStringLiteral("HintIgnore")).toBool())
            continue;
        if (blockProps.value(QStringLiteral("IdUsage")).toString() != QLatin1String("filesystem"))
            continue; // skip whole disks, swap, LUKS containers, unformatted partitions - nothing to mount

        const QString drivePath = blockProps.value(QStringLiteral("Drive")).value<QDBusObjectPath>().path();
        if (drivePath.isEmpty() || drivePath == QLatin1String("/"))
            continue;

        if (!driveRemovableCache.contains(drivePath)) {
            const QVariantMap driveProps = getProperties(drivePath, QStringLiteral("org.freedesktop.UDisks2.Drive"));
            driveRemovableCache.insert(drivePath, driveProps.value(QStringLiteral("Removable")).toBool());
        }
        if (!driveRemovableCache.value(drivePath))
            continue;

        const QVariantMap fsProps = getProperties(blockPath.path(), QStringLiteral("org.freedesktop.UDisks2.Filesystem"));
        if (fsProps.isEmpty())
            continue; // no Filesystem interface at all - GetAll on a missing interface just comes back empty

        // MountPoints is "aay" (array of byte-array paths) - only need to know if it's empty, so
        // just peek at the outer array length without demarshaling the inner byte-arrays at all
        QDBusArgument mountPointsArg = fsProps.value(QStringLiteral("MountPoints")).value<QDBusArgument>();
        mountPointsArg.beginArray();
        const bool alreadyMounted = !mountPointsArg.atEnd();
        mountPointsArg.endArray();
        if (alreadyMounted)
            continue;

        QString label = blockProps.value(QStringLiteral("IdLabel")).toString();
        if (label.isEmpty())
            label = blockProps.value(QStringLiteral("IdUUID")).toString();
        if (label.isEmpty())
            label = tr("Removable Disk");

        result << UnmountedDrive{label, blockPath.path()};
    }

    return result;
}

void PlacesSidebar::mountAndNavigate(const QString &blockObjectPath)
{
    QDBusInterface fsInterface(QStringLiteral("org.freedesktop.UDisks2"), blockObjectPath,
                                QStringLiteral("org.freedesktop.UDisks2.Filesystem"), QDBusConnection::systemBus());
    const QDBusReply<QString> reply = fsInterface.call(QStringLiteral("Mount"), QVariantMap());
    if (!reply.isValid()) {
        QMessageBox::warning(this, tr("Could Not Mount Drive"), reply.error().message());
        return;
    }
    refreshDrives(); // drops the now-stale unmounted entry and picks up the real mount immediately
    Q_EMIT placeActivated(QUrl::fromLocalFile(reply.value()));
}

void PlacesSidebar::refreshDrives()
{
    const QVector<UnmountedDrive> newUnmounted = scanUnmountedDrives();
    if (newUnmounted != m_unmountedDrives) {
        m_unmountedDrives = newUnmounted;
        rebuildAll();
    }

    if (!m_scanWatcher) {
        m_scanWatcher = new QFutureWatcher<ScanResult>(this);
        connect(m_scanWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] { applyScanResult(m_scanWatcher->result()); });
    } else if (m_scanWatcher->isRunning()) {
        return; // previous scan still in flight (e.g. a slow network share) - don't pile up scans
    }
    m_scanWatcher->setFuture(QtConcurrent::run(&PlacesSidebar::scanVolumes));
}

void PlacesSidebar::applyScanResult(const ScanResult &result)
{
    // nothing changed since last check (the usual case) -> skip rebuild so we don't flicker
    // or lose scroll/selection every 5s
    const bool unchanged = m_drivesInitialized && result.drives == m_drives && result.networkShares == m_networkShares
        && result.homeOnSeparateVolume == m_homeOnSeparateVolume
        && usageBucket(result.homeBytesTotal, result.homeBytesAvailable) == usageBucket(m_homeBytesTotal, m_homeBytesAvailable);

    m_drivesInitialized = true;
    m_drives = result.drives;
    m_networkShares = result.networkShares;
    m_homeBytesTotal = result.homeBytesTotal;
    m_homeBytesAvailable = result.homeBytesAvailable;
    m_homeOnSeparateVolume = result.homeOnSeparateVolume;

    if (!unchanged)
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
    QStringList result; // only Bookmarks/custom sections are pin targets, not Places/Devices/Network
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
    // reusing the old "CustomSections" key from before every section was reorderable, so an
    // old config won't have the fixed names in it yet - backfilled below at their old fixed
    // spots (Places first, Bookmarks next, Devices/Network last) so nothing jumps around
    m_sectionOrder = settings.value(QStringLiteral("CustomSections")).toStringList();

    // "Network" used to be a normal user section name before it became reserved. if someone
    // already has a custom section called that, it'd get swallowed by the new fixed-network
    // branch in rebuildAll() and become undeletable - rename it out of the way
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
        return !m_drives.isEmpty() || !m_unmountedDrives.isEmpty();
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
    // has to swap with the next *visible* neighbor - swapping with a hidden one (an empty
    // Bookmarks, say) would look like "Move Up" did nothing at all
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

void PlacesSidebar::setDriveDiskUsageEnabled(bool enabled)
{
    if (m_diskUsageEnabled == enabled)
        return;
    m_diskUsageEnabled = enabled;
    rebuildAll();
}

void PlacesSidebar::setDriveDiskUsageStyle(DiskUsageIndicator::Style style)
{
    if (m_diskUsageStyle == style)
        return;
    m_diskUsageStyle = style;
    if (m_diskUsageEnabled)
        rebuildAll();
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

        // top half of the row = insert before, bottom half = after - needed so you can drop
        // a section past its immediate neighbor at all (dropping "before" an adjacent item
        // is otherwise a no-op)
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
    if (dropWouldBeNoOpOrInvalid(urls, destDir)) {
        event->acceptProposedAction();
        return;
    }
    if (event->proposedAction() == Qt::MoveAction)
        FileOperations::moveTo(urls, destDir, this);
    else
        FileOperations::copyTo(urls, destDir, this);
    event->acceptProposedAction();
}
