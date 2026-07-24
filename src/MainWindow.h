#pragma once

#include <QHash>
#include <QMainWindow>
#include <QUrl>
#include <QVector>

class QToolButton;
class QLineEdit;
class QStackedWidget;
class QListView;
class QTreeView;
class QLabel;
class QAbstractItemView;
class QVBoxLayout;
class KDirLister;
class KDirModel;
class KDirSortFilterProxyModel;
class PlacesSidebar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private Q_SLOTS:
    void navigateTo(const QUrl &url, bool addToHistory = true);
    void goBack();
    void goForward();
    void goUp();
    void onAddressBarSubmitted();
    void onItemActivated(const QModelIndex &proxyIndex);
    void showViewContextMenu(const QPoint &pos);
    void switchToGridView();
    void switchToListView();
    void updateStatusBar();
    void onFilterTextChanged(const QString &text);
    void setIconSize(int size);
    void onSortIndicatorChanged(int column, Qt::SortOrder order);

private:
    void setupToolBar();
    void setupSidebar();
    void setupViews();
    void setupStatusBar();
    void applyStyle();
    QList<QUrl> selectedUrls() const;
    QAbstractItemView *currentView() const;
    void applySortForCurrentFolder();

    QToolButton *m_backButton = nullptr;
    QToolButton *m_forwardButton = nullptr;
    QToolButton *m_upButton = nullptr;
    QLineEdit *m_addressBar = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QToolButton *m_gridViewButton = nullptr;
    QToolButton *m_listViewButton = nullptr;
    QToolButton *m_iconSizeButton = nullptr;
    QToolButton *m_sortByButton = nullptr;

    PlacesSidebar *m_sidebar = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QListView *m_gridView = nullptr;
    QTreeView *m_listView = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;

    KDirLister *m_dirLister = nullptr;
    KDirModel *m_dirModel = nullptr;
    KDirSortFilterProxyModel *m_proxyModel = nullptr;

    QLabel *m_itemCountLabel = nullptr;
    QLabel *m_freeSpaceLabel = nullptr;

    QVector<QUrl> m_history;
    int m_historyIndex = -1;
    QUrl m_currentUrl;

    struct FolderSort {
        int column = 0;
        Qt::SortOrder order = Qt::AscendingOrder;
    };
    QHash<QString, FolderSort> m_folderSort;
    bool m_restoringSort = false;

    void loadFolderSort();
    void saveFolderSort(const QString &folderKey, int column, Qt::SortOrder order);
};
