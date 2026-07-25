#include "MainWindow.h"
#include "BrowserTab.h"
#include "FileOperations.h"
#include "PathBar.h"
#include "PlacesSidebar.h"
#include "SettingsTab.h"
#include "TabBar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <KIO/FileUndoManager>

MainWindow::MainWindow(const QUrl &startUrl, QWidget *parent)
    : QMainWindow(parent)
    , m_startUrl(startUrl)
{
    QSettings settings;
    const QSize savedSize = settings.value(QStringLiteral("MainWindow/Size")).toSize();
    resize(savedSize.width() > 0 && savedSize.height() > 0 ? savedSize : QSize(1120, 630));

    setupToolBar();
    setupSidebar();
    setupTabs();
    applyStyle();

    auto *central = new QWidget(this);
    auto *grid = new QGridLayout(central);
    grid->setContentsMargins(0, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(0);

    // Row 0, column 0 is an empty spacer above the sidebar so the tab bar (row 0, column 1)
    // lines up exactly with the content card's left edge (row 1, column 1) - the grid keeps
    // both rows' column widths in sync automatically, so the sidebar's own width drives it.
    grid->addWidget(new QWidget(central), 0, 0);
    grid->addWidget(m_tabBar, 0, 1);

    auto *sidebarContainer = new QWidget(central);
    auto *sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(m_sidebar, 1);

    auto *settingsButton = new QToolButton(sidebarContainer);
    settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    settingsButton->setToolTip(tr("Settings"));
    settingsButton->setAutoRaise(true);
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::openSettingsTab);
    sidebarLayout->addWidget(settingsButton);

    grid->addWidget(sidebarContainer, 1, 0);

    m_contentCard = new QFrame(this);
    m_contentCard->setObjectName(QStringLiteral("contentCard"));
    m_cardLayout = new QVBoxLayout(m_contentCard);
    m_cardLayout->setContentsMargins(6, 6, 6, 6);
    m_cardLayout->setSpacing(0);
    m_cardLayout->addWidget(m_tabStack, 1);
    grid->addWidget(m_contentCard, 1, 1);

    grid->setRowStretch(1, 1);
    grid->setColumnStretch(1, 1);

    setupStatusBar();
    setCentralWidget(central);

    setupShortcuts();

    addNewTab(m_startUrl.isValid() ? m_startUrl : QUrl::fromLocalFile(QDir::homePath()));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(QStringLiteral("MainWindow/Size"), size());
    QMainWindow::closeEvent(event);
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

    m_navigatorHost = new QWidget(this);
    m_navigatorHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_navigatorHostLayout = new QHBoxLayout(m_navigatorHost);
    m_navigatorHostLayout->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_navigatorHost);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(200);
    toolbar->addWidget(m_filterEdit);

    connect(m_backButton, &QToolButton::clicked, this, &MainWindow::goBack);
    connect(m_forwardButton, &QToolButton::clicked, this, &MainWindow::goForward);
    connect(m_upButton, &QToolButton::clicked, this, &MainWindow::goUp);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTextChanged);
}

void MainWindow::setupSidebar()
{
    m_sidebar = new PlacesSidebar(this);
    connect(m_sidebar, &PlacesSidebar::placeActivated, this, [this](const QUrl &url) {
        if (auto *tab = currentTab())
            tab->navigateTo(url);
    });
}

