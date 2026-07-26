#pragma once

#include <QWidget>

// Full-page view of TaskManager's activity list - what "Show More" in TaskProgressPopup
// opens (see MainWindow::openActivityTab()), same pattern as SettingsTab.
class ActivityTab : public QWidget
{
    Q_OBJECT

public:
    explicit ActivityTab(QWidget *parent = nullptr);
};
