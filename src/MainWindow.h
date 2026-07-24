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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QUrl &startUrl = QUrl(), QWidget *parent = nullptr);

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

    QUrl m_startUrl;
};
