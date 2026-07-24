#pragma once

#include <QHash>
#include <QUrl>
#include <QWidget>

class QListView;
class QTreeView;
class QStackedWidget;
class QAbstractItemView;
class KDirLister;
class KDirModel;
class KDirSortFilterProxyModel;
class PathBar;
class PlacesSidebar;

// One browsing context: its own directory listing/model, grid+list views,
// breadcrumb path bar, and back/forward/up history. MainWindow hosts several of
// these in a tab widget; the toolbar and sidebar act on whichever is active.
class BrowserTab : public QWidget
{
    Q_OBJECT

public:
    explicit BrowserTab(PlacesSidebar *sidebar, QWidget *parent = nullptr);

    void navigateTo(const QUrl &url);
    QUrl currentUrl() const { return m_currentUrl; }
    QList<QUrl> selectedUrls() const;
    QAbstractItemView *currentView() const;
    PathBar *pathBar() const { return m_pathBar; }

    bool canGoBack() const;
    bool canGoForward() const;
    bool canGoUp() const;
    void goBack();
    void goForward();
    void goUp();

    void switchToGridView();
    void switchToListView();
    void setIconSize(int size);
    void setFilterText(const QString &text);
    QString displayName() const;
    void activateCurrentItem();
    int itemCount() const;

signals:
    void urlChanged(const QUrl &url);
    void historyChanged();
    void statusChanged();
    void titleChanged();
    void openInNewTabRequested(const QUrl &url);
    void openInNewWindowRequested(const QUrl &url);

private slots:
    void onItemActivated(const QModelIndex &proxyIndex);
    void showViewContextMenu(const QPoint &pos);
    void onSortIndicatorChanged(int column, Qt::SortOrder order);
    void onUrlsDropped(const QUrl &destination, QDropEvent *event);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupViews();
    void loadUrl(const QUrl &url);
    void applySortForCurrentFolder();
    void applyViewModeForCurrentFolder();
    void setFolderIsGrid(bool isGrid);
    void loadFolderSort();
    void saveFolderSort(const QString &folderKey, int column, Qt::SortOrder order);
    void loadFolderViewModes();
    void saveFolderViewModes();

    PlacesSidebar *m_sidebar = nullptr;
    PathBar *m_pathBar = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QListView *m_gridView = nullptr;
    QTreeView *m_listView = nullptr;

    KDirLister *m_dirLister = nullptr;
    KDirModel *m_dirModel = nullptr;
    KDirSortFilterProxyModel *m_proxyModel = nullptr;

    QUrl m_currentUrl;
    QList<QUrl> m_history;
    int m_historyIndex = -1;

    struct FolderSort {
        int column = 0;
        Qt::SortOrder order = Qt::AscendingOrder;
    };
    QHash<QString, FolderSort> m_folderSort;
    bool m_restoringSort = false;
    QHash<QString, bool> m_folderIsGrid;
};
