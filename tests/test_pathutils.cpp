#include "FileOperations.h"
#include "PathUtils.h"

#include <QTest>

class PathUtilsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parentOf_basic();
    void parentOf_root();
    void parentOf_trailingSlash();
    void parentOf_topLevel();

    void renameDestination_basic();
    void renameDestination_trailingSlash();

    void mkdirDestination_basic();
    void mkdirDestination_trailingSlash();

    void dropWouldBeNoOpOrInvalid_trueWhenAllInDestination();
    void dropWouldBeNoOpOrInvalid_falseWhenElsewhereAndUnrelated();
    void dropWouldBeNoOpOrInvalid_trueForEmptyList();
    void dropWouldBeNoOpOrInvalid_trueWhenDroppedOntoItself();
    void dropWouldBeNoOpOrInvalid_trueWhenDroppedIntoOwnSubfolder();
    void dropWouldBeNoOpOrInvalid_falseForUnrelatedSimilarlyNamedFolder();
};

void PathUtilsTest::parentOf_basic()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/home/user/Documents"));
    QCOMPARE(parentOf(url).path(), QStringLiteral("/home/user"));
}

void PathUtilsTest::parentOf_root()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/"));
    QCOMPARE(parentOf(url).path(), QStringLiteral("/"));
}

void PathUtilsTest::parentOf_trailingSlash()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/home/user/Documents/"));
    QCOMPARE(parentOf(url).path(), QStringLiteral("/home/user"));
}

void PathUtilsTest::parentOf_topLevel()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/home"));
    QCOMPARE(parentOf(url).path(), QStringLiteral("/"));
}

void PathUtilsTest::renameDestination_basic()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/home/user/old.txt"));
    const QUrl dest = FileOperations::renameDestination(url, QStringLiteral("new.txt"));
    QCOMPARE(dest.path(), QStringLiteral("/home/user/new.txt"));
}

void PathUtilsTest::renameDestination_trailingSlash()
{
    const QUrl url = QUrl::fromLocalFile(QStringLiteral("/home/user/OldFolder/"));
    const QUrl dest = FileOperations::renameDestination(url, QStringLiteral("NewFolder"));
    QCOMPARE(dest.path(), QStringLiteral("/home/user/NewFolder"));
}

void PathUtilsTest::mkdirDestination_basic()
{
    const QUrl parent = QUrl::fromLocalFile(QStringLiteral("/home/user"));
    const QUrl dest = FileOperations::mkdirDestination(parent, QStringLiteral("NewFolder"));
    QCOMPARE(dest.path(), QStringLiteral("/home/user/NewFolder"));
}

void PathUtilsTest::mkdirDestination_trailingSlash()
{
    const QUrl parent = QUrl::fromLocalFile(QStringLiteral("/home/user/"));
    const QUrl dest = FileOperations::mkdirDestination(parent, QStringLiteral("NewFolder"));
    QCOMPARE(dest.path(), QStringLiteral("/home/user/NewFolder"));
}

void PathUtilsTest::dropWouldBeNoOpOrInvalid_trueWhenAllInDestination()
{
    const QUrl dest = QUrl::fromLocalFile(QStringLiteral("/home/user"));
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral("/home/user/a.txt")),
        QUrl::fromLocalFile(QStringLiteral("/home/user/b.txt")),
    };
    QVERIFY(dropWouldBeNoOpOrInvalid(urls, dest));
}

void PathUtilsTest::dropWouldBeNoOpOrInvalid_falseWhenElsewhereAndUnrelated()
{
    const QUrl dest = QUrl::fromLocalFile(QStringLiteral("/home/user/Downloads"));
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral("/home/user/a.txt")),
        QUrl::fromLocalFile(QStringLiteral("/home/user/Documents/b.txt")),
    };
    QVERIFY(!dropWouldBeNoOpOrInvalid(urls, dest));
}

void PathUtilsTest::dropWouldBeNoOpOrInvalid_trueForEmptyList()
{
    QVERIFY(dropWouldBeNoOpOrInvalid({}, QUrl::fromLocalFile(QStringLiteral("/home/user"))));
}

void PathUtilsTest::dropWouldBeNoOpOrInvalid_trueWhenDroppedOntoItself()
{
    // dragging a folder and dropping it onto its own icon/sidebar entry - destDir ends up
    // being the dragged folder's own URL
    const QUrl folder = QUrl::fromLocalFile(QStringLiteral("/home/user/Projects"));
    QVERIFY(dropWouldBeNoOpOrInvalid({folder}, folder));
}

void PathUtilsTest::dropWouldBeNoOpOrInvalid_trueWhenDroppedIntoOwnSubfolder()
{
    const QUrl folder = QUrl::fromLocalFile(QStringLiteral("/home/user/Projects"));
    const QUrl subfolder = QUrl::fromLocalFile(QStringLiteral("/home/user/Projects/minnow/src"));
    QVERIFY(dropWouldBeNoOpOrInvalid({folder}, subfolder));
}

void PathUtilsTest::dropWouldBeNoOpOrInvalid_falseForUnrelatedSimilarlyNamedFolder()
{
    // "/home/user/Documents2" must not be mistaken for a descendant of "/home/user/Documents"
    const QUrl folder = QUrl::fromLocalFile(QStringLiteral("/home/user/Documents"));
    const QUrl sibling = QUrl::fromLocalFile(QStringLiteral("/home/user/Documents2"));
    QVERIFY(!dropWouldBeNoOpOrInvalid({folder}, sibling));
}

QTEST_MAIN(PathUtilsTest)
#include "test_pathutils.moc"
