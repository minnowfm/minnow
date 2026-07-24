#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QUrl>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("minnow"));
    app.setApplicationDisplayName(QStringLiteral("Minnow"));
    app.setOrganizationName(QStringLiteral("minnow"));
    app.setApplicationVersion(QStringLiteral("0.1.1"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/minnow.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A simple, KIO-based file browser for KDE."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("Directory (or file) to open"), QStringLiteral("[path]"));
    parser.process(app);

    QUrl startUrl;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        const QString arg = args.first();
        QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (url.isLocalFile()) {
            const QFileInfo info(url.toLocalFile());
            if (info.isFile())
                url = QUrl::fromLocalFile(info.absolutePath());
        }
        startUrl = url;
    }

    MainWindow window(startUrl);
    window.show();

    return app.exec();
}
