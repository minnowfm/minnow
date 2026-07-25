#pragma once

#include <KFileItem>
#include <KFileMetaData/Properties>
#include <QDialog>

class QLabel;
class QStackedWidget;
class QToolButton;

namespace KIO
{
class DirectorySizeJob;
}

// Custom replacement for KPropertiesDialog: a left icon rail (General / Permissions /
// Media / Contents) instead of tabs, KPI tiles and a radial gauge for size/free-space,
// permissions rendered as owner/group/others x read/write/execute badges, and - for a
// single image/audio/video file - a Media pane populated via KFileMetaData (the same
// extraction Baloo/Dolphin use) rather than anything hand-rolled. Read-only - renaming
// and permission editing still go through the sidebar/context-menu actions that already
// exist rather than being duplicated here.
class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PropertiesDialog(const KFileItemList &items, QWidget *parent = nullptr);

private:
    QWidget *buildHero();
    QWidget *buildGeneralPane();
    QWidget *buildPermissionsPane();
    QWidget *buildContentsPane();
    QWidget *buildMediaPane();
    void startSizeCalculation();
    void applySize(quint64 bytes, quint64 files, quint64 subdirs, bool stillCalculating);
    void extractMediaProperties();

    KFileItemList m_items;
    KFileItem m_item; // primary/only item; null when m_items.size() != 1
    KFileMetaData::PropertyMultiMap m_mediaProps;
    QColor m_accent;

    QStackedWidget *m_stack = nullptr;
    QLabel *m_sizeTileValue = nullptr;
    QLabel *m_itemsTileValue = nullptr;
    QLabel *m_contentsFiles = nullptr;
    QLabel *m_contentsSubdirs = nullptr;
    QLabel *m_contentsSize = nullptr;

    KIO::DirectorySizeJob *m_sizeJob = nullptr;
};
