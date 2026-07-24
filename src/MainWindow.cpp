#include "MainWindow.h"
#include "FileOperations.h"
#include "PlacesSidebar.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <KDirLister>
#include <KDirModel>
#include <KDirSortFilterProxyModel>
#include <KFileItem>
#include <KIO/OpenUrlJob>

namespace
{
QUrl parentOf(const QUrl &url)
{
    QUrl u = url;
    QString path = u.path();
    if (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const int idx = path.lastIndexOf(QLatin1Char('/'));
    path = (idx <= 0) ? QStringLiteral("/") : path.left(idx);
    u.setPath(path);
    return u;
}

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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(960, 620);

    loadFolderSort();
    loadFolderViewModes();
    setupToolBar();
    setupSidebar();
    setupViews();
    applyStyle();

    QSettings settings;
    setIconSize(settings.value(QStringLiteral("View/IconSize"), 64).toInt());

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(m_sidebar);

    auto *contentCard = new QFrame(this);
    contentCard->setObjectName(QStringLiteral("contentCard"));
    m_cardLayout = new QVBoxLayout(contentCard);
    m_cardLayout->setContentsMargins(6, 6, 6, 6);
    m_cardLayout->setSpacing(4);
    m_cardLayout->addWidget(m_viewStack, 1);
    layout->addWidget(contentCard, 1);

    setupStatusBar();

    setCentralWidget(central);

    navigateTo(QUrl::fromLocalFile(QDir::homePath()));
}

void MainWindow::setupToolBar()
{
    QToolBar *toolbar = addToolBar(tr("Navigation"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));

    m_backButton = new QToolButton(this);
    m_backButton->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
    m_backButton->setToolTip(tr("Back"));
    m_backButton->setEnabled(false);

    m_forwardButton = new QToolButton(this);
    m_forwardButton->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
    m_forwardButton->setToolTip(tr("Forward"));
    m_forwardButton->setEnabled(false);

    m_upButton = new QToolButton(this);
    m_upButton->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    m_upButton->setToolTip(tr("Up"));

    toolbar->addWidget(m_backButton);
    toolbar->addWidget(m_forwardButton);
    toolbar->addWidget(m_upButton);

    m_addressBar = new QLineEdit(this);
    m_addressBar->setObjectName(QStringLiteral("addressBar"));
    toolbar->addWidget(m_addressBar);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search this folder…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(200);
    toolbar->addWidget(m_filterEdit);

    m_iconSizeButton = new QToolButton(this);
    m_iconSizeButton->setIcon(QIcon::fromTheme(QStringLiteral("zoom-fit-best")));
    m_iconSizeButton->setToolTip(tr("Icon size"));
    m_iconSizeButton->setPopupMode(QToolButton::InstantPopup);

    auto *sizeMenu = new QMenu(m_iconSizeButton);
    auto *sizeGroup = new QActionGroup(sizeMenu);
    sizeGroup->setExclusive(true);
    static const QList<QPair<QString, int>> sizes = {
        {tr("Small"), 32},
        {tr("Medium"), 48},
        {tr("Large"), 64},
        {tr("Huge"), 96},
    };
    for (const auto &entry : sizes) {
        QAction *act = sizeMenu->addAction(entry.first);
        act->setCheckable(true);
        act->setData(entry.second);
        sizeGroup->addAction(act);
        const int size = entry.second;
        connect(act, &QAction::triggered, this, [this, size] {
            setIconSize(size);
        });
    }
    connect(sizeMenu, &QMenu::aboutToShow, this, [this, sizeGroup] {
        const int current = m_gridView->iconSize().width();
        for (QAction *a : sizeGroup->actions())
            a->setChecked(a->data().toInt() == current);
    });
    m_iconSizeButton->setMenu(sizeMenu);
    toolbar->addWidget(m_iconSizeButton);

    connect(m_backButton, &QToolButton::clicked, this, &MainWindow::goBack);
    connect(m_forwardButton, &QToolButton::clicked, this, &MainWindow::goForward);
    connect(m_upButton, &QToolButton::clicked, this, &MainWindow::goUp);
    connect(m_addressBar, &QLineEdit::returnPressed, this, &MainWindow::onAddressBarSubmitted);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTextChanged);
}

