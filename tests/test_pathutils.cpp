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

    void allUrlsAlreadyIn_trueWhenAllInDestination();
    void allUrlsAlreadyIn_falseWhenAnyElsewhere();
    void allUrlsAlreadyIn_trueForEmptyList();
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

void PathUtilsTest::allUrlsAlreadyIn_trueWhenAllInDestination()
{
    const QUrl dest = QUrl::fromLocalFile(QStringLiteral("/home/user"));
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral("/home/user/a.txt")),
        QUrl::fromLocalFile(QStringLiteral("/home/user/b.txt")),
    };
    QVERIFY(allUrlsAlreadyIn(urls, dest));
}

void PathUtilsTest::allUrlsAlreadyIn_falseWhenAnyElsewhere()
{
    const QUrl dest = QUrl::fromLocalFile(QStringLiteral("/home/user"));
    const QList<QUrl> urls = {
        QUrl::fromLocalFile(QStringLiteral("/home/user/a.txt")),
        QUrl::fromLocalFile(QStringLiteral("/home/user/Documents/b.txt")),
    };
    QVERIFY(!allUrlsAlreadyIn(urls, dest));
}

void PathUtilsTest::allUrlsAlreadyIn_trueForEmptyList()
{
    QVERIFY(allUrlsAlreadyIn({}, QUrl::fromLocalFile(QStringLiteral("/home/user"))));
}

QTEST_MAIN(PathUtilsTest)
#include "test_pathutils.moc"
