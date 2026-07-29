#include "TaskProgressPopup.h"
#include "TaskListWidget.h"

#include <QFont>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

TaskProgressPopup::TaskProgressPopup(QWidget *parent)
    : QFrame(parent)
{
    setWindowFlags(Qt::Popup);
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(320); // height is left to grow with the list, capped below

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *heading = new QLabel(tr("Activity"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    // history can hold 50 entries, way taller than the screen - cap it and let it scroll past that
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QScreen *screen = QGuiApplication::primaryScreen();
    const int maxHeight = screen ? int(screen->availableGeometry().height() * 0.7) : 600;
    scrollArea->setMaximumHeight(maxHeight);
    scrollArea->setWidget(new TaskListWidget(scrollArea));
    layout->addWidget(scrollArea);

    auto *showMoreButton = new QPushButton(tr("Show More"), this);
    connect(showMoreButton, &QPushButton::clicked, this, [this] {
        Q_EMIT showMoreRequested();
        close();
    });
    layout->addWidget(showMoreButton);
}
