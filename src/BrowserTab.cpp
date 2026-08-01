#include "BrowserTab.h"
#include "FileOperations.h"
#include "PathBar.h"
#include "PathUtils.h"
#include "PlacesSidebar.h"
#include "ThumbnailProxyModel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <KDirLister>
#include <KDirModel>
#include <KFileItem>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KIO/ListJob>
#include <KIO/PreviewJob>
#include <KJob>

#include <memory>

#include <algorithm>

namespace
{
// animates wheel scrolling instead of the default per-line jump - closer to how a browser feels
class SmoothScroller : public QObject
{
public:
    explicit SmoothScroller(QAbstractItemView *view)
        : QObject(view)
        , m_view(view)
        , m_animation(new QPropertyAnimation(view->verticalScrollBar(), "value", this))
    {
        m_animation->setEasingCurve(QEasingCurve::OutCubic);
        view->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel) {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            const int notches = wheelEvent->angleDelta().y() / 120;
            if (notches == 0)
                return QObject::eventFilter(watched, event);

            QScrollBar *bar = m_view->verticalScrollBar();
            const int base = m_animation->state() == QAbstractAnimation::Running ? m_animation->endValue().toInt()
                                                                                  : bar->value();
            const int target = qBound(bar->minimum(), base - notches * kPixelsPerNotch, bar->maximum());

            m_animation->stop();
            m_animation->setStartValue(bar->value());
            m_animation->setEndValue(target);
            m_animation->setDuration(220);
            m_animation->start();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    static constexpr int kPixelsPerNotch = 90;
    QAbstractItemView *m_view;
    QPropertyAnimation *m_animation;
};
}

BrowserTab::BrowserTab(PlacesSidebar *sidebar, QWidget *parent)
    : QWidget(parent)
    , m_sidebar(sidebar)
{
    loadFolderSort();
    loadFolderViewModes();

    m_pathBar = new PathBar(this);
    connect(m_pathBar, &PathBar::urlActivated, this, &BrowserTab::navigateTo);
    connect(m_pathBar, &PathBar::urlsDropped, this, &BrowserTab::onUrlsDropped);

    setupViews();

    // m_pathBar isn't added here on purpose - MainWindow reparents the active tab's path bar
    // into a shared toolbar slot, so it looks like one bar even though every tab has its own
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_viewStack, 1);
}

void BrowserTab::setupViews()
{
    QSettings settings;
    m_showHiddenFiles = settings.value(QStringLiteral("View/ShowHiddenFiles"), false).toBool();
    m_showThumbnails = settings.value(QStringLiteral("View/ShowThumbnails"), true).toBool();

    m_dirLister = new KDirLister();
    m_dirLister->setAutoErrorHandlingEnabled(true);
    m_dirLister->setShowHiddenFiles(m_showHiddenFiles);

    m_dirModel = new KDirModel(this);
    m_dirModel->setDirLister(m_dirLister);

    m_proxyModel = new ThumbnailProxyModel(this);
    m_proxyModel->setSourceModel(m_dirModel);

    m_gridView = new QListView(this);
    m_gridView->setModel(m_proxyModel);
    m_gridView->setViewMode(QListView::IconMode);
    m_gridView->setResizeMode(QListView::Adjust);
    m_gridView->setGridSize(QSize(96, 88));
    m_gridView->setIconSize(QSize(64, 64));
    m_gridView->setSpacing(8);
    m_gridView->setWordWrap(true);
    m_gridView->setUniformItemSizes(true);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_gridView->setFrameShape(QFrame::NoFrame);
    m_gridView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_gridView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_gridView->setDragEnabled(true);
    m_gridView->setAcceptDrops(true);
    m_gridView->setDropIndicatorShown(true);
    m_gridView->setDragDropMode(QAbstractItemView::DragDrop);
    m_gridView->setDefaultDropAction(Qt::MoveAction);
    new SmoothScroller(m_gridView);

    m_listView = new QTreeView(this);
    m_listView->setModel(m_proxyModel);
    m_listView->setRootIsDecorated(false);
    m_listView->setItemsExpandable(false);
    m_listView->setSortingEnabled(true);
    m_listView->setAllColumnsShowFocus(true);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_listView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setDragEnabled(true);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    m_listView->setDragDropMode(QAbstractItemView::DragDrop);
    m_listView->setDefaultDropAction(Qt::MoveAction);
    new SmoothScroller(m_listView);

    m_searchResultsView = new QTreeWidget(this);
    m_searchResultsView->setColumnCount(2);
    m_searchResultsView->setHeaderLabels({tr("Name"), tr("Location")});
    m_searchResultsView->setRootIsDecorated(false);
    m_searchResultsView->setUniformRowHeights(true);
    m_searchResultsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_searchResultsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_searchResultsView->setFrameShape(QFrame::NoFrame);
    m_searchResultsView->setAlternatingRowColors(true);

    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &BrowserTab::startSearch);

    connect(m_searchResultsView, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        activateSearchResult(item);
    });

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_gridView);
    m_viewStack->addWidget(m_listView);
    m_viewStack->addWidget(m_searchResultsView);
    m_viewStack->setCurrentWidget(m_gridView);

    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &BrowserTab::onItemActivated);
    connect(m_listView, &QAbstractItemView::doubleClicked, this, &BrowserTab::onItemActivated);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, &BrowserTab::showViewContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, &BrowserTab::showViewContextMenu);
    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BrowserTab::statusChanged);
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BrowserTab::statusChanged);

    m_gridView->viewport()->installEventFilter(this);
    m_listView->viewport()->installEventFilter(this);

    // Return/Delete/etc. shortcuts live at the MainWindow level (setupShortcuts()), not per-view -
    // a window-scoped shortcut fires no matter which tab is visible, so per-view ones here
    // would double up across tabs

    connect(m_dirLister, &KCoreDirLister::completed, this, &BrowserTab::statusChanged);
    connect(m_dirLister, &KCoreDirLister::itemsAdded, this, [this](const QUrl &, const KFileItemList &items) {
        if (m_showThumbnails)
            requestThumbnails(items);
    });
    connect(m_dirLister, &KCoreDirLister::refreshItems, this, [this](const QList<QPair<KFileItem, KFileItem>> &items) {
        // Rename comes through here (old/new KFileItem pair), not itemsAdded - carry the
        // cached thumbnail over to the new URL instead of it going blank until a full relist.
        for (const auto &pair : items) {
            if (pair.first.url() != pair.second.url())
                m_proxyModel->renameThumbnail(pair.first.url(), pair.second.url());
        }
    });
    connect(m_listView->header(), &QHeaderView::sortIndicatorChanged, this, &BrowserTab::onSortIndicatorChanged);
}

