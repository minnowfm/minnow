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
    // Fixed width; height still grows with the task list (up to the screen height cap below)
    // rather than being fixed outright.
    setFixedWidth(320);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *heading = new QLabel(tr("Activity"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    // TaskManager keeps up to 50 history entries, which at full length would make the popup
    // taller than the screen with nothing scrollable to reach the rest - so it's allowed to
    // grow naturally (no scrollbar) up to a screen-relative cap, and only scrolls internally
    // past that point.
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
