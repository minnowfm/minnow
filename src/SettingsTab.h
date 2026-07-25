#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;

// Global defaults (QSettings-backed) for hidden files, thumbnails, icon size, and the
// preferred terminal. Lives as an ordinary tab (see MainWindow::openSettingsTab()) rather
// than a modal dialog, so every change applies immediately instead of behind an OK button.
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
