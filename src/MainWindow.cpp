#include "MainWindow.h"
#include "FileOperations.h"
#include "PlacesSidebar.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStorageInfo>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

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
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(960, 620);

    setupToolBar();
    setupSidebar();
    setupViews();
    setupStatusBar();
    applyStyle();

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_sidebar);
    layout->addWidget(m_viewStack, 1);
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

    m_gridViewButton = new QToolButton(this);
    m_gridViewButton->setIcon(QIcon::fromTheme(QStringLiteral("view-list-icons")));
    m_gridViewButton->setToolTip(tr("Grid view"));
    m_gridViewButton->setCheckable(true);
    m_gridViewButton->setChecked(true);

    m_listViewButton = new QToolButton(this);
    m_listViewButton->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details")));
    m_listViewButton->setToolTip(tr("List view"));
    m_listViewButton->setCheckable(true);

    toolbar->addWidget(m_gridViewButton);
    toolbar->addWidget(m_listViewButton);

    connect(m_backButton, &QToolButton::clicked, this, &MainWindow::goBack);
    connect(m_forwardButton, &QToolButton::clicked, this, &MainWindow::goForward);
    connect(m_upButton, &QToolButton::clicked, this, &MainWindow::goUp);
    connect(m_addressBar, &QLineEdit::returnPressed, this, &MainWindow::onAddressBarSubmitted);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTextChanged);
    connect(m_gridViewButton, &QToolButton::clicked, this, &MainWindow::switchToGridView);
    connect(m_listViewButton, &QToolButton::clicked, this, &MainWindow::switchToListView);
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
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_gridView->setFrameShape(QFrame::NoFrame);

    m_listView = new QTreeView(this);
    m_listView->setModel(m_proxyModel);
    m_listView->setRootIsDecorated(false);
    m_listView->setItemsExpandable(false);
    m_listView->setSortingEnabled(true);
    m_listView->setAllColumnsShowFocus(true);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->sortByColumn(0, Qt::AscendingOrder);

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_gridView);
    m_viewStack->addWidget(m_listView);
    m_viewStack->setCurrentWidget(m_gridView);

    connect(m_gridView, &QAbstractItemView::activated, this, &MainWindow::onItemActivated);
    connect(m_listView, &QAbstractItemView::activated, this, &MainWindow::onItemActivated);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, &MainWindow::showViewContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, &MainWindow::showViewContextMenu);
    connect(m_dirLister, &KCoreDirLister::completed, this, &MainWindow::updateStatusBar);
}

void MainWindow::setupStatusBar()
{
    m_itemCountLabel = new QLabel(this);
    m_freeSpaceLabel = new QLabel(this);
    statusBar()->addWidget(m_itemCountLabel);
    statusBar()->addPermanentWidget(m_freeSpaceLabel);
}

void MainWindow::applyStyle()
{
    setStyleSheet(
        QStringLiteral("QLineEdit { border-radius: 10px; padding: 4px 10px; }"
                        "QToolButton { border-radius: 8px; padding: 4px; }"
                        "QListWidget::item { border-radius: 10px; padding: 5px 8px; margin: 1px 4px; }"
                        "QListWidget::item:selected { border-radius: 10px; }"
                        "QListView { border: none; }"
                        "QListView::item { border-radius: 12px; padding: 6px; }"
                        "QListView::item:selected, QListView::item:hover { border-radius: 12px; }"
                        "QTreeView { border: none; }"
                        "QTreeView::item { border-radius: 10px; }"
                        "QTreeView::item:selected, QTreeView::item:hover { border-radius: 10px; }"));
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
    m_gridViewButton->setChecked(true);
    m_listViewButton->setChecked(false);
}

void MainWindow::switchToListView()
{
    m_viewStack->setCurrentWidget(m_listView);
    m_listViewButton->setChecked(true);
    m_gridViewButton->setChecked(false);
}

void MainWindow::onFilterTextChanged(const QString &text)
{
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterFixedString(text);
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
    QMenu menu(this);
    const QList<QUrl> selected = selectedUrls();

    if (!selected.isEmpty()) {
        QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), tr("Open"));
        connect(openAction, &QAction::triggered, this, [this, selected] {
            for (const QUrl &url : selected)
                FileOperations::openUrl(url, this);
        });

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

    QAction *newFolderAction = menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), tr("New Folder"));
    connect(newFolderAction, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, tr("New Folder"), &ok);
        if (ok && !name.isEmpty())
            FileOperations::mkdir(m_currentUrl, name, this);
    });

    menu.exec(view->viewport()->mapToGlobal(pos));
}
