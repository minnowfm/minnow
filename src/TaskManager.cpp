#include "TaskManager.h"

#include <KJob>
#include <KNotification>

namespace
{
// Recent-history cap so a long session doesn't grow this list unbounded.
constexpr int kMaxTasks = 50;
}

TaskManager *TaskManager::self()
{
    static TaskManager instance;
    return &instance;
}

TaskManager::TaskManager(QObject *parent)
    : QObject(parent)
{
}

TaskManager::Task *TaskManager::findTask(int id)
{
    for (Task &task : m_tasks) {
        if (task.id == id)
            return &task;
    }
    return nullptr;
}

bool TaskManager::hasActiveTasks() const
{
    for (const Task &task : m_tasks) {
        if (!task.finished)
            return true;
    }
    return false;
}

int TaskManager::addTask(const QString &description)
{
    Task task;
    task.id = m_nextId++;
    task.description = description;
    task.elapsed.start();
    m_tasks.prepend(task);
    while (m_tasks.size() > kMaxTasks)
        m_tasks.removeLast();
    Q_EMIT tasksChanged();
    return task.id;
}

void TaskManager::trackJob(KJob *job, const QString &description)
{
    const int id = addTask(description);

    connect(job, &KJob::percentChanged, this, [this, id](KJob *, unsigned long percent) {
        if (Task *task = findTask(id)) {
            task->percent = int(percent);
            Q_EMIT tasksChanged();
        }
    });
    connect(job, &KJob::result, this, [this, id](KJob *finishedJob) {
        if (Task *task = findTask(id)) {
            task->finished = true;
            task->failed = finishedJob->error() != 0;
            task->percent = 100;
            notifyFinished(*task);
            Q_EMIT tasksChanged();
        }
    });
}

int TaskManager::startTask(const QString &description)
{
    return addTask(description);
}

void TaskManager::updateTaskProgress(int id, int percent)
{
    if (Task *task = findTask(id)) {
        task->percent = percent;
        Q_EMIT tasksChanged();
    }
}

void TaskManager::finishTask(int id, bool failed)
{
    if (Task *task = findTask(id)) {
        task->finished = true;
        task->failed = failed;
        task->percent = 100;
        notifyFinished(*task);
        Q_EMIT tasksChanged();
    }
}

void TaskManager::removeTask(int id)
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks.at(i).id == id) {
            m_tasks.removeAt(i);
            Q_EMIT tasksChanged();
            return;
        }
    }
}

void TaskManager::notifyFinished(const Task &task)
{
    // No componentName passed - KNotification falls back to the application name (set via
    // QApplication::setApplicationName() in main.cpp) for the notification's config lookup.
    KNotification::event(task.failed ? KNotification::Warning : KNotification::Notification, task.description,
                          task.failed ? tr("Failed") : tr("Finished"));
}
