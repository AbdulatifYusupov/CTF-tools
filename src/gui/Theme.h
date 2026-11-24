#pragma once

#include <QString>

class QApplication;

namespace Theme {

void applyTheme(QApplication *app, const QString &themeName = QStringLiteral("dark"));

} // namespace Theme
