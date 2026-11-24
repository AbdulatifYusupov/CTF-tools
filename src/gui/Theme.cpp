#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QFont>
#include <QFontDatabase>

namespace Theme {

void applyTheme(QApplication *app, const QString &themeName) {
    if (!app) return;

    if (themeName == "light") {
        app->setStyle(QStringLiteral("Fusion"));

        QPalette lightPalette;
        lightPalette.setColor(QPalette::Window, QColor(245, 245, 247));
        lightPalette.setColor(QPalette::WindowText, QColor(20, 20, 20));
        lightPalette.setColor(QPalette::Base, QColor(255, 255, 255));
        lightPalette.setColor(QPalette::AlternateBase, QColor(240, 240, 242));
        lightPalette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
        lightPalette.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
        lightPalette.setColor(QPalette::Text, QColor(20, 20, 20));
        lightPalette.setColor(QPalette::Button, QColor(235, 235, 238));
        lightPalette.setColor(QPalette::ButtonText, QColor(20, 20, 20));
        lightPalette.setColor(QPalette::BrightText, Qt::red);
        lightPalette.setColor(QPalette::Highlight, QColor(0, 122, 255));
        lightPalette.setColor(QPalette::HighlightedText, Qt::white);

        app->setPalette(lightPalette);
        app->setStyleSheet(QStringLiteral(
            "QTableView { gridline-color: #d0d0d5; selection-background-color: #007aff; selection-color: white; }"
            "QHeaderView::section { background-color: #e8e8ed; padding: 5px; border: 1px solid #d0d0d5; font-weight: bold; }"
            "QGroupBox { font-weight: bold; border: 1px solid #d0d0d5; border-radius: 6px; margin-top: 12px; padding-top: 10px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
            "QPushButton { padding: 6px 12px; border-radius: 4px; border: 1px solid #c0c0c5; background-color: #ffffff; }"
            "QPushButton:hover { background-color: #f0f0f5; }"
            "QPushButton:pressed { background-color: #e0e0e5; }"
            "QPushButton#primaryBtn { background-color: #007aff; color: white; border: none; font-weight: bold; }"
            "QPushButton#primaryBtn:hover { background-color: #0062cc; }"
        ));
        return;
    }

    // Default: Dark Theme (Catppuccin Mocha inspired)
    app->setStyle(QStringLiteral("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 46));           // #1e1e2e
    darkPalette.setColor(QPalette::WindowText, QColor(205, 214, 244));      // #cdd6f4
    darkPalette.setColor(QPalette::Base, QColor(24, 24, 37));              // #181825
    darkPalette.setColor(QPalette::AlternateBase, QColor(30, 30, 46));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(49, 50, 68));
    darkPalette.setColor(QPalette::ToolTipText, QColor(205, 214, 244));
    darkPalette.setColor(QPalette::Text, QColor(205, 214, 244));
    darkPalette.setColor(QPalette::Button, QColor(49, 50, 68));            // #313244
    darkPalette.setColor(QPalette::ButtonText, QColor(205, 214, 244));
    darkPalette.setColor(QPalette::BrightText, QColor(243, 139, 168));      // red
    darkPalette.setColor(QPalette::Highlight, QColor(137, 180, 250));       // #89b4fa
    darkPalette.setColor(QPalette::HighlightedText, QColor(17, 17, 27));    // dark text on highlight

    app->setPalette(darkPalette);

    app->setStyleSheet(QStringLiteral(
        "QWidget { font-size: 13px; }"
        "QMainWindow, QDialog { background-color: #1e1e2e; color: #cdd6f4; }"
        "QTableView {"
        "  background-color: #181825; alternate-background-color: #1e1e2e;"
        "  color: #cdd6f4; gridline-color: #313244;"
        "  selection-background-color: #89b4fa; selection-color: #11111b;"
        "  border: 1px solid #313244; border-radius: 4px;"
        "}"
        "QHeaderView::section {"
        "  background-color: #24253a; color: #cdd6f4; padding: 6px; border: 1px solid #313244; font-weight: bold;"
        "}"
        "QPlainTextEdit, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
        "  background-color: #181825; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 4px;"
        "}"
        "QPlainTextEdit:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {"
        "  border: 1px solid #89b4fa;"
        "}"
        "QGroupBox {"
        "  font-weight: bold; border: 1px solid #45475a; border-radius: 6px; margin-top: 12px; padding-top: 12px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #89b4fa;"
        "}"
        "QPushButton {"
        "  background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; border-radius: 4px; padding: 6px 14px;"
        "}"
        "QPushButton:hover { background-color: #45475a; }"
        "QPushButton:pressed { background-color: #585b70; }"
        "QPushButton#primaryBtn {"
        "  background-color: #89b4fa; color: #11111b; font-weight: bold; border: none;"
        "}"
        "QPushButton#primaryBtn:hover { background-color: #b4befe; }"
        "QPushButton#cancelBtn {"
        "  background-color: #f38ba8; color: #11111b; font-weight: bold; border: none;"
        "}"
        "QPushButton#cancelBtn:hover { background-color: #eba0ac; }"
        "QProgressBar {"
        "  border: 1px solid #45475a; border-radius: 4px; text-align: center; background-color: #181825;"
        "}"
        "QProgressBar::chunk { background-color: #a6e3a1; border-radius: 3px; }"
        "QTabWidget::pane { border: 1px solid #313244; background: #181825; border-radius: 4px; }"
        "QTabBar::tab { background: #24253a; color: #a6adc8; padding: 6px 14px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background: #181825; color: #89b4fa; font-weight: bold; border: 1px solid #313244; border-bottom: none; }"
    ));
}

} // namespace Theme
