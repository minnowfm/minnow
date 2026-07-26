#pragma once

#include <QFrame>

// A small browser-downloads-style flyout listing active/recent file operations with their
// progress. Uses Qt::Popup so it closes on any outside click, same as a native menu. Opened
// from the sidebar's activity button (see MainWindow::showTaskPopup()).
class TaskProgressPopup : public QFrame
{
    Q_OBJECT

public:
    explicit TaskProgressPopup(QWidget *parent = nullptr);

signals:
    void showMoreRequested();
};
