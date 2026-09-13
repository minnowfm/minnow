#include "MainWindow.h"
#include "ActivityTab.h"
#include "BrowserTab.h"
#include "DiskUsageIndicator.h"
#include "FileManagerAdaptor.h"
#include "FileOperations.h"
#include "PathBar.h"
#include "PathUtils.h"
#include "PlacesSidebar.h"
#include "SettingsTab.h"
#include "TabBar.h"
#include "TaskManager.h"
#include "TaskProgressPopup.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStorageInfo>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <KFileItem>
#include <KIO/FileUndoManager>
#include <KIO/StatJob>
#include <KStartupInfo>

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
    auto *outerLayout = new QVBoxLayout(central);
    outerLayout->setContentsMargins(0, 8, 8, 8);
    outerLayout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false); // dragging all the way in shouldn't be able to hide the sidebar

    auto *sidebarContainer = new QWidget(splitter);
    sidebarContainer->setMinimumWidth(160); // the actual "can't make it too small" floor
    auto *sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(m_sidebar, 1);
    m_sidebarLayout = sidebarLayout;

    auto *bottomRow = new QWidget(sidebarContainer);
    auto *bottomRowLayout = new QHBoxLayout(bottomRow);
    bottomRowLayout->setContentsMargins(0, 0, 0, 0);

    auto *settingsButton = new QToolButton(bottomRow);
    settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    settingsButton->setToolTip(tr("Settings"));
    settingsButton->setAutoRaise(true);
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::openSettingsTab);
    bottomRowLayout->addWidget(settingsButton);

    bottomRowLayout->addStretch(1);

    m_tasksButton = new QToolButton(bottomRow);
    m_tasksButton->setIcon(QIcon::fromTheme(QStringLiteral("download")));
    m_tasksButton->setToolTip(tr("Activity"));
    m_tasksButton->setAutoRaise(true);
    connect(m_tasksButton, &QToolButton::clicked, this, &MainWindow::showTaskPopup);
    bottomRowLayout->addWidget(m_tasksButton);

    sidebarLayout->addWidget(bottomRow);

    splitter->addWidget(sidebarContainer);

    rebuildDiskSummary();

    // tab bar + content card share the splitter's right pane, stacked vertically, so they stay
    // aligned with each other without needing the old grid's column-syncing trick
    auto *rightPane = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_tabBar);

    m_contentCard = new QFrame(rightPane);
    m_contentCard->setObjectName(QStringLiteral("contentCard"));
    m_cardLayout = new QVBoxLayout(m_contentCard);
    m_cardLayout->setContentsMargins(6, 6, 6, 6);
    m_cardLayout->setSpacing(0);
    m_cardLayout->addWidget(m_tabStack, 1);
    rightLayout->addWidget(m_contentCard, 1);

    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    const int sidebarWidth = settings.value(QStringLiteral("MainWindow/SidebarWidth"), 200).toInt();
    splitter->setSizes({sidebarWidth, width() - sidebarWidth});
    connect(splitter, &QSplitter::splitterMoved, this, [splitter] {
        QSettings settings;
        settings.setValue(QStringLiteral("MainWindow/SidebarWidth"), splitter->sizes().constFirst());
    });

    outerLayout->addWidget(splitter);

    setupStatusBar();
    setCentralWidget(central);

    setupShortcuts();

    addNewTab(m_startUrl.isValid() ? m_startUrl : QUrl::fromLocalFile(QDir::homePath()));

    // Claims org.freedesktop.FileManager1 so a browser's "Show in folder" reaches Minnow. On a
    // real KDE Plasma session this name is *already* owned at all times by a persistent
    // "dolphin --daemon" tied to plasma-dolphin.service (a systemd user service, not just
    // lazy D-Bus activation) - a plain registerService() silently loses that race every time,
    // running or not. ReplaceExistingService/AllowReplacement contests it instead of just
    // trying once and giving up (though Dolphin's own daemon doesn't actually allow replacement
    // in practice - see README "Known limitations"). interface() can be null when no usable
    // D-Bus session exists at all (a minimal WM, a standalone display) - skip quietly instead
    // of crashing the whole window, same as a plain registerService() would have degraded.
    new FileManagerAdaptor(this);
    if (QDBusConnectionInterface *busInterface = QDBusConnection::sessionBus().interface()) {
        QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/freedesktop/FileManager1"), this);
        busInterface->registerService(QStringLiteral("org.freedesktop.FileManager1"),
                                       QDBusConnectionInterface::ReplaceExistingService,
                                       QDBusConnectionInterface::AllowReplacement);
    }
}

