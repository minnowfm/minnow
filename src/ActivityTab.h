#pragma once

#include <QWidget>

// full-page activity list, opened via "Show More" in the popup (MainWindow::openActivityTab())
class ActivityTab : public QWidget
{
    Q_OBJECT

public:
    explicit ActivityTab(QWidget *parent = nullptr);
};
