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
    app.setApplicationVersion(QStringLiteral(MINNOW_VERSION));

    // Resolve through the installed icon theme (as KDE apps normally do) rather than
    // pushing a raw client pixmap over Wayland - the latter was producing corrupted
    // decoration icon rendering that theme-resolved icons (e.g. Dolphin's) don't hit.
    QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.minnowfm.Minnow"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("minnow"), QIcon(QStringLiteral(":/icons/minnow.svg")));
    app.setWindowIcon(icon);

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
