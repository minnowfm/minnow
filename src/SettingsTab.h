#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;

// hidden files / thumbnails / icon size / terminal defaults, backed by QSettings.
// it's a regular tab (MainWindow::openSettingsTab()), not a dialog - changes apply live, no OK button
class SettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsTab(QWidget *parent = nullptr);

signals:
    void showHiddenFilesChanged(bool show);
    void showThumbnailsChanged(bool show);
    void iconSizeChanged(int size);

private:
    QCheckBox *m_hiddenFilesCheck = nullptr;
    QCheckBox *m_thumbnailsCheck = nullptr;
    QComboBox *m_iconSizeCombo = nullptr;
    QComboBox *m_terminalCombo = nullptr;
    QCheckBox *m_confirmDeleteCheck = nullptr;
};
