#include "FileOperations.h"
#include "TaskManager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

// round-trips real files through compressToArchive()/extractArchive() and their worker threads.
// only success paths - failure pops a modal QMessageBox that would just hang the test binary
// waiting for a click. archiveBaseName() itself is covered in test_fileoperations.cpp.
//
// needs a real QApplication because of the widget-based bits pulled in via TaskManager's
// KNotification calls. falls back to offscreen if there's no display.
class ArchiveTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void compressAndExtract_roundTripsFileContent();
    void compressAndExtract_preservesNestedDirectoryStructure();
    void compressAndExtract_preservesRelativeSymlinkTarget();
};

// startTask() runs synchronously before compressToArchive()/extractArchive() return, so by
// the time we get here the new task is already at the front of the list
static bool waitForTaskToFinish(int taskId)
{
    return QTest::qWaitFor(
        [taskId] {
            for (const auto &task : TaskManager::self()->tasks()) {
                if (task.id == taskId)
                    return task.finished;
            }
            return false;
        },
        10000);
}

void ArchiveTest::compressAndExtract_roundTripsFileContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString file1 = dir.filePath(QStringLiteral("file1.txt"));
    const QString file2 = dir.filePath(QStringLiteral("file2.txt"));
    for (const auto &pair : {std::make_pair(file1, QByteArrayLiteral("content one")), std::make_pair(file2, QByteArrayLiteral("content two"))}) {
        QFile f(pair.first);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(pair.second);
    }

    FileOperations::compressToArchive({QUrl::fromLocalFile(file1), QUrl::fromLocalFile(file2)}, nullptr);
    const int compressTaskId = TaskManager::self()->tasks().first().id;
    QVERIFY(waitForTaskToFinish(compressTaskId));

    const QString archivePath = dir.filePath(QStringLiteral("Archive.zip"));
    QVERIFY(QFileInfo::exists(archivePath));
    QVERIFY(QFileInfo(archivePath).size() > 0);

    FileOperations::extractArchive(QUrl::fromLocalFile(archivePath), nullptr);
    const int extractTaskId = TaskManager::self()->tasks().first().id;
    QVERIFY(waitForTaskToFinish(extractTaskId));

    const QString extractedDir = dir.filePath(QStringLiteral("Archive"));
    QFile extracted1(extractedDir + QStringLiteral("/file1.txt"));
    QVERIFY(extracted1.open(QIODevice::ReadOnly));
    QCOMPARE(extracted1.readAll(), QByteArrayLiteral("content one"));

    QFile extracted2(extractedDir + QStringLiteral("/file2.txt"));
    QVERIFY(extracted2.open(QIODevice::ReadOnly));
    QCOMPARE(extracted2.readAll(), QByteArrayLiteral("content two"));
}

void ArchiveTest::compressAndExtract_preservesNestedDirectoryStructure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("src/sub/deeper"))));

    QFile nested(dir.filePath(QStringLiteral("src/sub/deeper/nested.txt")));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("deep content");
    nested.close();

    FileOperations::compressToArchive({QUrl::fromLocalFile(dir.filePath(QStringLiteral("src")))}, nullptr);
    const int compressTaskId = TaskManager::self()->tasks().first().id;
    QVERIFY(waitForTaskToFinish(compressTaskId));

    const QString archivePath = dir.filePath(QStringLiteral("src.zip"));
    QVERIFY(QFileInfo::exists(archivePath));

    FileOperations::extractArchive(QUrl::fromLocalFile(archivePath), nullptr);
    const int extractTaskId = TaskManager::self()->tasks().first().id;
    QVERIFY(waitForTaskToFinish(extractTaskId));

    // "src" already exists so extraction lands in "src (2)" - and since the zip wraps
    // everything under a top-level "src" entry, we end up with "src (2)/src/..." not "src (2)/..."
    const QString extractedFile = dir.filePath(QStringLiteral("src (2)/src/sub/deeper/nested.txt"));
    QVERIFY(QFileInfo::exists(extractedFile));
    QFile extracted(extractedFile);
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArrayLiteral("deep content"));
}

void ArchiveTest::compressAndExtract_preservesRelativeSymlinkTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("src"))));

    QFile target(dir.filePath(QStringLiteral("src/target.txt")));
    QVERIFY(target.open(QIODevice::WriteOnly));
    target.write("link target content");
    target.close();

    // raw ::symlink(), not QFile::link() - need the stored target to be exactly "target.txt",
    // this is what rawSymLinkTarget() is supposed to preserve
    const QString linkPath = dir.filePath(QStringLiteral("src/link.txt"));
    QVERIFY(::symlink("target.txt", QFile::encodeName(linkPath).constData()) == 0);

    FileOperations::compressToArchive({QUrl::fromLocalFile(dir.filePath(QStringLiteral("src")))}, nullptr);
    const int compressTaskId = TaskManager::self()->tasks().first().id;
    QVERIFY(waitForTaskToFinish(compressTaskId));

    FileOperations::extractArchive(QUrl::fromLocalFile(dir.filePath(QStringLiteral("src.zip"))), nullptr);
    const int extractTaskId = TaskManager::self()->tasks().first().id;
    QVERIFY(waitForTaskToFinish(extractTaskId));

    // same double-nesting as above
    const QString extractedLink = dir.filePath(QStringLiteral("src (2)/src/link.txt"));
    QVERIFY(QFileInfo(extractedLink).isSymLink());

    char buffer[PATH_MAX];
    const ssize_t length = ::readlink(QFile::encodeName(extractedLink).constData(), buffer, sizeof(buffer) - 1);
    QVERIFY(length > 0);
    QCOMPARE(QByteArray(buffer, length), QByteArrayLiteral("target.txt"));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("MINNOW_NO_NOTIFICATIONS", "1"); // compress/extract completion fires a real KNotification otherwise
    QApplication app(argc, argv);
    ArchiveTest testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_archive.moc"
