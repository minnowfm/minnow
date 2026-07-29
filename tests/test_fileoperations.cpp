#include "FileOperations.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// pure path/name logic only, no live KIO session needed - the job-starting stuff (copyTo,
// moveTo, mkdir...) is in test_kio_operations.cpp, compress/extract in test_archive.cpp
class FileOperationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void isArchive_data();
    void isArchive();
    void isArchive_rejectsDirectory();
    void isArchive_rejectsMissingFile();
    void isArchive_rejectsNonLocalUrl();

    void archiveBaseName_data();
    void archiveBaseName();

    void uniqueFilePath_returnsPlainNameWhenFree();
    void uniqueFilePath_appendsCounterOnCollision();
    void uniqueFilePath_incrementsPastMultipleCollisions();
    void uniqueFilePath_worksForExtensionlessDirectoryNames();
};

void FileOperationsTest::isArchive_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<bool>("expected");

    QTest::newRow("zip") << "archive.zip" << true;
    QTest::newRow("tar") << "archive.tar" << true;
    QTest::newRow("tar.gz") << "archive.tar.gz" << true;
    QTest::newRow("tar.bz2") << "archive.tar.bz2" << true;
    QTest::newRow("tar.xz") << "archive.tar.xz" << true;
    QTest::newRow("tgz") << "archive.tgz" << true;
    QTest::newRow("tbz2") << "archive.tbz2" << true;
    QTest::newRow("txz") << "archive.txz" << true;
    QTest::newRow("uppercase suffix") << "Archive.ZIP" << true;
    QTest::newRow("plain text file") << "notes.txt" << false;
    QTest::newRow("no extension") << "README" << false;
    QTest::newRow("zip-like but not") << "notazip" << false;
}

void FileOperationsTest::isArchive()
{
    QFETCH(QString, fileName);
    QFETCH(bool, expected);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(fileName);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    QCOMPARE(FileOperations::isArchive(QUrl::fromLocalFile(path)), expected);
}

void FileOperationsTest::isArchive_rejectsDirectory()
{
    // a folder literally named "backup.zip" shouldn't count as an archive
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("backup.zip"));
    QVERIFY(QDir().mkpath(path));

    QVERIFY(!FileOperations::isArchive(QUrl::fromLocalFile(path)));
}

void FileOperationsTest::isArchive_rejectsMissingFile()
{
    QVERIFY(!FileOperations::isArchive(QUrl::fromLocalFile(QStringLiteral("/nonexistent/path/archive.zip"))));
}

void FileOperationsTest::isArchive_rejectsNonLocalUrl()
{
    QVERIFY(!FileOperations::isArchive(QUrl(QStringLiteral("http://example.com/archive.zip"))));
}

void FileOperationsTest::archiveBaseName_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("expected");

    // tar.gz should win over the plain .gz/.tar it also ends with
    QTest::newRow("tar.gz") << "photos.tar.gz" << "photos";
    QTest::newRow("tar.bz2") << "photos.tar.bz2" << "photos";
    QTest::newRow("tar.xz") << "photos.tar.xz" << "photos";
    QTest::newRow("tgz") << "photos.tgz" << "photos";
    QTest::newRow("plain tar") << "photos.tar" << "photos";
    QTest::newRow("zip") << "photos.zip" << "photos";
    QTest::newRow("uppercase") << "PHOTOS.ZIP" << "PHOTOS";
    QTest::newRow("name contains dots") << "my.photos.2024.zip" << "my.photos.2024";
    QTest::newRow("unrecognized suffix returns unchanged") << "photos.rar" << "photos.rar";
    QTest::newRow("no suffix returns unchanged") << "photos" << "photos";
}

void FileOperationsTest::archiveBaseName()
{
    QFETCH(QString, fileName);
    QFETCH(QString, expected);
    QCOMPARE(FileOperations::archiveBaseName(fileName), expected);
}

void FileOperationsTest::uniqueFilePath_returnsPlainNameWhenFree()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString result = FileOperations::uniqueFilePath(dir.path(), QStringLiteral("Archive"), QStringLiteral(".zip"));
    QCOMPARE(result, dir.filePath(QStringLiteral("Archive.zip")));
}

void FileOperationsTest::uniqueFilePath_appendsCounterOnCollision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile existing(dir.filePath(QStringLiteral("Archive.zip")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.close();

    const QString result = FileOperations::uniqueFilePath(dir.path(), QStringLiteral("Archive"), QStringLiteral(".zip"));
    QCOMPARE(result, dir.filePath(QStringLiteral("Archive (2).zip")));
}

void FileOperationsTest::uniqueFilePath_incrementsPastMultipleCollisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString &name : {QStringLiteral("Archive.zip"), QStringLiteral("Archive (2).zip"), QStringLiteral("Archive (3).zip")}) {
        QFile existing(dir.filePath(name));
        QVERIFY(existing.open(QIODevice::WriteOnly));
    }

    const QString result = FileOperations::uniqueFilePath(dir.path(), QStringLiteral("Archive"), QStringLiteral(".zip"));
    QCOMPARE(result, dir.filePath(QStringLiteral("Archive (4).zip")));
}

void FileOperationsTest::uniqueFilePath_worksForExtensionlessDirectoryNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("Photos"))));

    const QString result = FileOperations::uniqueFilePath(dir.path(), QStringLiteral("Photos"), QString());
    QCOMPARE(result, dir.filePath(QStringLiteral("Photos (2)")));
}

QTEST_GUILESS_MAIN(FileOperationsTest)
#include "test_fileoperations.moc"