void MainWindow::setupSidebar()
{
    m_sidebar = new PlacesSidebar(this);
    connect(m_sidebar, &PlacesSidebar::placeActivated, this, [this](const QUrl &url) {
        navigateTo(url);
    });
}

void MainWindow::setupViews()
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
    m_gridView->setIconSize(QSize(48, 48));
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
    new SmoothScroller(m_listView);

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_gridView);
    m_viewStack->addWidget(m_listView);
    m_viewStack->setCurrentWidget(m_gridView);

    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &MainWindow::onItemActivated);
    connect(m_listView, &QAbstractItemView::doubleClicked, this, &MainWindow::onItemActivated);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, &MainWindow::showViewContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, &MainWindow::showViewContextMenu);

    for (QAbstractItemView *view : {static_cast<QAbstractItemView *>(m_gridView), static_cast<QAbstractItemView *>(m_listView)}) {
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
            auto *shortcut = new QShortcut(QKeySequence(key), view);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut, &QShortcut::activated, this, [this, view] { onItemActivated(view->currentIndex()); });
        }
    }
    connect(m_dirLister, &KCoreDirLister::completed, this, &MainWindow::updateStatusBar);
    connect(m_listView->header(), &QHeaderView::sortIndicatorChanged, this, &MainWindow::onSortIndicatorChanged);
}

void MainWindow::setupStatusBar()
{
    auto *statusRow = new QWidget;
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(6, 0, 6, 0);

    m_itemCountLabel = new QLabel(statusRow);
    m_freeSpaceLabel = new QLabel(statusRow);
    m_itemCountLabel->setObjectName(QStringLiteral("footerLabel"));
    m_freeSpaceLabel->setObjectName(QStringLiteral("footerLabel"));

    statusLayout->addWidget(m_itemCountLabel);
    statusLayout->addStretch(1);
    statusLayout->addWidget(m_freeSpaceLabel);

    m_cardLayout->addWidget(statusRow);
}

