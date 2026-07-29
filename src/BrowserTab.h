#pragma once

#include <QHash>
#include <QUrl>
#include <QWidget>

class QListView;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QAbstractItemView;
class QTimer;
class KDirLister;
class KDirModel;
class KFileItem;
class ThumbnailProxyModel;
class PathBar;
class PlacesSidebar;

namespace KIO
{
class ListJob;
class Job;
}

// one browsing context - own model/listing, grid+list views, path bar, back/forward history.
// MainWindow keeps a bunch of these in a tab widget and the toolbar/sidebar act on whichever's active
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
    QString filterText() const { return m_filterText; }
    QString displayName() const;
    void activateCurrentItem();
    int itemCount() const;

    bool showHiddenFiles() const { return m_showHiddenFiles; }
    void setShowHiddenFiles(bool show);
    bool showThumbnails() const { return m_showThumbnails; }
    void setShowThumbnails(bool show);

    void renameSelectionInteractive(); // used by both the context menu and the F2 shortcut

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
    void requestThumbnails(const QList<KFileItem> &items);
    void startSearch();
    void stopSearch();
    void activateSearchResult(QTreeWidgetItem *item);
    bool searchActive() const { return !m_filterText.isEmpty(); }

    PlacesSidebar *m_sidebar = nullptr;
    PathBar *m_pathBar = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QListView *m_gridView = nullptr;
    QTreeView *m_listView = nullptr;

    KDirLister *m_dirLister = nullptr;
    KDirModel *m_dirModel = nullptr;
    ThumbnailProxyModel *m_proxyModel = nullptr;

    QUrl m_currentUrl;
    QList<QUrl> m_history;
    int m_historyIndex = -1;
    QString m_filterText;
    bool m_showHiddenFiles = false;
    bool m_showThumbnails = true;

    QTreeWidget *m_searchResultsView = nullptr;
    QTreeWidgetItem *m_searchHereSection = nullptr;
    QTreeWidgetItem *m_searchSubfoldersSection = nullptr;
    KIO::ListJob *m_searchJob = nullptr;
    QTimer *m_searchDebounceTimer = nullptr;

    struct FolderSort {
        int column = 0;
        Qt::SortOrder order = Qt::AscendingOrder;
    };
    QHash<QString, FolderSort> m_folderSort;
    bool m_restoringSort = false;
    QHash<QString, bool> m_folderIsGrid;
};
