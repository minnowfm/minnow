#include "FileOperations.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

// cutToClipboard()/copyToClipboard()/pasteClipboard() need a real QClipboard -> QApplication,
// but not a real display. offscreen QPA's clipboard is in-process, set/read within the same
// process just works with no X11/Wayland session needed.
class ClipboardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cutToClipboard_setsUrlsAndCutFlag();
    void copyToClipboard_setsUrlsWithoutCutFlag();
    void pasteClipboard_copiesWhenNotCut();
    void pasteClipboard_movesWhenCut();
};

void ClipboardTest::cutToClipboard_setsUrlsAndCutFlag()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/tmp/whatever.txt"));
    FileOperations::cutToClipboard({url});

    const QMimeData *mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime->hasUrls());
    QCOMPARE(mime->urls(), QList<QUrl>{url});
    QCOMPARE(mime->data(QStringLiteral("application/x-kde-cutselection")), QByteArrayLiteral("1"));
}

void ClipboardTest::copyToClipboard_setsUrlsWithoutCutFlag()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/tmp/whatever2.txt"));
    FileOperations::copyToClipboard({url});

    const QMimeData *mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime->hasUrls());
    QCOMPARE(mime->urls(), QList<QUrl>{url});
    QVERIFY(!mime->hasFormat(QStringLiteral("application/x-kde-cutselection")));
}

void ClipboardTest::pasteClipboard_copiesWhenNotCut()
{
    QTemporaryDir srcDir;
    QTemporaryDir destDir;
    QVERIFY(srcDir.isValid() && destDir.isValid());

    const QString sourcePath = srcDir.filePath(QStringLiteral("copyme.txt"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("copy paste content");
    source.close();

    FileOperations::copyToClipboard({QUrl::fromLocalFile(sourcePath)});
    FileOperations::pasteClipboard(QUrl::fromLocalFile(destDir.path()), nullptr);

    const QString destPath = destDir.filePath(QStringLiteral("copyme.txt"));
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(destPath); }, 5000));
    QVERIFY2(QFileInfo::exists(sourcePath), "paste of a copied item must not remove the original");
}

void ClipboardTest::pasteClipboard_movesWhenCut()
{
    QTemporaryDir srcDir;
    QTemporaryDir destDir;
    QVERIFY(srcDir.isValid() && destDir.isValid());

    const QString sourcePath = srcDir.filePath(QStringLiteral("moveme.txt"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("move paste content");
    source.close();

    FileOperations::cutToClipboard({QUrl::fromLocalFile(sourcePath)});
    FileOperations::pasteClipboard(QUrl::fromLocalFile(destDir.path()), nullptr);

    const QString destPath = destDir.filePath(QStringLiteral("moveme.txt"));
    QVERIFY(QTest::qWaitFor([&] { return QFileInfo::exists(destPath); }, 5000));
    QVERIFY2(!QFileInfo::exists(sourcePath), "paste of a cut item must remove the original");
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("MINNOW_NO_NOTIFICATIONS", "1"); // pasteClipboard()'s copy/move fires a real KNotification otherwise
    QApplication app(argc, argv);
    ClipboardTest testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_clipboard.moc"
