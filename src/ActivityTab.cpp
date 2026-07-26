#include "ActivityTab.h"
#include "TaskListWidget.h"

#include <QFont>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

ActivityTab::ActivityTab(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *heading = new QLabel(tr("Activity"), this);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 4);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(new TaskListWidget(scroll));
    layout->addWidget(scroll, 1);
}
