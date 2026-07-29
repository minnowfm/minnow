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

// replaces KPropertiesDialog with our own look: icon rail instead of tabs, tiles + a radial
// gauge for size/free-space, rwx badges for permissions, and a Media pane (KFileMetaData,
// same as Baloo/Dolphin) for single image/audio/video files. read-only - actual renaming and
// permission changes still go through the sidebar/context menu, not duplicated here
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
