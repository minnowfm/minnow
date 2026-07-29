#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

class KJob;

// tracks running/recent file ops (KIO jobs + the QtConcurrent archive work) as one shared
// list, so the popup and the Activity tab both just read from here instead of duplicating state
class TaskManager : public QObject
{
    Q_OBJECT

public:
    static TaskManager *self();

    struct Task {
        int id = 0;
        QString description;
        int percent = -1; // -1 = indeterminate (no measurable progress)
        bool finished = false;
        bool failed = false;
        QElapsedTimer elapsed; // for the ETA math, see TaskListWidget
    };

    QList<Task> tasks() const { return m_tasks; }
    bool hasActiveTasks() const;

    void trackJob(KJob *job, const QString &description); // hooks into an existing KJob's signals

    // for non-KJob work (the QtConcurrent archive stuff). id gets passed back to finishTask().
    int startTask(const QString &description);
    // call via QMetaObject::invokeMethod(..., Qt::QueuedConnection) from worker threads only
    void updateTaskProgress(int id, int percent);
    void finishTask(int id, bool failed = false);

    void removeTask(int id); // just hides the history entry, the actual job keeps running

signals:
    void tasksChanged();

private:
    explicit TaskManager(QObject *parent = nullptr);
    Task *findTask(int id);
    int addTask(const QString &description);
    void notifyFinished(const Task &task);

    QList<Task> m_tasks;
    int m_nextId = 1;
};
