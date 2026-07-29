#include "FileOperations.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// covers the KIO job-starting functions in FileOperations. they're fire-and-forget (void,
// no job handle), so we just poll real filesystem state with qWaitFor instead of catching a signal.
//
// needs a real QApplication, not QTEST_GUILESS_MAIN - KIO's default job UI delegate is
// widget-based. offscreen platform plugin covers CI boxes with no display.
//
// trash()/remove()/emptyTrash() are NOT here on purpose: trash() would hit the real XDG
// trash dir, remove()/emptyTrash() permanently delete stuff and the latter can block on a
// confirmation dialog. not worth it for automated tests.
class KioOperationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mkdir_createsDirectory();
    void copyTo_duplicatesFileContentAndKeepsSource();
    void moveTo_movesFileAndRemovesSource();
    void rename_renamesFileInPlace();
    void batchRename_renamesAllSelectedFiles();
};

void KioOperationsTest::mkdir_createsDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QUrl parent = QUrl::fromLocalFile(dir.path());

    FileOperations::mkdir(parent, QStringLiteral("NewFolder"), nullptr);

    const QString expected = dir.filePath(QStringLiteral("NewFolder"));
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(expected); }, 5000));
    QVERIFY(QFileInfo(expected).isDir());
}

void KioOperationsTest::copyTo_duplicatesFileContentAndKeepsSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("dest"))));

    const QString sourcePath = dir.filePath(QStringLiteral("source.txt"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("hello copy");
    source.close();

    FileOperations::copyTo({QUrl::fromLocalFile(sourcePath)}, QUrl::fromLocalFile(dir.filePath(QStringLiteral("dest"))), nullptr);

    const QString destPath = dir.filePath(QStringLiteral("dest/source.txt"));
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(destPath); }, 5000));
    QVERIFY2(QFileInfo::exists(sourcePath), "copy must not remove the original");

    QFile destFile(destPath);
    QVERIFY(destFile.open(QIODevice::ReadOnly));
    QCOMPARE(destFile.readAll(), QByteArrayLiteral("hello copy"));
}

void KioOperationsTest::moveTo_movesFileAndRemovesSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("dest"))));

    const QString sourcePath = dir.filePath(QStringLiteral("moveme.txt"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("hello move");
    source.close();

    FileOperations::moveTo({QUrl::fromLocalFile(sourcePath)}, QUrl::fromLocalFile(dir.filePath(QStringLiteral("dest"))), nullptr);

    const QString destPath = dir.filePath(QStringLiteral("dest/moveme.txt"));
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(destPath); }, 5000));
    QVERIFY2(!QFileInfo::exists(sourcePath), "move must remove the original");
}

void KioOperationsTest::rename_renamesFileInPlace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString oldPath = dir.filePath(QStringLiteral("old.txt"));
    QFile file(oldPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    FileOperations::rename(QUrl::fromLocalFile(oldPath), QStringLiteral("new.txt"), nullptr);

    const QString newPath = dir.filePath(QStringLiteral("new.txt"));
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(newPath); }, 5000));
    QVERIFY(!QFileInfo::exists(oldPath));
}

void KioOperationsTest::batchRename_renamesAllSelectedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QList<QUrl> urls;
    for (const QString &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}) {
        const QString path = dir.filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        urls << QUrl::fromLocalFile(path);
    }

    FileOperations::batchRename(urls, QStringLiteral("Renamed_#"), QLatin1Char('#'), nullptr);

    // renames happen one file at a time, wait for all 3
    QVERIFY(QTest::qWaitFor(
        [&] {
            for (const QString &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")}) {
                if (QFileInfo::exists(dir.filePath(name)))
                    return false;
            }
            return true;
        },
        5000));

    for (int i = 1; i <= 3; ++i)
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Renamed_%1.txt").arg(i))));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("MINNOW_NO_NOTIFICATIONS", "1"); // copyTo/moveTo/mkdir completion fires a real KNotification otherwise
    QApplication app(argc, argv);
    KioOperationsTest testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_kio_operations.moc"
