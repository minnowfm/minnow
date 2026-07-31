#pragma once

#include <QMainWindow>
#include <QUrl>

class QToolButton;
class QLineEdit;
class QLabel;
class QFrame;
class QCloseEvent;
class QStackedWidget;
class QVBoxLayout;
class QHBoxLayout;
class PlacesSidebar;
class BrowserTab;
class TabBar;
class SettingsTab;
class ActivityTab;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QUrl &startUrl = QUrl(), QWidget *parent = nullptr);

    // Entry points for FileManagerAdaptor (org.freedesktop.FileManager1) - opens a new tab at
    // the given/containing folder, bringing the window to front. revealItem() additionally
    // selects the item once the folder's listed; revealItemProperties() opens its Properties
    // dialog instead of selecting it.
    void revealFolder(const QUrl &folderUrl);
    void revealItem(const QUrl &itemUrl);
    void revealItemProperties(const QUrl &itemUrl);

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onTabUrlChanged(const QUrl &url);
    void onTabHistoryChanged();
    void onTabStatusChanged();
    void onTabTitleChanged();
    void onCurrentTabChanged(int index);
    void onFilterTextChanged(const QString &text);
    void goBack();
    void goForward();
    void goUp();
    void openNewWindow(const QUrl &url);
    void openSettingsTab();
    void openActivityTab();
    void showTaskPopup();

private:
    void setupToolBar();
    void setupSidebar();
    void setupTabs();
    void setupStatusBar();
    void setupShortcuts();
    void applyStyle();
    BrowserTab *currentTab() const;
    BrowserTab *addNewTab(const QUrl &url, bool activate = true);
    void closeTab(int index);
    void updateChromeForCurrentTab();
    void updateContentCardCorners();

    QToolButton *m_backButton = nullptr;
    QToolButton *m_forwardButton = nullptr;
    QToolButton *m_upButton = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QWidget *m_navigatorHost = nullptr;
    QHBoxLayout *m_navigatorHostLayout = nullptr;

    PlacesSidebar *m_sidebar = nullptr;
    TabBar *m_tabBar = nullptr;
    QStackedWidget *m_tabStack = nullptr;
    QFrame *m_contentCard = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;

    QLabel *m_itemCountLabel = nullptr;
    QLabel *m_freeSpaceLabel = nullptr;

    SettingsTab *m_settingsTab = nullptr;
    ActivityTab *m_activityTab = nullptr;
    QToolButton *m_tasksButton = nullptr;
    bool m_waitingForTasksToQuit = false;

    QUrl m_startUrl;
};
