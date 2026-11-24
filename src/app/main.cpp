#include <QApplication>
#include <QSettings>
#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xorbrute"));
    QApplication::setOrganizationName(QStringLiteral("CTF_Tools"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // Apply persisted theme (defaults to dark)
    QSettings settings;
    QString currentTheme = settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    Theme::applyTheme(&app, currentTheme);

    MainWindow window;
    window.show();

    return app.exec();
}
