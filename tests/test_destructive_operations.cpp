#include "FileOperations.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// trash()/remove()/emptyTrash() live here instead of test_kio_operations.cpp because they
// touch real shared state - the actual XDG trash dir, and a delete confirmation dialog that'd
// just block forever in an automated run.
//
// HOME, XDG_DATA_HOME and XDG_CONFIG_HOME all get pointed at a fresh QTemporaryDir before
// QApplication exists, so KIO's trash and our own "ConfirmPermanentDelete" setting both land
// under the fake home. redirecting just HOME isn't enough - under a real KDE session
// XDG_DATA_HOME/XDG_CONFIG_HOME are usually already set and win over HOME per the XDG spec,
// so those get overridden too. the confirmation dialog itself is skipped the normal way -
// writing the setting - not by faking a click.
class DestructiveOperationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void trash_movesFileIntoIsolatedTrashDirectory();
    void remove_permanentlyDeletesFileWithoutPrompting();
    void emptyTrash_removesAllTrashedFiles();
};

void DestructiveOperationsTest::initTestCase()
{
    // sanity check that the isolation above actually worked before trusting it - if this
    // fails, something upstream changed and these tests would be hitting the real filesystem
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QVERIFY2(dataHome.startsWith(qEnvironmentVariable("HOME")), "XDG data location escaped the isolated fake HOME");
}

void DestructiveOperationsTest::trash_movesFileIntoIsolatedTrashDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("trashme.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    FileOperations::trash({QUrl::fromLocalFile(path)}, nullptr);

    QVERIFY(QTest::qWaitFor([&] { return !QFileInfo::exists(path); }, 5000));

    const QString trashFiles = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(trashFiles + QStringLiteral("/trashme.txt")); }, 5000));
}

void DestructiveOperationsTest::remove_permanentlyDeletesFileWithoutPrompting()
{
    QSettings settings;
    settings.setValue(QStringLiteral("Confirmations/ConfirmPermanentDelete"), false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("deleteme.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    FileOperations::remove({QUrl::fromLocalFile(path)}, nullptr);

    QVERIFY(QTest::qWaitFor([&] { return !QFileInfo::exists(path); }, 5000));
}

void DestructiveOperationsTest::emptyTrash_removesAllTrashedFiles()
{
    QSettings settings;
    settings.setValue(QStringLiteral("Confirmations/ConfirmPermanentDelete"), false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("toempty.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    FileOperations::trash({QUrl::fromLocalFile(path)}, nullptr);
    const QString trashedPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files/toempty.txt");
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(trashedPath); }, 5000));

    FileOperations::emptyTrash(nullptr);
    QVERIFY(QTest::qWaitFor([&] { return !QFileInfo::exists(trashedPath); }, 5000));
}

int main(int argc, char *argv[])
{
    QTemporaryDir fakeHome;
    if (fakeHome.isValid()) {
        QDir().mkpath(fakeHome.filePath(QStringLiteral(".local/share")));
        QDir().mkpath(fakeHome.filePath(QStringLiteral(".config")));
        qputenv("HOME", fakeHome.path().toLocal8Bit());
        qputenv("XDG_DATA_HOME", fakeHome.filePath(QStringLiteral(".local/share")).toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", fakeHome.filePath(QStringLiteral(".config")).toLocal8Bit());
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("MINNOW_NO_NOTIFICATIONS", "1"); // emptyTrash() completion fires a real KNotification otherwise

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("minnow"));
    app.setOrganizationName(QStringLiteral("minnow"));

    DestructiveOperationsTest testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_destructive_operations.moc"