void BrowserTab::navigateTo(const QUrl &url)
{
    if (!url.isValid())
        return;
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(url);
    m_historyIndex = m_history.size() - 1;
    loadUrl(url);
}

void BrowserTab::loadUrl(const QUrl &url)
{
    m_currentUrl = url;
    m_pathBar->setUrl(url);
    m_proxyModel->clearThumbnails(); // else these just pile up forever across folder switches
    m_dirLister->openUrl(url);
    applySortForCurrentFolder();
    applyViewModeForCurrentFolder();
    if (searchActive())
        startSearch();
    Q_EMIT urlChanged(url);
    Q_EMIT titleChanged();
    Q_EMIT historyChanged();
}

bool BrowserTab::canGoBack() const
{
    return m_historyIndex > 0;
}

bool BrowserTab::canGoForward() const
{
    return m_historyIndex < m_history.size() - 1;
}

bool BrowserTab::canGoUp() const
{
    return parentOf(m_currentUrl) != m_currentUrl;
}

void BrowserTab::goBack()
{
    if (!canGoBack())
        return;
    --m_historyIndex;
    loadUrl(m_history.at(m_historyIndex));
}

void BrowserTab::goForward()
{
    if (!canGoForward())
        return;
    ++m_historyIndex;
    loadUrl(m_history.at(m_historyIndex));
}

void BrowserTab::goUp()
{
    const QUrl parent = parentOf(m_currentUrl);
    if (parent != m_currentUrl)
        navigateTo(parent);
}

QString BrowserTab::displayName() const
{
    const QString name = m_currentUrl.fileName();
    return name.isEmpty() ? m_currentUrl.toDisplayString(QUrl::PreferLocalFile) : name;
}

QAbstractItemView *BrowserTab::currentView() const
{
    return m_viewStack->currentWidget() == m_gridView ? static_cast<QAbstractItemView *>(m_gridView)
                                                        : static_cast<QAbstractItemView *>(m_listView);
}

QList<QUrl> BrowserTab::selectedUrls() const
{
    QList<QUrl> urls;

    if (searchActive()) {
        for (QTreeWidgetItem *item : m_searchResultsView->selectedItems())
            urls << item->data(0, Qt::UserRole).toUrl();
        return urls;
    }

    QAbstractItemView *view = currentView();
    if (!view->selectionModel())
        return urls;

    const QModelIndexList indexes = view->selectionModel()->selectedRows();
    for (const QModelIndex &proxyIndex : indexes) {
        const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        const KFileItem item = m_dirModel->itemForIndex(sourceIndex);
        if (!item.isNull())
            urls << item.url();
    }
    return urls;
}

void BrowserTab::activateCurrentItem()
{
    if (searchActive()) {
        activateSearchResult(m_searchResultsView->currentItem());
        return;
    }
    onItemActivated(currentView()->currentIndex());
}

void BrowserTab::activateSearchResult(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QUrl url = item->data(0, Qt::UserRole).toUrl();
    const bool isDir = item->data(0, Qt::UserRole + 1).toBool();
    if (!url.isValid())
        return;
    if (isDir)
        navigateTo(url);
    else
        FileOperations::openUrl(url, this);
}

