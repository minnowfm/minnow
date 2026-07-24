#include "BrowserTab.h"
#include "FileOperations.h"
#include "PathBar.h"
#include "PathUtils.h"
#include "PlacesSidebar.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
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
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <KDirLister>
#include <KDirModel>
#include <KDirSortFilterProxyModel>
#include <KFileItem>

#include <algorithm>

namespace
{
// Animates wheel-triggered scrolling instead of jumping straight to the target position,
// so it reads closer to a browser's smooth scroll than the default per-item/per-line jump.
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

    // m_pathBar is intentionally not added to this layout: MainWindow reparents whichever
    // tab is active into a shared toolbar slot, so the path bar reads like a single
    // top-level bar even though each tab keeps its own path bar/history.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_viewStack, 1);
}

void BrowserTab::setupViews()
{
    m_dirLister = new KDirLister();
    m_dirLister->setAutoErrorHandlingEnabled(true);

    m_dirModel = new KDirModel(this);
    m_dirModel->setDirLister(m_dirLister);

    m_proxyModel = new KDirSortFilterProxyModel(this);
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

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_gridView);
    m_viewStack->addWidget(m_listView);
    m_viewStack->setCurrentWidget(m_gridView);

    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &BrowserTab::onItemActivated);
    connect(m_listView, &QAbstractItemView::doubleClicked, this, &BrowserTab::onItemActivated);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, &BrowserTab::showViewContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, &BrowserTab::showViewContextMenu);

    m_gridView->viewport()->installEventFilter(this);
    m_listView->viewport()->installEventFilter(this);

    // Keyboard shortcuts (Return/Enter/Delete/Shift+Delete) are installed once at the
    // MainWindow level and act on whichever tab is active - see MainWindow::setupShortcuts().
    // Installing them per-view here would misbehave with multiple tabs: a window-scoped
    // shortcut fires regardless of which tab's view is actually visible.

    connect(m_dirLister, &KCoreDirLister::completed, this, &BrowserTab::statusChanged);
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
    m_dirLister->openUrl(url);
    applySortForCurrentFolder();
    applyViewModeForCurrentFolder();
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
    onItemActivated(currentView()->currentIndex());
}

int BrowserTab::itemCount() const
{
    return m_proxyModel->rowCount();
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

    // Handled here (rather than left to QAbstractItemView's own drop handling) for two
    // reasons: we need real KIO copy/move instead of an in-model row move, and letting Qt's
    // default view drop-handling run against a KDirSortFilterProxyModel (which doesn't
    // support reordering) is what was corrupting the view on same-folder drops.
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
        const bool droppedOntoOwnFolder =
            std::all_of(urls.constBegin(), urls.constEnd(), [&destDir](const QUrl &url) { return parentOf(url) == destDir; });
        if (!droppedOntoOwnFolder) {
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
    if (urls.isEmpty())
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
    m_filterText = text;
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterFixedString(text);
    Q_EMIT statusChanged();
}

void BrowserTab::setIconSize(int size)
{
    m_gridView->setIconSize(QSize(size, size));
    m_gridView->setGridSize(QSize(size + 48, size + 40));
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

    if (!selected.isEmpty()) {
        QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), tr("Open"));
        connect(openAction, &QAction::triggered, this, [this, selected] {
            for (const QUrl &url : selected)
                FileOperations::openUrl(url, this);
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

            addPinAction(tr("Pin to Sidebar"), dirUrl);
        }

        menu.addSeparator();

        QAction *copyAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy"));
        connect(copyAction, &QAction::triggered, this, [selected] {
            auto *mime = new QMimeData();
            mime->setUrls(selected);
            QApplication::clipboard()->setMimeData(mime);
        });

        QAction *cutAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")), tr("Cut"));
        connect(cutAction, &QAction::triggered, this, [selected] {
            auto *mime = new QMimeData();
            mime->setUrls(selected);
            mime->setData(QStringLiteral("application/x-kde-cutselection"), QByteArrayLiteral("1"));
            QApplication::clipboard()->setMimeData(mime);
        });

        menu.addSeparator();

        if (selected.size() == 1) {
            QAction *renameAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), tr("Rename"));
            const QUrl renameUrl = selected.first();
            connect(renameAction, &QAction::triggered, this, [this, renameUrl] {
                const QString oldName = renameUrl.fileName();
                bool ok = false;
                const QString newName =
                    QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, oldName, &ok);
                if (ok && !newName.isEmpty() && newName != oldName)
                    FileOperations::rename(renameUrl, newName, this);
            });
        }

        QAction *trashAction = menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), tr("Move to Trash"));
        connect(trashAction, &QAction::triggered, this, [this, selected] {
            FileOperations::trash(selected, this);
        });

        QAction *deleteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete permanently"));
        connect(deleteAction, &QAction::triggered, this, [this, selected] {
            FileOperations::remove(selected, this);
        });

        menu.addSeparator();
    }

    const QMimeData *clip = QApplication::clipboard()->mimeData();
    if (clip && clip->hasUrls()) {
        QAction *pasteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), tr("Paste"));
        const bool isCut = clip->data(QStringLiteral("application/x-kde-cutselection")) == QByteArrayLiteral("1");
        const QUrl destDir = m_currentUrl;
        const QList<QUrl> urls = clip->urls();
        connect(pasteAction, &QAction::triggered, this, [this, urls, destDir, isCut] {
            if (isCut)
                FileOperations::moveTo(urls, destDir, this);
            else
                FileOperations::copyTo(urls, destDir, this);
        });

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

    QAction *newFolderAction = menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), tr("New Folder"));
    connect(newFolderAction, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, tr("New Folder"), &ok);
        if (ok && !name.isEmpty())
            FileOperations::mkdir(m_currentUrl, name, this);
    });

    addPinAction(tr("Pin This Folder to Sidebar"), m_currentUrl);

    menu.exec(view->viewport()->mapToGlobal(pos));
}
