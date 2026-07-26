#include "TaskProgressPopup.h"
#include "TaskListWidget.h"

#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TaskProgressPopup::TaskProgressPopup(QWidget *parent)
    : QFrame(parent)
{
    setWindowFlags(Qt::Popup);
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_DeleteOnClose);
    // Fixed width, but height is left to the layout's sizeHint - the task list grows the
    // popup taller as entries pile up instead of scrolling within a capped height.
    setFixedWidth(320);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *heading = new QLabel(tr("Activity"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    layout->addWidget(new TaskListWidget(this));

    auto *showMoreButton = new QPushButton(tr("Show More"), this);
    connect(showMoreButton, &QPushButton::clicked, this, [this] {
        Q_EMIT showMoreRequested();
        close();
    });
    layout->addWidget(showMoreButton);
}