void MainWindow::setupTabs()
{
    m_tabBar = new TabBar(this);
    m_tabStack = new QStackedWidget(this);

    connect(m_tabBar, &TabBar::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(m_tabBar, &TabBar::tabCloseRequested, this, &MainWindow::closeTab);
}

BrowserTab *MainWindow::addNewTab(const QUrl &url, bool activate)
{
    auto *tab = new BrowserTab(m_sidebar, this);
    QSettings settings;
    tab->setIconSize(settings.value(QStringLiteral("View/IconSize"), 64).toInt());

    connect(tab, &BrowserTab::urlChanged, this, &MainWindow::onTabUrlChanged);
    connect(tab, &BrowserTab::historyChanged, this, &MainWindow::onTabHistoryChanged);
    connect(tab, &BrowserTab::statusChanged, this, &MainWindow::onTabStatusChanged);
    connect(tab, &BrowserTab::titleChanged, this, &MainWindow::onTabTitleChanged);
    connect(tab, &BrowserTab::openInNewTabRequested, this, [this](const QUrl &u) { addNewTab(u, false); });
    connect(tab, &BrowserTab::openInNewWindowRequested, this, &MainWindow::openNewWindow);

    m_tabStack->addWidget(tab);
    const int index = m_tabBar->addTab(tr("Loading…"));
    if (activate)
        m_tabBar->setCurrentIndex(index);
    tab->navigateTo(url);
    updateContentCardCorners();
    return tab;
}

void MainWindow::closeTab(int index)
{
    if (index < 0)
        return;
    if (m_tabStack->count() <= 1) {
        close();
        return;
    }
    QWidget *w = m_tabStack->widget(index);
    if (w == m_settingsTab)
        m_settingsTab = nullptr;
    m_tabBar->removeTab(index);
    m_tabStack->removeWidget(w);
    w->deleteLater();

    if (m_tabBar->currentIndex() < 0 && m_tabStack->count() > 0)
        m_tabBar->setCurrentIndex(qMin(index, m_tabStack->count() - 1));

    updateContentCardCorners();
}

BrowserTab *MainWindow::currentTab() const
{
    return qobject_cast<BrowserTab *>(m_tabStack->currentWidget());
}

void MainWindow::onCurrentTabChanged(int index)
{
    if (index < 0 || index >= m_tabStack->count())
        return;
    m_tabStack->setCurrentIndex(index);

    while (QLayoutItem *item = m_navigatorHostLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->hide();
        delete item;
    }
    if (BrowserTab *tab = currentTab()) {
        PathBar *nav = tab->pathBar();
        m_navigatorHostLayout->addWidget(nav);
        nav->show();

        const QSignalBlocker filterBlocker(m_filterEdit);
        m_filterEdit->setText(tab->filterText());
        m_filterEdit->setEnabled(true);
    } else {
        const QSignalBlocker filterBlocker(m_filterEdit);
        m_filterEdit->clear();
        m_filterEdit->setEnabled(false);
    }

    updateChromeForCurrentTab();
    updateContentCardCorners();
}

void MainWindow::openNewWindow(const QUrl &url)
{
    auto *window = new MainWindow(url);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void MainWindow::openSettingsTab()
{
    if (m_settingsTab) {
        const int idx = m_tabStack->indexOf(m_settingsTab);
        if (idx >= 0) {
            m_tabBar->setCurrentIndex(idx);
            return;
        }
        m_settingsTab = nullptr;
    }

    m_settingsTab = new SettingsTab(this);
    connect(m_settingsTab, &SettingsTab::showHiddenFilesChanged, this, [this](bool show) {
        for (int i = 0; i < m_tabStack->count(); ++i) {
            if (auto *tab = qobject_cast<BrowserTab *>(m_tabStack->widget(i)))
                tab->setShowHiddenFiles(show);
        }
    });
    connect(m_settingsTab, &SettingsTab::showThumbnailsChanged, this, [this](bool show) {
        for (int i = 0; i < m_tabStack->count(); ++i) {
            if (auto *tab = qobject_cast<BrowserTab *>(m_tabStack->widget(i)))
                tab->setShowThumbnails(show);
        }
    });
    connect(m_settingsTab, &SettingsTab::iconSizeChanged, this, [this](int size) {
        for (int i = 0; i < m_tabStack->count(); ++i) {
            if (auto *tab = qobject_cast<BrowserTab *>(m_tabStack->widget(i)))
                tab->setIconSize(size);
        }
    });

    m_tabStack->addWidget(m_settingsTab);
    const int index = m_tabBar->addTab(tr("Settings"));
    m_tabBar->setCurrentIndex(index);
    updateContentCardCorners();
}

void MainWindow::onTabUrlChanged(const QUrl &url)
{
    Q_UNUSED(url);
    if (qobject_cast<BrowserTab *>(sender()) == currentTab())
        updateChromeForCurrentTab();
}

void MainWindow::onTabHistoryChanged()
{
    if (qobject_cast<BrowserTab *>(sender()) == currentTab())
        updateChromeForCurrentTab();
}

void MainWindow::onTabStatusChanged()
{
    if (qobject_cast<BrowserTab *>(sender()) == currentTab())
        updateChromeForCurrentTab();
}

void MainWindow::onTabTitleChanged()
{
    auto *tab = qobject_cast<BrowserTab *>(sender());
    if (!tab)
        return;
    const int idx = m_tabStack->indexOf(tab);
    if (idx >= 0)
        m_tabBar->setTabText(idx, tab->displayName());
    if (tab == currentTab())
        updateChromeForCurrentTab();
}

void MainWindow::updateChromeForCurrentTab()
{
    BrowserTab *tab = currentTab();
    if (!tab) {
        m_backButton->setEnabled(false);
        m_forwardButton->setEnabled(false);
        m_upButton->setEnabled(false);
        setWindowTitle(tr("Settings"));
        m_itemCountLabel->clear();
        m_freeSpaceLabel->clear();
        return;
    }

    m_backButton->setEnabled(tab->canGoBack());
    m_forwardButton->setEnabled(tab->canGoForward());
    m_upButton->setEnabled(tab->canGoUp());
    setWindowTitle(tab->displayName());
    m_sidebar->setCurrentUrl(tab->currentUrl());

    const QList<QUrl> selected = tab->selectedUrls();
    if (selected.size() == 1)
        m_itemCountLabel->setText(selected.first().fileName());
    else
        m_itemCountLabel->setText(tr("%n item(s)", "", tab->itemCount()));
    const QString localPath = tab->currentUrl().isLocalFile() ? tab->currentUrl().toLocalFile() : QDir::homePath();
    QStorageInfo info(localPath);
    if (info.isValid()) {
        const double freeGb = info.bytesAvailable() / 1024.0 / 1024.0 / 1024.0;
        m_freeSpaceLabel->setText(tr("%1 GB free").arg(QString::number(freeGb, 'f', 1)));
    } else {
        m_freeSpaceLabel->clear();
    }
}

void MainWindow::goBack()
{
    if (auto *tab = currentTab())
        tab->goBack();
}

void MainWindow::goForward()
{
    if (auto *tab = currentTab())
        tab->goForward();
}

void MainWindow::goUp()
{
    if (auto *tab = currentTab())
        tab->goUp();
}

void MainWindow::onFilterTextChanged(const QString &text)
{
    if (auto *tab = currentTab())
        tab->setFilterText(text);
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

    m_cardLayout->addSpacing(6);
    m_cardLayout->addWidget(statusRow);
}

void MainWindow::setupShortcuts()
{
    // Scoped to this window (not ApplicationShortcut) so that with multiple windows open,
    // each one's shortcut fires unambiguously instead of Qt refusing to disambiguate two
    // identical app-wide shortcuts. uiInterface()->setParentWidget() is likewise called
    // right before each undo()/redo() - KIO::FileUndoManager is a single global instance
    // shared by every window, so its UI parent has to be (re-)pointed at whichever window
    // actually triggered the action, not fixed once at construction time.
    auto *undoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
    undoShortcut->setContext(Qt::WindowShortcut);
    connect(undoShortcut, &QShortcut::activated, this, [this] {
        KIO::FileUndoManager::self()->uiInterface()->setParentWidget(this);
        KIO::FileUndoManager::self()->undo();
    });

#ifdef MINNOW_HAVE_KIO_REDO
    auto *redoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
    redoShortcut->setContext(Qt::WindowShortcut);
    connect(redoShortcut, &QShortcut::activated, this, [this] {
        KIO::FileUndoManager::self()->uiInterface()->setParentWidget(this);
        KIO::FileUndoManager::self()->redo();
    });
#endif

    auto *newTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
    newTabShortcut->setContext(Qt::WindowShortcut);
    connect(newTabShortcut, &QShortcut::activated, this, [this] {
        addNewTab(currentTab() ? currentTab()->currentUrl() : QUrl::fromLocalFile(QDir::homePath()));
    });

    auto *closeTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    closeTabShortcut->setContext(Qt::WindowShortcut);
    connect(closeTabShortcut, &QShortcut::activated, this, [this] {
        closeTab(m_tabBar->currentIndex());
    });

    QList<QShortcut *> activateShortcuts;
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this] {
            if (auto *tab = currentTab())
                tab->activateCurrentItem();
        });
        activateShortcuts << shortcut;
    }
    // A WindowShortcut consumes the key event outright, regardless of which widget has
    // focus - it never even reaches a focused QLineEdit's own keyPressEvent. Disabling it
    // whenever a line edit (path bar edit line, search box) has focus lets Return reach
    // them normally instead of being stolen for "activate the selected item".
    connect(qApp, &QApplication::focusChanged, this, [activateShortcuts](QWidget *, QWidget *now) {
        const bool inLineEdit = qobject_cast<QLineEdit *>(now) != nullptr;
        for (QShortcut *shortcut : activateShortcuts)
            shortcut->setEnabled(!inLineEdit);
    });

    auto *trashShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    trashShortcut->setContext(Qt::WindowShortcut);
    connect(trashShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentTab()) {
            const QList<QUrl> urls = tab->selectedUrls();
            if (!urls.isEmpty())
                FileOperations::trash(urls, this);
        }
    });

    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete), this);
    deleteShortcut->setContext(Qt::WindowShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentTab()) {
            const QList<QUrl> urls = tab->selectedUrls();
            if (!urls.isEmpty())
                FileOperations::remove(urls, this);
        }
    });
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
                        "QFrame#contentCard { background: %1; border-left: 1px solid %2; border-right: 1px solid %2; "
                        "border-bottom: 1px solid %2; border-top: none; }"
                        "QLineEdit, PathBar { background: %1; border: 1px solid %2; border-radius: 10px; padding: 4px 10px; }"
                        "QToolButton { border-radius: 8px; padding: 4px; }"
                        "QToolButton:hover { background: %5; }"
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
                        "TabButton { background: %6; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
                        "TabButton[active=\"true\"] { background: %1; }"
                        "TabButton[active=\"false\"]:hover { background: %5; }"
                        "QToolButton#tabCloseButton { border: none; border-radius: 4px; background: transparent; }"
                        "QToolButton#tabCloseButton:hover { background: %3; }"
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

    updateContentCardCorners();
}

