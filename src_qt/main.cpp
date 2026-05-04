#include "main_window.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ResultSearch");
    app.setApplicationDisplayName(QString::fromWCharArray(L"检验结果查询"));
    app.setWindowIcon(QIcon(":/app.ico"));

    MainWindow window;
    window.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());
    window.showMaximized();

    return app.exec();
}
