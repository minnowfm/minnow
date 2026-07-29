#pragma once

#include <QFrame>

// downloads-flyout-style list of active/recent operations, Qt::Popup so outside clicks close it.
// opened from the sidebar activity button, see MainWindow::showTaskPopup()
class TaskProgressPopup : public QFrame
{
    Q_OBJECT

public:
    explicit TaskProgressPopup(QWidget *parent = nullptr);

signals:
    void showMoreRequested();
};