void BrowserTab::selectAndReveal(const QUrl &url)
{
    // A dotfile target would otherwise never appear in indexForUrl() below - the lister is
    // filtering it out at the default View/ShowHiddenFiles=false setting, so it stays invalid
    // even after listing completes. Force it visible for this tab only (not persisted - a
    // reveal request for one hidden file shouldn't flip the user's saved preference).
    if (!m_showHiddenFiles && url.fileName().startsWith(QLatin1Char('.'))) {
        m_showHiddenFiles = true;
        m_dirLister->setShowHiddenFiles(true);
        m_dirLister->emitChanges();
    }

    const QModelIndex sourceIndex = m_dirModel->indexForUrl(url);
    if (sourceIndex.isValid()) {
        const QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
        if (!proxyIndex.isValid())
            return;
        QAbstractItemView *view = currentView();
        view->selectionModel()->select(proxyIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(proxyIndex);
        view->scrollTo(proxyIndex);
        return;
    }

    // Folder just navigated to and hasn't finished listing yet - retry once it does. The
    // shared_ptr'd connection handle disconnects just this lambda, not every "completed"
    // listener (setupViews() has its own, for the status bar).
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_dirLister, &KCoreDirLister::completed, this, [this, url, connection] {
        QObject::disconnect(*connection);
        selectAndReveal(url);
    });
}

int BrowserTab::itemCount() const
{
    if (!searchActive())
        return m_proxyModel->rowCount();
    int count = 0;
    if (m_searchHereSection)
        count += m_searchHereSection->childCount();
    if (m_searchSubfoldersSection)
        count += m_searchSubfoldersSection->childCount();
    return count;
}

void BrowserTab::onItemActivated(const QModelIndex &proxyIndex)
{
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const KFileItem item = m_dirModel->itemForIndex(sourceIndex);
    if (item.isNull())
        return;

    if (item.isDir())
        navigateTo(item.url());
    else
        FileOperations::openUrl(item.url(), this);
}

