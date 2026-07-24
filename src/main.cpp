#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("minnow"));
    app.setApplicationDisplayName(QStringLiteral("Minnow"));
    app.setOrganizationName(QStringLiteral("minnow"));

    MainWindow window;
    window.show();

    return app.exec();
}
