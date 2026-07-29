#include "TaskManager.h"

#include <KIO/MkdirJob>
#include <KJob>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// TaskManager::self() is a singleton so state carries over between test functions - each
// test here only touches the task id(s) it made itself and cleans them up, so order doesn't matter
class TaskManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void startTask_appearsInTasksList();
    void finishTask_marksFinishedAndFailed();
    void finishTask_successLeavesFailedFalse();
    void updateTaskProgress_updatesCorrectTaskOnly();
    void updateTaskProgress_ignoresUnknownId();
    void hasActiveTasks_reflectsUnfinishedTasks();
    void removeTask_refusesWhileActive();
    void removeTask_succeedsOnceFinished();
    void removeTask_ignoresUnknownId();
    void tasksChanged_emittedOnEveryStateChange();
    void historyCap_neverEvictsAnActiveTask();
    void trackJob_reflectsRealJobCompletion();

private:
    static const TaskManager::Task *findTask(const QList<TaskManager::Task> &tasks, int id);
};

void TaskManagerTest::initTestCase()
{
    // finishTask()/trackJob() completion fires a real KNotification::event() - without this,
    // every test below would pop an actual desktop notification on whatever KDE session runs it.
    qputenv("MINNOW_NO_NOTIFICATIONS", "1");
}

const TaskManager::Task *TaskManagerTest::findTask(const QList<TaskManager::Task> &tasks, int id)
{
    for (const auto &task : tasks) {
        if (task.id == id)
            return &task;
    }
    return nullptr;
}

void TaskManagerTest::startTask_appearsInTasksList()
{
    TaskManager *tm = TaskManager::self();
    const int id = tm->startTask(QStringLiteral("startTask test task"));

    const auto tasks = tm->tasks(); // named local, not a temporary - findTask()'s pointer needs it alive
    const auto *task = findTask(tasks, id);
    QVERIFY(task);
    QCOMPARE(task->description, QStringLiteral("startTask test task"));
    QVERIFY(!task->finished);
    QCOMPARE(task->percent, -1);

    tm->finishTask(id);
}

void TaskManagerTest::finishTask_marksFinishedAndFailed()
{
    TaskManager *tm = TaskManager::self();
    const int id = tm->startTask(QStringLiteral("finishTask failure test"));
    tm->finishTask(id, /*failed=*/true);

    const auto tasks = tm->tasks();
    const auto *task = findTask(tasks, id);
    QVERIFY(task);
    QVERIFY(task->finished);
    QVERIFY(task->failed);
    QCOMPARE(task->percent, 100);
}

void TaskManagerTest::finishTask_successLeavesFailedFalse()
{
    TaskManager *tm = TaskManager::self();
    const int id = tm->startTask(QStringLiteral("finishTask success test"));
    tm->finishTask(id);

    const auto tasks = tm->tasks();
    const auto *task = findTask(tasks, id);
    QVERIFY(task);
    QVERIFY(task->finished);
    QVERIFY(!task->failed);
}

void TaskManagerTest::updateTaskProgress_updatesCorrectTaskOnly()
{
    TaskManager *tm = TaskManager::self();
    const int idA = tm->startTask(QStringLiteral("progress test A"));
    const int idB = tm->startTask(QStringLiteral("progress test B"));

    tm->updateTaskProgress(idA, 42);

    const auto tasks = tm->tasks();
    QCOMPARE(findTask(tasks, idA)->percent, 42);
    QVERIFY(findTask(tasks, idB)->percent != 42);

    tm->finishTask(idA);
    tm->finishTask(idB);
}

void TaskManagerTest::updateTaskProgress_ignoresUnknownId()
{
    TaskManager *tm = TaskManager::self();
    const int bogusId = -12345;
    tm->updateTaskProgress(bogusId, 50); // must not crash, must not create a phantom entry
    QVERIFY(!findTask(tm->tasks(), bogusId));
}

void TaskManagerTest::hasActiveTasks_reflectsUnfinishedTasks()
{
    TaskManager *tm = TaskManager::self();
    const bool baseline = tm->hasActiveTasks();

    const int id = tm->startTask(QStringLiteral("hasActiveTasks probe"));
    QVERIFY(tm->hasActiveTasks());

    tm->finishTask(id);
    QCOMPARE(tm->hasActiveTasks(), baseline);
}

void TaskManagerTest::removeTask_refusesWhileActive()
{
    TaskManager *tm = TaskManager::self();
    const int id = tm->startTask(QStringLiteral("removeTask active guard test"));

    tm->removeTask(id); // must be a no-op while active, see removeTask() in TaskManager.cpp
    QVERIFY(findTask(tm->tasks(), id) != nullptr);

    tm->finishTask(id);
    tm->removeTask(id);
}

void TaskManagerTest::removeTask_succeedsOnceFinished()
{
    TaskManager *tm = TaskManager::self();
    const int id = tm->startTask(QStringLiteral("removeTask finished test"));
    tm->finishTask(id);

    tm->removeTask(id);
    QVERIFY(findTask(tm->tasks(), id) == nullptr);
}

void TaskManagerTest::removeTask_ignoresUnknownId()
{
    TaskManager *tm = TaskManager::self();
    tm->removeTask(-99999); // must not crash
}

void TaskManagerTest::tasksChanged_emittedOnEveryStateChange()
{
    TaskManager *tm = TaskManager::self();
    QSignalSpy spy(tm, &TaskManager::tasksChanged);

    const int id = tm->startTask(QStringLiteral("signal test task"));
    QVERIFY(spy.count() >= 1);

    spy.clear();
    tm->updateTaskProgress(id, 10);
    QVERIFY(spy.count() >= 1);

    spy.clear();
    tm->finishTask(id);
    QVERIFY(spy.count() >= 1);

    spy.clear();
    tm->removeTask(id);
    QVERIFY(spy.count() >= 1);
}

void TaskManagerTest::historyCap_neverEvictsAnActiveTask()
{
    TaskManager *tm = TaskManager::self();
    const int activeId = tm->startTask(QStringLiteral("history cap sentinel"));

    // blow past the 50-entry history cap - the sentinel is the oldest entry throughout, so
    // if eviction were purely age-based it'd be first to go
    QList<int> fillerIds;
    for (int i = 0; i < 60; ++i) {
        const int id = tm->startTask(QStringLiteral("history cap filler %1").arg(i));
        tm->finishTask(id);
        fillerIds << id;
    }

    const auto tasks = tm->tasks();
    const auto *sentinel = findTask(tasks, activeId);
    QVERIFY2(sentinel, "active task must survive history eviction regardless of age");
    QVERIFY(!sentinel->finished);

    tm->finishTask(activeId);
}

void TaskManagerTest::trackJob_reflectsRealJobCompletion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QUrl target = QUrl::fromLocalFile(dir.filePath(QStringLiteral("TrackJobTestDir")));

    KIO::SimpleJob *job = KIO::mkdir(target);
    TaskManager *tm = TaskManager::self();
    tm->trackJob(job, QStringLiteral("trackJob real job test"));

    QSignalSpy resultSpy(job, &KJob::result);
    QVERIFY(resultSpy.wait(5000));

    const auto tasks = tm->tasks(); // trackJob() prepends, so it's at the front
    QVERIFY(!tasks.isEmpty());
    QCOMPARE(tasks.first().description, QStringLiteral("trackJob real job test"));
    QVERIFY(tasks.first().finished);
    QVERIFY(!tasks.first().failed);
    QCOMPARE(tasks.first().percent, 100);
}

QTEST_GUILESS_MAIN(TaskManagerTest)
#include "test_taskmanager.moc"