void MainWindow::updateContentCardCorners()
{
    if (!m_contentCard)
        return;

    const QColor windowColor = palette().color(QPalette::Window);
    const bool dark = windowColor.lightness() < 128;
    const QColor cardColor = dark ? windowColor.lighter(125) : windowColor.lighter(106);
    const QColor borderColor = dark ? windowColor.lighter(150) : windowColor.darker(112);

    // The top-left corner only stays square while the tab bar is visible AND the first tab
    // (the one sitting flush at the left) is the active one - any other state means nothing
    // is flush against that corner, so it should read as a normal rounded card corner.
    const bool tabBarVisible = m_tabBar->count() > 1;
    const bool firstTabActive = m_tabBar->currentIndex() == 0;
    const bool squareTopLeft = tabBarVisible && firstTabActive;

    m_contentCard->setStyleSheet(
        QStringLiteral("QFrame#contentCard { background: %1; border-left: 1px solid %2; border-right: 1px solid %2; "
                        "border-bottom: 1px solid %2; border-top: none; "
                        "border-top-left-radius: %3px; border-top-right-radius: 14px; "
                        "border-bottom-left-radius: 14px; border-bottom-right-radius: 14px; }")
            .arg(cardColor.name(), borderColor.name())
            .arg(squareTopLeft ? 0 : 14));
}
