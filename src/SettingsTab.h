#pragma once

#include "DiskUsageIndicator.h"

#include <QWidget>

class QCheckBox;
class QComboBox;

// hidden files / thumbnails / icon size / terminal defaults, backed by QSettings.
// it's a regular tab (MainWindow::openSettingsTab()), not a dialog - changes apply live, no OK button
class SettingsTab : public QWidget
{
    Q_OBJECT

public:
    enum class DiskSummaryPosition { Top, Bottom, Hidden };

    explicit SettingsTab(QWidget *parent = nullptr);

signals:
    void showHiddenFilesChanged(bool show);
    void showThumbnailsChanged(bool show);
    void iconSizeChanged(int size);
    void diskUsageEnabledChanged(bool enabled);
    void diskUsageStyleChanged(DiskUsageIndicator::Style style);
    void diskSummaryPositionChanged(SettingsTab::DiskSummaryPosition position);

private:
    QCheckBox *m_hiddenFilesCheck = nullptr;
    QCheckBox *m_thumbnailsCheck = nullptr;
    QComboBox *m_iconSizeCombo = nullptr;
    QComboBox *m_terminalCombo = nullptr;
    QCheckBox *m_confirmDeleteCheck = nullptr;
    QCheckBox *m_diskUsageCheck = nullptr;
    QComboBox *m_diskUsageStyleCombo = nullptr;
    QComboBox *m_diskSummaryPositionCombo = nullptr;
};