// Consumes the D-Bus caller's startup notification token (if any) before raising the window,
// so the window manager honors this specific activation instead of treating it as unsolicited
// focus-stealing - without it, raise()/activateWindow() can get silently ignored or just flash
// the taskbar entry, especially under Wayland's stricter policies.
static void activateWithStartupId(QWidget *window, const QString &startupId)
{
    if (!startupId.isEmpty()) {
        if (QWindow *handle = window->windowHandle())
            KStartupInfo::setNewStartupId(handle, startupId.toUtf8());
    }
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::revealFolder(const QUrl &folderUrl, const QString &startupId)
{
    if (!folderUrl.isValid())
        return;
    activateWithStartupId(this, startupId);
    addNewTab(folderUrl);
}

void MainWindow::revealItem(const QUrl &itemUrl, const QString &startupId)
{
    if (!itemUrl.isValid())
        return;
    activateWithStartupId(this, startupId);
    if (BrowserTab *tab = addNewTab(parentOf(itemUrl)))
        tab->selectAndReveal(itemUrl);
}

void MainWindow::revealItemProperties(const QUrl &itemUrl, const QString &startupId)
{
    if (!itemUrl.isValid())
        return;
    activateWithStartupId(this, startupId);

    // KFileItem(url) alone has no stat info (size/date/permissions/owner all unknown) - the
    // properties dialog needs the real thing, same as it gets from a normal directory listing.
    KIO::StatJob *job = KIO::stat(itemUrl, KIO::HideProgressInfo);
    connect(job, &KIO::StatJob::result, this, [this, job] {
        if (job->error())
            return;
        FileOperations::showProperties({KFileItem(job->statResult(), job->url())}, this);
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // hide instead of closing while a job is still running in the background, actually
    // close once everything's done - don't want to cut off a copy/compress mid-flight
    if (TaskManager::self()->hasActiveTasks()) {
        event->ignore();
        hide();
        if (!m_waitingForTasksToQuit) {
            m_waitingForTasksToQuit = true;
            connect(TaskManager::self(), &TaskManager::tasksChanged, this, [this] {
                if (m_waitingForTasksToQuit && !TaskManager::self()->hasActiveTasks())
                    close();
            });
        }
        return;
    }

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

    m_emptyTrashButton = new QToolButton(this);
    m_emptyTrashButton->setIcon(QIcon::fromTheme(QStringLiteral("user-trash")));
    m_emptyTrashButton->setToolTip(tr("Empty Trash"));

    toolbar->addWidget(m_backButton);
    toolbar->addWidget(m_forwardButton);
    toolbar->addWidget(m_upButton);
    // toggle m_emptyTrashAction's visibility, not the button's own hide()/show() - QToolBar wraps
    // addWidget() widgets in its own QWidgetAction and manages visibility through that, so toggling
    // the widget directly gets silently overridden on the next toolbar layout pass
    m_emptyTrashAction = toolbar->addWidget(m_emptyTrashButton);
    m_emptyTrashAction->setVisible(false); // only shown while browsing trash:/, see updateChromeForCurrentTab()

    m_navigatorHost = new QWidget(this);
    m_navigatorHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_navigatorHostLayout = new QHBoxLayout(m_navigatorHost);
    m_navigatorHostLayout->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(m_navigatorHost);

    // Shown instead of a real PathBar while Settings/Activity is the current tab - browsers
    // still show something in the address bar on an internal page instead of leaving it blank.
    m_pseudoPathLabel = new QLabel(this);
    m_pseudoPathLabel->setObjectName(QStringLiteral("pseudoPathLabel"));
    m_pseudoPathLabel->hide();

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(200);
    toolbar->addWidget(m_filterEdit);

    connect(m_backButton, &QToolButton::clicked, this, &MainWindow::goBack);
    connect(m_forwardButton, &QToolButton::clicked, this, &MainWindow::goForward);
    connect(m_upButton, &QToolButton::clicked, this, &MainWindow::goUp);
    connect(m_emptyTrashButton, &QToolButton::clicked, this, [this] { FileOperations::emptyTrash(this); });
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

// Builds the "/" disk-usage summary and places it above the sidebar, above the settings/activity
// row, or not at all, per Sidebar/DiskSummaryPosition. Just "/" - "/home" isn't shown here since
// it already gets its own inline indicator on the "Home" place item in PlacesSidebar.
// Torn down and rebuilt from scratch each time rather than updated in place - it's just a couple
// of small widgets, and this keeps position/style changes from needing separate code paths.
void MainWindow::rebuildDiskSummary()
{
    delete m_diskSummaryContainer;
    m_diskSummaryContainer = nullptr;

    QSettings settings;
    const auto position = static_cast<SettingsTab::DiskSummaryPosition>(
        settings.value(QStringLiteral("Sidebar/DiskSummaryPosition"), static_cast<int>(SettingsTab::DiskSummaryPosition::Hidden)).toInt());
    if (position == SettingsTab::DiskSummaryPosition::Hidden)
        return;

    const auto style = settings.value(QStringLiteral("Sidebar/DiskUsageStyle"), static_cast<int>(DiskUsageIndicator::Style::Text)).toInt()
                == static_cast<int>(DiskUsageIndicator::Style::Bar)
        ? DiskUsageIndicator::Style::Bar
        : DiskUsageIndicator::Style::Text;

    m_diskSummaryContainer = new QWidget();
    auto *layout = new QVBoxLayout(m_diskSummaryContainer);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    auto *nameLabel = new QLabel(tr("Root (/)"), m_diskSummaryContainer);
    QFont font = nameLabel->font();
    font.setPointSizeF(font.pointSizeF() * 0.82);
    font.setBold(true);
    nameLabel->setFont(font);
    layout->addWidget(nameLabel);

    auto *indicator = new DiskUsageIndicator(m_diskSummaryContainer);
    indicator->setStyle(style);
    indicator->setPath(QStringLiteral("/"));
    layout->addWidget(indicator);

    if (position == SettingsTab::DiskSummaryPosition::Top)
        m_sidebarLayout->insertWidget(0, m_diskSummaryContainer);
    else
        m_sidebarLayout->insertWidget(m_sidebarLayout->count() - 1, m_diskSummaryContainer); // just above bottomRow
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
    if (w == m_activityTab)
        m_activityTab = nullptr;
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
        m_pseudoPathLabel->setText(m_tabStack->currentWidget() == m_activityTab ? tr("Activity") : tr("Settings"));
        m_navigatorHostLayout->addWidget(m_pseudoPathLabel);
        m_pseudoPathLabel->show();

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
    connect(m_settingsTab, &SettingsTab::diskUsageEnabledChanged, this, [this](bool enabled) {
        m_sidebar->setDriveDiskUsageEnabled(enabled);
    });
    connect(m_settingsTab, &SettingsTab::diskUsageStyleChanged, this, [this](DiskUsageIndicator::Style style) {
        m_sidebar->setDriveDiskUsageStyle(style);
        rebuildDiskSummary();
    });
    connect(m_settingsTab, &SettingsTab::diskSummaryPositionChanged, this, [this] { rebuildDiskSummary(); });

    m_tabStack->addWidget(m_settingsTab);
    const int index = m_tabBar->addTab(tr("Settings"));
    m_tabBar->setCurrentIndex(index);
    updateContentCardCorners();
}

void MainWindow::openActivityTab()
{
    if (m_activityTab) {
        const int idx = m_tabStack->indexOf(m_activityTab);
        if (idx >= 0) {
            m_tabBar->setCurrentIndex(idx);
            return;
        }
        m_activityTab = nullptr;
    }

    m_activityTab = new ActivityTab(this);
    m_tabStack->addWidget(m_activityTab);
    const int index = m_tabBar->addTab(tr("Activity"));
    m_tabBar->setCurrentIndex(index);
    updateContentCardCorners();
}

void MainWindow::showTaskPopup()
{
    auto *popup = new TaskProgressPopup(this);
    connect(popup, &TaskProgressPopup::showMoreRequested, this, &MainWindow::openActivityTab);

    // anchor to the button's left edge, not right-aligned - sidebar's narrower than the
    // popup so right-aligning would run it off the window edge instead of over the content area
    const QPoint buttonTopLeft = m_tasksButton->mapToGlobal(QPoint(0, 0));
    const int popupHeight = popup->sizeHint().height();
    popup->move(buttonTopLeft.x(), buttonTopLeft.y() - popupHeight);
    popup->show();
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
        m_emptyTrashAction->setVisible(false);
        setWindowTitle(m_tabStack->currentWidget() == m_activityTab ? tr("Activity") : tr("Settings"));
        m_itemCountLabel->clear();
        m_freeSpaceLabel->clear();
        return;
    }

    m_backButton->setEnabled(tab->canGoBack());
    m_forwardButton->setEnabled(tab->canGoForward());
    m_upButton->setEnabled(tab->canGoUp());
    m_emptyTrashAction->setVisible(tab->currentUrl().scheme() == QLatin1String("trash"));
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
    // WindowShortcut, not ApplicationShortcut - with two windows open, an app-wide shortcut
    // is ambiguous and Qt just refuses to fire it. setParentWidget() gets called again right
    // before undo()/redo() below since FileUndoManager is one global instance shared by every
    // window - has to be re-pointed at whichever window actually triggered it.
    auto *undoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
    undoShortcut->setContext(Qt::WindowShortcut);
    connect(undoShortcut, &QShortcut::activated, this, [this] {
        // Cut (Ctrl+X) never pushes anything onto the undo stack by itself - cutToClipboard()
        // just marks the clipboard, no KIO job runs until something's actually pasted. So if
        // there's a cut still pending, treat Ctrl+Z as "cancel that cut" instead of reaching
        // past it into whatever real file operation came before.
        if (!FileOperations::cutClipboardUrls().isEmpty()) {
            QApplication::clipboard()->clear();
            return;
        }
        // isUndoAvailable() guard is load-bearing, not an optimization - calling undo() with
        // nothing on the stack crashes (SEGV) inside KIO itself instead of just no-op'ing.
        if (!KIO::FileUndoManager::self()->isUndoAvailable())
            return;
        KIO::FileUndoManager::self()->uiInterface()->setParentWidget(this);
        KIO::FileUndoManager::self()->undo();
    });

#ifdef MINNOW_HAVE_KIO_REDO
    auto *redoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
    redoShortcut->setContext(Qt::WindowShortcut);
    connect(redoShortcut, &QShortcut::activated, this, [this] {
        if (!KIO::FileUndoManager::self()->isRedoAvailable())
            return; // same crash risk as undo() above with nothing to redo
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

    auto *cutShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_X), this);
    cutShortcut->setContext(Qt::WindowShortcut);
    connect(cutShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentTab())
            FileOperations::cutToClipboard(tab->selectedUrls());
    });
    activateShortcuts << cutShortcut;

    auto *copyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), this);
    copyShortcut->setContext(Qt::WindowShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentTab())
            FileOperations::copyToClipboard(tab->selectedUrls());
    });
    activateShortcuts << copyShortcut;

    auto *pasteShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_V), this);
    pasteShortcut->setContext(Qt::WindowShortcut);
    connect(pasteShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentTab())
            FileOperations::pasteClipboard(tab->currentUrl(), this);
    });
    activateShortcuts << pasteShortcut;

    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    renameShortcut->setContext(Qt::WindowShortcut);
    connect(renameShortcut, &QShortcut::activated, this, [this] {
        if (auto *tab = currentTab())
            tab->renameSelectionInteractive();
    });
    activateShortcuts << renameShortcut;

    // WindowShortcut swallows the key event no matter who has focus - a focused QLineEdit
    // never even sees it. disable these while any line edit has focus so Ctrl+C/V/X, F2 etc
    // work normally there instead of getting hijacked for the file list
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
            if (urls.isEmpty())
                return;
            // already in Trash - "move to trash" a second time doesn't mean anything, so Delete
            // here does what Shift+Delete does everywhere else instead
            if (tab->currentUrl().scheme() == QLatin1String("trash"))
                FileOperations::remove(urls, this);
            else
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
                        "QSplitter::handle { background: transparent; }"
                        "QFrame#contentCard { background: %1; border-left: 1px solid %2; border-right: 1px solid %2; "
                        "border-bottom: 1px solid %2; border-top: none; }"
                        "QLineEdit, PathBar { background: %1; border: 1px solid %2; border-radius: 10px; padding: 4px 10px; }"
                        "QLabel#pseudoPathLabel { background: %1; border: 1px solid %2; border-radius: 10px; padding: 4px 10px; }"
                        "QComboBox { background: %1; border: 1px solid %2; border-radius: 10px; padding: 4px 10px; }"
                        "QComboBox:hover { background: %5; }"
                        "QComboBox::drop-down { border: none; width: 20px; }"
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

    // only square off the top-left corner when the tab bar is showing and tab 0 is active -
    // that's the only case where something actually sits flush against that corner
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