void MainWindow::applyStyle()
{
    const QColor windowColor = palette().color(QPalette::Window);
    const bool dark = windowColor.lightness() < 128;
    const QColor cardColor = dark ? windowColor.lighter(125) : windowColor.lighter(106);
    const QColor borderColor = dark ? windowColor.lighter(150) : windowColor.darker(112);
    const QColor handleColor = dark ? windowColor.lighter(200) : windowColor.darker(160);
    const QColor footerTextColor = dark ? QColor(190, 190, 190) : QColor(90, 90, 90);
    const QColor hoverColor = dark ? cardColor.lighter(115) : cardColor.darker(105);

    setStyleSheet(
        QStringLiteral("QMainWindow { background: palette(window); }"
                        "QToolBar { background: palette(window); border: none; spacing: 4px; padding: 4px; }"
                        "PlacesSidebar { background: palette(window); border: none; }"
                        "QFrame#contentCard { background: %1; border: 1px solid %2; border-radius: 14px; }"
                        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 10px; padding: 4px 10px; }"
                        "QToolButton { border-radius: 8px; padding: 4px; }"
                        "QListWidget::item { border-radius: 10px; padding: 5px 8px; margin: 1px 4px; }"
                        "QListWidget::item:selected { border-radius: 10px; }"
                        "QListView { border: none; background: transparent; }"
                        "QListView::item { border-radius: 12px; padding: 6px; }"
                        "QListView::item:hover { background: %5; border-radius: 12px; }"
                        "QListView::item:selected { background: palette(highlight); color: palette(highlighted-text); border-radius: 12px; }"
                        "QTreeView { border: none; background: transparent; }"
                        "QTreeView::item { border-radius: 10px; }"
                        "QTreeView::item:hover { background: %5; border-radius: 10px; }"
                        "QTreeView::item:selected { background: palette(highlight); color: palette(highlighted-text); border-radius: 10px; }"
                        "QLabel#footerLabel { color: %4; font-size: 11px; }"
                        "QFrame#contentCard QScrollBar:vertical, QFrame#contentCard QScrollBar:horizontal { background: %1; border: none; }"
                        "QFrame#contentCard QScrollBar:vertical { width: 10px; margin: 0px; }"
                        "QFrame#contentCard QScrollBar:horizontal { height: 10px; margin: 0px; }"
                        "QFrame#contentCard QScrollBar::handle { background: %3; border-radius: 5px; }"
                        "QFrame#contentCard QScrollBar::handle:vertical { min-height: 24px; }"
                        "QFrame#contentCard QScrollBar::handle:horizontal { min-width: 24px; }"
                        "QFrame#contentCard QScrollBar::add-line, QFrame#contentCard QScrollBar::sub-line { background: %1; border: none; width: 0px; height: 0px; }"
                        "QFrame#contentCard QScrollBar::add-page, QFrame#contentCard QScrollBar::sub-page { background: %1; border: none; }"
                        "QFrame#contentCard QScrollBar::corner { background: %1; border: none; }"
                        "PlacesSidebar QScrollBar:vertical, PlacesSidebar QScrollBar:horizontal { background: %6; border: none; }"
                        "PlacesSidebar QScrollBar:vertical { width: 10px; margin: 0px; }"
                        "PlacesSidebar QScrollBar:horizontal { height: 10px; margin: 0px; }"
                        "PlacesSidebar QScrollBar::handle { background: %3; border-radius: 5px; }"
                        "PlacesSidebar QScrollBar::handle:vertical { min-height: 24px; }"
                        "PlacesSidebar QScrollBar::handle:horizontal { min-width: 24px; }"
                        "PlacesSidebar QScrollBar::add-line, PlacesSidebar QScrollBar::sub-line { background: %6; border: none; width: 0px; height: 0px; }"
                        "PlacesSidebar QScrollBar::add-page, PlacesSidebar QScrollBar::sub-page { background: %6; border: none; }"
                        "PlacesSidebar QScrollBar::corner { background: %6; border: none; }")
            .arg(cardColor.name(), borderColor.name(), handleColor.name(), footerTextColor.name(), hoverColor.name())
            .arg(windowColor.name()));
}

QAbstractItemView *MainWindow::currentView() const
{
    return m_viewStack->currentWidget() == m_gridView ? static_cast<QAbstractItemView *>(m_gridView)
                                                        : static_cast<QAbstractItemView *>(m_listView);
}

QList<QUrl> MainWindow::selectedUrls() const
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

void MainWindow::navigateTo(const QUrl &url, bool addToHistory)
{
    if (!url.isValid())
        return;

    m_currentUrl = url;
    m_dirLister->openUrl(url);
    m_addressBar->setText(url.toDisplayString(QUrl::PreferLocalFile));
    m_sidebar->setCurrentUrl(url);
    applySortForCurrentFolder();
    applyViewModeForCurrentFolder();
    const QString name = url.fileName();
    setWindowTitle(QStringLiteral("Minnow — %1").arg(name.isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : name));

    if (addToHistory) {
        m_history.resize(m_historyIndex + 1);
        m_history.append(url);
        m_historyIndex = m_history.size() - 1;
    }

    m_backButton->setEnabled(m_historyIndex > 0);
    m_forwardButton->setEnabled(m_historyIndex < m_history.size() - 1);
    m_upButton->setEnabled(parentOf(url) != url);
}

void MainWindow::goBack()
{
    if (m_historyIndex > 0) {
        --m_historyIndex;
        navigateTo(m_history[m_historyIndex], false);
    }
}

