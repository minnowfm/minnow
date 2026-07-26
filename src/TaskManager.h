#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

class KJob;

// Tracks in-flight and recently finished file operations (KIO jobs, plus the QtConcurrent
// compress/extract work in FileOperations) so the sidebar's activity popup and the full
// Activity tab can both show the same live list without either one owning the data.
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
        // Used to estimate time remaining from elapsed time and current percent - see
        // TaskListWidget, which does the actual formatting.
        QElapsedTimer elapsed;
    };

    QList<Task> tasks() const { return m_tasks; }
    bool hasActiveTasks() const;

    // Attaches to an existing KJob's percent/result signals under `description`.
    void trackJob(KJob *job, const QString &description);

    // For work that isn't a KJob (e.g. the QtConcurrent-based archive compress/extract).
    // Returns an id to pass to finishTask() once the work completes.
    int startTask(const QString &description);
    // Called from a worker thread via QMetaObject::invokeMethod(..., Qt::QueuedConnection) -
    // never call this directly from another thread.
    void updateTaskProgress(int id, int percent);
    void finishTask(int id, bool failed = false);

    // Dismisses one entry from the visible history - does not touch whatever job/work it
    // was tracking, which keeps running regardless (this only affects what's displayed).
    void removeTask(int id);

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