bool BrowserTab::eventFilter(QObject *watched, QEvent *event)
{
    const bool isGridViewport = watched == m_gridView->viewport();
    const bool isListViewport = watched == m_listView->viewport();
    if (!isGridViewport && !isListViewport)
        return QWidget::eventFilter(watched, event);

    QAbstractItemView *view = isGridViewport ? static_cast<QAbstractItemView *>(m_gridView)
                                              : static_cast<QAbstractItemView *>(m_listView);

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::MiddleButton) {
            const QModelIndex proxyIndex = view->indexAt(mouseEvent->pos());
            if (proxyIndex.isValid()) {
                const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
                const KFileItem item = m_dirModel->itemForIndex(sourceIndex);
                if (!item.isNull() && item.isDir()) {
                    Q_EMIT openInNewTabRequested(item.url());
                    return true;
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    // handled manually because we need a real KIO copy/move, not a model row move - and Qt's
    // default drop handling against KDirSortFilterProxyModel (no reordering support) was what
    // corrupted the view on same-folder drops
    if (event->type() == QEvent::DragEnter) {
        auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        if (!dragEvent->mimeData()->hasUrls())
            return QWidget::eventFilter(watched, event);
        dragEvent->acceptProposedAction();
        return true;
    }
    if (event->type() == QEvent::DragMove) {
        auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (!dragEvent->mimeData()->hasUrls())
            return QWidget::eventFilter(watched, event);
        dragEvent->acceptProposedAction();
        return true;
    }
    if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (!dropEvent->mimeData()->hasUrls())
            return QWidget::eventFilter(watched, event);

        QUrl destDir = m_currentUrl;
        const QModelIndex proxyIndex = view->indexAt(dropEvent->position().toPoint());
        if (proxyIndex.isValid()) {
            const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
            const KFileItem item = m_dirModel->itemForIndex(sourceIndex);
            if (!item.isNull() && item.isDir())
                destDir = item.url();
        }

        const QList<QUrl> urls = dropEvent->mimeData()->urls();
        if (!dropWouldBeNoOpOrInvalid(urls, destDir)) {
            if (dropEvent->proposedAction() == Qt::MoveAction)
                FileOperations::moveTo(urls, destDir, this);
            else
                FileOperations::copyTo(urls, destDir, this);
        }
        dropEvent->acceptProposedAction();
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void BrowserTab::onUrlsDropped(const QUrl &destination, QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || dropWouldBeNoOpOrInvalid(urls, destination))
        return;
    if (event->proposedAction() == Qt::MoveAction)
        FileOperations::moveTo(urls, destination, this);
    else
        FileOperations::copyTo(urls, destination, this);
}

void BrowserTab::switchToGridView()
{
    m_viewStack->setCurrentWidget(m_gridView);
    setFolderIsGrid(true);
}

void BrowserTab::switchToListView()
{
    m_viewStack->setCurrentWidget(m_listView);
    setFolderIsGrid(false);
}

void BrowserTab::setFolderIsGrid(bool isGrid)
{
    if (!m_currentUrl.isValid())
        return;
    m_folderIsGrid[m_currentUrl.toString()] = isGrid;
    saveFolderViewModes();
}

void BrowserTab::applyViewModeForCurrentFolder()
{
    const bool isGrid = m_folderIsGrid.value(m_currentUrl.toString(), true);
    m_viewStack->setCurrentWidget(isGrid ? static_cast<QWidget *>(m_gridView) : static_cast<QWidget *>(m_listView));
}

void BrowserTab::loadFolderViewModes()
{
    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("FolderViewMode"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString url = settings.value(QStringLiteral("url")).toString();
        const bool isGrid = settings.value(QStringLiteral("isGrid"), true).toBool();
        if (!url.isEmpty())
            m_folderIsGrid[url] = isGrid;
    }
    settings.endArray();
}

void BrowserTab::saveFolderViewModes()
{
    QSettings settings;
    settings.remove(QStringLiteral("FolderViewMode"));
    settings.beginWriteArray(QStringLiteral("FolderViewMode"));
    int idx = 0;
    for (auto it = m_folderIsGrid.constBegin(); it != m_folderIsGrid.constEnd(); ++it) {
        settings.setArrayIndex(idx++);
        settings.setValue(QStringLiteral("url"), it.key());
        settings.setValue(QStringLiteral("isGrid"), it.value());
    }
    settings.endArray();
}

void BrowserTab::setFilterText(const QString &text)
{
    const bool wasActive = searchActive();
    m_filterText = text;

    if (searchActive()) {
        m_searchDebounceTimer->start(400);
    } else if (wasActive) {
        m_searchDebounceTimer->stop();
        stopSearch();
        applyViewModeForCurrentFolder();
    }
    Q_EMIT statusChanged();
}

void BrowserTab::startSearch()
{
    if (m_searchJob) {
        m_searchJob->kill();
        m_searchJob = nullptr;
    }
    m_searchResultsView->clear();
    m_searchHereSection = nullptr;
    m_searchSubfoldersSection = nullptr;
    m_viewStack->setCurrentWidget(m_searchResultsView);

    if (m_filterText.isEmpty())
        return;

    m_searchHereSection = new QTreeWidgetItem(m_searchResultsView);
    m_searchHereSection->setText(0, tr("In This Folder"));
    m_searchHereSection->setFlags(Qt::ItemIsEnabled);
    m_searchHereSection->setFirstColumnSpanned(true);
    QFont sectionFont = m_searchHereSection->font(0);
    sectionFont.setBold(true);
    m_searchHereSection->setFont(0, sectionFont);
    m_searchHereSection->setExpanded(true);

    m_searchSubfoldersSection = new QTreeWidgetItem(m_searchResultsView);
    m_searchSubfoldersSection->setText(0, tr("In Subfolders"));
    m_searchSubfoldersSection->setFlags(Qt::ItemIsEnabled);
    m_searchSubfoldersSection->setFirstColumnSpanned(true);
    m_searchSubfoldersSection->setFont(0, sectionFont);
    m_searchSubfoldersSection->setExpanded(true);

    m_searchJob = KIO::listRecursive(m_currentUrl, KIO::HideProgressInfo);
    const QString query = m_filterText;
    const QUrl rootDir = m_currentUrl.adjusted(QUrl::StripTrailingSlash);
    connect(m_searchJob, &KIO::ListJob::entries, this,
            [this, query, rootDir](KIO::Job *job, const KIO::UDSEntryList &list) {
        const QUrl dirUrl = static_cast<KIO::ListJob *>(job)->url();
        for (const KIO::UDSEntry &entry : list) {
            const QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            if (name == QLatin1String(".") || name == QLatin1String(".."))
                continue;
            if (!name.contains(query, Qt::CaseInsensitive))
                continue;
            if (!m_showHiddenFiles) {
                // name can be a relative path for nested matches (".config/foo") so check
                // every segment, not just the basename - want hidden dirs excluded too
                bool hidden = false;
                for (const QString &segment : name.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
                    if (segment.startsWith(QLatin1Char('.'))) {
                        hidden = true;
                        break;
                    }
                }
                if (hidden)
                    continue;
            }

            const KFileItem item(entry, dirUrl, true, true);
            // use the item's own parent dir, not the job's url() - for a recursive listing
            // that doesn't reliably match which subfolder a batch actually came from
            const QUrl itemDir = item.url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
            QTreeWidgetItem *parentItem = itemDir == rootDir ? m_searchHereSection : m_searchSubfoldersSection;

            auto *treeItem = new QTreeWidgetItem(parentItem);
            treeItem->setText(0, item.url().fileName());
            treeItem->setText(1, item.url().adjusted(QUrl::RemoveFilename).toDisplayString(QUrl::PreferLocalFile));
            treeItem->setIcon(0, QIcon::fromTheme(item.iconName()));
            treeItem->setData(0, Qt::UserRole, item.url());
            treeItem->setData(0, Qt::UserRole + 1, item.isDir());
        }
        m_searchHereSection->setHidden(m_searchHereSection->childCount() == 0);
        m_searchSubfoldersSection->setHidden(m_searchSubfoldersSection->childCount() == 0);
        Q_EMIT statusChanged();
    });
    connect(m_searchJob, &KJob::result, this, [this](KJob *job) {
        if (m_searchJob == job)
            m_searchJob = nullptr;
        Q_EMIT statusChanged();
    });
}

void BrowserTab::stopSearch()
{
    if (m_searchJob) {
        m_searchJob->kill();
        m_searchJob = nullptr;
    }
    m_searchResultsView->clear();
    m_searchHereSection = nullptr;
    m_searchSubfoldersSection = nullptr;
}

void BrowserTab::setIconSize(int size)
{
    m_gridView->setIconSize(QSize(size, size));
    m_gridView->setGridSize(QSize(size + 48, size + 40));
}

void BrowserTab::setShowHiddenFiles(bool show)
{
    if (m_showHiddenFiles == show)
        return;
    m_showHiddenFiles = show;
    m_dirLister->setShowHiddenFiles(show);
    m_dirLister->emitChanges();

    QSettings settings;
    settings.setValue(QStringLiteral("View/ShowHiddenFiles"), show);

    if (searchActive()) // rerun so hidden-file matches show up (or disappear) immediately
        startSearch();

    Q_EMIT statusChanged();
}

void BrowserTab::setShowThumbnails(bool show)
{
    if (m_showThumbnails == show)
        return;
    m_showThumbnails = show;

    QSettings settings;
    settings.setValue(QStringLiteral("View/ShowThumbnails"), show);

    if (show)
        requestThumbnails(m_dirLister->items());
    else
        m_proxyModel->clearThumbnails();
}

void BrowserTab::requestThumbnails(const QList<KFileItem> &items)
{
    QList<KFileItem> fileItems;
    for (const KFileItem &item : items) {
        if (item.isFile())
            fileItems << item;
    }
    if (fileItems.isEmpty())
        return;

    // pass every installed plugin explicitly instead of relying on KIO's preview defaults,
    // which have historically shipped with video off regardless of whether ffmpegthumbs etc
    // is actually installed
    static const QStringList enabledPlugins = KIO::PreviewJob::availablePlugins();
    KIO::PreviewJob *job = KIO::filePreview(fileItems, QSize(128, 128), &enabledPlugins);
    connect(job, &KIO::PreviewJob::gotPreview, m_proxyModel, [this](const KFileItem &item, const QPixmap &preview) {
        // thumbnails toggle can flip off mid-job - without this check, previews already
        // queued keep landing after clearThumbnails() and the toggle looks broken
        if (!m_showThumbnails)
            return;
        m_proxyModel->setThumbnail(item.url(), QIcon(preview));
    });
}

void BrowserTab::onSortIndicatorChanged(int column, Qt::SortOrder order)
{
    if (m_restoringSort || !m_currentUrl.isValid())
        return;
    saveFolderSort(m_currentUrl.toString(), column, order);
}

void BrowserTab::applySortForCurrentFolder()
{
    const FolderSort fs = m_folderSort.value(m_currentUrl.toString());
    m_restoringSort = true;
    m_listView->header()->setSortIndicator(fs.column, fs.order);
    m_restoringSort = false;
}

void BrowserTab::loadFolderSort()
{
    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("FolderSort"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString url = settings.value(QStringLiteral("url")).toString();
        const int column = settings.value(QStringLiteral("column"), 0).toInt();
        const int order = settings.value(QStringLiteral("order"), int(Qt::AscendingOrder)).toInt();
        if (!url.isEmpty())
            m_folderSort[url] = FolderSort{column, static_cast<Qt::SortOrder>(order)};
    }
    settings.endArray();
}

void BrowserTab::saveFolderSort(const QString &folderKey, int column, Qt::SortOrder order)
{
    m_folderSort[folderKey] = FolderSort{column, order};

    QSettings settings;
    settings.remove(QStringLiteral("FolderSort"));
    settings.beginWriteArray(QStringLiteral("FolderSort"));
    int idx = 0;
    for (auto it = m_folderSort.constBegin(); it != m_folderSort.constEnd(); ++it) {
        settings.setArrayIndex(idx++);
        settings.setValue(QStringLiteral("url"), it.key());
        settings.setValue(QStringLiteral("column"), it.value().column);
        settings.setValue(QStringLiteral("order"), int(it.value().order));
    }
    settings.endArray();
}

void BrowserTab::renameSelectionInteractive()
{
    const QList<QUrl> selected = selectedUrls();
    if (selected.isEmpty())
        return;

    if (selected.size() == 1) {
        const QUrl renameUrl = selected.first();
        const QString oldName = renameUrl.fileName();
        bool ok = false;
        const QString newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, oldName, &ok);
        if (ok && !newName.isEmpty() && newName != oldName)
            FileOperations::rename(renameUrl, newName, this);
    } else {
        bool ok = false;
        const QString pattern =
            QInputDialog::getText(this, tr("Bulk Rename"),
                                   tr("New name (use # for an auto-incrementing number, e.g. \"photo #\"):"),
                                   QLineEdit::Normal, QStringLiteral("# "), &ok);
        if (ok && !pattern.isEmpty())
            FileOperations::batchRename(selected, pattern, QLatin1Char('#'), this);
    }
}

void BrowserTab::showViewContextMenu(const QPoint &pos)
{
    QAbstractItemView *view = currentView();

    const QModelIndex indexUnderCursor = view->indexAt(pos);
    if (view->selectionModel()) {
        if (indexUnderCursor.isValid()) {
            if (!view->selectionModel()->isSelected(indexUnderCursor)) {
                view->selectionModel()->select(indexUnderCursor,
                                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                view->setCurrentIndex(indexUnderCursor);
            }
        } else {
            view->selectionModel()->clearSelection();
        }
    }

    QMenu menu(this);
    const QList<QUrl> selected = selectedUrls();

    auto addPinAction = [this, &menu](const QString &label, const QUrl &url) {
        if (m_sidebar->isPinned(url))
            return;
        const QStringList sections = m_sidebar->availableSections();
        if (sections.size() <= 1) {
            QAction *act = menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), label);
            connect(act, &QAction::triggered, this, [this, url] { m_sidebar->pinPlace(url); });
        } else {
            QMenu *pinMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("bookmark-new")), label);
            for (const QString &section : sections) {
                QAction *act = pinMenu->addAction(section);
                connect(act, &QAction::triggered, this, [this, url, section] { m_sidebar->pinPlace(url, section); });
            }
        }
    };

    KFileItem singleDirItem;
    if (view->selectionModel() && view->selectionModel()->selectedRows().size() == 1) {
        const QModelIndex idx = view->selectionModel()->selectedRows().first();
        const KFileItem it = m_dirModel->itemForIndex(m_proxyModel->mapToSource(idx));
        if (!it.isNull() && it.isDir())
            singleDirItem = it;
    }

    KFileItemList selectedItems;
    if (view->selectionModel()) {
        for (const QModelIndex &proxyIndex : view->selectionModel()->selectedRows()) {
            const KFileItem item = m_dirModel->itemForIndex(m_proxyModel->mapToSource(proxyIndex));
            if (!item.isNull())
                selectedItems << item;
        }
    }

    // declared up here so the quick-menu built further down can reuse these same pointers -
    // a QAction can live in more than one QMenu, no need to build everything twice
    QAction *openAction = nullptr;
    QAction *cutAction = nullptr;
    QAction *copyAction = nullptr;
    QAction *renameAction = nullptr;
    QAction *trashAction = nullptr;
    QAction *newFolderAction = nullptr;
    QAction *terminalAction = nullptr;

    if (!selected.isEmpty()) {
        openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), tr("Open"));
        connect(openAction, &QAction::triggered, this, [this, selectedItems] {
            // dirs navigate in place like a double-click - going through openUrl() would
            // hand it to the desktop's default file-manager association, usually a new window
            bool navigatedInPlace = false;
            for (const KFileItem &item : selectedItems) {
                if (item.isDir()) {
                    if (!navigatedInPlace) {
                        navigateTo(item.url());
                        navigatedInPlace = true;
                    } else {
                        Q_EMIT openInNewTabRequested(item.url());
                    }
                } else {
                    FileOperations::openUrl(item.url(), this);
                }
            }
        });

        if (!singleDirItem.isNull()) {
            const QUrl dirUrl = singleDirItem.url();
            QAction *newTabAction = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Open in New Tab"));
            connect(newTabAction, &QAction::triggered, this, [this, dirUrl] { Q_EMIT openInNewTabRequested(dirUrl); });

            QAction *newWindowAction =
                menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Open in New Window"));
            connect(newWindowAction, &QAction::triggered, this, [this, dirUrl] {
                Q_EMIT openInNewWindowRequested(dirUrl);
            });
        }

        QAction *openWithAction = menu.addAction(QIcon::fromTheme(QStringLiteral("system-run")), tr("Open With…"));
        connect(openWithAction, &QAction::triggered, this, [this, selected] { FileOperations::openWith(selected, this); });

        if (!selectedItems.isEmpty()) {
            // service-menu stuff (K3b, Share, Activities, Assign Tags, whatever's installed) -
            // added straight to the top-level menu rather than wrapped in our own extra "More
            // Actions" submenu, which just read as a second, confusing "more" next to the
            // quick-menu's own "Show More Options". KFileItemActions groups its own items into
            // submenus (Share, Activities, ...) where that makes sense on its own.
            auto *fileItemActions = new KFileItemActions(&menu);
            fileItemActions->setParentWidget(this);
            fileItemActions->setItemListProperties(KFileItemListProperties(selectedItems));
            fileItemActions->addActionsTo(&menu);
        }

        menu.addSeparator();

        cutAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")), tr("Cut"));
        connect(cutAction, &QAction::triggered, this, [selected] { FileOperations::cutToClipboard(selected); });

        copyAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy"));
        connect(copyAction, &QAction::triggered, this, [selected] { FileOperations::copyToClipboard(selected); });

        menu.addSeparator();

        QAction *archiveAction = nullptr;
        if (selected.size() == 1 && FileOperations::isArchive(selected.first())) {
            archiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")), tr("Extract Here"));
            const QUrl archiveUrl = selected.first();
            connect(archiveAction, &QAction::triggered, this, [this, archiveUrl] { FileOperations::extractArchive(archiveUrl, this); });
        } else {
            archiveAction = menu.addAction(QIcon::fromTheme(QStringLiteral("archive-insert")), tr("Compress to Zip"));
            connect(archiveAction, &QAction::triggered, this, [this, selected] { FileOperations::compressToArchive(selected, this); });
        }

        menu.addSeparator();

        const QString renameLabel = selected.size() == 1 ? tr("Rename") : tr("Rename %1 Items…").arg(selected.size());
        renameAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), renameLabel);
        connect(renameAction, &QAction::triggered, this, &BrowserTab::renameSelectionInteractive);

        if (!singleDirItem.isNull()) {
            menu.addSeparator();
            addPinAction(tr("Pin to Sidebar"), singleDirItem.url());
        }

        menu.addSeparator();

        trashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), tr("Move to Trash"));
        connect(trashAction, &QAction::triggered, this, [this, selected] {
            FileOperations::trash(selected, this);
        });

        QAction *deleteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete permanently"));
        connect(deleteAction, &QAction::triggered, this, [this, selected] {
            FileOperations::remove(selected, this);
        });

        menu.addSeparator();
    }

    QAction *pasteAction = nullptr;
    const QMimeData *clip = QApplication::clipboard()->mimeData();
    if (clip && clip->hasUrls()) {
        pasteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), tr("Paste"));
        const QUrl destDir = m_currentUrl;
        connect(pasteAction, &QAction::triggered, this, [this, destDir] { FileOperations::pasteClipboard(destDir, this); });

        menu.addSeparator();
    }

    QMenu *viewMenu = menu.addMenu(tr("View"));
    QAction *iconsAction = viewMenu->addAction(tr("Icons"));
    iconsAction->setCheckable(true);
    iconsAction->setChecked(m_viewStack->currentWidget() == m_gridView);
    QAction *listAction = viewMenu->addAction(tr("List"));
    listAction->setCheckable(true);
    listAction->setChecked(m_viewStack->currentWidget() == m_listView);
    auto *viewGroup = new QActionGroup(viewMenu);
    viewGroup->setExclusive(true);
    viewGroup->addAction(iconsAction);
    viewGroup->addAction(listAction);
    connect(iconsAction, &QAction::triggered, this, &BrowserTab::switchToGridView);
    connect(listAction, &QAction::triggered, this, &BrowserTab::switchToListView);

    viewMenu->addSeparator();
    QAction *hiddenFilesAction = viewMenu->addAction(tr("Show Hidden Files"));
    hiddenFilesAction->setCheckable(true);
    hiddenFilesAction->setChecked(m_showHiddenFiles);
    connect(hiddenFilesAction, &QAction::triggered, this, &BrowserTab::setShowHiddenFiles);

    QAction *thumbnailsAction = viewMenu->addAction(tr("Show Thumbnails"));
    thumbnailsAction->setCheckable(true);
    thumbnailsAction->setChecked(m_showThumbnails);
    connect(thumbnailsAction, &QAction::triggered, this, &BrowserTab::setShowThumbnails);

    viewMenu->addSeparator();
    QMenu *sizeMenu = viewMenu->addMenu(tr("Icon Size"));
    static const QList<QPair<QString, int>> sizes = {
        {tr("Small"), 32},
        {tr("Medium"), 48},
        {tr("Large"), 64},
        {tr("Huge"), 96},
    };
    auto *sizeGroup = new QActionGroup(sizeMenu);
    sizeGroup->setExclusive(true);
    const int currentSize = m_gridView->iconSize().width();
    for (const auto &entry : sizes) {
        QAction *act = sizeMenu->addAction(entry.first);
        act->setCheckable(true);
        act->setChecked(entry.second == currentSize);
        sizeGroup->addAction(act);
        const int size = entry.second;
        connect(act, &QAction::triggered, this, [this, size] {
            setIconSize(size);
            QSettings settings;
            settings.setValue(QStringLiteral("View/IconSize"), size);
        });
    }

    QMenu *sortMenu = menu.addMenu(tr("Sort by"));
    static const QList<QPair<QString, int>> sortColumns = {
        {tr("Name"), 0}, {tr("Size"), 1}, {tr("Date modified"), 2},
        {tr("Permissions"), 3}, {tr("Owner"), 4}, {tr("Group"), 5}, {tr("Type"), 6},
    };
    auto *columnGroup = new QActionGroup(sortMenu);
    columnGroup->setExclusive(true);
    const int currentCol = m_listView->header()->sortIndicatorSection();
    const Qt::SortOrder currentOrder = m_listView->header()->sortIndicatorOrder();
    for (const auto &entry : sortColumns) {
        QAction *act = sortMenu->addAction(entry.first);
        act->setCheckable(true);
        act->setChecked(entry.second == currentCol);
        columnGroup->addAction(act);
        const int column = entry.second;
        connect(act, &QAction::triggered, this, [this, column] {
            m_listView->header()->setSortIndicator(column, m_listView->header()->sortIndicatorOrder());
        });
    }
    sortMenu->addSeparator();
    QAction *ascAction = sortMenu->addAction(tr("Ascending"));
    ascAction->setCheckable(true);
    ascAction->setChecked(currentOrder == Qt::AscendingOrder);
    QAction *descAction = sortMenu->addAction(tr("Descending"));
    descAction->setCheckable(true);
    descAction->setChecked(currentOrder == Qt::DescendingOrder);
    auto *orderGroup = new QActionGroup(sortMenu);
    orderGroup->setExclusive(true);
    orderGroup->addAction(ascAction);
    orderGroup->addAction(descAction);
    connect(ascAction, &QAction::triggered, this, [this] {
        m_listView->header()->setSortIndicator(m_listView->header()->sortIndicatorSection(), Qt::AscendingOrder);
    });
    connect(descAction, &QAction::triggered, this, [this] {
        m_listView->header()->setSortIndicator(m_listView->header()->sortIndicatorSection(), Qt::DescendingOrder);
    });

    menu.addSeparator();

    if (m_currentUrl.scheme() == QLatin1String("trash")) {
        QAction *emptyTrashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), tr("Empty Trash"));
        connect(emptyTrashAction, &QAction::triggered, this, [this] { FileOperations::emptyTrash(this); });
        menu.addSeparator();
    } else {
        newFolderAction = menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), tr("New Folder"));
        connect(newFolderAction, &QAction::triggered, this, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal,
                                                         tr("New Folder"), &ok);
            if (ok && !name.isEmpty())
                FileOperations::mkdir(m_currentUrl, name, this);
        });

        if (m_currentUrl.isLocalFile()) {
            terminalAction = menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open Terminal Here"));
            const QUrl terminalUrl = m_currentUrl;
            connect(terminalAction, &QAction::triggered, this, [terminalUrl] { FileOperations::openTerminal(terminalUrl, nullptr); });
        }

        addPinAction(tr("Pin This Folder to Sidebar"), m_currentUrl);
    }

    menu.addSeparator();

    QAction *selectAllAction = menu.addAction(tr("Select All"));
    connect(selectAllAction, &QAction::triggered, this, [view] { view->selectAll(); });

    QAction *invertSelectionAction = menu.addAction(tr("Invert Selection"));
    connect(invertSelectionAction, &QAction::triggered, this, [view] {
        if (!view->model() || !view->selectionModel())
            return;
        const QModelIndex topLeft = view->model()->index(0, 0);
        const QModelIndex bottomRight = view->model()->index(view->model()->rowCount() - 1, view->model()->columnCount() - 1);
        view->selectionModel()->select(QItemSelection(topLeft, bottomRight), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    });

    // Properties stays pinned at the very bottom, Dolphin/Windows-style
    menu.addSeparator();
    QAction *propertiesAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-properties")), tr("Properties"));
    KFileItemList propertiesItems = selectedItems;
    if (propertiesItems.isEmpty()) {
        const KFileItem rootItem = m_dirLister->rootItem();
        if (!rootItem.isNull())
            propertiesItems << rootItem;
    }
    connect(propertiesAction, &QAction::triggered, this, [this, propertiesItems] {
        FileOperations::showProperties(propertiesItems, this);
    });

    // `menu` has basically everything and is too long to scan - what actually opens on
    // right-click is this shorter "quick" menu (common actions + a "Show More Options"
    // entry, Win11-style) built by reusing the same QAction pointers from above
    QMenu quickMenu(this);
    if (openAction)
        quickMenu.addAction(openAction);
    if (openAction || cutAction || pasteAction)
        quickMenu.addSeparator();
    if (cutAction)
        quickMenu.addAction(cutAction);
    if (copyAction)
        quickMenu.addAction(copyAction);
    if (pasteAction)
        quickMenu.addAction(pasteAction);
    if (renameAction)
        quickMenu.addAction(renameAction);
    if (cutAction || copyAction || pasteAction || renameAction)
        quickMenu.addSeparator();
    if (trashAction)
        quickMenu.addAction(trashAction);
    if (newFolderAction)
        quickMenu.addAction(newFolderAction);
    if (terminalAction)
        quickMenu.addAction(terminalAction);
    if (trashAction || newFolderAction || terminalAction)
        quickMenu.addSeparator();
    quickMenu.addAction(propertiesAction);
    quickMenu.addSeparator();

    QAction *showMoreAction = quickMenu.addAction(tr("Show More Options"));

    // Checked against exec()'s return value, not a triggered() connection - a connection would
    // fire while quickMenu's own exec() loop is still running, so menu.exec() would open nested
    // inside it instead of after it actually closes (looked like "more options" stacked inside
    // more options).
    const QAction *chosen = quickMenu.exec(view->viewport()->mapToGlobal(pos));
    if (chosen == showMoreAction)
        menu.exec(view->viewport()->mapToGlobal(pos));
}