void MainWindow::goForward()
{
    if (m_historyIndex < m_history.size() - 1) {
        ++m_historyIndex;
        navigateTo(m_history[m_historyIndex], false);
    }
}

void MainWindow::goUp()
{
    const QUrl parent = parentOf(m_currentUrl);
    if (parent != m_currentUrl)
        navigateTo(parent);
}

void MainWindow::onAddressBarSubmitted()
{
    const QString text = m_addressBar->text().trimmed();
    if (text.isEmpty())
        return;

    const QUrl url = text.startsWith(QLatin1Char('/')) ? QUrl::fromLocalFile(text) : QUrl::fromUserInput(text);
    if (url.isValid())
        navigateTo(url);
}

void MainWindow::onItemActivated(const QModelIndex &proxyIndex)
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

void MainWindow::switchToGridView()
{
    m_viewStack->setCurrentWidget(m_gridView);
    setFolderIsGrid(true);
}

void MainWindow::switchToListView()
{
    m_viewStack->setCurrentWidget(m_listView);
    setFolderIsGrid(false);
}

void MainWindow::setFolderIsGrid(bool isGrid)
{
    if (!m_currentUrl.isValid())
        return;
    m_folderIsGrid[m_currentUrl.toString()] = isGrid;
    saveFolderViewModes();
}

void MainWindow::applyViewModeForCurrentFolder()
{
    const bool isGrid = m_folderIsGrid.value(m_currentUrl.toString(), true);
    m_viewStack->setCurrentWidget(isGrid ? static_cast<QWidget *>(m_gridView) : static_cast<QWidget *>(m_listView));
}

void MainWindow::loadFolderViewModes()
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

void MainWindow::saveFolderViewModes()
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

void MainWindow::onFilterTextChanged(const QString &text)
{
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterFixedString(text);
}

void MainWindow::setIconSize(int size)
{
    m_gridView->setIconSize(QSize(size, size));
    m_gridView->setGridSize(QSize(size + 48, size + 40));
    QSettings settings;
    settings.setValue(QStringLiteral("View/IconSize"), size);
}

void MainWindow::onSortIndicatorChanged(int column, Qt::SortOrder order)
{
    if (m_restoringSort || !m_currentUrl.isValid())
        return;
    saveFolderSort(m_currentUrl.toString(), column, order);
}

void MainWindow::applySortForCurrentFolder()
{
    const FolderSort fs = m_folderSort.value(m_currentUrl.toString());
    m_restoringSort = true;
    m_listView->header()->setSortIndicator(fs.column, fs.order);
    m_restoringSort = false;
}

void MainWindow::loadFolderSort()
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

void MainWindow::saveFolderSort(const QString &folderKey, int column, Qt::SortOrder order)
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

void MainWindow::updateStatusBar()
{
    const int count = m_proxyModel->rowCount();
    m_itemCountLabel->setText(tr("%n item(s)", "", count));

    const QString localPath = m_currentUrl.isLocalFile() ? m_currentUrl.toLocalFile() : QDir::homePath();
    QStorageInfo info(localPath);
    if (info.isValid()) {
        const double freeGb = info.bytesAvailable() / 1024.0 / 1024.0 / 1024.0;
        m_freeSpaceLabel->setText(tr("%1 GB free").arg(QString::number(freeGb, 'f', 1)));
    } else {
        m_freeSpaceLabel->clear();
    }
}

void MainWindow::showViewContextMenu(const QPoint &pos)
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

        if (!singleDirItem.isNull())
            addPinAction(tr("Pin to Sidebar"), singleDirItem.url());

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

        QAction *renameAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), tr("Rename"));
        connect(renameAction, &QAction::triggered, this, [view] {
            view->edit(view->currentIndex());
        });

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
    connect(iconsAction, &QAction::triggered, this, &MainWindow::switchToGridView);
    connect(listAction, &QAction::triggered, this, &MainWindow::switchToListView);

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
