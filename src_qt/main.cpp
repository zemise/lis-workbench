#include "main_window.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ResultSearch");
    app.setApplicationDisplayName(QString::fromWCharArray(L"检验结果查询"));

    MainWindow window;
    window.show();

    return app.exec();
}
