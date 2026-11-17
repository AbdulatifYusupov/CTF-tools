#include <QApplication>
#include <QMainWindow>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow w;  // replace with MainWindow once it exists
    w.setWindowTitle("Stego Tool");
    w.resize(1000, 700);
    w.show();
    return app.exec();
}
