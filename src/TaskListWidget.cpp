#include "TaskListWidget.h"
#include "IndeterminateBar.h"
#include "TaskManager.h"

#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace
{
QString formatDuration(qint64 ms)
{
    const qint64 totalSeconds = ms / 1000;
    if (totalSeconds < 60)
        return QObject::tr("%1s").arg(totalSeconds);
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    if (minutes < 60)
        return QObject::tr("%1m %2s").arg(minutes).arg(seconds);
    const qint64 hours = minutes / 60;
    return QObject::tr("%1h %2m").arg(hours).arg(minutes % 60);
}
}

TaskListWidget::TaskListWidget(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(10);

    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(60);
    connect(m_animationTimer, &QTimer::timeout, this, &TaskListWidget::tickAnimation);

    connect(TaskManager::self(), &TaskManager::tasksChanged, this, &TaskListWidget::rebuild);
    rebuild();
}

void TaskListWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // ignore height-only changes, otherwise rebuild() resizing us triggers another rebuild()
    if (event->oldSize().width() != event->size().width())
        rebuild();
}

void TaskListWidget::tickAnimation()
{
    m_animationPhase += 0.025;
    if (m_animationPhase > 1.0)
        m_animationPhase -= 1.0;
    for (IndeterminateBar *bar : std::as_const(m_indeterminateBars))
        bar->setPhase(m_animationPhase);
}

void TaskListWidget::rebuild()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_indeterminateBars.clear();

    const QList<TaskManager::Task> tasks = TaskManager::self()->tasks();
    if (tasks.isEmpty()) {
        auto *empty = new QLabel(tr("No recent activity"), this);
        empty->setAlignment(Qt::AlignCenter);
        QFont font = empty->font();
        font.setItalic(true);
        empty->setFont(font);
        m_layout->addWidget(empty);
        m_animationTimer->stop();
        return;
    }

    for (const TaskManager::Task &task : tasks) {
        auto *row = new QWidget(this);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(3);

        auto *headerRow = new QWidget(row);
        auto *headerLayout = new QHBoxLayout(headerRow);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(6);

        auto *statusLabel = new QLabel(headerRow);
        if (task.finished) {
            statusLabel->setText(task.failed ? tr("Failed") : tr("Done"));
        } else if (task.percent > 0) {
            // KJob has no ETA of its own, so just extrapolate from elapsed time + percent
            const qint64 etaMs = task.elapsed.elapsed() * (100 - task.percent) / task.percent;
            statusLabel->setText(tr("%1% · %2 left").arg(task.percent).arg(formatDuration(etaMs)));
        } else if (task.percent == 0) {
            statusLabel->setText(tr("0%"));
        }
        if (task.failed) {
            QPalette pal = statusLabel->palette();
            pal.setColor(QPalette::WindowText, Qt::red);
            statusLabel->setPalette(pal);
        }
        statusLabel->adjustSize();

        auto *dismissButton = new QToolButton(headerRow);
        dismissButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
        dismissButton->setAutoRaise(true);
        dismissButton->setFixedSize(20, 20);
        // removeTask() just drops the history entry, it doesn't touch the job - dismissing
        // a running one would make hasActiveTasks() blind to it and let the app close early
        dismissButton->setEnabled(task.finished);
        dismissButton->setToolTip(task.finished ? tr("Remove from history") : tr("Still running"));
        const int taskId = task.id;
        connect(dismissButton, &QToolButton::clicked, this, [taskId] { TaskManager::self()->removeTask(taskId); });

        // elide to whatever's left after the status text + button, recomputed each rebuild
        // so it works in both the fixed-width popup and the wider Activity tab
        const int reserved = statusLabel->sizeHint().width() + dismissButton->width() + headerLayout->spacing() * 2;
        const int available = qMax(40, width() - reserved);
        const QFontMetrics metrics(font());
        auto *label = new QLabel(metrics.elidedText(task.description, Qt::ElideRight, available), headerRow);

        headerLayout->addWidget(label, 1);
        headerLayout->addWidget(statusLabel);
        headerLayout->addWidget(dismissButton);
        rowLayout->addWidget(headerRow);

        if (!task.finished && task.percent < 0) {
            auto *bar = new IndeterminateBar(row);
            bar->setPhase(m_animationPhase);
            m_indeterminateBars.insert(task.id, bar);
            rowLayout->addWidget(bar);
        } else {
            auto *bar = new QProgressBar(row);
            bar->setTextVisible(false);
            bar->setFixedHeight(6);
            bar->setRange(0, 100);
            bar->setValue(task.finished ? (task.failed ? 0 : 100) : task.percent);
            rowLayout->addWidget(bar);
        }

        m_layout->addWidget(row);
    }
    m_layout->addStretch(1);

    if (m_indeterminateBars.isEmpty())
        m_animationTimer->stop();
    else if (!m_animationTimer->isActive())
        m_animationTimer->start();
}
